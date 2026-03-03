"""
Trains independent per-strategy policy networks for the v10.2 Commander-Executor architecture.

Key Changes from v10.1:
- NO local MCTS - agents are pure executors
- Receives commanded strategy from Squad Commander (Assault/Defend/Support)
- Outputs 6-dim EQS weights in range [-1, 1]
- Local observation only (49-dim base: includes 44 tactical + 5 capture point statuses)

Architecture (v10.2.1 — Independent Models):
- 3 independent single-head policies, one per strategy (Assault/Defend/Support)
- Each policy: Input 52-dim (49 obs + 3 strategy one-hot) → Encoder [256,256] → Single Head → 6-dim EQS
- RLlib multi-agent routes each agent to its strategy's policy via policy_mapping_fn
- No shared encoder, no gradient interference, full per-strategy tuning

Previous architecture (v10.2.0 — removed):
- Single multi-head model with shared encoder + 3 heads
- Suffered from gradient interference causing support head collapse
- When used with 3 RLlib policies, created 6 dead heads per policy instance

Action Space: Box([-1, 1]^6)
- [0]: EnemyObjectiveProximity
- [1]: AllyObjectiveProximity
- [2]: CoverDensity
- [3]: EnemyVisibility
- [4]: AllyProximity
- [5]: CombatRange

Target:
- 100,000 transitions
- Training stability: No divergence
- Combat capability: >40% win rate vs baseline

See v10.2Architecture.md for full specification.
"""

import sys
import os
import time
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.tensorboard import SummaryWriter
from typing import Dict, List, Tuple, Optional
from collections import defaultdict, deque
from dataclasses import dataclass
import json

# Add parent directory to path
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


STRATEGY_NAMES = {0: "Assault", 1: "Defend", 2: "Support"}

# UEnum::GetValueAsString returns 'EStrategyType::Assault' - maps suffix to index
STRATEGY_STR_TO_IDX = {"Assault": 0, "Defend": 1, "Support": 2}


@dataclass
class Transition:
    """Single training transition for v10.2."""
    state: np.ndarray              # (52,) local observation (52 base + 5 capture point statuses)
    commanded_strategy: int        # 0=Assault, 1=Defend, 2=Support
    eqs_weights: np.ndarray        # (7,) in range [-1, 1]
    reward: float
    next_state: np.ndarray         # (52,)
    done: bool
    info: Dict
    log_prob: float = 0.0          # Log probability under policy at collection time


