"""
Team World Model Training Script
MOC v10.2 - Pure Team Model Approach

Trains a neural network to predict:
  Input: FTeamState (60-dim) + ETacticalPlay (10-dim one-hot) = 70-dim
  Output: Next FTeamState (60-dim) + Reward (3-dim) + Confidence (1-dim) = 64-dim

Usage:
  python train_team_world_model.py --data_dir ../Content/TrainingData/TeamTransitions --epochs 100
"""

import argparse
import os
import numpy as np
import pandas as pd
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
from pathlib import Path


class TeamWorldModelDataset(Dataset):
    """Dataset for team state transitions"""

    def __init__(self, csv_files, device='cpu'):
        """
        Args:
            csv_files: List of CSV file paths
            device: torch device
        """
        self.samples = []
        self.device = device

        for csv_file in csv_files:
            df = pd.read_csv(csv_file)
            print(f"Loading {csv_file}: {len(df)} samples")

            for _, row in df.iterrows():
                # Extract current state (60-dim)
                cur_state = np.array([row[f'CurState_{i}'] for i in range(60)], dtype=np.float32)

                # Extract tactical play (one-hot encode to 10-dim)
                tactical_play = int(row['TacticalPlay'])
                tactical_play_onehot = np.zeros(10, dtype=np.float32)
                if 0 <= tactical_play < 10:
                    tactical_play_onehot[tactical_play] = 1.0

                # Input: concat state + tactical play = 70-dim
                input_tensor = np.concatenate([cur_state, tactical_play_onehot])

                # Extract next state (60-dim)
                next_state = np.array([row[f'NextState_{i}'] for i in range(60)], dtype=np.float32)

                # Extract reward (3-dim)
                reward = np.array([
                    row['WinProb'],
                    row['HealthDelta'],
                    row['ObjectiveScore']
                ], dtype=np.float32)

                # Confidence: Use 1.0 for real samples (high confidence)
                confidence = np.array([1.0], dtype=np.float32)

                # Output: concat next_state + reward + confidence = 64-dim
                output_tensor = np.concatenate([next_state, reward, confidence])

                self.samples.append({
                    'input': torch.from_numpy(input_tensor),
                    'output': torch.from_numpy(output_tensor)
                })

        print(f"Total samples loaded: {len(self.samples)}")

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        return self.samples[idx]


class TeamWorldModel(nn.Module):
    """
    Team World Model Neural Network

    Architecture: Multi-layer perceptron with residual connections
    - Input: 70-dim (60 state + 10 tactical play one-hot)
    - Hidden: [256, 512, 512, 256]
    - Output: 64-dim (60 next state + 3 reward + 1 confidence)
    """

    def __init__(self):
        super(TeamWorldModel, self).__init__()

        # Encoder
        self.encoder = nn.Sequential(
            nn.Linear(70, 256),
            nn.LayerNorm(256),
            nn.ReLU(),
            nn.Dropout(0.1),

            nn.Linear(256, 512),
            nn.LayerNorm(512),
            nn.ReLU(),
            nn.Dropout(0.1),
        )

        # Core processing with residual
        self.core1 = nn.Linear(512, 512)
        self.core_norm1 = nn.LayerNorm(512)
        self.core2 = nn.Linear(512, 512)
        self.core_norm2 = nn.LayerNorm(512)

        # Decoder
        self.decoder = nn.Sequential(
            nn.Linear(512, 256),
            nn.LayerNorm(256),
            nn.ReLU(),
            nn.Dropout(0.1),
        )

        # Output heads
        self.next_state_head = nn.Linear(256, 60)  # Next state prediction
        self.reward_head = nn.Linear(256, 3)       # WinProb, HealthDelta, ObjectiveScore
        self.confidence_head = nn.Linear(256, 1)   # Confidence score

    def forward(self, x):
        # Encode
        h = self.encoder(x)

        # Residual blocks
        h_res = torch.relu(self.core_norm1(self.core1(h)))
        h = h + h_res
        h_res = torch.relu(self.core_norm2(self.core2(h)))
        h = h + h_res

        # Decode
        h = self.decoder(h)

        # Output heads
        next_state = self.next_state_head(h)
        reward = self.reward_head(h)
        confidence = torch.sigmoid(self.confidence_head(h))  # [0, 1]

        # Apply constraints
        # WinProb should be sigmoid [0, 1]
        reward_constrained = torch.cat([
            torch.sigmoid(reward[:, 0:1]),  # WinProb [0, 1]
            torch.tanh(reward[:, 1:2]),     # HealthDelta [-1, 1]
            torch.tanh(reward[:, 2:3])      # ObjectiveScore [-1, 1]
        ], dim=1)

        # Concatenate outputs: [next_state (60) + reward (3) + confidence (1)] = 64-dim
        output = torch.cat([next_state, reward_constrained, confidence], dim=1)

        return output


def custom_loss(pred, target):
    """
    Custom loss function for team world model

    Weighted MSE loss with different weights for:
    - Next state prediction (weight: 1.0)
    - Reward prediction (weight: 2.0, more important)
    - Confidence prediction (weight: 0.5, less critical)
    """
    # Split predictions and targets
    pred_state = pred[:, :60]
    pred_reward = pred[:, 60:63]
    pred_conf = pred[:, 63:64]

    target_state = target[:, :60]
    target_reward = target[:, 60:63]
    target_conf = target[:, 63:64]

    # Compute weighted losses
    loss_state = nn.MSELoss()(pred_state, target_state) * 1.0
    loss_reward = nn.MSELoss()(pred_reward, target_reward) * 2.0
    loss_conf = nn.MSELoss()(pred_conf, target_conf) * 0.5

    total_loss = loss_state + loss_reward + loss_conf

    return total_loss, {
        'state': loss_state.item(),
        'reward': loss_reward.item(),
        'confidence': loss_conf.item()
    }


