"""
ONNX Export for Schola Inference - Single 54-dim Input Wrapper (v10.2.3)

Schola's InferencePolicy feeds a single flat observation buffer to ONNX.
The observer produces 54-dim: [51 base+composition obs, 3 strategy one-hot].
The trained policy expects 2 inputs: obs(51) + strategy_idx(int64).

This script wraps each trained per-strategy policy in a module that:
  Input:  obs (B, 54) float32  — single tensor from Schola observer
  Output: eqs_weights (B, 6) float32  — 6-dim EQS weights in [-1, 1]

Usage:
  # Export all three strategy models:
  python tools/export_schola_onnx.py --checkpoint <path/to/best> --all

  # Export a single strategy model:
  python tools/export_schola_onnx.py --checkpoint <path/to/best> --policy assault_policy --output assault_model_schola.onnx
"""

import sys
import os
import argparse
import torch
import torch.nn as nn
from pathlib import Path

# Add parent directory to path
sys.path.append(str(Path(__file__).parent.parent))

from training.policy_training import SingleHeadPolicy


class ScholaInferenceWrapper(nn.Module):
    """
    Wrapper that accepts Schola's single 54-dim observation tensor
    and routes through the trained single-head policy.

    Input:  obs (B, 54) float32 — [51 base+composition features, 3 strategy one-hot]
    Output: eqs_weights (B, 6) float32 — EQS weights in [-1, 1]

    The last 3 dims (strategy one-hot) are converted to a strategy index
    via argmax and passed to the inner policy's strategy_idx argument.
    """

    def __init__(self, inner_policy: SingleHeadPolicy):
        super().__init__()
        self.inner = inner_policy

    def forward(self, obs: torch.Tensor) -> torch.Tensor:
        # Split: 51 base+composition obs + 3 strategy one-hot
        base_obs = obs[:, :51]
        strategy_onehot = obs[:, 51:54]
        strategy_idx = torch.argmax(strategy_onehot, dim=1)
        return self.inner(base_obs, strategy_idx)


def load_policy_from_rllib(checkpoint_path: str, policy_name: str) -> SingleHeadPolicy:
    """Load a single-strategy policy directly from RLlib checkpoint pickle."""
    import pickle

    policy_state_path = os.path.join(checkpoint_path, "policies", policy_name, "policy_state.pkl")
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

    strategy_name = policy_name.replace("_policy", "")
    policy = SingleHeadPolicy(obs_dim=51, strategy_dim=3, eqs_dim=6, strategy_name=strategy_name)
    policy.load_state_dict(inner_state_dict, strict=False)
    print(f"Loaded policy '{policy_name}' from: {checkpoint_path}")
    print(f"  Loaded {len(inner_state_dict)} weight tensors")
    return policy


def export_schola_onnx(policy: SingleHeadPolicy, output_path: str):
    """Export wrapped model compatible with Schola InferencePolicy."""
    wrapper = ScholaInferenceWrapper(policy)
    wrapper.eval()

    dummy_obs = torch.randn(1, 54)

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
    print(f"  Input:  obs (B, 54) float32")
    print(f"  Output: eqs_weights (B, 6) float32 in [-1, 1]")


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
            "EnemyVis", "AllyProx", "CombatRange"
        ]

        print("\nSample inference per strategy:")
        for strat_idx, strat_name in strategies.items():
            obs = np.random.randn(1, 54).astype(np.float32)
            # Set strategy one-hot (last 3 dims)
            obs[0, 51:54] = 0.0
            obs[0, 51 + strat_idx] = 1.0

            result = session.run(None, {"obs": obs})[0][0]
            weights_str = ", ".join(f"{n}={v:.3f}" for n, v in zip(eqs_labels, result))
            print(f"  {strat_name}: {weights_str}")

    except ImportError:
        print("onnxruntime not available, skipping inference test")


def main():
    parser = argparse.ArgumentParser(description="Export Schola-compatible ONNX (54-dim input, v10.2.3)")
    parser.add_argument("--checkpoint", type=str, required=True,
                        help="RLlib checkpoint directory (e.g. .../best)")
    parser.add_argument("--policy", type=str, default=None,
                        choices=["assault_policy", "defend_policy", "support_policy"],
                        help="Single policy to export (omit to use --all)")
    parser.add_argument("--all", action="store_true",
                        help="Export all three strategy policies")
    parser.add_argument("--output", type=str, default=None,
                        help="Output ONNX path (used only with --policy; defaults to <policy_name>_schola.onnx)")
    parser.add_argument("--output_dir", type=str, default=".",
                        help="Output directory when using --all (default: current dir)")
    parser.add_argument("--validate", action="store_true", default=True,
                        help="Validate and test the exported model")
    args = parser.parse_args()

    if not args.all and not args.policy:
        parser.error("Specify --policy <name> or --all")

    policies_to_export = (
        ["assault_policy", "defend_policy", "support_policy"]
        if args.all
        else [args.policy]
    )

    for policy_name in policies_to_export:
        print(f"\n{'='*60}")
        print(f"Exporting: {policy_name}")
        print(f"{'='*60}")

        policy = load_policy_from_rllib(args.checkpoint, policy_name)
        policy.eval()

        if args.all or args.output is None:
            output_path = os.path.join(args.output_dir, f"{policy_name}_schola.onnx")
        else:
            output_path = args.output

        os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
        export_schola_onnx(policy, output_path)

        if args.validate:
            validate_onnx(output_path)

    print("\nDone. Import the .onnx files into UE5 Content/Game/Models/ via drag-and-drop.")


if __name__ == "__main__":
    main()