class SingleHeadPolicy_v10_2(nn.Module):
    """
    v10.2.1 Single-Head Policy for one strategy.

    Each strategy (Assault/Defend/Support) gets its own independent instance of this
    model via RLlib multi-agent.  No shared encoder, no multi-head routing, no dead
    parameters.

    Architecture:
    - Input: 52-dim (49 local obs + 3 strategy one-hot)
    - Encoder: [256, 256] ReLU + LayerNorm
    - Action Head: 256 → 64 → 6 (tanh)
    - Value Head: 256 → 64 → 1
    - Learnable log_std: (6,)
    """

    LOG_STD_MIN = -2.5   # σ_min ≈ 0.08   (allows tighter convergence)
    LOG_STD_MAX = -0.7   # σ_max ≈ 0.50   (raised from -1.2/σ=0.30 — wider exploration before convergence)

    def __init__(
        self,
        obs_dim: int = 49,
        strategy_dim: int = 3,
        eqs_dim: int = 6,
        hidden_dims: List[int] = [256, 256],
        strategy_name: str = "unknown"
    ):
        super().__init__()

        self.obs_dim = obs_dim
        self.strategy_dim = strategy_dim
        self.eqs_dim = eqs_dim
        self.strategy_name = strategy_name

        input_dim = obs_dim + strategy_dim

        # Encoder
        encoder_layers = []
        prev_dim = input_dim
        for hidden_dim in hidden_dims:
            encoder_layers.extend([
                nn.Linear(prev_dim, hidden_dim),
                nn.ReLU(),
                nn.LayerNorm(hidden_dim)
            ])
            prev_dim = hidden_dim
        self.state_encoder = nn.Sequential(*encoder_layers)

        # Single action head → 6-dim EQS weights in [-1, 1]
        final_dim = hidden_dims[-1]
        self.action_head = nn.Sequential(
            nn.Linear(final_dim, 64),
            nn.ReLU(),
            nn.Linear(64, eqs_dim),
            nn.Tanh()
        )

        # Single value head
        self.value_head = nn.Sequential(
            nn.Linear(final_dim, 64),
            nn.ReLU(),
            nn.Linear(64, 1)
        )

        # Learnable log standard deviation
        self.log_std = nn.Parameter(torch.zeros(eqs_dim) - 1.5)  # init std ≈ 0.22 (mid-range of [-2.5, -1.2])

        total_params = sum(p.numel() for p in self.parameters())
        print(f"[v10.2.1 Policy ({strategy_name})] {input_dim}-dim → {eqs_dim}-dim EQS, "
              f"encoder={hidden_dims}, params={total_params:,}")

    def forward(self, obs: torch.Tensor, strategy_idx: torch.Tensor) -> torch.Tensor:
        """
        Forward pass.  strategy_idx is accepted for API compatibility with RLlib
        wrapper but is not used for routing (each policy instance serves one strategy).

        Args:
            obs: Local observation (B, obs_dim)
            strategy_idx: Strategy indices (B,) — used only for one-hot encoding

        Returns:
            eqs_weights: (B, 6) in range [-1, 1]
        """
        batch_size = obs.shape[0]
        strategy_onehot = torch.zeros(batch_size, self.strategy_dim, device=obs.device)
        strategy_onehot.scatter_(1, strategy_idx.unsqueeze(1).long(), 1.0)
        x = torch.cat([obs, strategy_onehot], dim=-1)
        features = self.state_encoder(x)
        return self.action_head(features)

    def get_std(self) -> torch.Tensor:
        """Return current standard deviation (eqs_dim,)."""
        clamped_log_std = torch.clamp(self.log_std, self.LOG_STD_MIN, self.LOG_STD_MAX)
        return torch.exp(clamped_log_std)

    def sample_action(
        self,
        obs: torch.Tensor,
        strategy_idx: torch.Tensor
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        """
        Sample actions from Gaussian policy.

        Returns:
            actions: (B, 6) clamped to [-1, 1]
            log_probs: (B,)
        """
        means = self.forward(obs, strategy_idx)
        std = self.get_std()
        dist = torch.distributions.Normal(means, std)
        raw_actions = dist.rsample()
        log_probs = dist.log_prob(raw_actions).sum(dim=-1)
        return raw_actions.clamp(-1.0, 1.0), log_probs

    def compute_log_prob(
        self,
        obs: torch.Tensor,
        strategy_idx: torch.Tensor,
        actions: torch.Tensor
    ) -> torch.Tensor:
        """Compute log probability of given actions.  Returns (B,)."""
        means = self.forward(obs, strategy_idx)
        std = self.get_std()
        dist = torch.distributions.Normal(means, std)
        return dist.log_prob(actions).sum(dim=-1)

    def get_value(self, obs: torch.Tensor, strategy_idx: torch.Tensor) -> torch.Tensor:
        """Compute value estimate.  Returns (B,)."""
        batch_size = obs.shape[0]
        strategy_onehot = torch.zeros(batch_size, self.strategy_dim, device=obs.device)
        strategy_onehot.scatter_(1, strategy_idx.unsqueeze(1).long(), 1.0)
        x = torch.cat([obs, strategy_onehot], dim=-1)
        features = self.state_encoder(x)
        return self.value_head(features).squeeze(-1)

    def export_onnx(self, filepath: str, strategy_idx_value: int = 0, batch_size: int = 1):
        """
        Export to ONNX for UE5 inference.

        Args:
            filepath: Output .onnx path
            strategy_idx_value: The strategy index this model serves
            batch_size: Export batch size
        """
        self.eval()
        dummy_obs = torch.randn(batch_size, self.obs_dim)
        dummy_strategy = torch.full((batch_size,), strategy_idx_value, dtype=torch.long)

        torch.onnx.export(
            self,
            (dummy_obs, dummy_strategy),
            filepath,
            input_names=['observation', 'strategy_index'],
            output_names=['eqs_weights'],
            dynamic_axes={
                'observation': {0: 'batch_size'},
                'strategy_index': {0: 'batch_size'},
                'eqs_weights': {0: 'batch_size'}
            },
            opset_version=14
        )
        print(f"[ONNX Export] {self.strategy_name} model → {filepath}")

    def print_architecture(self):
        """Print model summary."""
        total_params = sum(p.numel() for p in self.parameters())
        trainable_params = sum(p.numel() for p in self.parameters() if p.requires_grad)

        print(f"\n{'='*60}")
        print(f"v10.2.1 Single-Head Policy: {self.strategy_name}")
        print(f"{'='*60}")
        print(f"  Parameters: {total_params:,} (trainable: {trainable_params:,})")
        print(f"  Encoder: {self.state_encoder}")
        print(f"  Action Head: {self.action_head}")
        print(f"  Value Head: {self.value_head}")
        print(f"  log_std: {self.log_std.data.numpy()}")
        print(f"{'='*60}\n")


class StrategyBalancedReplayBuffer:
    """
    Replay buffer with balanced strategy sampling.
    Ensures each strategy gets equal representation.
    """

    def __init__(self, capacity: int = 100000):
        self.capacity = capacity
        self.buffers = {
            strategy: deque(maxlen=capacity // 3)
            for strategy in range(3)  # Assault, Defend, Support
        }
        self.total_count = 0

    def add(self, transition: Transition):
        """Add transition to appropriate strategy buffer."""
        strategy = transition.commanded_strategy
        self.buffers[strategy].append(transition)
        self.total_count += 1

    def sample(self, batch_size: int) -> List[Transition]:
        """Sample batch with balanced strategy distribution."""
        samples_per_strategy = batch_size // 3

        batch = []
        for strategy in range(3):
            buffer = self.buffers[strategy]
            if len(buffer) < samples_per_strategy:
                # Take all available
                batch.extend(list(buffer))
            else:
                # Random sampling
                indices = np.random.choice(len(buffer), samples_per_strategy, replace=False)
                batch.extend([buffer[i] for i in indices])

        # Shuffle
        np.random.shuffle(batch)
        return batch

    def get_strategy_distribution(self) -> Dict[int, float]:
        """Get current distribution of strategies in buffer."""
        total = sum(len(buf) for buf in self.buffers.values())
        if total == 0:
            return {i: 0.0 for i in range(3)}

        return {
            strategy: len(self.buffers[strategy]) / total
            for strategy in range(3)
        }

    def size(self) -> int:
        """Total number of transitions in buffer."""
        return sum(len(buf) for buf in self.buffers.values())

    def __len__(self) -> int:
        return self.size()


class PPOTrainer_v10_2:
    """
    Standalone PPO trainer for v10.2.1 single-head policy.

    Note: This trainer is for standalone (non-RLlib) use.  The RLlib path uses
    RLlib's built-in PPO with per-strategy policies — see create_ppo_config().
    """

    def __init__(
        self,
        policy: SingleHeadPolicy_v10_2,
        learning_rate: float = 1.5e-4,
        critic_lr_scale: float = 1.0,
        clip_epsilon: float = 0.2,
        value_coef: float = 0.5,
        entropy_coef: float = 0.005,
        max_grad_norm: float = 0.5,
        device: str = 'cuda' if torch.cuda.is_available() else 'cpu'
    ):
        self.policy = policy.to(device)
        self.device = device

        # PPO hyperparameters
        self.clip_epsilon = clip_epsilon
        self.value_coef = value_coef
        self.entropy_coef = entropy_coef
        self.max_grad_norm = max_grad_norm

        # Separate actor/critic learning rates.
        # Critic runs at critic_lr_scale × learning_rate (default 30% of actor LR).
        encoder_params = list(policy.state_encoder.parameters())
        actor_params = list(policy.action_head.parameters()) + [policy.log_std]
        critic_params = list(policy.value_head.parameters())
        self.optimizer = optim.Adam([
            {'params': encoder_params, 'lr': learning_rate},
            {'params': actor_params,   'lr': learning_rate},
            {'params': critic_params,  'lr': learning_rate * critic_lr_scale},
        ])

    def compute_advantages(
        self,
        rewards: torch.Tensor,
        values: torch.Tensor,
        dones: torch.Tensor,
        gamma: float = 0.99,
        gae_lambda: float = 0.95
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        """Compute GAE advantages."""
        advantages = torch.zeros_like(rewards)
        last_advantage = 0

        for t in reversed(range(len(rewards))):
            if t == len(rewards) - 1:
                next_value = 0
            else:
                next_value = values[t + 1]

            delta = rewards[t] + gamma * next_value * (1 - dones[t]) - values[t]
            advantages[t] = delta + gamma * gae_lambda * (1 - dones[t]) * last_advantage
            last_advantage = advantages[t]

        returns = advantages + values
        return advantages, returns

    def update(
        self,
        batch: List[Transition],
        epochs: int = 10
    ) -> Dict[str, float]:
        """Update policy using PPO."""
        # Convert batch to tensors
        states = torch.tensor(
            np.array([t.state for t in batch]),
            dtype=torch.float32,
            device=self.device
        )
        strategy_idxs = torch.tensor(
            [t.commanded_strategy for t in batch],
            dtype=torch.long,
            device=self.device
        )
        old_eqs_weights = torch.tensor(
            np.array([t.eqs_weights for t in batch]),
            dtype=torch.float32,
            device=self.device
        )
        rewards = torch.tensor(
            [t.reward for t in batch],
            dtype=torch.float32,
            device=self.device
        )
        dones = torch.tensor(
            [float(t.done) for t in batch],
            dtype=torch.float32,
            device=self.device
        )

        # Use stored log probs from collection time
        old_log_probs = torch.tensor(
            [t.log_prob for t in batch],
            dtype=torch.float32,
            device=self.device
        )

        with torch.no_grad():
            old_values = self.policy.get_value(states, strategy_idxs)

        # Compute advantages
        advantages, returns = self.compute_advantages(rewards, old_values, dones)

        # Advantage normalization — with independent single-head models, each policy
        # only sees data from one strategy, so we normalize globally.
        advantages = (advantages - advantages.mean()) / (advantages.std() + 1e-8)

        # PPO update epochs
        metrics = defaultdict(list)

        for epoch in range(epochs):
            new_log_probs = self.policy.compute_log_prob(states, strategy_idxs, old_eqs_weights)
            new_values = self.policy.get_value(states, strategy_idxs)

            # Entropy from single learned log_std
            std = self.policy.get_std()
            entropy = (0.5 * torch.log(2 * np.pi * np.e * std ** 2)).sum()

            # PPO clipped policy loss
            ratio = torch.exp(new_log_probs - old_log_probs)
            surr1 = ratio * advantages
            surr2 = torch.clamp(ratio, 1 - self.clip_epsilon, 1 + self.clip_epsilon) * advantages
            policy_loss = -torch.min(surr1, surr2).mean()

            # Value loss
            value_loss = nn.MSELoss()(new_values, returns)

            # Total loss
            total_loss = policy_loss + self.value_coef * value_loss - self.entropy_coef * entropy

            self.optimizer.zero_grad()
            total_loss.backward()
            nn.utils.clip_grad_norm_(self.policy.parameters(), self.max_grad_norm)
            self.optimizer.step()

            metrics['policy_loss'].append(policy_loss.item())
            metrics['value_loss'].append(value_loss.item())
            metrics['entropy'].append(entropy.item())
            metrics['total_loss'].append(total_loss.item())
            metrics['approx_kl'].append((old_log_probs - new_log_probs).mean().item())

        return {key: np.mean(values) for key, values in metrics.items()}



# ============================================================================
# PYTHON INTEGRATION EXAMPLE
# ============================================================================

def example_training_integration():
    """
    Example showing how to integrate with Schola/UE5.

    v10.2.1: Demonstrates 3 independent single-head policies.
    """

    print("\n" + "="*80)
    print("v10.2.1 Python Training Integration Example")
    print("="*80 + "\n")

    # 1. Initialize 3 independent policies
    policies = {}
    for strat_idx, strat_name in STRATEGY_NAMES.items():
        policies[strat_name] = SingleHeadPolicy_v10_2(
            obs_dim=49,
            strategy_dim=3,
            eqs_dim=6,
            hidden_dims=[256, 256],
            strategy_name=strat_name
        )
    policies["Assault"].print_architecture()

    print("\nSetup: 3 independent policies created.\n")
    print("Integration Steps:")
    print("1. Connect to UE5 environment via Schola gRPC")
    print("2. Receive commanded strategy from Squad Commander")
    print("3. Route agent to the corresponding policy (assault/defend/support)")
    print("4. Run policy inference:")
    print("   >>> eqs_weights = policy(obs_tensor, strategy_tensor)  # Output: (1, 6)")
    print("5. Send eqs_weights to TacticalParameterActuator_v10_2 via Schola")
    print("\n" + "="*80 + "\n")

    # Example inference
    print("Example Inference:")
    dummy_obs = torch.randn(1, 49)
    dummy_strategy = torch.tensor([0], dtype=torch.long)

    with torch.no_grad():
        eqs_weights = policies["Assault"](dummy_obs, dummy_strategy)
        print(f"  Input: obs(1, 49), strategy=Assault")
        print(f"  Output: eqs_weights = {eqs_weights.numpy()[0]}")
        print(f"  Range: [{eqs_weights.min().item():.2f}, {eqs_weights.max().item():.2f}]")

    print("\n" + "="*80 + "\n")


# ============================================================================
# RLLIB INTEGRATION
# ============================================================================

try:
    from ray.rllib.models.torch.torch_modelv2 import TorchModelV2
    from ray.rllib.utils.annotations import override
    RLLIB_AVAILABLE = True
except ImportError:
    RLLIB_AVAILABLE = False
    print("Warning: RLlib not available. Install with: pip install ray[rllib]")


if RLLIB_AVAILABLE:
    class SingleHeadPolicy_v10_2_RLlib(TorchModelV2, nn.Module):
        """
        RLlib wrapper for SingleHeadPolicy_v10_2.

        Each RLlib policy (assault_policy, defend_policy, support_policy) gets its
        own instance of this wrapper, each containing an independent SingleHeadPolicy.
        No multi-head routing — clean, simple, one model per strategy.
        """

        def __init__(self, obs_space, action_space, num_outputs, model_config, name):
            TorchModelV2.__init__(self, obs_space, action_space, num_outputs, model_config, name)
            nn.Module.__init__(self)

            custom_config = model_config.get("custom_model_config", {})
            obs_dim = custom_config.get("obs_dim", 49)
            strategy_dim = custom_config.get("strategy_dim", 3)
            eqs_dim = custom_config.get("eqs_dim", 6)
            hidden_dims = custom_config.get("hidden_dims", [256, 256])
            strategy_name = custom_config.get("strategy_name", name)

            self.policy = SingleHeadPolicy_v10_2(
                obs_dim=obs_dim,
                strategy_dim=strategy_dim,
                eqs_dim=eqs_dim,
                hidden_dims=hidden_dims,
                strategy_name=strategy_name
            )

            self.num_outputs = num_outputs
            self.action_dim = eqs_dim

            self._last_features = None
            self._last_strategy_idx = None

            print(f"[v10.2.1 RLlib] {strategy_name}: obs={obs_dim}, eqs={eqs_dim}")

        @override(TorchModelV2)
        def forward(self, input_dict, state, seq_lens):
            """
            Forward pass.  Returns (B, 12) = [means(6), log_stds(6)].
            """
            obs = input_dict["obs"]

            base_obs = obs[:, :49]
            strategy_onehot = obs[:, 49:52]
            strategy_idx = torch.argmax(strategy_onehot, dim=1)

            self._last_features = obs
            self._last_strategy_idx = strategy_idx

            # Single head — no routing needed
            eqs_weights = self.policy(base_obs, strategy_idx)  # (B, 6)

            # Broadcast clamped log_std to batch (prevents entropy spiral)
            clamped_log_std = torch.clamp(
                self.policy.log_std,
                SingleHeadPolicy_v10_2.LOG_STD_MIN,
                SingleHeadPolicy_v10_2.LOG_STD_MAX,
            )
            log_stds = clamped_log_std.unsqueeze(0).expand_as(eqs_weights)  # (B, 6)

            output = torch.cat([eqs_weights, log_stds], dim=-1)  # (B, 12)
            return output, state

        @override(TorchModelV2)
        def value_function(self):
            """Value estimate for the last forward pass.  Returns (B,)."""
            if self._last_features is None or self._last_strategy_idx is None:
                raise ValueError("Must call forward() before value_function()")

            base_obs = self._last_features[:, :49]
            return self.policy.get_value(base_obs, self._last_strategy_idx)


# ============================================================================
# TRAINING CONFIGURATION
# ============================================================================

class MOCv10_2TrainingConfig:
    """Training configuration for MOC v10.2.

    Core counts are read from environment variables so Docker Compose (or the
    batch launcher) can override them without editing source:

        NUM_SCHOLA_ENVS  – number of UE5 Schola environments  (default: 4)
        NUM_WORKERS      – number of Ray RLlib env-runners      (default: 0)
        NUM_ITERATIONS   – total training iterations            (default: 100)
    """

    # Environment
    HOST = "localhost"
    PORT = 50051
    NUM_UE5_ENVIRONMENTS = int(os.environ.get('NUM_SCHOLA_ENVS', 4))

    # Network architecture
    HIDDEN_DIMS = [256, 256]

    # PPO hyperparameters
    LEARNING_RATE = 5e-5          
    TRAIN_BATCH_SIZE = 12000          
    SGD_MINIBATCH_SIZE = 1024        
    NUM_SGD_ITER = 4                  
    GAMMA = 0.99
    GAE_LAMBDA = 0.90              
    CLIP_PARAM = 0.2                  
    ENTROPY_COEFF = 0.003             
    VF_LOSS_COEFF = 1.0              
    GRAD_CLIP = 0.5
    VF_CLIP_PARAM = 0.2             

    # Training
    NUM_WORKERS = int(os.environ.get('NUM_WORKERS', 0))
    NUM_ENVS_PER_WORKER = 1
    NUM_ITERATIONS = int(os.environ.get('NUM_ITERATIONS', 100))
    CHECKPOINT_FREQ = 10

    # Schedules — hold longer at peak before decaying; entropy decays slower
    LR_SCHEDULE = [[0, 8e-5], [300000, 5e-5], [500000, 2e-5], [700000, 1e-5],
                   [1200000, 2e-5], [1500000, 1e-5], [2000000, 5e-6]]
    # Resume bump at 1.2M: entropy rises from 0.0005 back to 0.002 to allow
    # the Defend policy to re-explore after the reward fix, then decays again.
    ENTROPY_COEFF_SCHEDULE = [[0, 0.003], [200000, 0.002], [400000, 0.001], [600000, 0.0005],
                              [1200000, 0.002], [1400000, 0.001], [1600000, 0.0005], [2000000, 0.0002]]

    # Paths
    OUTPUT_DIR = "training_results_v10_2"


def create_env_config():
    """Create environment configuration for v10.2."""
    return {
        "host": MOCv10_2TrainingConfig.HOST,
        "base_port": MOCv10_2TrainingConfig.PORT,
        "num_envs": MOCv10_2TrainingConfig.NUM_UE5_ENVIRONMENTS,
        "force_uniform_strategy": True,  # Override UE5 SquadCommander for balanced training
    }


if RLLIB_AVAILABLE:
    from ray.rllib.algorithms.callbacks import DefaultCallbacks

    class MOCv10_2Callbacks(DefaultCallbacks):
        """Custom RLlib callbacks for per-strategy metrics tracking."""

        def on_episode_start(self, *, episode, **kwargs):
            episode.user_data["strategy_rewards"] = {0: [], 1: [], 2: []}
            episode.user_data["strategy_counts"] = {0: 0, 1: 0, 2: 0}

        def on_episode_step(self, *, episode, **kwargs):
            # EpisodeV2 API: use get_agents() + last_info_for()
            # Info dict contains 'Strategy' (UEnum string) and 'LastStepReward' directly
            for agent_id in episode.get_agents():
                info = episode.last_info_for(agent_id)
                if not info:
                    continue
                raw_strategy = info.get('Strategy')
                if raw_strategy is None:
                    continue
                # UEnum::GetValueAsString returns 'EStrategyType::Assault' - extract name after '::'
                strategy_name = raw_strategy.split('::')[-1]
                if strategy_name not in STRATEGY_STR_TO_IDX:
                    raise ValueError(
                        f"[on_episode_step] Unrecognized strategy value '{raw_strategy}'."
                    )
                strategy_idx = STRATEGY_STR_TO_IDX[strategy_name]
                reward = float(info.get('LastStepReward', 0.0))
                episode.user_data["strategy_rewards"][strategy_idx].append(reward)
                episode.user_data["strategy_counts"][strategy_idx] += 1

        def on_episode_end(self, *, episode, **kwargs):
            for strat_idx, name in STRATEGY_NAMES.items():
                rewards = episode.user_data["strategy_rewards"][strat_idx]
                count = episode.user_data["strategy_counts"][strat_idx]
                name_lower = name.lower()
                episode.custom_metrics[f"strategy_{name_lower}_reward_mean"] = (
                    np.mean(rewards) if rewards else 0.0
                )
                episode.custom_metrics[f"strategy_{name_lower}_reward_std"] = (
                    np.std(rewards) if len(rewards) > 1 else 0.0
                )
                episode.custom_metrics[f"strategy_{name_lower}_count"] = count

            total = sum(episode.user_data["strategy_counts"].values())
            if total > 0:
                for strat_idx, name in STRATEGY_NAMES.items():
                    episode.custom_metrics[f"strategy_{name.lower()}_frac"] = (
                        episode.user_data["strategy_counts"][strat_idx] / total
                    )

                # Round-robin detection: log which strategy is dominant this episode
                dominant_strat = max(
                    episode.user_data["strategy_counts"],
                    key=episode.user_data["strategy_counts"].get
                )
                episode.custom_metrics["round_robin_active_strategy"] = dominant_strat

        def on_train_result(self, *, algorithm, result, **kwargs):
            """Extract per-policy loss metrics from learner_stats and re-publish
            them as top-level custom_metrics so TensorBoard writes them to
            ray/tune/custom_metrics/* alongside the per-strategy reward curves."""
            learner_info = result.get("info", {}).get("learner", {})

            for policy_name, strategy_label in [
                ("assault_policy", "assault"),
                ("defend_policy", "defend"),
                ("support_policy", "support"),
            ]:
                policy_stats = learner_info.get(policy_name, {}).get("learner_stats", {})
                if not policy_stats:
                    continue
                for key in ["policy_loss", "vf_loss", "entropy", "total_loss"]:
                    if key in policy_stats:
                        result.setdefault("custom_metrics", {})[
                            f"train_{key}_{strategy_label}"
                        ] = policy_stats[key]


_STRATEGY_TO_POLICY = {0: "assault_policy", 1: "defend_policy", 2: "support_policy"}


def _strategy_policy_mapping_fn(agent_id, episode, worker, **kwargs):
    """Map agents to per-strategy policies based on the strategy one-hot in their observation.

    The env assigns strategies uniformly (round-robin) and encodes them in obs[49:52].
    We extract the strategy index from the latest observation to route to the correct policy.
    """
    # Try to get the latest observation for this agent
    try:
        obs = episode.last_observation_for(agent_id)
        if obs is not None and len(obs) >= 52:
            strategy_onehot = obs[49:52]
            strategy_idx = int(np.argmax(strategy_onehot))
            return _STRATEGY_TO_POLICY.get(strategy_idx, "assault_policy")
    except Exception:
        pass

    # Fallback: derive strategy from agent ID hash for deterministic assignment
    # agent_id format: "agent_{env_idx}_{schola_idx}"
    try:
        parts = agent_id.split("_")
        agent_num = int(parts[-1])  # schola_agent_idx
        return _STRATEGY_TO_POLICY.get(agent_num % 3, "assault_policy")
    except (ValueError, IndexError):
        return "assault_policy"


def create_ppo_config():
    """Create RLlib PPO configuration for v10.2."""
    if not RLLIB_AVAILABLE:
        raise ImportError("RLlib not available. Install with: pip install ray[rllib]")

    from ray.rllib.algorithms.ppo import PPOConfig

    config = PPOConfig()

    # Environment
    config = config.environment(
        env="moc_v10_2_env",
        env_config=create_env_config(),
        disable_env_checking=True,
    )

    # Framework and Runner
    config = config.framework("torch")
    config = config.env_runners(
        num_env_runners=MOCv10_2TrainingConfig.NUM_WORKERS,
        num_envs_per_env_runner=MOCv10_2TrainingConfig.NUM_ENVS_PER_WORKER,
        rollout_fragment_length=100,  # Matches episode length for clean episode boundaries
        batch_mode="truncate_episodes",
    )

    # On Windows, Ray's Learner actor hangs during inter-process weight sync.
    # Force local learner on Windows; on Linux (Docker) both modes work.
    import platform
    if platform.system() == "Windows":
        try:
            config = config.learners(num_learners=0)
            print("[v10.2] Windows: using num_learners=0 (local learner)")
        except Exception:
            config = config.api_stack(
                enable_rl_module_and_learner=False,
                enable_env_runner_and_connector_v2=False,
            )
            print("[v10.2] Windows fallback: using old API stack")

    # Multi-agent: 3 independent per-strategy policies.
    # Each policy is a separate SingleHeadPolicy_v10_2 instance — no shared weights,
    # no gradient interference, full per-strategy tuning.
    config = config.multi_agent(
        policies={
            "assault_policy",
            "defend_policy",
            "support_policy",
        },
        policy_mapping_fn=_strategy_policy_mapping_fn,
        count_steps_by="agent_steps",
    )

    # Callbacks
    config = config.callbacks(MOCv10_2Callbacks)

    # Debugging & Reporting
    config = config.debugging(log_level="WARN")
    config = config.reporting(
        metrics_num_episodes_for_smoothing=10,
        min_sample_timesteps_per_iteration=MOCv10_2TrainingConfig.TRAIN_BATCH_SIZE,
    )

    # Training
    config = config.training(
        lr=MOCv10_2TrainingConfig.LEARNING_RATE,
        lr_schedule=MOCv10_2TrainingConfig.LR_SCHEDULE,
        entropy_coeff_schedule=MOCv10_2TrainingConfig.ENTROPY_COEFF_SCHEDULE,
        train_batch_size=MOCv10_2TrainingConfig.TRAIN_BATCH_SIZE,
        lambda_=MOCv10_2TrainingConfig.GAE_LAMBDA,
        clip_param=MOCv10_2TrainingConfig.CLIP_PARAM,
        vf_clip_param=MOCv10_2TrainingConfig.VF_CLIP_PARAM,
        entropy_coeff=MOCv10_2TrainingConfig.ENTROPY_COEFF,
        vf_loss_coeff=MOCv10_2TrainingConfig.VF_LOSS_COEFF,
        grad_clip=MOCv10_2TrainingConfig.GRAD_CLIP,
        use_gae=True,
        use_critic=True,
        use_kl_loss=True,
        kl_coeff=0.2,
        kl_target=0.01,
    )

    # PPO-specific
    config.num_epochs = MOCv10_2TrainingConfig.NUM_SGD_ITER
    config.minibatch_size = MOCv10_2TrainingConfig.SGD_MINIBATCH_SIZE
    config.shuffle_batch_per_epoch = True

    # Model configuration — each policy uses the same single-head architecture
    # but trains independently.  strategy_name is cosmetic (for logging).
    config.model = {
        "custom_model": "single_head_policy_v10_2",
        "custom_model_config": {
            "obs_dim": 49,
            "strategy_dim": 3,
            "eqs_dim": 6,
            "hidden_dims": MOCv10_2TrainingConfig.HIDDEN_DIMS,
        },
        "max_seq_len": 20,
    }

    return config


def register_env():
    """Register v10.2 environment with Ray."""
    from ray.tune.registry import register_env
    from moc_v10_2_env import MOCv10_2MultiAgentEnv

    def env_creator(config):
        return MOCv10_2MultiAgentEnv(**config)

    register_env("moc_v10_2_env", env_creator)
    print("[v10.2] Environment registered")


def register_custom_model():
    """Register v10.2.1 single-head policy with RLlib."""
    if not RLLIB_AVAILABLE:
        return

    from ray.rllib.models import ModelCatalog

    ModelCatalog.register_custom_model("single_head_policy_v10_2", SingleHeadPolicy_v10_2_RLlib)
    print("[v10.2.1] Single-head policy registered")


def export_onnx(algo, output_dir):
    """Export 3 trained per-strategy policies to ONNX for UE5."""
    strategy_policies = {
        0: ("assault_policy", "assault"),
        1: ("defend_policy", "defend"),
        2: ("support_policy", "support"),
    }

    for strategy_idx, (policy_name, strategy_label) in strategy_policies.items():
        rllib_policy = algo.get_policy(policy_name)
        if not rllib_policy:
            print(f"\n[WARN] export_onnx: Could not get '{policy_name}', skipping")
            continue

        model = rllib_policy.model.policy  # Unwrap RLlib wrapper → SingleHeadPolicy_v10_2
        model_path = os.path.join(output_dir, f"moc_policy_v10_2_{strategy_label}.onnx")
        model.export_onnx(model_path, strategy_idx_value=strategy_idx)

    print(f"  Input: observation(B, 49), strategy_index(B)")
    print(f"  Output: eqs_weights(B, 6) in [-1, 1]")


def train_with_rllib(args):
    """Main training loop using RLlib."""
    import ray
    from datetime import datetime
    from pathlib import Path

    print("=" * 80)
    print("MOC v10.2 - Command-Driven Policy Training")
    print("=" * 80)
    print(f"  Host: {MOCv10_2TrainingConfig.HOST}:{MOCv10_2TrainingConfig.PORT}")
    print(f"  Workers: {MOCv10_2TrainingConfig.NUM_WORKERS}")
    print(f"  UE5 Environments: {MOCv10_2TrainingConfig.NUM_UE5_ENVIRONMENTS}")
    print(f"  Iterations: {args.iterations}")
    print()

    # Cleanup any existing Ray
    try:
        ray.shutdown()
    except:
        pass

    # Windows multiprocessing fix
    import multiprocessing
    try:
        multiprocessing.set_start_method('spawn', force=True)
    except RuntimeError:
        pass

    # Initialize Ray
    import tempfile
    ray_temp_dir = os.path.join(tempfile.gettempdir(), "ray_moc_v10_2")
    os.makedirs(ray_temp_dir, exist_ok=True)

    print("Initializing Ray...")
    try:
        ray.init(
            ignore_reinit_error=True,
            include_dashboard=False,
            _temp_dir=ray_temp_dir,
            num_cpus=4,
            object_store_memory=1 * 1024**3,
            logging_level="ERROR",
        )
        print("Ray initialized successfully")
    except Exception as e:
        print(f"\n[FATAL] Ray initialization failed: {e}")
        sys.exit(1)

    register_env()
    register_custom_model()

    # Create output directory
    import shutil
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = os.path.join(MOCv10_2TrainingConfig.OUTPUT_DIR, f"v10_2_{timestamp}")
    os.makedirs(output_dir, exist_ok=True)
    latest_dir = os.path.join(output_dir, "latest")
    print(f"Output: {output_dir}")

    # Create logger
    from ray.tune.logger import UnifiedLogger, TBXLogger, JsonLogger, CSVLogger

    def logger_creator(config_dict):
        return UnifiedLogger(config_dict, output_dir, loggers=[JsonLogger, CSVLogger, TBXLogger])

    # Build algorithm
    print("\nConnecting to UE5...")
    config = create_ppo_config()
    try:
        algo = config.build(logger_creator=logger_creator)
        print("Connected!\n")
    except Exception as e:
        print(f"[ERROR] Failed to connect: {e}")
        ray.shutdown()
        return

    # Resume from checkpoint if requested
    if args.resume:
        print(f"\nResuming from checkpoint: {args.resume}")
        try:
            algo.restore(args.resume)
            print("Checkpoint loaded successfully.\n")
        except Exception as e:
            print(f"[ERROR] Failed to restore checkpoint: {e}")
            ray.shutdown()
            return

    # Training loop
    best_reward = float("-inf")
    cumulative_episodes = 0
    cumulative_steps = 0

    print("\n" + "="*80)
    print("TRAINING PROGRESS")
    print("="*80)
    print(f"{'Iter':<6} {'Reward':>10} {'EpLen':>8} {'Episodes':>10} {'Steps':>12} {'Time':>8} {'Best':>10}")
    print("-"*80)

    for i in range(args.iterations):
        iter_start = time.time()
        print(f"\n[ITER {i+1}/{args.iterations}] Starting algo.train() at {time.strftime('%H:%M:%S')}")

        result = algo.train()
        print(f"[ITER {i+1}/{args.iterations}] algo.train() returned at {time.strftime('%H:%M:%S')}")

        iter_time = time.time() - iter_start

        # Extract metrics
        env_results = result.get("env_runners", {})
        reward = env_results.get("episode_reward_mean", 0.0)
        ep_len = env_results.get("episode_len_mean", 0.0)
        episodes = env_results.get("episodes_this_iter", 0)
        agent_steps = result.get("num_agent_steps_sampled", 0)

        # Handle nan values
        if reward is None or np.isnan(reward):
            reward = 0
        if ep_len is None or np.isnan(ep_len):
            ep_len = 0

        # Update cumulative counters
        cumulative_episodes += episodes
        cumulative_steps += agent_steps

        # Print progress
        current_iter = i + 1
        status_indicator = "✓" if episodes > 0 else "→"
        print(f"{status_indicator} {current_iter:>3}/{args.iterations:<3} {reward:>10.2f} {ep_len:>8.1f} "
              f"{episodes:>10} {agent_steps:>12} {iter_time:>7.1f}s {best_reward:>10.2f}")

        # Detailed breakdown every 10 iterations
        if i == 0 or (i + 1) % 10 == 0:
            print(f"\n  [ITERATION {current_iter} DETAILS]")
            if episodes > 0:
                reward_min = env_results.get("episode_reward_min", 0.0)
                reward_max = env_results.get("episode_reward_max", 0.0)
                print(f"    Episode Reward: mean={reward:.2f}, min={reward_min:.2f}, max={reward_max:.2f}")
                print(f"    Episode length: {ep_len:.1f} steps")
                print(f"    Episodes this iteration: {episodes}")
            print(f"    Agent steps this iteration: {agent_steps}")
            print(f"    Cumulative: {cumulative_episodes} episodes, {cumulative_steps} steps")

            # Show per-policy learner stats
            learner_all = result.get('info', {}).get('learner', {})
            for pol_name, pol_label in [("assault_policy", "Assault"), ("defend_policy", "Defend"), ("support_policy", "Support")]:
                pol_stats = learner_all.get(pol_name, {}).get('learner_stats', {})
                if pol_stats:
                    tl = pol_stats.get('total_loss', 'N/A')
                    vl = pol_stats.get('vf_loss', 'N/A')
                    pl = pol_stats.get('policy_loss', 'N/A')
                    ent = pol_stats.get('entropy', 'N/A')
                    if isinstance(tl, float):
                        print(f"    {pol_label}: total={tl:.4f}, policy={pl:.4f}, vf={vl:.4f}, entropy={ent:.2f}" if isinstance(ent, float) else f"    {pol_label}: total={tl:.4f}")
            print()

        # Numbered checkpoint (kept permanently)
        if (i + 1) % args.checkpoint_freq == 0:
            algo.save(output_dir)
            print(f"  >> Checkpoint saved at iteration {current_iter}\n")

        # Latest checkpoint (overwritten every latest_freq iterations for easy resume)
        if (i + 1) % args.latest_freq == 0:
            if os.path.exists(latest_dir):
                shutil.rmtree(latest_dir)
            algo.save(latest_dir)

        # Track best
        if reward > best_reward and not np.isnan(reward):
            best_reward = reward
            best_dir = os.path.join(output_dir, "best")
            algo.save(best_dir)
            print(f"  >> NEW BEST REWARD: {best_reward:.2f} (iteration {current_iter})\n")

    # Final save
    print("\n" + "="*80)
    print("TRAINING COMPLETE!")
    print("="*80)
    print(f"Total iterations: {args.iterations}")
    print(f"Total episodes: {cumulative_episodes}")
    print(f"Total agent steps: {cumulative_steps}")
    print(f"Best reward: {best_reward:.2f}")
    print(f"Output directory: {output_dir}")
    print("="*80 + "\n")

    algo.save(output_dir)
    print(f"Final model saved to: {output_dir}")

    # Export ONNX (3 separate models, one per strategy)
    best_dir = os.path.join(output_dir, "best")
    if os.path.exists(best_dir):
        algo.restore(os.path.abspath(best_dir))
    export_onnx(algo, output_dir)

    algo.stop()
    ray.shutdown()

    return output_dir


# ============================================================================
# VALIDATION MODE
# ============================================================================

def run_validation():
    """Run unit tests validating single-head policy, ONNX export, replay buffer, and value function."""
    import tempfile

    print("\n" + "="*80)
    print("MOC v10.2.1 - Validation Suite (Independent Single-Head Policies)")
    print("="*80 + "\n")

    passed = 0
    failed = 0

    def check(name, condition, detail=""):
        nonlocal passed, failed
        if condition:
            passed += 1
            print(f"  PASS: {name}")
        else:
            failed += 1
            print(f"  FAIL: {name} {detail}")

    # Create 3 independent policies
    policies = {}
    for strat_idx, strat_name in STRATEGY_NAMES.items():
        policies[strat_name] = SingleHeadPolicy_v10_2(
            obs_dim=49, strategy_dim=3, eqs_dim=6, strategy_name=strat_name
        )

    batch = 8
    obs = torch.randn(batch, 49)

    # --- Test 1: Forward pass shapes (each policy independently) ---
    print("[Test 1] Forward pass shape (all 3 policies)")
    for strat_idx, strat_name in STRATEGY_NAMES.items():
        strat = torch.full((batch,), strat_idx, dtype=torch.long)
        out = policies[strat_name](obs, strat)
        check(f"{strat_name} output shape (B, 6)", out.shape == (batch, 6), f"got {out.shape}")

    # --- Test 2: Output range [-1, 1] ---
    print("[Test 2] Output range")
    with torch.no_grad():
        large_obs = torch.randn(256, 49)
        for strat_idx, strat_name in STRATEGY_NAMES.items():
            strat = torch.full((256,), strat_idx, dtype=torch.long)
            out = policies[strat_name](large_obs, strat)
            check(f"{strat_name} all values in [-1, 1]",
                  (out >= -1.0).all().item() and (out <= 1.0).all().item(),
                  f"range [{out.min().item():.4f}, {out.max().item():.4f}]")

    # --- Test 3: Independent policies produce different outputs ---
    print("[Test 3] Independent policies produce different outputs")
    with torch.no_grad():
        test_obs = torch.randn(1, 49)
        outputs = []
        for strat_idx, strat_name in STRATEGY_NAMES.items():
            strat = torch.full((1,), strat_idx, dtype=torch.long)
            o = policies[strat_name](test_obs, strat)
            outputs.append(o.squeeze().numpy())
        diffs = []
        for i in range(3):
            for j in range(i+1, 3):
                diffs.append(np.linalg.norm(outputs[i] - outputs[j]))
    check("Different policies produce different outputs (random init)",
          all(d > 1e-4 for d in diffs),
          f"diffs={[f'{d:.6f}' for d in diffs]}")

    # --- Test 4: ONNX export & reload ---
    print("[Test 4] ONNX export and reload")
    try:
        import onnxruntime as ort
        policy = policies["Assault"]
        with tempfile.NamedTemporaryFile(suffix=".onnx", delete=False) as f:
            onnx_path = f.name
        policy.export_onnx(onnx_path, strategy_idx_value=0)
        sess = ort.InferenceSession(onnx_path)
        test_obs_np = test_obs.numpy()
        test_strat_np = np.array([0], dtype=np.int64)
        ort_out = sess.run(None, {
            "observation": test_obs_np,
            "strategy_index": test_strat_np
        })[0]
        with torch.no_grad():
            torch_out = policy(test_obs, torch.tensor([0])).numpy()
        max_err = np.abs(ort_out - torch_out).max()
        check("ONNX output matches PyTorch (error < 1e-4)", max_err < 1e-4, f"max_err={max_err:.6f}")
        os.unlink(onnx_path)
    except ImportError:
        print("  SKIP: onnxruntime not installed (pip install onnxruntime)")

    # --- Test 5: Replay buffer balanced sampling ---
    print("[Test 5] Replay buffer balanced sampling")
    buf = StrategyBalancedReplayBuffer(capacity=9000)
    for s in range(3):
        for _ in range(1000):
            t = Transition(
                state=np.zeros(49), commanded_strategy=s,
                eqs_weights=np.zeros(6), reward=0.0,
                next_state=np.zeros(49), done=False, info={}, log_prob=0.0
            )
            buf.add(t)
    sample = buf.sample(300)
    counts = {0: 0, 1: 0, 2: 0}
    for t in sample:
        counts[t.commanded_strategy] += 1
    fracs = {s: c / len(sample) for s, c in counts.items()}
    check("Each strategy 20-40% in sample",
          all(0.20 <= f <= 0.40 for f in fracs.values()),
          f"fracs={fracs}")

    # --- Test 6: Value function returns scalar ---
    print("[Test 6] Value function output")
    for strat_idx, strat_name in STRATEGY_NAMES.items():
        strat = torch.full((batch,), strat_idx, dtype=torch.long)
        with torch.no_grad():
            val = policies[strat_name].get_value(obs, strat)
        check(f"{strat_name} value shape (B,)", val.shape == (batch,), f"got {val.shape}")

    # --- Test 7: sample_action produces valid actions ---
    print("[Test 7] sample_action")
    for strat_idx, strat_name in STRATEGY_NAMES.items():
        strat = torch.full((batch,), strat_idx, dtype=torch.long)
        actions, log_probs = policies[strat_name].sample_action(obs, strat)
        check(f"{strat_name} sample_action shape", actions.shape == (batch, 6))
        check(f"{strat_name} sample_action range",
              (actions >= -1).all().item() and (actions <= 1).all().item())
        check(f"{strat_name} log_probs shape", log_probs.shape == (batch,))

    # --- Test 8: Parameter independence ---
    print("[Test 8] Parameter independence (no shared weights)")
    enc_params_assault = set(id(p) for p in policies["Assault"].state_encoder.parameters())
    enc_params_defend = set(id(p) for p in policies["Defend"].state_encoder.parameters())
    check("Assault and Defend encoders share no parameters",
          len(enc_params_assault & enc_params_defend) == 0)

    print("\n" + "="*80)
    print(f"Results: {passed} passed, {failed} failed out of {passed + failed} tests")
    print("="*80 + "\n")
    return failed == 0


# ============================================================================
# EVALUATION MODE
# ============================================================================

def evaluate_checkpoint(checkpoint_path: str):
    """Load an RLlib checkpoint and evaluate per-strategy EQS weight profiles."""
    print("\n" + "="*80)
    print("MOC v10.2.1 - Checkpoint Evaluation")
    print("="*80 + "\n")

    eqs_labels = [
        "EnemyObjProx", "AllyObjProx", "CoverDensity", "EnemyVis",
        "AllyProx", "CombatRange"
    ]

    strategy_policies = {
        0: ("assault_policy", "Assault"),
        1: ("defend_policy", "Defend"),
        2: ("support_policy", "Support"),
    }

    if checkpoint_path.endswith(".pt") or checkpoint_path.endswith(".pth"):
        # Single PyTorch model file — load as one policy and evaluate
        print(f"Loading PyTorch checkpoint: {checkpoint_path}")
        policy = SingleHeadPolicy_v10_2(obs_dim=49, strategy_dim=3, eqs_dim=6, strategy_name="loaded")
        state_dict = torch.load(checkpoint_path, map_location="cpu")
        policy.load_state_dict(state_dict)
        policy.eval()

        num_samples = 100
        test_obs = torch.randn(num_samples, 49)
        with torch.no_grad():
            strat_tensor = torch.zeros(num_samples, dtype=torch.long)
            outputs = policy(test_obs, strat_tensor)
            means = outputs.mean(dim=0).numpy()
            stds = outputs.std(dim=0).numpy()

            print(f"\n  EQS Weight Profile:")
            for i, label in enumerate(eqs_labels):
                bar_len = int((means[i] + 1) / 2 * 20)
                bar = "#" * bar_len + "." * (20 - bar_len)
                print(f"    {label:<15} {means[i]:>6.3f} +/- {stds[i]:.3f}  [{bar}]")

            std_vals = torch.exp(policy.log_std).detach().numpy()
            print(f"\n  Learned std: {np.round(std_vals, 3)}")
    else:
        # RLlib checkpoint — load all 3 policies
        print(f"Loading RLlib checkpoint: {checkpoint_path}")
        try:
            import ray
            ray.init(ignore_reinit_error=True, include_dashboard=False, logging_level="ERROR")
            register_custom_model()
            register_env()
            config = create_ppo_config()
            algo = config.build()
            algo.restore(checkpoint_path)

            num_samples = 100
            test_obs = torch.randn(num_samples, 49)

            print("\nPer-Strategy EQS Weight Profiles:")
            print("-"*70)

            for strat_idx, (policy_name, strat_label) in strategy_policies.items():
                rllib_policy = algo.get_policy(policy_name)
                if not rllib_policy:
                    print(f"  [WARN] {policy_name} not found, skipping")
                    continue

                policy = rllib_policy.model.policy
                policy.eval()

                with torch.no_grad():
                    strat_tensor = torch.full((num_samples,), strat_idx, dtype=torch.long)
                    outputs = policy(test_obs, strat_tensor)
                    means = outputs.mean(dim=0).numpy()
                    stds = outputs.std(dim=0).numpy()

                    print(f"\n  {strat_label} (strategy {strat_idx}):")
                    for i, label in enumerate(eqs_labels):
                        bar_len = int((means[i] + 1) / 2 * 20)
                        bar = "#" * bar_len + "." * (20 - bar_len)
                        print(f"    {label:<15} {means[i]:>6.3f} +/- {stds[i]:.3f}  [{bar}]")

                    std_vals = torch.exp(policy.log_std).detach().numpy()
                    print(f"  Learned std: {np.round(std_vals, 3)}")

            algo.stop()
            ray.shutdown()
        except Exception as e:
            print(f"\n[FATAL] Failed to load RLlib checkpoint: {e}")
            sys.exit(1)

    print("\n" + "="*80 + "\n")


# ============================================================================
# Main Entry Point
# ============================================================================

if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description="Train MOC v10.2 command-driven policy")
    parser.add_argument("--mode", type=str, default="rllib",
                       choices=["example", "rllib", "validate", "eval"],
                       help="Run mode: 'example' demo, 'rllib' training, 'validate' tests, 'eval' checkpoint")
    parser.add_argument("--iterations", type=int, default=100,
                       help="Number of training iterations (for rllib mode)")
    parser.add_argument("--checkpoint-freq", type=int, default=10,
                       help="Checkpoint frequency (for rllib mode)")
    parser.add_argument("--latest-freq", type=int, default=1,
                       help="How often to overwrite the 'latest' checkpoint (1 = every iteration)")
    parser.add_argument("--host", type=str, default=MOCv10_2TrainingConfig.HOST,
                       help="UE5 host address")
    parser.add_argument("--port", type=int, default=MOCv10_2TrainingConfig.PORT,
                       help="UE5 gRPC port")
    parser.add_argument("--checkpoint", type=str, default=None,
                       help="Checkpoint path (for eval mode)")
    parser.add_argument("--resume", type=str,
                       default=os.environ.get("RESUME_CHECKPOINT") or None,
                       help="RLlib checkpoint path to resume from (rllib mode)")

    args = parser.parse_args()

    if args.mode == "example":
        print("""
    ╔══════════════════════════════════════════════════════════════════════════╗
    ║                                                                          ║
    ║              v10.2 Command-Driven Policy Training Script                ║
    ║                                                                          ║
    ║  This script demonstrates the v10.2 architecture integration.           ║
    ║                                                                          ║
    ╚══════════════════════════════════════════════════════════════════════════╝
        """)

        example_training_integration()

        print("\nTo start full training:")
        print("  python phase1_policy_training_v10_2.py --mode rllib --iterations 100")
        print("\nSee v10.2Architecture.md for detailed specification.")

    elif args.mode == "validate":
        success = run_validation()
        sys.exit(0 if success else 1)

    elif args.mode == "eval":
        if not args.checkpoint:
            print("Error: --checkpoint required for eval mode")
            sys.exit(1)
        evaluate_checkpoint(args.checkpoint)

    elif args.mode == "rllib":
        # Update config from args
        MOCv10_2TrainingConfig.HOST = args.host
        MOCv10_2TrainingConfig.PORT = args.port

        if not RLLIB_AVAILABLE:
            print("Error: RLlib not available. Install with: pip install ray[rllib]")
            sys.exit(1)

        train_with_rllib(args)
