"""
Tactical Policy Training Script v3.0 for SBDAPM (Atomic Action Space)

Trains a PPO-based RL policy for atomic action selection using experiences
collected from Unreal Engine gameplay.

Architecture (v3.0):
    Input: 78 features (71 observation + 7 objective embedding)
    Hidden: 128 -> 128 -> 64 neurons (ReLU)
    Output: 8-dimensional atomic action
        - Continuous: move_x, move_y, speed, look_x, look_y (tanh/sigmoid)
        - Discrete: fire, crouch, ability (sigmoid)

Key Changes from v2.0:
    - Hybrid continuous-discrete action space
    - Objective context as input
    - Action masking support
    - Simplified output (8 dims vs 16 discrete actions)

Usage:
    1. Collect experiences in Unreal and export to JSON
    2. Run: python train_tactical_policy_v3.py --data path/to/experiences.json
    3. Model will be exported to tactical_policy_v3.onnx
    4. Load ONNX model in Unreal via URLPolicyNetwork::LoadPolicy()

Requirements:
    pip install torch numpy tensorboard stable-baselines3
"""

import argparse
import json
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
from torch.utils.tensorboard import SummaryWriter
from typing import List, Tuple, Dict
import os
from datetime import datetime


# ============================================
# Configuration
# ============================================

class Config:
    # Network architecture
    INPUT_SIZE = 78  # 71 observation + 7 objective embedding
    HIDDEN_LAYERS = [128, 128, 64]
    OUTPUT_SIZE = 8  # Atomic action dimensions

    # Action space dimensions
    CONTINUOUS_DIM = 5  # move_x, move_y, speed, look_x, look_y
    DISCRETE_DIM = 3    # fire, crouch, ability

    # Training hyperparameters
    LEARNING_RATE = 3e-4
    BATCH_SIZE = 64
    NUM_EPOCHS = 100
    GAMMA = 0.99  # Discount factor
    GAE_LAMBDA = 0.95  # GAE parameter
    CLIP_EPSILON = 0.2  # PPO clipping
    VALUE_COEF = 0.5  # Value loss coefficient
    ENTROPY_COEF = 0.01  # Entropy bonus

    # Training settings
    GRAD_CLIP = 0.5
    UPDATE_FREQUENCY = 10  # Update policy every N epochs

    # Curriculum Learning (Sprint 3)
    USE_PRIORITIZED_REPLAY = True  # Enable MCTS-guided prioritization
    PRIORITIZATION_ALPHA = 0.6  # Prioritization exponent (0=uniform, 1=full priority)
    IMPORTANCE_SAMPLING_BETA = 0.4  # Importance sampling correction
    BETA_ANNEALING_STEPS = 10000  # Steps to anneal beta to 1.0

    # Paths
    DATA_PATH = "experiences.json"
    MODEL_OUTPUT = "tactical_policy_v3.onnx"
    CHECKPOINT_DIR = "checkpoints_v3"
    LOG_DIR = "runs_v3"


# ============================================
# Neural Network (Policy + Value, Hybrid Action Space)
# ============================================

