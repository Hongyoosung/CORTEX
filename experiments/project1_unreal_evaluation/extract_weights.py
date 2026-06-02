"""
Run INSIDE the de-training Docker image (native ray 2.52.1) to extract the
policy actor weights from each role's policy_state.pkl into a plain torch file
(`policy_sd.pt` = stripped `policy.*` state dict). This sidesteps unpickling the
full RLlib policy state in the game_ai env, which has a different ray version.

Usage (from repo root):
  docker run --rm \
    -v "${PWD}/DE_Training/training_results:/app/training_results" \
    -v "${PWD}/experiments:/app/experiments" \
    de-training:latest \
    python /app/experiments/project1_unreal_evaluation/extract_weights.py \
        /app/training_results/20260602_092901/best
"""
import os
import pickle
import sys

import torch

ROLES = ["strike_policy", "vanguard_policy", "support_policy"]


def main(best_dir):
    for name in ROLES:
        pkl = os.path.join(best_dir, "policies", name, "policy_state.pkl")
        if not os.path.exists(pkl):
            print(f"[skip] {pkl} missing")
            continue
        with open(pkl, "rb") as f:
            state = pickle.load(f)
        weights = state.get("weights", {})
        sd = {}
        for k, v in weights.items():
            if not k.startswith("policy."):
                continue
            t = v if isinstance(v, torch.Tensor) else torch.as_tensor(v)
            sd[k[len("policy."):]] = t
        out = os.path.join(best_dir, "policies", name, "policy_sd.pt")
        torch.save(sd, out)
        print(f"[ok] {name}: {len(sd)} tensors -> {out}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "/app/training_results")
