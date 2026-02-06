"""
Phase 1: Option-Conditioned RL Policy Training

Trains the multi-head policy network to learn strategy-specific behaviors.
Uses random option sampling to collect balanced dataset across all 5 strategies.

Target:
- 100,000 transitions
- Strategy diversity: >20% per option
- Training stability: No divergence
- Combat capability: >40% win rate vs random

See v10.0Architecture.md Section 5.1 for full specification.
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

from models.multi_head_policy import MultiHeadRLPolicy
from schola_bridge import MocEnvironment, MocMultiAgentEnvironment
from training.reward_functions import StrategyRewards


@dataclass
class Transition:
    """Single training transition."""
    state: np.ndarray           # (52,)
    option_idx: int             # 0-4
    target_pos: np.ndarray      # (3,)
    duration: float             # seconds
    eqs_weights: np.ndarray     # (8,)
    reward: float
    next_state: np.ndarray      # (52,)
    done: bool
    info: Dict


class StrategyBalancedReplayBuffer:
    """
    Replay buffer with balanced strategy sampling.
    Ensures each strategy gets >20% representation.
    """

    def __init__(self, capacity: int = 100000):
        self.capacity = capacity
        self.buffers = {
            strategy: deque(maxlen=capacity // 5)
            for strategy in range(5)
        }
        self.total_count = 0

    def add(self, transition: Transition):
        """Add transition to appropriate strategy buffer."""
        strategy = transition.option_idx
        self.buffers[strategy].append(transition)
        self.total_count += 1

    def sample(self, batch_size: int) -> List[Transition]:
        """Sample batch with balanced strategy distribution."""
        samples_per_strategy = batch_size // 5

        batch = []
        for strategy in range(5):
            buffer = self.buffers[strategy]
            if len(buffer) < samples_per_strategy:
                # If not enough samples, take all available
                batch.extend(list(buffer))
            else:
                # Random sampling from this strategy
                indices = np.random.choice(len(buffer), samples_per_strategy, replace=False)
                batch.extend([buffer[i] for i in indices])

        # Shuffle batch
        np.random.shuffle(batch)
        return batch

    def get_strategy_distribution(self) -> Dict[int, float]:
        """Get current distribution of strategies in buffer."""
        total = sum(len(buf) for buf in self.buffers.values())
        if total == 0:
            return {i: 0.0 for i in range(5)}

        return {
            strategy: len(self.buffers[strategy]) / total
            for strategy in range(5)
        }

    def size(self) -> int:
        """Total number of transitions in buffer."""
        return sum(len(buf) for buf in self.buffers.values())

    def __len__(self) -> int:
        return self.size()


class PPOTrainer:
    """
    Proximal Policy Optimization trainer for multi-head policy.
    """

    def __init__(
        self,
        policy: MultiHeadRLPolicy,
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

        # Value network (shared backbone, separate head)
        self.value_head = nn.Sequential(
            nn.Linear(256, 128),
            nn.ReLU(),
            nn.Linear(128, 1)
        ).to(device)

    def compute_advantages(
        self,
        rewards: torch.Tensor,
        values: torch.Tensor,
        dones: torch.Tensor,
        gamma: float = 0.99,
        gae_lambda: float = 0.95
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        """
        Compute Generalized Advantage Estimation (GAE).

        Args:
            rewards: (T,)
            values: (T,)
            dones: (T,)
            gamma: Discount factor
            gae_lambda: GAE lambda parameter

        Returns:
            advantages: (T,)
            returns: (T,)
        """
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
        """
        Update policy using PPO algorithm.

        Args:
            batch: List of transitions
            epochs: Number of PPO update epochs

        Returns:
            Training metrics dictionary
        """
        # Convert batch to tensors
        states = torch.tensor(
            np.array([t.state for t in batch]),
            dtype=torch.float32,
            device=self.device
        )
        option_idxs = torch.tensor(
            [t.option_idx for t in batch],
            dtype=torch.long,
            device=self.device
        )
        targets = torch.tensor(
            np.array([t.target_pos for t in batch]),
            dtype=torch.float32,
            device=self.device
        )
        durations = torch.tensor(
            [[t.duration] for t in batch],
            dtype=torch.float32,
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

        # Compute old log probabilities (using Gaussian policy)
        old_log_probs = self._compute_log_prob(old_eqs_weights, old_eqs_weights)

        # Compute values
        with torch.no_grad():
            state_features = self.policy.state_encoder(states)
            old_values = self.value_head(state_features).squeeze(-1)

        # Compute advantages
        advantages, returns = self.compute_advantages(rewards, old_values, dones)
        advantages = (advantages - advantages.mean()) / (advantages.std() + 1e-8)

        # PPO update epochs
        metrics = defaultdict(list)

        for epoch in range(epochs):
            # Forward pass
            new_eqs_weights = self.policy(states, option_idxs, targets, durations)
            state_features = self.policy.state_encoder(states)
            new_values = self.value_head(state_features).squeeze(-1)

            # Compute new log probabilities
            new_log_probs = self._compute_log_prob(new_eqs_weights, old_eqs_weights)

            # Policy loss (PPO clip objective)
            ratio = torch.exp(new_log_probs - old_log_probs)
            surr1 = ratio * advantages
            surr2 = torch.clamp(ratio, 1 - self.clip_epsilon, 1 + self.clip_epsilon) * advantages
            policy_loss = -torch.min(surr1, surr2).mean()

            # Value loss
            value_loss = nn.MSELoss()(new_values, returns)

            # Entropy bonus (encourage exploration)
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


class Phase1Trainer:
    """
    Main trainer for Phase 1: Option-conditioned policy learning.
    """

    def __init__(
        self,
        env_host: str = 'localhost',
        env_port: int = 8888,
        model_save_path: str = './checkpoints',
        log_dir: str = './runs/phase1',
        device: str = 'cuda' if torch.cuda.is_available() else 'cpu'
    ):
        self.env_host = env_host
        self.env_port = env_port
        self.model_save_path = model_save_path
        self.device = device

        # Create directories
        os.makedirs(model_save_path, exist_ok=True)
        os.makedirs(log_dir, exist_ok=True)

        # Initialize policy
        self.policy = MultiHeadRLPolicy().to(device)
        self.policy.print_architecture()

        # Initialize trainer
        self.trainer = PPOTrainer(self.policy, device=device)

        # Initialize replay buffer
        self.replay_buffer = StrategyBalancedReplayBuffer(capacity=100000)

        # Initialize environment (will connect on start)
        self.env: Optional[MocEnvironment] = None

        # Tensorboard writer
        self.writer = SummaryWriter(log_dir)

        # Training statistics
        self.stats = {
            'episode_count': 0,
            'total_transitions': 0,
            'strategy_counts': {i: 0 for i in range(5)},
            'episode_rewards': [],
            'win_rate': []
        }

    def connect_environment(self):
        """Connect to UE5 environment."""
        print(f"\nConnecting to UE5 environment at {self.env_host}:{self.env_port}...")
        self.env = MocEnvironment(host=self.env_host, port=self.env_port)
        print("✅ Connected successfully!")

    def sample_strategy_and_target(self) -> Tuple[int, np.ndarray, float]:
        """
        Sample random strategy, target, and duration for exploration.

        Returns:
            option_idx: Strategy index (0-4)
            target_pos: Random target position
            duration: Random duration (5-20s)
        """
        # Random strategy sampling (balanced)
        option_idx = np.random.randint(0, 5)

        # Random target in map bounds (150m x 150m)
        target_pos = np.array([
            np.random.uniform(-7500, 7500),
            np.random.uniform(-7500, 7500),
            0.0
        ])

        # Random duration
        duration = np.random.uniform(5.0, 20.0)

        return option_idx, target_pos, duration

    def run_training(
        self,
        target_transitions: int = 100000,
        batch_size: int = 256,
        update_frequency: int = 2048,
        save_frequency: int = 10000
    ):
        """
        Run Phase 1 training loop.

        Args:
            target_transitions: Total transitions to collect (default 100k)
            batch_size: Mini-batch size for PPO updates
            update_frequency: Update policy every N transitions
            save_frequency: Save checkpoint every N transitions
        """
        print("\n" + "="*80)
        print("Starting Phase 1: Option-Conditioned Policy Training")
        print("="*80)
        print(f"Target Transitions: {target_transitions:,}")
        print(f"Batch Size: {batch_size}")
        print(f"Update Frequency: {update_frequency}")
        print(f"Device: {self.device}")
        print("="*80 + "\n")

        if self.env is None:
            self.connect_environment()

        start_time = time.time()
        episode_count = 0

        while self.stats['total_transitions'] < target_transitions:
            # Run episode
            episode_reward, episode_transitions = self.run_episode()

            episode_count += 1
            self.stats['episode_count'] = episode_count
            self.stats['episode_rewards'].append(episode_reward)

            # Update policy if buffer is large enough
            if len(self.replay_buffer) >= update_frequency:
                print(f"\nUpdating policy (buffer size: {len(self.replay_buffer)})...")
                update_start = time.time()

                for _ in range(4):  # 4 update cycles
                    batch = self.replay_buffer.sample(batch_size)
                    metrics = self.trainer.update(batch, epochs=10)

                    # Log metrics
                    for key, value in metrics.items():
                        self.writer.add_scalar(f'train/{key}', value, self.stats['total_transitions'])

                update_time = time.time() - update_start
                print(f"Update completed in {update_time:.2f}s")
                print(f"  Policy Loss: {metrics['policy_loss']:.4f}")
                print(f"  Value Loss: {metrics['value_loss']:.4f}")
                print(f"  Entropy: {metrics['entropy']:.4f}")

            # Save checkpoint
            if self.stats['total_transitions'] % save_frequency < episode_transitions:
                self.save_checkpoint(f'policy_step_{self.stats["total_transitions"]}.pth')

            # Log progress
            self.log_progress()

        # Final save
        self.save_checkpoint('policy_final.pth')
        self.export_onnx('policy_weights.onnx')

        elapsed_time = time.time() - start_time
        print(f"\n✅ Phase 1 training completed in {elapsed_time/3600:.2f} hours")
        print(f"Total episodes: {episode_count}")
        print(f"Total transitions: {self.stats['total_transitions']:,}")

        self.writer.close()

    def run_episode(self) -> Tuple[float, int]:
        """
        Run single training episode.

        Returns:
            episode_reward: Total reward for episode
            num_transitions: Number of transitions collected
        """
        state = self.env.reset()
        done = False
        episode_reward = 0.0
        num_transitions = 0

        # Sample initial strategy
        option_idx, target_pos, duration = self.sample_strategy_and_target()
        steps_in_option = 0

        while not done:
            # Get EQS weights from policy
            with torch.no_grad():
                state_tensor = torch.tensor(state, dtype=torch.float32, device=self.device).unsqueeze(0)
                option_tensor = torch.tensor([option_idx], dtype=torch.long, device=self.device)
                target_tensor = torch.tensor([target_pos], dtype=torch.float32, device=self.device)
                duration_tensor = torch.tensor([[duration]], dtype=torch.float32, device=self.device)

                eqs_weights = self.policy(state_tensor, option_tensor, target_tensor, duration_tensor)
                eqs_weights = eqs_weights.squeeze(0).cpu().numpy()

            # Execute action in environment
            next_state, reward, done, info = self.env.step(
                option_type=option_idx,
                target_position=target_pos.tolist(),
                option_duration=duration,
                eqs_weights=eqs_weights.tolist()
            )

            # Store transition
            transition = Transition(
                state=state,
                option_idx=option_idx,
                target_pos=target_pos,
                duration=duration,
                eqs_weights=eqs_weights,
                reward=reward,
                next_state=next_state,
                done=done,
                info=info
            )
            self.replay_buffer.add(transition)

            episode_reward += reward
            num_transitions += 1
            self.stats['total_transitions'] += 1
            self.stats['strategy_counts'][option_idx] += 1
            steps_in_option += 1

            # Option switching logic
            volatility = np.linalg.norm(next_state - state)
            if steps_in_option >= 200 or volatility > 0.5:
                option_idx, target_pos, duration = self.sample_strategy_and_target()
                steps_in_option = 0

            state = next_state

        return episode_reward, num_transitions

    def save_checkpoint(self, filename: str):
        """Save training checkpoint."""
        filepath = os.path.join(self.model_save_path, filename)
        torch.save({
            'policy_state_dict': self.policy.state_dict(),
            'optimizer_state_dict': self.trainer.optimizer.state_dict(),
            'stats': self.stats,
            'replay_buffer_size': len(self.replay_buffer)
        }, filepath)
        print(f"💾 Checkpoint saved: {filepath}")

    def export_onnx(self, filename: str):
        """Export policy to ONNX format."""
        filepath = os.path.join(self.model_save_path, filename)
        self.policy.export_onnx(filepath, batch_size=1)

    def log_progress(self):
        """Log training progress."""
        if self.stats['episode_count'] % 10 == 0:
            strategy_dist = self.replay_buffer.get_strategy_distribution()
            avg_reward = np.mean(self.stats['episode_rewards'][-100:]) if self.stats['episode_rewards'] else 0.0

            print(f"\n{'='*80}")
            print(f"Episode {self.stats['episode_count']} | "
                  f"Transitions: {self.stats['total_transitions']:,}")
            print(f"Avg Reward (last 100): {avg_reward:.2f}")
            print(f"Strategy Distribution:")
            strategies = ['Assault', 'Defend', 'Support', 'Scout', 'Retreat']
            for i, name in enumerate(strategies):
                print(f"  {name}: {strategy_dist[i]*100:.1f}%")
            print(f"{'='*80}\n")

            # Tensorboard logging
            self.writer.add_scalar('episode/reward', avg_reward, self.stats['episode_count'])
            for i, name in enumerate(strategies):
                self.writer.add_scalar(f'strategy_distribution/{name}', strategy_dist[i], self.stats['total_transitions'])


# ============================================================================
# Main Entry Point
# ============================================================================

if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description='Phase 1: Option-Conditioned Policy Training')
    parser.add_argument('--host', type=str, default='localhost', help='UE5 server host')
    parser.add_argument('--port', type=int, default=8888, help='UE5 server port')
    parser.add_argument('--transitions', type=int, default=100000, help='Target transitions')
    parser.add_argument('--batch-size', type=int, default=256, help='PPO batch size')
    parser.add_argument('--device', type=str, default='cuda', help='Device (cuda/cpu)')

    args = parser.parse_args()

    # Initialize trainer
    trainer = Phase1Trainer(
        env_host=args.host,
        env_port=args.port,
        device=args.device if torch.cuda.is_available() else 'cpu'
    )

    # Run training
    try:
        trainer.run_training(
            target_transitions=args.transitions,
            batch_size=args.batch_size
        )
    except KeyboardInterrupt:
        print("\n⚠️ Training interrupted by user")
        trainer.save_checkpoint('policy_interrupted.pth')
        trainer.export_onnx('policy_interrupted.onnx')
    except Exception as e:
        print(f"\n❌ Training failed: {e}")
        import traceback
        traceback.print_exc()
    finally:
        if trainer.env:
            trainer.env.close()
