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
# Main Entry Point
# ============================================================================

if __name__ == '__main__':
    print("""
    ╔══════════════════════════════════════════════════════════════════════════╗
    ║                                                                          ║
    ║              v10.2 Command-Driven Policy Training Script                ║
    ║                                                                          ║
    ║  This script demonstrates the v10.2 architecture integration.           ║
    ║  For full training, integrate with your Schola environment.             ║
    ║                                                                          ║
    ╚══════════════════════════════════════════════════════════════════════════╝
    """)

    example_training_integration()

    print("\nNext Steps:")
    print("1. Update your Schola environment to pass commanded_strategy")
    print("2. Modify observation collection to use 52-dim local obs (no team state)")
    print("3. Update action space to Box([-1, 1]^8)")
    print("4. Connect TacticalParameterActuator_v10_2 in Blueprint")
    print("5. Run training loop with PPOTrainer_v10_2")
    print("\nSee ACTUATOR_v10.2_SUMMARY.md for detailed integration guide.")
