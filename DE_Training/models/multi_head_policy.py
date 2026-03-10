"""
Multi-Head Option-Conditioned RL Policy

Five-strategy specialized policy network that outputs EQS weight parameters
instead of raw movement vectors. This design enables tactical intent separation
from low-level navigation.

Architecture:
- Input: 61-dim (State=52, OptionOneHot=5, Target=3, Duration=1)
- Output: 7-dim EQS weights (range [-1, 1])
- Five heads: Assault, Defend, Support, Scout, Retreat

See v10.0Architecture.md Section 2 for full specification.
"""

import torch
import torch.nn as nn
import numpy as np
from typing import Tuple, Optional


class MultiHeadRLPolicy(nn.Module):
    """
    Strategy-specialized policy with five independent EQS weight generation heads.
    Each head learns distinct tactical behaviors through option-conditioned rewards.
    """

    def __init__(
        self,
        state_dim: int = 52,
        option_dim: int = 5,
        target_dim: int = 3,
        duration_dim: int = 1,
        backbone_dim: int = 256,
        option_embed_dim: int = 16,
        eqs_weight_dim: int = 7
    ):
        super().__init__()

        self.state_dim = state_dim
        self.option_dim = option_dim
        self.eqs_weight_dim = eqs_weight_dim

        # === Shared Components ===

        # State encoder: 52 → 256
        self.state_encoder = nn.Sequential(
            nn.Linear(state_dim, 128),
            nn.ReLU(),
            nn.Linear(128, backbone_dim),
            nn.LayerNorm(backbone_dim),
            nn.ReLU()
        )

        # Option embedding: 5 one-hot → 16-dim learned embedding
        self.option_embedding = nn.Embedding(option_dim, option_embed_dim)

        # Target encoder: 3 (X, Y, Z) → 16-dim
        self.target_encoder = nn.Sequential(
            nn.Linear(target_dim, 16),
            nn.ReLU()
        )

        # === Strategy-Specific EQS Weight Heads ===

        head_input_dim = backbone_dim + option_embed_dim + 16 + duration_dim  # 256+16+16+1 = 289

        self.heads = nn.ModuleDict({
            'assault': self._build_eqs_head(head_input_dim, eqs_weight_dim),
            'defend': self._build_eqs_head(head_input_dim, eqs_weight_dim),
            'support': self._build_eqs_head(head_input_dim, eqs_weight_dim),
            'scout': self._build_eqs_head(head_input_dim, eqs_weight_dim),
            'retreat': self._build_eqs_head(head_input_dim, eqs_weight_dim)
        })

        self.strategy_names = ['assault', 'defend', 'support', 'scout', 'retreat']

        # Initialize weights
        self.apply(self._init_weights)

    def _build_eqs_head(self, input_dim: int, output_dim: int) -> nn.Module:
        """
        Build EQS weight generation head.

        Each head:
        - Input: 289-dim (combined features)
        - Hidden: 128-dim with ReLU + Dropout
        - Output: 7-dim with Tanh (range [-1, 1])
        """
        return nn.Sequential(
            nn.Linear(input_dim, 128),
            nn.ReLU(),
            nn.Dropout(0.1),
            nn.Linear(128, output_dim),
            nn.Tanh()  # Output range [-1, 1]
        )

    def _init_weights(self, module: nn.Module):
        """Initialize network weights using Xavier initialization."""
        if isinstance(module, nn.Linear):
            nn.init.xavier_uniform_(module.weight)
            if module.bias is not None:
                nn.init.constant_(module.bias, 0.0)
        elif isinstance(module, nn.Embedding):
            nn.init.normal_(module.weight, mean=0.0, std=0.1)

    def forward(
        self,
        state: torch.Tensor,
        option_idx: torch.Tensor,
        target_pos: torch.Tensor,
        duration: torch.Tensor
    ) -> torch.Tensor:
        """
        Forward pass through multi-head policy.

        Args:
            state: (B, 52) - Base observation
            option_idx: (B,) - Strategy index {0=Assault, 1=Defend, 2=Support, 3=Scout, 4=Retreat}
            target_pos: (B, 3) - Target coordinates (X, Y, Z)
            duration: (B, 1) - Option duration in seconds

        Returns:
            eqs_weights: (B, 7) - EQS weight parameters from selected head
        """
        batch_size = state.size(0)

        # Encode inputs
        state_feat = self.state_encoder(state)               # (B, 256)
        option_feat = self.option_embedding(option_idx)      # (B, 16)
        target_feat = self.target_encoder(target_pos)        # (B, 16)

        # Concatenate all features
        combined = torch.cat([state_feat, option_feat, target_feat, duration], dim=-1)  # (B, 289)

        # Route to appropriate head based on option_idx
        eqs_weights = []
        for i in range(batch_size):
            opt = option_idx[i].item()
            feat = combined[i:i+1]

            # Select strategy head
            if 0 <= opt < len(self.strategy_names):
                head_name = self.strategy_names[opt]
                weight = self.heads[head_name](feat)
            else:
                # Fallback to assault head for invalid indices
                weight = self.heads['assault'](feat)

            eqs_weights.append(weight)

        return torch.cat(eqs_weights, dim=0)

    def get_head_output(
        self,
        state: torch.Tensor,
        strategy_name: str,
        target_pos: torch.Tensor,
        duration: torch.Tensor
    ) -> torch.Tensor:
        """
        Get output from specific strategy head (for debugging/analysis).

        Args:
            state: (B, 52)
            strategy_name: One of ['assault', 'defend', 'support', 'scout', 'retreat']
            target_pos: (B, 3)
            duration: (B, 1)

        Returns:
            eqs_weights: (B, 7)
        """
        # Map strategy name to option index
        option_idx = torch.tensor(
            [self.strategy_names.index(strategy_name)] * state.size(0),
            dtype=torch.long,
            device=state.device
        )

        return self.forward(state, option_idx, target_pos, duration)

    def export_onnx(self, filepath: str, batch_size: int = 1):
        """
        Export model to ONNX format for UE5 inference.

        Args:
            filepath: Output .onnx file path
            batch_size: Batch size for export (default 1 for single-agent)
        """
        # Create dummy inputs
        dummy_state = torch.randn(batch_size, self.state_dim)
        dummy_option = torch.randint(0, self.option_dim, (batch_size,))
        dummy_target = torch.randn(batch_size, 3)
        dummy_duration = torch.randn(batch_size, 1)

        # Set model to eval mode
        self.eval()

        # Export to ONNX
        torch.onnx.export(
            self,
            (dummy_state, dummy_option, dummy_target, dummy_duration),
            filepath,
            input_names=['state', 'option_idx', 'target_pos', 'duration'],
            output_names=['eqs_weights'],
            dynamic_axes={
                'state': {0: 'batch'},
                'option_idx': {0: 'batch'},
                'target_pos': {0: 'batch'},
                'duration': {0: 'batch'},
                'eqs_weights': {0: 'batch'}
            },
            opset_version=14,
            do_constant_folding=True,
            verbose=False
        )

        print(f"✅ Model exported to ONNX: {filepath}")
        print(f"   Input shapes: state=[{batch_size}, 52], option_idx=[{batch_size}], "
              f"target=[{batch_size}, 3], duration=[{batch_size}, 1]")
        print(f"   Output shape: eqs_weights=[{batch_size}, 7]")

    def get_num_parameters(self) -> int:
        """Get total number of trainable parameters."""
        return sum(p.numel() for p in self.parameters() if p.requires_grad)

    def print_architecture(self):
        """Print model architecture summary."""
        print("\n" + "="*60)
        print("Multi-Head RL Policy Architecture")
        print("="*60)
        print(f"Total Parameters: {self.get_num_parameters():,}")
        print("\nShared Components:")
        print(f"  - State Encoder: {self.state_dim} → 256")
        print(f"  - Option Embedding: {self.option_dim} → 16")
        print(f"  - Target Encoder: 3 → 16")
        print("\nStrategy Heads (5 total):")
        for name in self.strategy_names:
            head_params = sum(p.numel() for p in self.heads[name].parameters())
            print(f"  - {name.capitalize()}: 289 → 128 → {self.eqs_weight_dim} ({head_params:,} params)")
        print("="*60 + "\n")