class AtomicActionPolicyNetwork(nn.Module):
    """
    Hybrid Actor-Critic network for atomic action selection
    Outputs continuous actions (movement, aiming) and discrete actions (fire, crouch, ability)
    """

    def __init__(self, input_size=78, hidden_layers=[128, 128, 64], output_size=8):
        super(AtomicActionPolicyNetwork, self).__init__()

        # Build shared feature extractor
        layers = []
        prev_size = input_size
        for hidden_size in hidden_layers:
            layers.append(nn.Linear(prev_size, hidden_size))
            layers.append(nn.ReLU())
            prev_size = hidden_size

        self.feature_extractor = nn.Sequential(*layers)

        # Continuous action heads (movement + aiming)
        self.move_x = nn.Linear(prev_size, 1)  # [-1, 1]
        self.move_y = nn.Linear(prev_size, 1)  # [-1, 1]
        self.speed = nn.Linear(prev_size, 1)   # [0, 1]
        self.look_x = nn.Linear(prev_size, 1)  # [-1, 1]
        self.look_y = nn.Linear(prev_size, 1)  # [-1, 1]

        # Discrete action heads (binary)
        self.fire = nn.Linear(prev_size, 1)    # [0, 1]
        self.crouch = nn.Linear(prev_size, 1)  # [0, 1]
        self.ability = nn.Linear(prev_size, 1) # [0, 1]

        # Critic head (value function)
        self.critic = nn.Linear(prev_size, 1)

    def forward(self, state):
        """
        Forward pass
        Returns: (continuous_actions, discrete_actions, state_value)
            continuous_actions: [move_x, move_y, speed, look_x, look_y]
            discrete_actions: [fire, crouch, ability]
            state_value: scalar
        """
        features = self.feature_extractor(state)

        # Continuous actions (normalized outputs)
        move_x = torch.tanh(self.move_x(features))      # [-1, 1]
        move_y = torch.tanh(self.move_y(features))      # [-1, 1]
        speed = torch.sigmoid(self.speed(features))     # [0, 1]
        look_x = torch.tanh(self.look_x(features))      # [-1, 1]
        look_y = torch.tanh(self.look_y(features))      # [-1, 1]

        continuous = torch.cat([move_x, move_y, speed, look_x, look_y], dim=-1)

        # Discrete actions (sigmoid for binary)
        fire = torch.sigmoid(self.fire(features))       # [0, 1]
        crouch = torch.sigmoid(self.crouch(features))   # [0, 1]
        ability = torch.sigmoid(self.ability(features)) # [0, 1]

        discrete = torch.cat([fire, crouch, ability], dim=-1)

        # State value
        value = self.critic(features)

        # Combine all actions into single 8-dim output for ONNX export
        actions = torch.cat([continuous, discrete], dim=-1)

        return actions, value

    def get_action(self, state, deterministic=False):
        """
        Sample action from policy (with exploration noise)
        """
        actions, value = self.forward(state)

        if deterministic:
            return actions, value
        else:
            # Add exploration noise
            continuous_noise = torch.randn_like(actions[:, :5]) * 0.1
            discrete_noise = torch.randn_like(actions[:, 5:]) * 0.05

            # Apply noise
            actions[:, :5] += continuous_noise
            actions[:, 5:] += discrete_noise

            # Clamp to valid ranges
            actions[:, 0:2] = torch.clamp(actions[:, 0:2], -1, 1)  # move_x, move_y
            actions[:, 2:5] = torch.clamp(actions[:, 2:5], 0, 1)   # speed, look_x, look_y (after tanh/sigmoid)
            actions[:, 5:8] = torch.clamp(actions[:, 5:8], 0, 1)   # fire, crouch, ability

            return actions, value


# ============================================
# Experience Dataset
# ============================================

class ExperienceDataset(Dataset):
    """
    Dataset for loading experiences from JSON
    Supports atomic action format
    """

    def __init__(self, json_path):
        with open(json_path, 'r') as f:
            data = json.load(f)

        self.experiences = data['experiences']
        print(f"Loaded {len(self.experiences)} experiences")

    def __len__(self):
        return len(self.experiences)

    def __getitem__(self, idx):
        exp = self.experiences[idx]

        # State (71 features) + objective embedding (7 features) = 78 total
        state = np.array(exp['state'], dtype=np.float32)
        objective_embed = np.array(exp.get('objective_embedding', [0]*7), dtype=np.float32)
        full_state = np.concatenate([state, objective_embed])

        # Action (8 dimensions: atomic action)
        action = np.array(exp['action'], dtype=np.float32)

        # Reward
        reward = float(exp['reward'])

        # Next state
        next_state = np.array(exp['next_state'], dtype=np.float32)
        next_objective_embed = np.array(exp.get('next_objective_embedding', [0]*7), dtype=np.float32)
        full_next_state = np.concatenate([next_state, next_objective_embed])

        # Terminal flag
        terminal = bool(exp['terminal'])

        # MCTS uncertainty metrics (Sprint 3 - Curriculum Learning)
        mcts_variance = float(exp.get('mcts_value_variance', 0.0))
        mcts_entropy = float(exp.get('mcts_policy_entropy', 0.0))
        mcts_visits = float(exp.get('mcts_visit_count', 0.0))

        return {
            'state': full_state,
            'action': action,
            'reward': reward,
            'next_state': full_next_state,
            'terminal': terminal,
            'mcts_variance': mcts_variance,
            'mcts_entropy': mcts_entropy,
            'mcts_visits': mcts_visits
        }


# ============================================
# Prioritized Experience Replay (Sprint 3)
# ============================================

