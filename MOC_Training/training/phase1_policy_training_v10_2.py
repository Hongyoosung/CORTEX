"""
Trains the multi-head policy network for the v10.2 Commander-Executor architecture.

Key Changes from v10.1:
- NO local MCTS - agents are pure executors
- Receives commanded strategy from Squad Commander (Assault/Defend/Support)
- Outputs 7-dim EQS weights in range [-1, 1] (Corrected from 8-dim)
- Local observation only (49-dim base: includes 44 tactical + 5 capture point statuses)

Architecture:
- Input: 52-dim total (49-dim base observation + 3-dim strategy 1-hot)
- Multi-head network: 3 strategy-specific heads (Assault/Defend/Support)
- Output: 7-dim EQS weights per head, range [-1, 1]

Action Space: Box([-1, 1]^7)
- [0]: EnemyObjectiveProximity
- [1]: AllyObjectiveProximity
- [2]: CoverDensity
- [3]: EnemyVisibility
- [4]: AllyProximity
- [5]: CombatRange
- [6]: PickupProximity

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


class MultiHeadRLPolicy_v10_2(nn.Module):
    """
    v10.2 Multi-Head Policy for Command-Driven Execution.

    Architecture:
    - Input: 52-dim (49 local obs + 3 strategy one-hot)
    - Shared Encoder: [256, 256] ReLU
    - 3 Strategy Heads: Assault, Defend, Support
    - Each head outputs: 7-dim EQS weights (tanh activation for [-1, 1] range)
    """

    def __init__(
        self,
        obs_dim: int = 52,
        num_strategies: int = 3,
        eqs_dim: int = 7,
        hidden_dims: List[int] = [256, 256]
    ):
        super().__init__()

        self.obs_dim = obs_dim
        self.num_strategies = num_strategies
        self.eqs_dim = eqs_dim

        # Input: 49 (obs) + 3 (strategy one-hot) = 52
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
        # Each outputs 7-dim weights with tanh activation (range [-1, 1])
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

        # Per-head learnable log standard deviation — separate so each strategy can
        # independently tune its exploration variance without gradient conflicts.
        self.assault_log_std = nn.Parameter(torch.zeros(eqs_dim) - 0.5)  # init std ≈ 0.6
        self.defend_log_std  = nn.Parameter(torch.zeros(eqs_dim) - 0.5)
        self.support_log_std = nn.Parameter(torch.zeros(eqs_dim) - 0.5)

        print(f"[v10.2 Policy] Initialized: {obs_dim}-dim obs → {eqs_dim}-dim EQS weights")
        print(f"  Input: {input_dim} (49 obs + 3 strategy)")
        print(f"  Encoder: {hidden_dims}")
        print(f"  Heads: 3 strategies × 7 EQS weights")
        print(f"  Action Range: [-1, 1] (tanh activation)")
        print(f"  Learnable log_std: per-head ({eqs_dim}-dim × 3 strategies)")

    def forward(
        self,
        obs: torch.Tensor,            # (B, 49)
        strategy_idx: torch.Tensor    # (B,) indices in [0, 1, 2]
    ) -> torch.Tensor:
        """
        Forward pass with commanded strategy.

        Args:
            obs: Local observation (B, 49)
            strategy_idx: Commanded strategy indices (B,)

        Returns:
            eqs_weights: (B, 7) in range [-1, 1]
        """
        batch_size = obs.shape[0]

        # Create strategy one-hot encoding
        strategy_onehot = torch.zeros(batch_size, self.num_strategies, device=obs.device)
        strategy_onehot.scatter_(1, strategy_idx.unsqueeze(1), 1.0)

        # Concatenate obs + strategy
        input_tensor = torch.cat([obs, strategy_onehot], dim=-1)

        # Encode
        features = self.state_encoder(input_tensor)

        # Route to appropriate head (vectorized — no data-dependent branching)
        assault_out = self.assault_head(features)   # (B, 7)
        defend_out  = self.defend_head(features)    # (B, 7)
        support_out = self.support_head(features)   # (B, 7)

        assault_mask = (strategy_idx == 0).float().unsqueeze(1)  # (B, 1)
        defend_mask  = (strategy_idx == 1).float().unsqueeze(1)
        support_mask = (strategy_idx == 2).float().unsqueeze(1)

        return assault_out * assault_mask + defend_out * defend_mask + support_out * support_mask

    def _get_std(self, strategy_idx: torch.Tensor) -> torch.Tensor:
        """
        Build per-sample std tensor by routing each sample to its strategy's log_std.

        Returns:
            std: (B, eqs_dim)
        """
        assault_std = torch.exp(self.assault_log_std).clamp(min=1e-6)  # (eqs_dim,)
        defend_std  = torch.exp(self.defend_log_std).clamp(min=1e-6)
        support_std = torch.exp(self.support_log_std).clamp(min=1e-6)

        assault_mask = (strategy_idx == 0).float().unsqueeze(1)  # (B, 1)
        defend_mask  = (strategy_idx == 1).float().unsqueeze(1)
        support_mask = (strategy_idx == 2).float().unsqueeze(1)

        return assault_std * assault_mask + defend_std * defend_mask + support_std * support_mask

    def sample_action(
        self,
        obs: torch.Tensor,
        strategy_idx: torch.Tensor
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        """
        Sample actions from Gaussian policy.

        Returns:
            actions: (B, 7) sampled EQS weights clamped to [-1, 1]
            log_probs: (B,) log probabilities
        """
        means = self.forward(obs, strategy_idx)
        std = self._get_std(strategy_idx)
        dist = torch.distributions.Normal(means, std)
        raw_actions = dist.rsample()
        log_probs = dist.log_prob(raw_actions).sum(dim=-1)
        actions = raw_actions.clamp(-1.0, 1.0)
        return actions, log_probs

    def compute_log_prob(
        self,
        obs: torch.Tensor,
        strategy_idx: torch.Tensor,
        actions: torch.Tensor
    ) -> torch.Tensor:
        """
        Compute log probability of given actions under current policy.

        Returns:
            log_probs: (B,)
        """
        means = self.forward(obs, strategy_idx)
        std = self._get_std(strategy_idx)
        dist = torch.distributions.Normal(means, std)
        return dist.log_prob(actions).sum(dim=-1)

    def get_value(
        self,
        obs: torch.Tensor,
        strategy_idx: torch.Tensor
    ) -> torch.Tensor:
        """
        Compute value estimates for advantage calculation.

        Args:
            obs: Local observation (B, 49)
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

        # Route to appropriate value head (vectorized)
        assault_val = self.assault_value(features).squeeze(1)   # (B,)
        defend_val  = self.defend_value(features).squeeze(1)
        support_val = self.support_value(features).squeeze(1)

        assault_mask = (strategy_idx == 0).float()
        defend_mask  = (strategy_idx == 1).float()
        support_mask = (strategy_idx == 2).float()

        return assault_val * assault_mask + defend_val * defend_mask + support_val * support_mask

    def export_onnx(self, filepath: str, batch_size: int = 1):
        """
        Export policy to ONNX format for UE5 inference.

        Expected usage in UE5:
        - Input: [obs (49), strategy_idx (1)]
        - Output: [eqs_weights (7)]
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
        print(f"  Input: observation(B, 49), strategy_index(B)")
        print(f"  Output: eqs_weights(B, 7) in [-1, 1]")

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
        critic_lr_scale: float = 0.3,      # FIX (Issue 1): Critic gets lower LR
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

        # FIX (Issue 1 — Critic Collapse): Separate actor/critic learning rates.
        # Using a single LR causes the actor gradient (dominated by the clear
        # Assault reward signal) to corrupt the shared encoder and destabilize
        # the critic for Defend/Support strategies.
        # Solution: one Adam with two param-groups — critic heads run at
        # critic_lr_scale × learning_rate (default 30% of actor LR).
        encoder_params = list(policy.state_encoder.parameters())
        actor_params = (
            list(policy.assault_head.parameters()) +
            list(policy.defend_head.parameters()) +
            list(policy.support_head.parameters()) +
            [policy.assault_log_std, policy.defend_log_std, policy.support_log_std]
        )
        critic_params = (
            list(policy.assault_value.parameters()) +
            list(policy.defend_value.parameters()) +
            list(policy.support_value.parameters())
        )
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

        # Per-strategy advantage normalization: normalize within each strategy group
        # so that the different reward scales (Assault positive, Defend/Support smaller)
        # don't cause one strategy to dominate the gradient.
        for strat_idx in range(3):
            mask = (strategy_idxs == strat_idx)
            if mask.sum() > 1:
                strat_adv = advantages[mask]
                advantages[mask] = (strat_adv - strat_adv.mean()) / (strat_adv.std() + 1e-8)
        # Fallback: if only one strategy present, normalize globally
        if len(torch.unique(strategy_idxs)) == 1:
            advantages = (advantages - advantages.mean()) / (advantages.std() + 1e-8)

        # PPO update epochs
        metrics = defaultdict(list)
        strategy_names = {0: "assault", 1: "defend", 2: "support"}

        for epoch in range(epochs):
            # Recompute log probs and values under current policy
            new_log_probs = self.policy.compute_log_prob(states, strategy_idxs, old_eqs_weights)
            new_values = self.policy.get_value(states, strategy_idxs)

            # Per-head entropy: mean over the batch of each head's entropy
            per_head_entropy = {}
            for strat_idx, name in strategy_names.items():
                log_std_param = getattr(self.policy, f"{name}_log_std")
                std = torch.exp(log_std_param).clamp(min=1e-6)
                per_head_entropy[name] = (0.5 * torch.log(2 * np.pi * np.e * std ** 2)).sum()
            # Weighted entropy for total loss: average across active strategies in batch
            entropy = sum(per_head_entropy.values()) / len(per_head_entropy)

            # Policy loss (PPO clip) — per-strategy breakdown
            ratio = torch.exp(new_log_probs - old_log_probs)
            surr1 = ratio * advantages
            surr2 = torch.clamp(ratio, 1 - self.clip_epsilon, 1 + self.clip_epsilon) * advantages
            clipped = -torch.min(surr1, surr2)
            policy_loss = clipped.mean()

            # Value loss — per-strategy breakdown
            value_loss = nn.MSELoss()(new_values, returns)

            # Total loss
            total_loss = policy_loss + self.value_coef * value_loss - self.entropy_coef * entropy

            # Backward pass
            self.optimizer.zero_grad()
            total_loss.backward()
            nn.utils.clip_grad_norm_(self.policy.parameters(), self.max_grad_norm)
            self.optimizer.step()

            # Aggregate metrics
            metrics['policy_loss'].append(policy_loss.item())
            metrics['value_loss'].append(value_loss.item())
            metrics['entropy'].append(entropy.item())
            metrics['total_loss'].append(total_loss.item())
            metrics['approx_kl'].append((old_log_probs - new_log_probs).mean().item())

            # Per-strategy metrics (computed without grad for logging only)
            with torch.no_grad():
                for strat_idx, name in strategy_names.items():
                    mask = (strategy_idxs == strat_idx)
                    if mask.sum() == 0:
                        continue
                    metrics[f'policy_loss/{name}'].append(clipped[mask].mean().item())
                    metrics[f'value_loss/{name}'].append(
                        nn.MSELoss()(new_values[mask], returns[mask]).item()
                    )
                    metrics[f'entropy/{name}'].append(per_head_entropy[name].item())
                    metrics[f'approx_kl/{name}'].append(
                        (old_log_probs[mask] - new_log_probs[mask]).mean().item()
                    )

        # Average metrics across epochs
        return {key: np.mean(values) for key, values in metrics.items()}



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
        obs_dim=49,
        num_strategies=3,
        eqs_dim=7,
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
    print("3. Collect local observation (49-dim: 44 base + 5 capture point statuses)")
    print("4. Run policy inference:")
    print("   >>> obs_tensor = torch.tensor(obs, dtype=torch.float32)")
    print("   >>> strategy_tensor = torch.tensor([commanded_strategy], dtype=torch.long)")
    print("   >>> eqs_weights = policy(obs_tensor, strategy_tensor)  # Output: (1, 7)")
    print("5. Send eqs_weights to TacticalParameterActuator_v10_2 via Schola")
    print("6. Actuator calls TakeAction() and applies weights to AIController")
    print("7. Collect reward and next observation")
    print("8. Store transition and update policy with PPO")
    print("\n" + "="*80 + "\n")

    # Example inference
    print("Example Inference:")
    dummy_obs = torch.randn(1, 49)
    dummy_strategy = torch.tensor([0], dtype=torch.long)  # Assault

    with torch.no_grad():
        eqs_weights = policy(dummy_obs, dummy_strategy)
        print(f"  Input: obs(1, 49), strategy=Assault")
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
            obs_dim = custom_config.get("obs_dim", 49)
            num_strategies = custom_config.get("num_strategies", 3)
            eqs_dim = custom_config.get("eqs_dim", 7)
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
                input_dict: Contains 'obs' with shape (B, 52)
                state: RNN state (unused for feedforward)
                seq_lens: Sequence lengths (unused for feedforward)

            Returns:
                output: (B, num_outputs) - contains means and log_stds
                state: Unchanged RNN state
            """
            obs = input_dict["obs"]
            batch_size = obs.shape[0]

            # Extract base observation and strategy from obs
            # obs format: [49 base features, 3 strategy one-hot] = 52 total
            base_obs = obs[:, :49]
            strategy_onehot = obs[:, 49:52]
            strategy_idx = torch.argmax(strategy_onehot, dim=1)

            # Store for value function
            self._last_features = obs
            self._last_strategy_idx = strategy_idx

            # Get EQS weights from policy (these are the means)
            eqs_weights = self.policy(base_obs, strategy_idx)  # (B, 8)

            # For RLlib's continuous action space, return [means, log_stds].
            # FIX (Issue 2 — Entropy Flattening): Route per-head LEARNED log_std by
            # strategy instead of using a hardcoded constant.  The old constant
            # (-0.5 for every dim, every strategy) pinned entropy at a permanent
            # 6.43, making the entropy schedule a no-op and killing exploration.
            assault_log_std = self.policy.assault_log_std  # (eqs_dim,) Parameter
            defend_log_std  = self.policy.defend_log_std
            support_log_std = self.policy.support_log_std

            assault_mask = (strategy_idx == 0).float().unsqueeze(1)  # (B, 1)
            defend_mask  = (strategy_idx == 1).float().unsqueeze(1)
            support_mask = (strategy_idx == 2).float().unsqueeze(1)

            # Select the appropriate per-head log_std for each sample (B, eqs_dim)
            log_stds = (assault_log_std * assault_mask +
                        defend_log_std  * defend_mask  +
                        support_log_std * support_mask)

            # Concatenate means and log_stds
            output = torch.cat([eqs_weights, log_stds], dim=-1)  # (B, 14)

            

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
            base_obs = self._last_features[:, :49]

            # Get value from policy
            values = self.policy.get_value(base_obs, self._last_strategy_idx)

            return values


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
    # FIX (Issue 1 — Critic Collapse): Larger batch + more SGD epochs give the
    # critic more gradient steps per iteration, stabilising vf_explained_var.
    LEARNING_RATE = 3e-4
    TRAIN_BATCH_SIZE = 20000          # was 10000 — larger batch reduces reward-variance noise for critic
    SGD_MINIBATCH_SIZE = 512          # ~39 minibatches per epoch (20000 ÷ 512)
    NUM_SGD_ITER = 10                 # was 5  — more critic update steps per iteration
    GAMMA = 0.99
    GAE_LAMBDA = 0.95
    CLIP_PARAM = 0.2
    ENTROPY_COEFF = 0.01
    VF_LOSS_COEFF = 1.0               # was 0.5 — give critic loss equal weight to actor
    GRAD_CLIP = 0.5
    VF_CLIP_PARAM = 10.0

    # Training
    NUM_WORKERS = int(os.environ.get('NUM_WORKERS', 0))
    NUM_ENVS_PER_WORKER = 1
    NUM_ITERATIONS = int(os.environ.get('NUM_ITERATIONS', 100))
    CHECKPOINT_FREQ = 10

    # Schedules
    LR_SCHEDULE = [[0, 3e-4], [50000, 1e-4], [100000, 5e-5]]
    ENTROPY_COEFF_SCHEDULE = [[0, 0.01], [50000, 0.005], [100000, 0.001]]

    # Paths
    OUTPUT_DIR = "training_results_v10_2"


