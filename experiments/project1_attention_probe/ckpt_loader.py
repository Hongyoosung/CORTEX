"""
Checkpoint loader for the attention probe.

The trained RLlib `policy_state.pkl` files were produced by a different Ray
version than the one in the `game_ai` env, so a plain `pickle.load` fails on
`ray.rllib.*` imports. We only need the `weights` dict (numpy arrays whose keys
match `EntityCentricPolicy` after stripping the `policy.` prefix), so a tolerant
unpickler that stubs any unimportable class is sufficient and safe.
"""

import io
import os
import pickle
import sys

import torch

# Make `policy` (DE_Training/training/policy.py) importable.
_TRAINING_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "DE_Training", "training")
)
if _TRAINING_DIR not in sys.path:
    sys.path.insert(0, _TRAINING_DIR)

from policy import EntityCentricPolicy, EntityCentricPolicy_NoSelfAttn  # noqa: E402

# Maps RLlib policy directory name -> human role name used in the probe.
ROLE_FROM_POLICY = {
    "strike_policy": "strike",
    "vanguard_policy": "vanguard",
    "support_policy": "support",
}


class _Stub:
    """Placeholder for any class that fails to import during unpickling."""

    def __init__(self, *a, **k):
        pass

    def __setstate__(self, state):
        pass


class _TolerantUnpickler(pickle.Unpickler):
    def find_class(self, module, name):
        try:
            return super().find_class(module, name)
        except (ModuleNotFoundError, AttributeError, ImportError):
            return _Stub


def _tolerant_load(path):
    with open(path, "rb") as f:
        return _TolerantUnpickler(io.BytesIO(f.read())).load()


def _strip_to_policy_state_dict(weights):
    """Keep only `policy.*` keys and strip the prefix → EntityCentricPolicy state."""
    out = {}
    for k, v in weights.items():
        if not k.startswith("policy."):
            continue
        new_k = k[len("policy.") :]
        out[new_k] = v if isinstance(v, torch.Tensor) else torch.as_tensor(v)
    return out


def load_role_policies(checkpoint_dir):
    """Load the three per-role EntityCentricPolicy models from an RLlib checkpoint.

    Returns: dict[role] -> (EntityCentricPolicy in eval mode, raw policy.* state_dict)
    """
    checkpoint_dir = os.path.abspath(checkpoint_dir)
    models = {}
    for policy_name, role in ROLE_FROM_POLICY.items():
        pkl = os.path.join(checkpoint_dir, "policies", policy_name, "policy_state.pkl")
        if not os.path.exists(pkl):
            print(f"  [SKIP] {pkl} not found")
            continue
        state = _tolerant_load(pkl)
        sd = _strip_to_policy_state_dict(state.get("weights", {}))

        model = EntityCentricPolicy()
        missing, unexpected = model.load_state_dict(sd, strict=False)
        if missing:
            print(f"  [warn] {role}: missing keys {missing}")
        if unexpected:
            print(f"  [warn] {role}: unexpected keys {unexpected}")
        model.eval()
        models[role] = (model, sd)
        print(f"  Loaded {role:9s} from {policy_name}")
    return models