def train_epoch(model, dataloader, optimizer, device):
    """Train for one epoch"""
    model.train()
    total_loss = 0.0
    loss_components = {'state': 0.0, 'reward': 0.0, 'confidence': 0.0}

    for batch_idx, batch in enumerate(dataloader):
        inputs = batch['input'].to(device)
        targets = batch['output'].to(device)

        # Forward pass
        optimizer.zero_grad()
        outputs = model(inputs)

        # Compute loss
        loss, components = custom_loss(outputs, targets)

        # Backward pass
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=1.0)
        optimizer.step()

        total_loss += loss.item()
        for key, val in components.items():
            loss_components[key] += val

    avg_loss = total_loss / len(dataloader)
    avg_components = {k: v / len(dataloader) for k, v in loss_components.items()}

    return avg_loss, avg_components


def validate(model, dataloader, device):
    """Validate model"""
    model.eval()
    total_loss = 0.0
    loss_components = {'state': 0.0, 'reward': 0.0, 'confidence': 0.0}

    with torch.no_grad():
        for batch in dataloader:
            inputs = batch['input'].to(device)
            targets = batch['output'].to(device)

            outputs = model(inputs)
            loss, components = custom_loss(outputs, targets)

            total_loss += loss.item()
            for key, val in components.items():
                loss_components[key] += val

    avg_loss = total_loss / len(dataloader)
    avg_components = {k: v / len(dataloader) for k, v in loss_components.items()}

    return avg_loss, avg_components


def export_to_onnx(model, output_path, device):
    """Export trained model to ONNX format for UE5"""
    model.eval()

    # Dummy input: batch_size=1, input_dim=70
    dummy_input = torch.randn(1, 70, device=device)

    torch.onnx.export(
        model,
        dummy_input,
        output_path,
        export_params=True,
        opset_version=11,
        do_constant_folding=True,
        input_names=['input'],
        output_names=['output'],
        dynamic_axes={
            'input': {0: 'batch_size'},
            'output': {0: 'batch_size'}
        }
    )

    print(f"Model exported to ONNX: {output_path}")


def main():
    parser = argparse.ArgumentParser(description='Train Team World Model')
    parser.add_argument('--data_dir', type=str, required=True, help='Directory containing CSV training data')
    parser.add_argument('--output_dir', type=str, default='./output', help='Output directory for models')
    parser.add_argument('--epochs', type=int, default=100, help='Number of training epochs')
    parser.add_argument('--batch_size', type=int, default=256, help='Batch size')
    parser.add_argument('--lr', type=float, default=1e-3, help='Learning rate')
    parser.add_argument('--val_split', type=float, default=0.1, help='Validation split ratio')
    parser.add_argument('--device', type=str, default='cuda' if torch.cuda.is_available() else 'cpu')

    args = parser.parse_args()

    # Create output directory
    os.makedirs(args.output_dir, exist_ok=True)

    # Load data
    print("Loading training data...")
    csv_files = list(Path(args.data_dir).glob('*.csv'))
    if not csv_files:
        raise ValueError(f"No CSV files found in {args.data_dir}")

    dataset = TeamWorldModelDataset(csv_files, device=args.device)

    # Train/val split
    val_size = int(len(dataset) * args.val_split)
    train_size = len(dataset) - val_size
    train_dataset, val_dataset = torch.utils.data.random_split(dataset, [train_size, val_size])

    train_loader = DataLoader(train_dataset, batch_size=args.batch_size, shuffle=True, num_workers=0)
    val_loader = DataLoader(val_dataset, batch_size=args.batch_size, shuffle=False, num_workers=0)

    print(f"Train samples: {train_size}, Val samples: {val_size}")

    # Create model
    model = TeamWorldModel().to(args.device)
    optimizer = optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = optim.lr_scheduler.ReduceLROnPlateau(optimizer, mode='min', factor=0.5, patience=5)

    # Training loop
    best_val_loss = float('inf')

    for epoch in range(args.epochs):
        train_loss, train_components = train_epoch(model, train_loader, optimizer, args.device)
        val_loss, val_components = validate(model, val_loader, args.device)

        scheduler.step(val_loss)

        print(f"Epoch {epoch+1}/{args.epochs} | "
              f"Train Loss: {train_loss:.4f} (S:{train_components['state']:.4f} R:{train_components['reward']:.4f} C:{train_components['confidence']:.4f}) | "
              f"Val Loss: {val_loss:.4f} (S:{val_components['state']:.4f} R:{val_components['reward']:.4f} C:{val_components['confidence']:.4f})")

        # Save best model
        if val_loss < best_val_loss:
            best_val_loss = val_loss
            torch.save(model.state_dict(), os.path.join(args.output_dir, 'best_model.pth'))
            print(f"  → Saved best model (val_loss: {val_loss:.4f})")

    # Load best model and export to ONNX
    model.load_state_dict(torch.load(os.path.join(args.output_dir, 'best_model.pth')))
    onnx_path = os.path.join(args.output_dir, 'team_world_model.onnx')
    export_to_onnx(model, onnx_path, args.device)

    print(f"\nTraining complete! Best validation loss: {best_val_loss:.4f}")
    print(f"ONNX model saved to: {onnx_path}")
    print(f"\nTo use in UE5:")
    print(f"  1. Copy {onnx_path} to Content/AI/Models/")
    print(f"  2. Call UTeamWorldModel::InitModel(\"path/to/team_world_model.onnx\")")


if __name__ == '__main__':
    main()