def create_env_config():
    """Create environment configuration for v10.2."""
    return {
        "host": MOCv10_2TrainingConfig.HOST,
        "base_port": MOCv10_2TrainingConfig.PORT,
        "num_envs": MOCv10_2TrainingConfig.NUM_UE5_ENVIRONMENTS,
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
            """Extract per-strategy loss metrics from learner_stats and re-publish
            them as top-level custom_metrics so TensorBoard writes them to
            ray/tune/custom_metrics/* alongside the per-strategy reward curves."""
            learner_stats = (
                result.get("info", {})
                      .get("learner", {})
                      .get("shared_policy", {})
                      .get("learner_stats", {})
            )
            if not learner_stats:
                return

            per_strategy_keys = [
                "policy_loss", "value_loss", "entropy", "approx_kl"
            ]
            for key in per_strategy_keys:
                for name in ["assault", "defend", "support"]:
                    stat_key = f"{key}/{name}"
                    if stat_key in learner_stats:
                        result.setdefault("custom_metrics", {})[
                            f"train_{key}_{name}"
                        ] = learner_stats[stat_key]


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
        rollout_fragment_length=300,  # Matches episode length for clean episode boundaries
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

    # Multi-agent
    config = config.multi_agent(
        policies={"shared_policy"},
        policy_mapping_fn=lambda agent_id, episode, worker, **kwargs: "shared_policy",
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

    # Model configuration
    config.model = {
        "custom_model": "multi_head_policy_v10_2",
        "custom_model_config": {
            "obs_dim": 49,
            "num_strategies": 3,
            "eqs_dim": 7,
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
    policy = algo.get_policy("shared_policy")
    if not policy:
        print("\n[FATAL] export_onnx: Could not get 'shared_policy'")
        sys.exit(1)

    model = policy.model.policy  # Unwrap the RLlib wrapper
    model.eval()

    dummy_obs = torch.randn(1, 49)
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
    print(f"  Input: observation(B, 49), strategy_index(B)")
    print(f"  Output: eqs_weights(B, 7) in [-1, 1]")


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
# VALIDATION MODE
# ============================================================================

def run_validation():
    """Run unit tests validating policy, ONNX export, replay buffer, and value function."""
    import tempfile

    print("\n" + "="*80)
    print("MOC v10.2 - Validation Suite")
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

    policy = MultiHeadRLPolicy_v10_2(obs_dim=49, num_strategies=3, eqs_dim=7)

    # --- Test 1: Forward pass shapes ---
    print("[Test 1] Forward pass shape")
    batch = 8
    obs = torch.randn(batch, 49)
    strat = torch.randint(0, 3, (batch,))
    out = policy(obs, strat)
    check("Output shape (B, 7)", out.shape == (batch, 7), f"got {out.shape}")

    # --- Test 2: Output range [-1, 1] ---
    print("[Test 2] Output range")
    with torch.no_grad():
        large_obs = torch.randn(256, 49)
        large_strat = torch.randint(0, 3, (256,))
        large_out = policy(large_obs, large_strat)
    check("All values in [-1, 1]",
          (large_out >= -1.0).all().item() and (large_out <= 1.0).all().item(),
          f"range [{large_out.min().item():.4f}, {large_out.max().item():.4f}]")

    # --- Test 3: Strategy head distinctness ---
    print("[Test 3] Strategy head distinctness")
    with torch.no_grad():
        test_obs = torch.randn(1, 49)
        outputs = []
        for s in range(3):
            o = policy(test_obs, torch.tensor([s]))
            outputs.append(o.squeeze().numpy())
        # Check pairwise differences
        diffs = []
        for i in range(3):
            for j in range(i+1, 3):
                diffs.append(np.linalg.norm(outputs[i] - outputs[j]))
    check("Different strategies produce different outputs",
          all(d > 1e-4 for d in diffs),
          f"diffs={[f'{d:.6f}' for d in diffs]}")

    # --- Test 4: ONNX export & reload ---
    print("[Test 4] ONNX export and reload")
    try:
        import onnxruntime as ort
        with tempfile.NamedTemporaryFile(suffix=".onnx", delete=False) as f:
            onnx_path = f.name
        policy.export_onnx(onnx_path)
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
                eqs_weights=np.zeros(7), reward=0.0,
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
    with torch.no_grad():
        val = policy.get_value(obs, strat)
    check("Value shape (B,)", val.shape == (batch,), f"got {val.shape}")

    # --- Test 7: sample_action produces valid actions ---
    print("[Test 7] sample_action")
    actions, log_probs = policy.sample_action(obs, strat)
    check("sample_action shape", actions.shape == (batch, 7))
    check("sample_action range", (actions >= -1).all().item() and (actions <= 1).all().item())
    check("log_probs shape", log_probs.shape == (batch,))

    print("\n" + "="*80)
    print(f"Results: {passed} passed, {failed} failed out of {passed + failed} tests")
    print("="*80 + "\n")
    return failed == 0


# ============================================================================
# EVALUATION MODE
# ============================================================================

def evaluate_checkpoint(checkpoint_path: str):
    """Load a checkpoint and evaluate per-strategy EQS weight profiles."""
    print("\n" + "="*80)
    print("MOC v10.2 - Checkpoint Evaluation")
    print("="*80 + "\n")

    # Try loading as raw PyTorch model first
    policy = MultiHeadRLPolicy_v10_2(obs_dim=49, num_strategies=3, eqs_dim=7)

    if checkpoint_path.endswith(".pt") or checkpoint_path.endswith(".pth"):
        print(f"Loading PyTorch checkpoint: {checkpoint_path}")
        state_dict = torch.load(checkpoint_path, map_location="cpu")
        policy.load_state_dict(state_dict)
    else:
        # Try RLlib checkpoint
        print(f"Loading RLlib checkpoint: {checkpoint_path}")
        try:
            import ray
            ray.init(ignore_reinit_error=True, include_dashboard=False, logging_level="ERROR")
            register_custom_model()
            register_env()
            config = create_ppo_config()
            algo = config.build()
            algo.restore(checkpoint_path)
            rllib_policy = algo.get_policy("shared_policy")
            policy = rllib_policy.model.policy
            print("Loaded from RLlib checkpoint")
        except Exception as e:
            print(f"\n[FATAL] Failed to load RLlib checkpoint: {e}")
            sys.exit(1)

    policy.eval()

    # Generate evaluation data
    print("\nPer-Strategy EQS Weight Profiles:")
    print("-"*70)
    eqs_labels = [
        "EnemyObjProx", "AllyObjProx", "CoverDensity", "EnemyVis",
        "AllyProx", "CombatRange", "PickupProx"
    ]

    num_samples = 100
    test_obs = torch.randn(num_samples, 49)

    with torch.no_grad():
        for strat_idx, strat_name in STRATEGY_NAMES.items():
            strat_tensor = torch.full((num_samples,), strat_idx, dtype=torch.long)
            outputs = policy(test_obs, strat_tensor)
            means = outputs.mean(dim=0).numpy()
            stds = outputs.std(dim=0).numpy()

            print(f"\n  {strat_name} (strategy {strat_idx}):")
            for i, label in enumerate(eqs_labels):
                bar_len = int((means[i] + 1) / 2 * 20)  # map [-1,1] to [0,20]
                bar = "#" * bar_len + "." * (20 - bar_len)
                print(f"    {label:<15} {means[i]:>6.3f} +/- {stds[i]:.3f}  [{bar}]")

    # Per-head learned std
    for name in ["assault", "defend", "support"]:
        std_vals = torch.exp(getattr(policy, f"{name}_log_std")).detach().numpy()
        print(f"\n  Learned std ({name}): {np.round(std_vals, 3)}")

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
    parser.add_argument("--host", type=str, default=MOCv10_2TrainingConfig.HOST,
                       help="UE5 host address")
    parser.add_argument("--port", type=int, default=MOCv10_2TrainingConfig.PORT,
                       help="UE5 gRPC port")
    parser.add_argument("--checkpoint", type=str, default=None,
                       help="Checkpoint path (for eval mode)")

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