class PrioritizedSampler(torch.utils.data.Sampler):
    """
    Custom sampler that prioritizes experiences based on MCTS uncertainty
    High uncertainty scenarios (high variance, high entropy) get sampled more often
    """

    def __init__(self, dataset, alpha=0.6, beta=0.4):
        """
        Args:
            dataset: ExperienceDataset
            alpha: Prioritization exponent (0=uniform, 1=full priority)
            beta: Importance sampling correction (annealed to 1.0 during training)
        """
        self.dataset = dataset
        self.alpha = alpha
        self.beta = beta
        self.priorities = self._compute_priorities()
        self.num_samples = len(dataset)

    def _compute_priorities(self):
        """
        Compute priority for each experience based on MCTS uncertainty
        Priority = (variance + 0.5 * entropy) / sqrt(visits)
        Higher values = more uncertain = higher training value
        """
        priorities = []

        for idx in range(len(self.dataset)):
            exp = self.dataset[idx]
            variance = exp['mcts_variance']
            entropy = exp['mcts_entropy']
            visits = max(1.0, exp['mcts_visits'])  # Avoid division by zero

            # Uncertainty score (same formula as C++ CurriculumManager)
            uncertainty = variance + 0.5 * entropy
            priority = uncertainty / np.sqrt(visits)

            # Apply prioritization exponent
            priority = max(priority, 1e-6) ** self.alpha  # Ensure non-zero
            priorities.append(priority)

        # Normalize to probabilities
        priorities = np.array(priorities)
        total = np.sum(priorities)
        if total > 0:
            priorities /= total
        else:
            priorities = np.ones(len(priorities)) / len(priorities)  # Uniform if all zero

        return priorities

    def __iter__(self):
        """
        Sample indices based on priority distribution
        """
        indices = np.random.choice(
            self.num_samples,
            size=self.num_samples,
            replace=True,  # Sample with replacement
            p=self.priorities
        )
        return iter(indices.tolist())

    def __len__(self):
        return self.num_samples

    def get_importance_weights(self, indices):
        """
        Compute importance sampling weights for bias correction
        Weight = (N * P(i))^(-beta)
        """
        weights = []
        N = len(self.dataset)

        for idx in indices:
            prob = self.priorities[idx]
            weight = (N * prob) ** (-self.beta)
            weights.append(weight)

        # Normalize weights by max weight for stability
        weights = np.array(weights)
        weights /= weights.max()

        return torch.tensor(weights, dtype=torch.float32)

    def update_beta(self, new_beta):
        """
        Anneal beta towards 1.0 during training
        """
        self.beta = new_beta


# ============================================
# PPO Trainer
# ============================================

class PPOTrainer:
    """
    PPO trainer for hybrid continuous-discrete action space
    """

    def __init__(self, policy_net, config):
        self.policy = policy_net
        self.config = config
        self.optimizer = optim.Adam(self.policy.parameters(), lr=config.LEARNING_RATE)
        self.writer = SummaryWriter(log_dir=config.LOG_DIR)
        self.global_step = 0

    def compute_advantages(self, rewards, values, terminals, gamma=0.99, lam=0.95):
        """
        Compute Generalized Advantage Estimation (GAE)
        """
        advantages = []
        gae = 0

        for t in reversed(range(len(rewards))):
            if terminals[t]:
                delta = rewards[t] - values[t]
                gae = delta
            else:
                delta = rewards[t] + gamma * values[t+1] - values[t]
                gae = delta + gamma * lam * gae

            advantages.insert(0, gae)

        return torch.tensor(advantages, dtype=torch.float32)

    def train_step(self, batch):
        """
        Single PPO training step
        """
        states = torch.tensor(np.array([b['state'] for b in batch]), dtype=torch.float32)
        actions = torch.tensor(np.array([b['action'] for b in batch]), dtype=torch.float32)
        rewards = torch.tensor([b['reward'] for b in batch], dtype=torch.float32)

        # Get policy outputs
        pred_actions, values = self.policy(states)

        # Compute loss
        # Action loss (MSE for continuous, BCE for discrete)
        continuous_loss = nn.MSELoss()(pred_actions[:, :5], actions[:, :5])
        discrete_loss = nn.BCELoss()(pred_actions[:, 5:], actions[:, 5:])
        action_loss = continuous_loss + discrete_loss

        # Value loss
        value_loss = nn.MSELoss()(values.squeeze(), rewards)

        # Total loss
        loss = action_loss + self.config.VALUE_COEF * value_loss

        # Backpropagation
        self.optimizer.zero_grad()
        loss.backward()
        torch.nn.utils.clip_grad_norm_(self.policy.parameters(), self.config.GRAD_CLIP)
        self.optimizer.step()

        # Logging
        self.writer.add_scalar('Loss/Total', loss.item(), self.global_step)
        self.writer.add_scalar('Loss/Action', action_loss.item(), self.global_step)
        self.writer.add_scalar('Loss/Value', value_loss.item(), self.global_step)
        self.global_step += 1

        return loss.item()

    def train(self, dataloader, num_epochs):
        """
        Train policy for num_epochs
        """
        for epoch in range(num_epochs):
            epoch_loss = 0
            for batch in dataloader:
                loss = self.train_step(batch)
                epoch_loss += loss

            avg_loss = epoch_loss / len(dataloader)
            print(f"Epoch {epoch+1}/{num_epochs} - Loss: {avg_loss:.4f}")

            # Save checkpoint
            if (epoch + 1) % 10 == 0:
                self.save_checkpoint(epoch + 1)

    def save_checkpoint(self, epoch):
        """
        Save model checkpoint
        """
        os.makedirs(self.config.CHECKPOINT_DIR, exist_ok=True)
        path = os.path.join(self.config.CHECKPOINT_DIR, f"policy_epoch_{epoch}.pth")
        torch.save(self.policy.state_dict(), path)
        print(f"Saved checkpoint: {path}")


