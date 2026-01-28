"""
Export ONNX model from a saved RLlib checkpoint.

Usage:
    python export_onnx_from_checkpoint.py --checkpoint training_results/20260128_114130/checkpoint_000029

This will create cortex_policy_v8.onnx in the same directory as the checkpoint.
"""

import argparse
import os
import sys
from pathlib import Path

import torch
import torch.nn as nn
from ray.rllib.algorithms.ppo import PPO

# Import config
try:
    from training_env.config import RLConfig
except ImportError:
    print("Warning: training_env/config.py not found. Using defaults.")
    class RLConfig:
        OBSERVATION_SIZE = 50


def export_onnx_from_checkpoint(checkpoint_path, output_path=None):
    """
    Export ONNX model from a saved checkpoint.

    Args:
        checkpoint_path: Path to the checkpoint directory
        output_path: Optional output path for ONNX file (defaults to checkpoint_dir/cortex_policy_v8.onnx)
    """
    checkpoint_path = os.path.abspath(checkpoint_path)

    if not os.path.exists(checkpoint_path):
        print(f"ERROR: Checkpoint not found: {checkpoint_path}")
        return False

    print(f"Loading checkpoint from: {checkpoint_path}")

    try:
        # Restore algorithm from checkpoint
        algo = PPO.from_checkpoint(checkpoint_path)

        # Get the trained policy
        policy = algo.get_policy("shared_policy")
        if not policy:
            print("ERROR: Could not get 'shared_policy'")
            return False

        model = policy.model
        print(f"Model type: {type(model).__name__}")

        # Create wrapper that exports all strategy-specific heads
        class MultiHeadPolicyWrapper(nn.Module):
            def __init__(self, model):
                super().__init__()
                self.model = model

            def forward(self, obs):
                features = self.model.shared_trunk(obs)

                # Strategy-specific tactical parameters (4 heads)
                assault_tactical = torch.sigmoid(self.model.assault_mean_head(features))
                defend_tactical = torch.sigmoid(self.model.defend_mean_head(features))
                support_tactical = torch.sigmoid(self.model.support_mean_head(features))
                retreat_tactical = torch.sigmoid(self.model.retreat_mean_head(features))

                # Combat priority
                combat_priority = torch.sigmoid(self.model.combat_mean_head(features))

                # v8.10: Strategy-specific value heads (CRITICAL FIX)
                assault_value = self.model.assault_value_head(features)
                defend_value = self.model.defend_value_head(features)
                support_value = self.model.support_value_head(features)
                retreat_value = self.model.retreat_value_head(features)

                return (assault_tactical, defend_tactical, support_tactical,
                       retreat_tactical, combat_priority,
                       assault_value, defend_value, support_value, retreat_value)

        wrapper = MultiHeadPolicyWrapper(model)
        wrapper.eval()

        # Determine output path
        if output_path is None:
            checkpoint_dir = Path(checkpoint_path).parent
            output_path = checkpoint_dir / "cortex_policy_v8.onnx"
        else:
            output_path = Path(output_path)

        # Create dummy input
        dummy_input = torch.randn(1, RLConfig.OBSERVATION_SIZE)

        print(f"Exporting ONNX to: {output_path}")

        # Export to ONNX
        torch.onnx.export(
            wrapper,
            dummy_input,
            str(output_path),
            input_names=["observation"],
            output_names=[
                "assault_tactical", "defend_tactical", "support_tactical",
                "retreat_tactical", "combat_priority",
                "assault_value", "defend_value", "support_value", "retreat_value"
            ],
            dynamic_axes={
                "observation": {0: "batch_size"},
                "assault_tactical": {0: "batch_size"},
                "defend_tactical": {0: "batch_size"},
                "support_tactical": {0: "batch_size"},
                "retreat_tactical": {0: "batch_size"},
                "combat_priority": {0: "batch_size"},
                "assault_value": {0: "batch_size"},
                "defend_value": {0: "batch_size"},
                "support_value": {0: "batch_size"},
                "retreat_value": {0: "batch_size"}
            },
            opset_version=11
        )

        print(f"✅ ONNX export successful!")
        print(f"   Model: {output_path}")
        print(f"   Outputs: 9 tensors (4 tactical + 1 combat + 4 value heads)")
        print(f"\nNext steps:")
        print(f"   1. Copy {output_path} to UE5: Content/AI/Models/cortex_policy_v8.onnx")
        print(f"   2. Restart UE5 to load the new model")
        print(f"   3. Verify in UE5 logs that 9 outputs are detected")

        algo.stop()
        return True

    except Exception as e:
        print(f"❌ ONNX export failed: {e}")
        import traceback
        traceback.print_exc()
        return False


def main():
    parser = argparse.ArgumentParser(description="Export ONNX from RLlib checkpoint")
    parser.add_argument(
        "--checkpoint",
        type=str,
        required=True,
        help="Path to checkpoint directory (e.g., training_results/20260128_114130/checkpoint_000029)"
    )
    parser.add_argument(
        "--output",
        type=str,
        default=None,
        help="Optional output path for ONNX file (defaults to checkpoint_dir/cortex_policy_v8.onnx)"
    )

    args = parser.parse_args()

    success = export_onnx_from_checkpoint(args.checkpoint, args.output)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
