"""
ONNX Export for Entity-Centric Policy (DE v10.2+)

Loads the latest EntityCentricPolicy checkpoint (170-dim input, 7-dim output)
and exports it to ONNX for UE5 NNE inference.

Usage:
    # Export latest checkpoint (auto-detected):
    python tools/export_entity_centric_onnx.py

    # Export specific checkpoint:
    python tools/export_entity_centric_onnx.py --checkpoint training_results/20260313_022747/latest

    # Export best checkpoint:
    python tools/export_entity_centric_onnx.py --checkpoint training_results/20260313_022747/best

    # Custom output path:
    python tools/export_entity_centric_onnx.py --output models/entity_centric_policy.onnx
"""

import sys
import os
import pickle
import types
import argparse
import glob
from pathlib import Path

import torch
import torch.nn as nn

# Add parent directory to path so we can import training.policy
sys.path.append(str(Path(__file__).parent.parent))

from training.policy import EntityCentricPolicy, OBS_DIM, EQS_DIM


POLICY_NAME = "entity_centric_policy"


def find_latest_checkpoint(results_dir: str) -> str:
    """Find the most recent training run's 'latest' checkpoint that contains entity_centric_policy."""
    pattern = os.path.join(results_dir, "*", "latest")
    candidates = sorted(glob.glob(pattern))
    # Keep only those that actually contain the target policy
    valid = [
        c for c in candidates
        if os.path.exists(os.path.join(c, "policies", POLICY_NAME, "policy_state.pkl"))
    ]
    if not valid:
        raise FileNotFoundError(
            f"No checkpoints with '{POLICY_NAME}' found under: {results_dir}"
        )
    chosen = valid[-1]  # most recent lexicographically
    print(f"[auto-detect] Using checkpoint: {chosen}")
    return chosen


class _CrossVersionUnpickler(pickle.Unpickler):
    """
    Handles Python 3.10 cloudpickle pickles loaded on Python 3.12.

    ray.cloudpickle serializes code objects via _builtin_type('CodeType').
    Python 3.11+ changed the CodeType constructor signature (added qualname,
    exceptiontable), so pickles made on 3.10 fail with:
        TypeError: code() argument 13 must be str, not int

    This unpickler intercepts _builtin_type and pads the old 16/17-arg
    call to the 18-arg form expected by Python 3.12.
    """

    def find_class(self, module: str, name: str):
        if module == "ray.cloudpickle.cloudpickle" and name == "_builtin_type":
            return _CrossVersionUnpickler._safe_builtin_type
        return super().find_class(module, name)

    @staticmethod
    def _safe_builtin_type(type_name: str):
        if type_name == "CodeType":
            return _CrossVersionUnpickler._safe_code
        import builtins as _b, types as _t
        return getattr(_t, type_name, getattr(_b, type_name, None))

    @staticmethod
    def _safe_code(*args):
        """CodeType constructor compat shim: 16-arg (Py3.10) / 17-arg (Py3.11) -> 18-arg (Py3.12)."""
        if len(args) == 16:
            ac, pac, kwc, nl, ss, fl, cs, co, na, va, fn, nm, fl2, lt, fv, cv = args
            args = (ac, pac, kwc, nl, ss, fl, cs, co, na, va, fn, nm, nm, fl2, lt, b"", fv, cv)
        elif len(args) == 17:
            ac, pac, kwc, nl, ss, fl, cs, co, na, va, fn, nm, fl2, lt, et, fv, cv = args
            args = (ac, pac, kwc, nl, ss, fl, cs, co, na, va, fn, nm, nm, fl2, lt, et, fv, cv)
        return types.CodeType(*args)


def load_policy(checkpoint_path: str) -> EntityCentricPolicy:
    """Load EntityCentricPolicy weights from an RLlib checkpoint pickle."""
    policy_state_path = os.path.join(
        checkpoint_path, "policies", POLICY_NAME, "policy_state.pkl"
    )
    if not os.path.exists(policy_state_path):
        raise FileNotFoundError(f"Policy state not found: {policy_state_path}")

    print(f"Loading weights from: {policy_state_path}")
    with open(policy_state_path, "rb") as f:
        state = _CrossVersionUnpickler(f).load()

    weights = state["weights"]

    # RLlib wraps our policy under a 'policy.' prefix — strip it
    inner_state: dict = {}
    skipped = []
    for key, value in weights.items():
        if key.startswith("policy."):
            inner_key = key[len("policy."):]
            inner_state[inner_key] = (
                torch.tensor(value) if not isinstance(value, torch.Tensor) else value
            )
        else:
            skipped.append(key)

    if skipped:
        print(f"  Skipped {len(skipped)} keys without 'policy.' prefix: {skipped[:5]}")

    policy = EntityCentricPolicy()
    missing, unexpected = policy.load_state_dict(inner_state, strict=False)
    if missing:
        print(f"  Missing keys  ({len(missing)}): {missing[:5]}")
    if unexpected:
        print(f"  Unexpected keys ({len(unexpected)}): {unexpected[:5]}")
    print(f"  Loaded {len(inner_state)} weight tensors successfully.")
    return policy