# ============================================================================
# Example Usage
# ============================================================================

if __name__ == '__main__':
    print("Testing MultiHeadRLPolicy...")

    # Initialize model
    model = MultiHeadRLPolicy()
    model.print_architecture()

    # Test forward pass
    batch_size = 4
    state = torch.randn(batch_size, 52)
    option_idx = torch.tensor([0, 1, 2, 3])  # Assault, Defend, Support, Scout
    target_pos = torch.randn(batch_size, 3)
    duration = torch.rand(batch_size, 1) * 15.0 + 5.0  # Random duration 5-20s

    with torch.no_grad():
        eqs_weights = model(state, option_idx, target_pos, duration)

    print(f"\nTest Forward Pass:")
    print(f"  Input: state={state.shape}, option_idx={option_idx.shape}, "
          f"target={target_pos.shape}, duration={duration.shape}")
    print(f"  Output: eqs_weights={eqs_weights.shape}")
    print(f"\nEQS Weights (per strategy):")
    strategy_names = ['Assault', 'Defend', 'Support', 'Scout']
    for i, name in enumerate(strategy_names):
        weights = eqs_weights[i].numpy()
        print(f"  {name}: {weights}")

    # Test ONNX export
    print("\nExporting to ONNX...")
    model.export_onnx("test_policy.onnx", batch_size=1)

    print("\n✅ Multi-Head Policy test completed successfully!")