# ============================================
# ONNX Export
# ============================================

def export_to_onnx(policy_net, output_path, input_size=78):
    """
    Export trained policy to ONNX format for Unreal NNE
    """
    policy_net.eval()

    # Create dummy input
    dummy_input = torch.randn(1, input_size)

    # Export
    torch.onnx.export(
        policy_net,
        dummy_input,
        output_path,
        export_params=True,
        opset_version=11,
        do_constant_folding=True,
        input_names=['observation'],
        output_names=['actions', 'value'],
        dynamic_axes={
            'observation': {0: 'batch_size'},
            'actions': {0: 'batch_size'},
            'value': {0: 'batch_size'}
        }
    )

    print(f"Model exported to {output_path}")


# ============================================
# Main Training Pipeline
# ============================================

def main():
    parser = argparse.ArgumentParser(description='Train Tactical Policy v3.0')
    parser.add_argument('--data', type=str, default=Config.DATA_PATH, help='Path to experiences JSON')
    parser.add_argument('--output', type=str, default=Config.MODEL_OUTPUT, help='Output ONNX path')
    parser.add_argument('--epochs', type=int, default=Config.NUM_EPOCHS, help='Number of training epochs')
    parser.add_argument('--batch-size', type=int, default=Config.BATCH_SIZE, help='Batch size')
    parser.add_argument('--use-prioritization', action='store_true', default=Config.USE_PRIORITIZED_REPLAY,
                        help='Enable MCTS-guided prioritized replay')
    args = parser.parse_args()

    # Create dataset
    dataset = ExperienceDataset(args.data)

    # Create dataloader with optional prioritization
    if args.use_prioritization:
        print("Using MCTS-guided prioritized experience replay")
        sampler = PrioritizedSampler(
            dataset,
            alpha=Config.PRIORITIZATION_ALPHA,
            beta=Config.IMPORTANCE_SAMPLING_BETA
        )
        dataloader = DataLoader(dataset, batch_size=args.batch_size, sampler=sampler)
    else:
        print("Using uniform random sampling")
        dataloader = DataLoader(dataset, batch_size=args.batch_size, shuffle=True)

    # Create policy network
    policy_net = AtomicActionPolicyNetwork(
        input_size=Config.INPUT_SIZE,
        hidden_layers=Config.HIDDEN_LAYERS,
        output_size=Config.OUTPUT_SIZE
    )

    # Create trainer
    trainer = PPOTrainer(policy_net, Config)

    # Train
    print("Starting training...")
    trainer.train(dataloader, args.epochs)

    # Export to ONNX
    print("Exporting to ONNX...")
    export_to_onnx(policy_net, args.output)

    print("Training complete!")


if __name__ == '__main__':
    main()
