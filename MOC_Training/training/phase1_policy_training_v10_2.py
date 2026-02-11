"""
Phase 1: v10.2 Command-Driven Policy Training

Trains the multi-head policy network for the v10.2 Commander-Executor architecture.

Key Changes from v10.1:
- NO local MCTS - agents are pure executors
- Receives commanded strategy from Squad Commander (Assault/Defend/Support)
- Outputs 8-dim EQS weights in range [-1, 1] (not [0, 1])
- Local observation only (52-dim), no team-level planning

Architecture:
- Input: 52-dim local observation + commanded strategy (1-hot, 3-dim) = 55-dim
- Multi-head network: 3 strategy-specific heads (Assault/Defend/Support)
- Output: 8-dim EQS weights per head, range [-1, 1]

Action Space: Box([-1, 1]^8)
- [0]: EnemyObjectiveProximity
- [1]: AllyObjectiveProximity
- [2]: CoverDensity
- [3]: EnemyVisibility
- [4]: AllyProximity
- [5]: CombatRange
- [6]: PickupProximity
- [7]: HeightAdvantage

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


@dataclass
class Transition:
    """Single training transition for v10.2."""
    state: np.ndarray              # (52,) local observation
    commanded_strategy: int        # 0=Assault, 1=Defend, 2=Support
    eqs_weights: np.ndarray        # (8,) in range [-1, 1]
    reward: float
    next_state: np.ndarray         # (52,)
    done: bool
    info: Dict


class MultiHeadRLPolicy_v10_2(nn.Module):
    """
    v10.2 Multi-Head Policy for Command-Driven Execution.

    Architecture:
    - Input: 55-dim (52 local obs + 3 strategy one-hot)
    - Shared Encoder: [256, 256] ReLU
    - 3 Strategy Heads: Assault, Defend, Support
    - Each head outputs: 8-dim EQS weights (tanh activation for [-1, 1] range)
    """

    def __init__(
        self,
        obs_dim: int = 52,
        num_strategies: int = 3,
        eqs_dim: int = 8,
        hidden_dims: List[int] = [256, 256]
    ):
        super().__init__()

        self.obs_dim = obs_dim
        self.num_strategies = num_strategies
        self.eqs_dim = eqs_dim

        # Input: 52 (obs) + 3 (strategy one-hot) = 55
        input_dim = obs_dim + num_strategies

        # Shared state encoder
        encoder_layers = []
        prev_dim = input_dim
        for hidden_dim in hidden_dims:
            encoder_layers.extend([
                nn.Linear(prev_dim, hidden_dim),
                nn.ReLU(),
                nn.LayerNorm(hidden_dim)  # Stabilize training
            ])
            prev_dim = hidden_dim

        self.state_encoder = nn.Sequential(*encoder_layers)

        # Strategy-specific EQS weight heads
        # Each outputs 8-dim weights with tanh activation (range [-1, 1])
        final_dim = hidden_dims[-1]

        self.assault_head = nn.Sequential(
            nn.Linear(final_dim, 64),
            nn.ReLU(),
            nn.Linear(64, eqs_dim),
            nn.Tanh()  # Output range: [-1, 1]
        )

        self.defend_head = nn.Sequential(
            nn.Linear(final_dim, 64),
            nn.ReLU(),
            nn.Linear(64, eqs_dim),
            nn.Tanh()
        )

        self.support_head = nn.Sequential(
            nn.Linear(final_dim, 64),
            nn.ReLU(),
            nn.Linear(64, eqs_dim),
            nn.Tanh()
        )

        # Value heads (for advantage estimation)
        self.assault_value = nn.Sequential(
            nn.Linear(final_dim, 64),
            nn.ReLU(),
            nn.Linear(64, 1)
        )

        self.defend_value = nn.Sequential(
            nn.Linear(final_dim, 64),
            nn.ReLU(),
            nn.Linear(64, 1)
        )

        self.support_value = nn.Sequential(
            nn.Linear(final_dim, 64),
            nn.ReLU(),
            nn.Linear(64, 1)
        )

        print(f"[v10.2 Policy] Initialized: {obs_dim}-dim obs → {eqs_dim}-dim EQS weights")
        print(f"  Input: {input_dim} (52 obs + 3 strategy)")
        print(f"  Encoder: {hidden_dims}")
        print(f"  Heads: 3 strategies × 8 EQS weights")
        print(f"  Action Range: [-1, 1] (tanh activation)")

    def forward(
        self,
        obs: torch.Tensor,            # (B, 52)
        strategy_idx: torch.Tensor    # (B,) indices in [0, 1, 2]
    ) -> torch.Tensor:
        """
        Forward pass with commanded strategy.

        Args:
            obs: Local observation (B, 52)
            strategy_idx: Commanded strategy indices (B,)

        Returns:
            eqs_weights: (B, 8) in range [-1, 1]
        """
        batch_size = obs.shape[0]

        # Create strategy one-hot encoding
        strategy_onehot = torch.zeros(batch_size, self.num_strategies, device=obs.device)
        strategy_onehot.scatter_(1, strategy_idx.unsqueeze(1), 1.0)

        # Concatenate obs + strategy
        input_tensor = torch.cat([obs, strategy_onehot], dim=-1)

        # Encode
        features = self.state_encoder(input_tensor)

        # Route to appropriate head
        eqs_weights = torch.zeros(batch_size, self.eqs_dim, device=obs.device)

        for i in range(batch_size):
            strat = strategy_idx[i].item()

            if strat == 0:  # Assault
                eqs_weights[i] = self.assault_head(features[i:i+1]).squeeze(0)
            elif strat == 1:  # Defend
                eqs_weights[i] = self.defend_head(features[i:i+1]).squeeze(0)
            else:  # Support
                eqs_weights[i] = self.support_head(features[i:i+1]).squeeze(0)

        return eqs_weights

    def get_value(
        self,
        obs: torch.Tensor,
        strategy_idx: torch.Tensor
    ) -> torch.Tensor:
        """
        Compute value estimates for advantage calculation.

        Args:
            obs: Local observation (B, 52)
            strategy_idx: Commanded strategy indices (B,)

        Returns:
            values: (B,)
        """
        batch_size = obs.shape[0]

        # Create strategy one-hot
        strategy_onehot = torch.zeros(batch_size, self.num_strategies, device=obs.device)
        strategy_onehot.scatter_(1, strategy_idx.unsqueeze(1), 1.0)

        # Encode
        input_tensor = torch.cat([obs, strategy_onehot], dim=-1)
        features = self.state_encoder(input_tensor)

        # Route to appropriate value head
        values = torch.zeros(batch_size, device=obs.device)

        for i in range(batch_size):
            strat = strategy_idx[i].item()

            if strat == 0:  # Assault
                values[i] = self.assault_value(features[i:i+1]).squeeze()
            elif strat == 1:  # Defend
                values[i] = self.defend_value(features[i:i+1]).squeeze()
            else:  # Support
                values[i] = self.support_value(features[i:i+1]).squeeze()

        return values

    def export_onnx(self, filepath: str, batch_size: int = 1):
        """
        Export policy to ONNX format for UE5 inference.

        Expected usage in UE5:
        - Input: [obs (52), strategy_idx (1)]
        - Output: [eqs_weights (8)]
        """
        self.eval()

        # Create dummy inputs
        dummy_obs = torch.randn(batch_size, self.obs_dim)
        dummy_strategy = torch.zeros(batch_size, dtype=torch.long)

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

        print(f"[ONNX Export] Model saved to: {filepath}")
        print(f"  Input: observation(B, 52), strategy_index(B)")
        print(f"  Output: eqs_weights(B, 8) in [-1, 1]")

    def print_architecture(self):
        """Print model architecture summary."""
        total_params = sum(p.numel() for p in self.parameters())
        trainable_params = sum(p.numel() for p in self.parameters() if p.requires_grad)

        print("\n" + "="*80)
        print("v10.2 Multi-Head Policy Architecture")
        print("="*80)
        print(f"Total Parameters: {total_params:,}")
        print(f"Trainable Parameters: {trainable_params:,}")
        print("\nLayer Structure:")
        print(f"  State Encoder: {self.state_encoder}")
        print(f"  Assault Head: {self.assault_head}")
        print(f"  Defend Head: {self.defend_head}")
        print(f"  Support Head: {self.support_head}")
        print("="*80 + "\n")


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
    PPO trainer for v10.2 command-driven policy.
    """

    def __init__(
        self,
        policy: MultiHeadRLPolicy_v10_2,
        learning_rate: float = 3e-4,
        clip_epsilon: float = 0.2,
        value_coef: float = 0.5,
        entropy_coef: float = 0.01,
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

        # Optimizer
        self.optimizer = optim.Adam(policy.parameters(), lr=learning_rate)

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

        # Compute old log probabilities and values
        with torch.no_grad():
            old_log_probs = self._compute_log_prob(old_eqs_weights, old_eqs_weights)
            old_values = self.policy.get_value(states, strategy_idxs)

        # Compute advantages
        advantages, returns = self.compute_advantages(rewards, old_values, dones)
        advantages = (advantages - advantages.mean()) / (advantages.std() + 1e-8)

        # PPO update epochs
        metrics = defaultdict(list)

        for epoch in range(epochs):
            # Forward pass
            new_eqs_weights = self.policy(states, strategy_idxs)
            new_values = self.policy.get_value(states, strategy_idxs)

            # Compute new log probabilities
            new_log_probs = self._compute_log_prob(new_eqs_weights, old_eqs_weights)

            # Policy loss (PPO clip)
            ratio = torch.exp(new_log_probs - old_log_probs)
            surr1 = ratio * advantages
            surr2 = torch.clamp(ratio, 1 - self.clip_epsilon, 1 + self.clip_epsilon) * advantages
            policy_loss = -torch.min(surr1, surr2).mean()

            # Value loss
            value_loss = nn.MSELoss()(new_values, returns)

            # Entropy (encourage exploration)
            entropy = -new_log_probs.mean()

            # Total loss
            total_loss = policy_loss + self.value_coef * value_loss - self.entropy_coef * entropy

            # Backward pass
            self.optimizer.zero_grad()
            total_loss.backward()
            nn.utils.clip_grad_norm_(self.policy.parameters(), self.max_grad_norm)
            self.optimizer.step()

            # Log metrics
            metrics['policy_loss'].append(policy_loss.item())
            metrics['value_loss'].append(value_loss.item())
            metrics['entropy'].append(entropy.item())
            metrics['total_loss'].append(total_loss.item())

        # Average metrics
        return {key: np.mean(values) for key, values in metrics.items()}

    def _compute_log_prob(
        self,
        actions: torch.Tensor,
        means: torch.Tensor,
        std: float = 0.1
    ) -> torch.Tensor:
        """
        Compute log probability under Gaussian policy.

        Args:
            actions: (B, 8) - Sampled EQS weights
            means: (B, 8) - Mean from policy
            std: Standard deviation

        Returns:
            log_prob: (B,)
        """
        var = std ** 2
        log_prob = -0.5 * ((actions - means) ** 2 / var + np.log(2 * np.pi * var))
        return log_prob.sum(dim=-1)


# ============================================================================
# PYTHON INTEGRATION EXAMPLE
# ============================================================================

def example_training_integration():
    """
    Example showing how to integrate with Schola/UE5.

    This is the interface you'll use from your Python training script.
    """

    print("\n" + "="*80)
    print("v10.2 Python Training Integration Example")
    print("="*80 + "\n")

    # 1. Initialize policy
    policy = MultiHeadRLPolicy_v10_2(
        obs_dim=52,
        num_strategies=3,
        eqs_dim=8,
        hidden_dims=[256, 256]
    )
    policy.print_architecture()

    # 2. Initialize trainer
    trainer = PPOTrainer_v10_2(policy, device='cpu')  # or 'cuda'

    # 3. Create replay buffer
    replay_buffer = StrategyBalancedReplayBuffer(capacity=100000)

    print("Setup complete! Ready for training.\n")
    print("Integration Steps:")
    print("1. Connect to UE5 environment via Schola gRPC")
    print("2. Receive commanded strategy from Squad Commander")
    print("3. Collect local observation (52-dim)")
    print("4. Run policy inference:")
    print("   >>> obs_tensor = torch.tensor(obs, dtype=torch.float32)")
    print("   >>> strategy_tensor = torch.tensor([commanded_strategy], dtype=torch.long)")
    print("   >>> eqs_weights = policy(obs_tensor, strategy_tensor)  # Output: (1, 8)")
    print("5. Send eqs_weights to TacticalParameterActuator_v10_2 via Schola")
    print("6. Actuator calls TakeAction() and applies weights to AIController")
    print("7. Collect reward and next observation")
    print("8. Store transition and update policy with PPO")
    print("\n" + "="*80 + "\n")

    # Example inference
    print("Example Inference:")
    dummy_obs = torch.randn(1, 52)
    dummy_strategy = torch.tensor([0], dtype=torch.long)  # Assault

    with torch.no_grad():
        eqs_weights = policy(dummy_obs, dummy_strategy)
        print(f"  Input: obs(1, 52), strategy=Assault")
        print(f"  Output: eqs_weights = {eqs_weights.numpy()[0]}")
        print(f"  Range: [{eqs_weights.min().item():.2f}, {eqs_weights.max().item():.2f}]")
        print(f"  ✓ All values in [-1, 1]: {(eqs_weights >= -1).all() and (eqs_weights <= 1).all()}")

    print("\n" + "="*80)
    print("Ready to integrate with your training loop!")
    print("="*80 + "\n")


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
    class MultiHeadRLPolicy_v10_2_RLlib(TorchModelV2, nn.Module):
        """
        RLlib wrapper for MultiHeadRLPolicy_v10_2.

        This adapter allows the v10.2 policy to work with RLlib's PPO algorithm.
        """

        def __init__(self, obs_space, action_space, num_outputs, model_config, name):
            TorchModelV2.__init__(self, obs_space, action_space, num_outputs, model_config, name)
            nn.Module.__init__(self)

            # Extract configuration
            custom_config = model_config.get("custom_model_config", {})
            obs_dim = custom_config.get("obs_dim", 52)
            num_strategies = custom_config.get("num_strategies", 3)
            eqs_dim = custom_config.get("eqs_dim", 8)
            hidden_dims = custom_config.get("hidden_dims", [256, 256])

            # Create the core v10.2 policy
            self.policy = MultiHeadRLPolicy_v10_2(
                obs_dim=obs_dim,
                num_strategies=num_strategies,
                eqs_dim=eqs_dim,
                hidden_dims=hidden_dims
            )

            # RLlib expects num_outputs = action_dim * 2 for continuous actions (mean + log_std)
            self.num_outputs = num_outputs
            self.action_dim = eqs_dim

            # Store features for value function
            self._last_features = None
            self._last_strategy_idx = None

            print(f"[v10.2 RLlib] Initialized with obs_dim={obs_dim}, strategies={num_strategies}, eqs_dim={eqs_dim}")

        @override(TorchModelV2)
        def forward(self, input_dict, state, seq_lens):
            """
            Forward pass for RLlib.

            Args:
                input_dict: Contains 'obs' with shape (B, 55)
                state: RNN state (unused for feedforward)
                seq_lens: Sequence lengths (unused for feedforward)

            Returns:
                output: (B, num_outputs) - contains means and log_stds
                state: Unchanged RNN state
            """
            obs = input_dict["obs"]
            batch_size = obs.shape[0]

            # Extract base observation and strategy from obs
            # obs format: [52 base features, 3 strategy one-hot] = 55 total
            base_obs = obs[:, :52]
            strategy_onehot = obs[:, 52:55]
            strategy_idx = torch.argmax(strategy_onehot, dim=1)

            # Store for value function
            self._last_features = obs
            self._last_strategy_idx = strategy_idx

            # Get EQS weights from policy (these are the means)
            eqs_weights = self.policy(base_obs, strategy_idx)  # (B, 8)

            # For RLlib's continuous action space, we need to return [means, log_stds]
            # Use fixed log_std for simplicity (can be learned if needed)
            log_stds = torch.zeros(batch_size, self.action_dim, device=obs.device) - 0.5  # log(std) = -0.5 -> std ≈ 0.6

            # Concatenate means and log_stds
            output = torch.cat([eqs_weights, log_stds], dim=-1)  # (B, 16)

            return output, state

        @override(TorchModelV2)
        def value_function(self):
            """
            Compute value estimate for the last forward pass.

            Returns:
                values: (B,) value estimates
            """
            if self._last_features is None or self._last_strategy_idx is None:
                raise ValueError("Must call forward() before value_function()")

            # Extract base observation
            base_obs = self._last_features[:, :52]

            # Get value from policy
            values = self.policy.get_value(base_obs, self._last_strategy_idx)

            return values


# ============================================================================
# TRAINING CONFIGURATION
# ============================================================================

class MOCv10_2TrainingConfig:
    """Training configuration for MOC v10.2."""

    # Environment
    HOST = "localhost"
    PORT = 50051
    NUM_UE5_ENVIRONMENTS = 4

    # Network architecture
    HIDDEN_DIMS = [256, 256]

    # PPO hyperparameters
    LEARNING_RATE = 3e-4
    TRAIN_BATCH_SIZE = 32000
    SGD_MINIBATCH_SIZE = 2048
    NUM_SGD_ITER = 10
    GAMMA = 0.99
    GAE_LAMBDA = 0.95
    CLIP_PARAM = 0.2
    ENTROPY_COEFF = 0.01
    VF_LOSS_COEFF = 0.5
    GRAD_CLIP = 0.5
    VF_CLIP_PARAM = 10.0

    # Training
    NUM_WORKERS = 0  # Windows: single process
    NUM_ENVS_PER_WORKER = 1
    NUM_ITERATIONS = 100
    CHECKPOINT_FREQ = 10

    # Paths
    OUTPUT_DIR = "training_results_v10_2"


def create_env_config():
    """Create environment configuration for v10.2."""
    return {
        "host": MOCv10_2TrainingConfig.HOST,
        "base_port": MOCv10_2TrainingConfig.PORT,
        "num_envs": MOCv10_2TrainingConfig.NUM_UE5_ENVIRONMENTS,
    }


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
        rollout_fragment_length=256,
        batch_mode="truncate_episodes",
    )

    # Multi-agent
    config = config.multi_agent(
        policies={"shared_policy"},
        policy_mapping_fn=lambda agent_id, episode, worker, **kwargs: "shared_policy",
        count_steps_by="agent_steps",
    )

    # Debugging & Reporting
    config = config.debugging(log_level="WARN")
    config = config.reporting(
        metrics_num_episodes_for_smoothing=10,
        min_sample_timesteps_per_iteration=MOCv10_2TrainingConfig.TRAIN_BATCH_SIZE,
    )

    # Training
    config = config.training(
        lr=MOCv10_2TrainingConfig.LEARNING_RATE,
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

    # Model configuration
    config.model = {
        "custom_model": "multi_head_policy_v10_2",
        "custom_model_config": {
            "obs_dim": 52,
            "num_strategies": 3,
            "eqs_dim": 8,
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
    """Register v10.2 policy with RLlib."""
    if not RLLIB_AVAILABLE:
        return

    from ray.rllib.models import ModelCatalog

    ModelCatalog.register_custom_model("multi_head_policy_v10_2", MultiHeadRLPolicy_v10_2_RLlib)
    print("[v10.2] Multi-head policy registered")


def export_onnx(algo, output_dir):
    """Export trained policy to ONNX format for UE5."""
    try:
        policy = algo.get_policy("shared_policy")
        if not policy:
            print("ERROR: Could not get 'shared_policy'")
            return False

        model = policy.model.policy  # Unwrap the RLlib wrapper

        model.eval()

        # Export with strategy conditioning
        # UE5 will provide: [obs (52-dim), strategy_idx (scalar)]
        dummy_obs = torch.randn(1, 52)
        dummy_strategy = torch.zeros(1, dtype=torch.long)

        model_path = os.path.join(output_dir, "moc_policy_v10_2.onnx")

        torch.onnx.export(
            model,
            (dummy_obs, dummy_strategy),
            model_path,
            input_names=['observation', 'strategy_index'],
            output_names=['eqs_weights'],
            dynamic_axes={
                'observation': {0: 'batch_size'},
                'strategy_index': {0: 'batch_size'},
                'eqs_weights': {0: 'batch_size'}
            },
            opset_version=14
        )

        print(f"[ONNX Export] Model saved to: {model_path}")
        print(f"  Input: observation(B, 52), strategy_index(B)")
        print(f"  Output: eqs_weights(B, 8) in [-1, 1]")
        return True

    except Exception as e:
        print(f"ONNX export failed: {e}")
        import traceback
        traceback.print_exc()
        return False


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
        print(f"Ray init failed: {e}, using local mode")
        ray.init(local_mode=True, ignore_reinit_error=True)

    register_env()
    register_custom_model()

    # Create output directory
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = os.path.join(MOCv10_2TrainingConfig.OUTPUT_DIR, f"v10_2_{timestamp}")
    os.makedirs(output_dir, exist_ok=True)
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

        result = algo.train()

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

            # Show learner stats
            learner_info = result.get('info', {}).get('learner', {}).get('shared_policy', {}).get('learner_stats', {})
            if learner_info:
                total_loss = learner_info.get('total_loss', 'N/A')
                vf_loss = learner_info.get('vf_loss', 'N/A')
                policy_loss = learner_info.get('policy_loss', 'N/A')
                entropy = learner_info.get('entropy', 'N/A')
                print(f"    Loss: total={total_loss:.4f}, policy={policy_loss:.4f}, vf={vf_loss:.4f}" if isinstance(total_loss, float) else f"    Loss: {total_loss}")
                if isinstance(entropy, float):
                    print(f"    Entropy: {entropy:.2f}")
            print()

        # Checkpoint
        if (i + 1) % args.checkpoint_freq == 0:
            algo.save(output_dir)
            print(f"  >> Checkpoint saved at iteration {current_iter}\n")

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

    # Export ONNX
    best_dir = os.path.join(output_dir, "best")
    if os.path.exists(best_dir):
        algo.restore(os.path.abspath(best_dir))
    export_onnx(algo, output_dir)

    algo.stop()
    ray.shutdown()

    return output_dir


# ============================================================================
# Main Entry Point
# ============================================================================

if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description="Train MOC v10.2 command-driven policy")
    parser.add_argument("--mode", type=str, default="rllib", choices=["example", "rllib"],
                       help="Run mode: 'example' for architecture demo, 'rllib' for full training")
    parser.add_argument("--iterations", type=int, default=100,
                       help="Number of training iterations (for rllib mode)")
    parser.add_argument("--checkpoint-freq", type=int, default=10,
                       help="Checkpoint frequency (for rllib mode)")
    parser.add_argument("--host", type=str, default=MOCv10_2TrainingConfig.HOST,
                       help="UE5 host address")
    parser.add_argument("--port", type=int, default=MOCv10_2TrainingConfig.PORT,
                       help="UE5 gRPC port")

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

    elif args.mode == "rllib":
        # Update config from args
        MOCv10_2TrainingConfig.HOST = args.host
        MOCv10_2TrainingConfig.PORT = args.port

        if not RLLIB_AVAILABLE:
            print("Error: RLlib not available. Install with: pip install ray[rllib]")
            sys.exit(1)

        train_with_rllib(args)
