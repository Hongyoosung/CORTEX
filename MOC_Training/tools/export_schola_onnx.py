"""
ONNX Export for Schola Inference - Single 52-dim Input Wrapper

Schola's InferencePolicy feeds a single flat observation buffer to ONNX.
The observer produces 52-dim: [49 base obs, 3 strategy one-hot].
The trained policy expects 2 inputs: obs(49) + strategy_index(int64).

This script wraps the trained policy in a module that:
  Input:  obs (B, 52) float32  — single tensor from Schola observer
  Output: eqs_weights (B, 7) float32  — 7-dim EQS weights in [-1, 1]

Usage:
  # From RLlib checkpoint (via Docker):
  python tools/export_schola_onnx.py \
    --checkpoint data/v10_2_20260221_064433/best \
    --output data/assault_model_schola.onnx

  # From PyTorch .pt file:
  python tools/export_schola_onnx.py \
    --checkpoint model.pt \
    --output assault_model_schola.onnx
"""

import sys
import os
import argparse
import torch
import torch.nn as nn
from pathlib import Path

# Add parent directory to path
sys.path.append(str(Path(__file__).parent.parent))

from training.phase1_policy_training_v10_2 import MultiHeadRLPolicy_v10_2


class ScholaInferenceWrapper(nn.Module):
    """
    Wrapper that accepts Schola's single 52-dim observation tensor
    and routes through the trained multi-head policy.

    Input:  obs (B, 52) float32 — [49 base features, 3 strategy one-hot]
    Output: eqs_weights (B, 7) float32 — EQS weights in [-1, 1]
    """

    def __init__(self, inner_policy: MultiHeadRLPolicy_v10_2):
        super().__init__()
        self.inner = inner_policy

    def forward(self, obs: torch.Tensor) -> torch.Tensor:
        # Split: 49 base obs + 3 strategy one-hot
        base_obs = obs[:, :49]
        strategy_onehot = obs[:, 49:52]
        strategy_idx = torch.argmax(strategy_onehot, dim=1)
        return self.inner(base_obs, strategy_idx)


def load_policy_from_rllib(checkpoint_path: str) -> MultiHeadRLPolicy_v10_2:
    """Load inner policy directly from RLlib checkpoint pickle (no gRPC needed)."""
    import pickle

    policy_state_path = os.path.join(checkpoint_path, "policies", "shared_policy", "policy_state.pkl")
    if not os.path.exists(policy_state_path):
        raise FileNotFoundError(f"Policy state not found: {policy_state_path}")

    with open(policy_state_path, "rb") as f:
        state = pickle.load(f)

    weights = state["weights"]

    # RLlib wraps our policy under 'policy.' prefix — strip it for the inner model
    inner_state_dict = {}
    for key, value in weights.items():
        if key.startswith("policy."):
            inner_key = key[len("policy."):]
            inner_state_dict[inner_key] = torch.tensor(value) if not isinstance(value, torch.Tensor) else value

    policy = MultiHeadRLPolicy_v10_2(obs_dim=49, num_strategies=3, eqs_dim=7)
    policy.load_state_dict(inner_state_dict, strict=False)
    print(f"Loaded inner policy from RLlib checkpoint: {checkpoint_path}")
    print(f"  Loaded {len(inner_state_dict)} weight tensors")
    return policy


def load_policy_from_pytorch(checkpoint_path: str) -> MultiHeadRLPolicy_v10_2:
    """Load inner policy from PyTorch .pt/.pth file."""
    policy = MultiHeadRLPolicy_v10_2(obs_dim=49, num_strategies=3, eqs_dim=7)
    state_dict = torch.load(checkpoint_path, map_location="cpu")
    policy.load_state_dict(state_dict)
    print(f"Loaded inner policy from PyTorch checkpoint: {checkpoint_path}")
    return policy


def export_schola_onnx(policy: MultiHeadRLPolicy_v10_2, output_path: str):
    """Export wrapped model compatible with Schola InferencePolicy."""
    wrapper = ScholaInferenceWrapper(policy)
    wrapper.eval()

    dummy_obs = torch.randn(1, 52)

    torch.onnx.export(
        wrapper,
        dummy_obs,
        output_path,
        input_names=["obs"],
        output_names=["eqs_weights"],
        dynamic_axes={
            "obs": {0: "batch_size"},
            "eqs_weights": {0: "batch_size"},
        },
        opset_version=18,
    )

    file_size_kb = os.path.getsize(output_path) / 1024
    print(f"\nExported Schola-compatible ONNX: {output_path} ({file_size_kb:.1f} KB)")
    print(f"  Input:  obs (B, 52) float32")
    print(f"  Output: eqs_weights (B, 7) float32 in [-1, 1]")


def validate_onnx(output_path: str):
    """Validate and test the exported model."""
    try:
        import onnx
        model = onnx.load(output_path)
        onnx.checker.check_model(model)
        print("\nONNX validation: PASSED")

        for inp in model.graph.input:
            shape = [d.dim_value for d in inp.type.tensor_type.shape.dim]
            print(f"  Input:  {inp.name} {shape}")
        for out in model.graph.output:
            shape = [d.dim_value for d in out.type.tensor_type.shape.dim]
            print(f"  Output: {out.name} {shape}")
    except ImportError:
        print("onnx package not available, skipping validation")
        return

    try:
        import onnxruntime as ort
        import numpy as np
        session = ort.InferenceSession(output_path)

        # Test each strategy
        strategies = {0: "Assault", 1: "Defend", 2: "Support"}
        eqs_labels = [
            "EnemyObjProx", "AllyObjProx", "CoverDensity",
            "EnemyVis", "AllyProx", "CombatRange", "PickupProx"
        ]

        print("\nSample inference per strategy:")
        for strat_idx, strat_name in strategies.items():
            obs = np.random.randn(1, 52).astype(np.float32)
            # Set strategy one-hot
            obs[0, 49:52] = 0.0
            obs[0, 49 + strat_idx] = 1.0

            result = session.run(None, {"obs": obs})[0][0]
            weights_str = ", ".join(f"{n}={v:.3f}" for n, v in zip(eqs_labels, result))
            print(f"  {strat_name}: {weights_str}")

    except ImportError:
        print("onnxruntime not available, skipping inference test")


def main():
    parser = argparse.ArgumentParser(description="Export Schola-compatible ONNX (single 52-dim input)")
    parser.add_argument("--checkpoint", type=str, required=True,
                        help="RLlib checkpoint dir or PyTorch .pt/.pth file")
    parser.add_argument("--output", type=str, default="assault_model_schola.onnx",
                        help="Output ONNX file path")
    parser.add_argument("--validate", action="store_true", default=True,
                        help="Validate and test the exported model")
    args = parser.parse_args()

    # Load policy
    if args.checkpoint.endswith(".pt") or args.checkpoint.endswith(".pth"):
        policy = load_policy_from_pytorch(args.checkpoint)
    else:
        policy = load_policy_from_rllib(args.checkpoint)

    policy.eval()

    # Export
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    export_schola_onnx(policy, args.output)

    # Validate
    if args.validate:
        validate_onnx(args.output)

    print("\nDone. Import this .onnx file into UE5 Content/Game/Models/ via drag-and-drop.")


if __name__ == "__main__":
    main()