def export_onnx(policy: EntityCentricPolicy, output_path: str):
    """Export to ONNX with dynamic batch axis."""
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    policy.eval()

    dummy = torch.zeros(1, OBS_DIM)

    torch.onnx.export(
        policy,
        dummy,
        output_path,
        input_names=["observation"],
        output_names=["eqs_weights"],
        dynamic_axes={
            "observation": {0: "batch_size"},
            "eqs_weights": {0: "batch_size"},
        },
        opset_version=14,
    )

    size_kb = os.path.getsize(output_path) / 1024
    print(f"\nExported ONNX: {output_path}  ({size_kb:.1f} KB)")
    print(f"  Input:  observation  (B, {OBS_DIM})  float32")
    print(f"  Output: eqs_weights  (B, {EQS_DIM})   float32  in [-1, 1]")


def validate_onnx(output_path: str):
    """Validate the exported model with onnx + onnxruntime."""
    try:
        import onnx
        model = onnx.load(output_path)
        onnx.checker.check_model(model)
        print("\nONNX graph validation: PASSED")
    except ImportError:
        print("\nonnx package not installed — skipping graph validation.")
        return

    try:
        import onnxruntime as ort
        import numpy as np

        session = ort.InferenceSession(output_path)
        strategies = {0: "Assault", 1: "Defend", 2: "Support"}
        eqs_labels = [
            "EnemyObjProx", "AllyObjProx", "CoverDensity",
            "EnemyVis", "AllyProx", "CombatRange", "AssignedBaseProx",
        ]

        print("\nSample inference — mean EQS weights per strategy:")
        for strat_idx, strat_name in strategies.items():
            obs = np.zeros((1, OBS_DIM), dtype=np.float32)
            obs[0, 167 + strat_idx] = 1.0          # strategy one-hot

            result = session.run(None, {"observation": obs})[0][0]
            weights_str = "  ".join(
                f"{n}={v:+.3f}" for n, v in zip(eqs_labels, result)
            )
            print(f"  [{strat_name:7s}]  {weights_str}")

    except ImportError:
        print("onnxruntime not installed -- skipping inference test.")


def main():
    parser = argparse.ArgumentParser(
        description="Export EntityCentricPolicy to ONNX (170→7)"
    )
    parser.add_argument(
        "--checkpoint",
        type=str,
        default=None,
        help="RLlib checkpoint directory (default: auto-detect latest under training_results/)",
    )
    parser.add_argument(
        "--output",
        type=str,
        default=None,
        help="Output .onnx path (default: training_results/<run>/latest/entity_centric_policy.onnx)",
    )
    parser.add_argument(
        "--no-validate",
        action="store_true",
        help="Skip ONNX validation and inference test",
    )
    args = parser.parse_args()

    # ── Resolve checkpoint path ──────────────────────────────────────────────
    script_dir = Path(__file__).parent.parent  # repo root
    results_dir = str(script_dir / "training_results")

    if args.checkpoint:
        checkpoint_path = args.checkpoint
    else:
        checkpoint_path = find_latest_checkpoint(results_dir)

    # ── Resolve output path ──────────────────────────────────────────────────
    if args.output:
        output_path = args.output
    else:
        output_path = os.path.join(checkpoint_path, "entity_centric_policy.onnx")

    # ── Load, export, validate ───────────────────────────────────────────────
    print(f"\n{'='*60}")
    print("  DE v10.2+ -- EntityCentricPolicy ONNX Export")
    print(f"{'='*60}")

    policy = load_policy(checkpoint_path)
    export_onnx(policy, output_path)

    if not args.no_validate:
        validate_onnx(output_path)

    print(f"\nDone. Copy {output_path}")
    print("to Content/Game/Models/ in UE5 (drag-and-drop import).")


if __name__ == "__main__":
    main()
