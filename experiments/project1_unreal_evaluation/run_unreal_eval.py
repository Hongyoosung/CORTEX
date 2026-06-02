"""
Stage 2: Unreal short behavioral evaluation.

Connects to a running headless UE5 (Schola gRPC), drives the *final Self+Cross
attention* MAPPO checkpoint (the three per-role EntityCentricPolicy networks)
against the fixed Scripted AI, and logs per-step behaviour so we can compute the
Problem-2 behavioural metrics:

  - Duplicate objective selection rate
  - Objective coverage
  - (secondary) win rate / mean episode reward

"Selected objective" is derived per agent from its own 226-dim observation,
which already contains the base/objective tokens — no extra UE5 logging needed:

  base token i = obs[143 + i*7 : 143 + i*7 + 7]
      [rel_x/15000, rel_y/15000, rel_z/1000, ownership, capture_progress,
       is_assigned_target, strategic_value]
  base_mask    = obs[215:223]   (0 = present, 1 = padding)
  self health  = obs[6]

Two objective definitions are logged (both are reported in the result doc):
  - assigned : argmax is_assigned_target over valid bases (Squad-Commander intent)
  - nearest  : argmin |rel_pos| over valid bases (realised position, what the
               attention-driven EQS movement actually produces)

Usage (UE5 must already be running headless with Schola — see launch_ue5 below):
  python run_unreal_eval.py --episodes 24 --port 50051 \
      --checkpoint ../../DE_Training/training_results/20260328_032546/best
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from collections import defaultdict
from datetime import datetime

import numpy as np
import torch

# Make DE_Training/training importable (policy, env_wrapper, eval_live helpers).
_TRAIN_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "DE_Training", "training")
)
if _TRAIN_DIR not in sys.path:
    sys.path.insert(0, _TRAIN_DIR)

from policy import (  # noqa: E402
    BASE_START, BASE_MASK_START, STRATEGY_START, BASE_DIM, MAX_BASES, EQS_DIM,
)
from env_wrapper import DEEntityCentricEnv, AGENT_STRATEGY_REGISTRY, AGENT_OBS_DIM  # noqa: E402
from eval_live import STRATEGY_NAMES  # noqa: E402

# Reuse the Stage-1 tolerant checkpoint loader (handles the Ray-version pickle
# mismatch that a plain pickle.load chokes on).
_PROBE_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "project1_attention_probe")
)
if _PROBE_DIR not in sys.path:
    sys.path.insert(0, _PROBE_DIR)
from ckpt_loader import load_role_policies  # noqa: E402


def load_policies_from_checkpoint(checkpoint_dir):
    """dict[role] -> EntityCentricPolicy (eval mode), via the tolerant loader."""
    return {role: model for role, (model, _sd) in load_role_policies(checkpoint_dir).items()}

# Base-token field offsets within a single base token.
B_OWNERSHIP = 3
B_ASSIGNED = 5

NO_OBJECTIVE = -1


# ── Objective derivation from a single agent's observation ────────────────────

def _agent_obs_226(obs):
    """The wrapper returns 297-dim MAPPO obs; the agent obs is the first 226."""
    return obs[:AGENT_OBS_DIM]


def _valid_bases(obs226):
    mask = obs226[BASE_MASK_START:STRATEGY_START] > 0.5  # True = padding
    return ~mask  # True = valid


def _is_alive(obs226):
    # Self token layout (DEObservationTypes.h):
    #   [pos_x, pos_y, pos_z, health, vel_x, vel_y, vel_z]  → health at index 3.
    return float(obs226[3]) > 0.05


def selected_objectives(obs226):
    """Return (assigned_id, nearest_id) for one agent, or NO_OBJECTIVE."""
    valid = _valid_bases(obs226)
    if valid.sum() == 0:
        return NO_OBJECTIVE, NO_OBJECTIVE

    assigned_flags = np.full(MAX_BASES, -np.inf)
    dists = np.full(MAX_BASES, np.inf)
    for i in range(MAX_BASES):
        if not valid[i]:
            continue
        s = BASE_START + i * BASE_DIM
        assigned_flags[i] = float(obs226[s + B_ASSIGNED])
        rx, ry = float(obs226[s + 0]), float(obs226[s + 1])
        dists[i] = (rx * rx + ry * ry) ** 0.5

    # assigned objective only counts if any base is actually flagged (>0.5)
    assigned_id = int(np.argmax(assigned_flags)) if np.max(assigned_flags) > 0.5 else NO_OBJECTIVE
    nearest_id = int(np.argmin(dists))
    return assigned_id, nearest_id


def num_valid_objectives(obs226):
    return int(_valid_bases(obs226).sum())


# ── Team-level duplicate / coverage from a set of per-agent selections ────────

def team_duplicate_and_coverage(selections, n_objectives):
    """selections: list of objective ids for ALIVE teammates (NO_OBJECTIVE filtered).
    Returns (is_duplicate_step: int|None, coverage: float|None).
    None when there are no valid alive selections."""
    sel = [s for s in selections if s != NO_OBJECTIVE]
    n_alive = len(sel)
    if n_alive == 0 or n_objectives <= 0:
        return None, None
    unique = len(set(sel))
    is_dup = 1 if unique < n_alive else 0  # 2+ agents share an objective
    coverage = unique / min(n_alive, n_objectives)
    return is_dup, coverage


# ── Inference (single checkpoint, all envs) ──────────────────────────────────

@torch.no_grad()
def compute_actions(obs_dict, policies):
    actions = {}
    for agent_id, obs in obs_dict.items():
        if agent_id == "__all__":
            continue
        strat = AGENT_STRATEGY_REGISTRY.get(agent_id, 0)
        role = STRATEGY_NAMES.get(strat, "strike")
        agent_obs = torch.from_numpy(_agent_obs_226(obs)).float().unsqueeze(0)
        a = policies[role](agent_obs).squeeze(0).numpy()
        actions[agent_id] = np.clip(a, -1.0, 1.0).astype(np.float32)
    return actions


# ── Episode result from terminal info ────────────────────────────────────────

def episode_result(env, env_idx, info_dict):
    agents = env._agents_for_env(env_idx)
    winner = None
    ended = False
    for aid in agents:
        info = info_dict.get(aid, {})
        if str(info.get("MatchEnded", "false")).lower() == "true":
            ended = True
            raw = info.get("WinnerTeamID")
            if raw is not None:
                try:
                    winner = int(raw)
                except (ValueError, TypeError):
                    pass
            break
    steps = env._env_episode_steps.get(env_idx, 0)
    if not ended and steps >= env._max_episode_steps:
        return "timeout"
    if winner == 1:
        return "win"
    if winner == 0:
        return "loss"
    return "draw"


# ── Main loop ────────────────────────────────────────────────────────────────

def run(args):
    out_dir = args.out_dir
    os.makedirs(out_dir, exist_ok=True)

    print("=" * 70)
    print("Stage 2 — Unreal Short Behavioral Evaluation")
    print(f"  checkpoint : {args.checkpoint}")
    print(f"  target eps : {args.episodes}")
    print(f"  UE5        : {args.host}:{args.port}")
    print("=" * 70)

    policies = load_policies_from_checkpoint(args.checkpoint)
    print(f"  policies loaded: {list(policies.keys())}")

    env = DEEntityCentricEnv(
        host=args.host, port=args.port, num_envs=args.num_envs,
        agents_per_env=args.agents_per_env,
        post_reset_delay=float(os.environ.get("POST_RESET_DELAY", "4.0")),
    )
    print(f"  connected: num_envs={env.num_envs}\n")

    # Per-step behaviour log rows (raw artifact).
    step_rows = []
    # Per-(env) running episode index.
    ep_index = {ei: 0 for ei in range(env.num_envs)}
    # Aggregates.
    dup_assigned = []   # per team-step 0/1
    cov_assigned = []
    dup_nearest = []
    cov_nearest = []
    episode_results = []   # win/loss/draw/timeout
    episode_rewards = []

    total_eps = 0
    target = args.episodes

    obs_dict, info_dict = env.reset()
    prev_done = {ei: env._env_episodes_done.get(ei, 0) for ei in range(env.num_envs)}
    t0 = time.time()
    global_step = 0

    while total_eps < target:
        actions = compute_actions(obs_dict, policies)
        obs_dict, rew_dict, term_dict, trunc_dict, info_dict = env.step(actions)
        global_step += 1

        # ── Per-env team-level behaviour logging ─────────────────────────────
        for ei in range(env.num_envs):
            if ei in env._completed_envs:
                continue
            agents = env._agents_for_env(ei)
            step_in_ep = env._env_episode_steps.get(ei, 0)

            assigned_sel, nearest_sel = [], []
            n_obj = 0
            for aid in agents:
                if aid not in obs_dict:
                    continue
                o = _agent_obs_226(obs_dict[aid])
                if not _is_alive(o):
                    continue
                a_id, n_id = selected_objectives(o)
                assigned_sel.append(a_id)
                nearest_sel.append(n_id)
                n_obj = max(n_obj, num_valid_objectives(o))

                strat = AGENT_STRATEGY_REGISTRY.get(aid, 0)
                step_rows.append({
                    "env_id": ei,
                    "episode": ep_index[ei],
                    "step": step_in_ep,
                    "agent_id": aid,
                    "role": STRATEGY_NAMES.get(strat, "strike"),
                    "assigned_objective": a_id,
                    "nearest_objective": n_id,
                    "n_valid_objectives": num_valid_objectives(o),
                    "reward": round(float(rew_dict.get(aid, 0.0)), 5),
                })

            d_a, c_a = team_duplicate_and_coverage(assigned_sel, n_obj)
            d_n, c_n = team_duplicate_and_coverage(nearest_sel, n_obj)
            if d_a is not None:
                dup_assigned.append(d_a); cov_assigned.append(c_a)
            if d_n is not None:
                dup_nearest.append(d_n); cov_nearest.append(c_n)

        # ── Episode-end detection ────────────────────────────────────────────
        for ei in range(env.num_envs):
            if total_eps >= target:
                break
            cur = env._env_episodes_done.get(ei, 0)
            if cur <= prev_done[ei]:
                continue
            res = episode_result(env, ei, info_dict)
            if res == "draw":
                agents = env._agents_for_env(ei)
                if any(trunc_dict.get(a, False) for a in agents):
                    res = "timeout"
            ep_r = sum(env._agent_ep_rewards.get(a, 0.0) for a in env._agents_for_env(ei))
            episode_results.append(res)
            episode_rewards.append(ep_r)
            ep_index[ei] += 1
            prev_done[ei] = cur
            total_eps += 1
            print(f"  [ep {total_eps}/{target}] env{ei} {res.upper():8s} "
                  f"reward={ep_r:.2f}  ({time.time()-t0:.0f}s elapsed)")

        # ── Reset when all envs finished their episode batch ─────────────────
        if term_dict.get("__all__") or trunc_dict.get("__all__"):
            if total_eps < target:
                obs_dict, info_dict = env.reset()
                prev_done = {ei: env._env_episodes_done.get(ei, 0) for ei in range(env.num_envs)}

    elapsed = time.time() - t0
    env.close()

    # ── Compute summary metrics ──────────────────────────────────────────────
    def _mean(x):
        return round(float(np.mean(x)), 5) if x else None

    n_win = episode_results.count("win")
    n_loss = episode_results.count("loss")
    n_draw = episode_results.count("draw")
    n_to = episode_results.count("timeout")
    decided = n_win + n_loss + n_draw

    summary = {
        "run_timestamp": datetime.now().isoformat(timespec="seconds"),
        "checkpoint": os.path.abspath(args.checkpoint),
        "torch_version": torch.__version__,
        "host_port": f"{args.host}:{args.port}",
        "num_envs": env.num_envs,
        "episodes": total_eps,
        "elapsed_seconds": round(elapsed, 1),
        "team_steps_logged": {
            "assigned": len(dup_assigned),
            "nearest": len(dup_nearest),
        },
        "metrics": {
            "duplicate_rate_assigned": _mean(dup_assigned),
            "duplicate_rate_nearest": _mean(dup_nearest),
            "objective_coverage_assigned": _mean(cov_assigned),
            "objective_coverage_nearest": _mean(cov_nearest),
            "win_rate": round(n_win / decided, 4) if decided else None,
            "mean_episode_reward": _mean(episode_rewards),
            "episode_reward_std": round(float(np.std(episode_rewards)), 5) if episode_rewards else None,
        },
        "episode_breakdown": {
            "win": n_win, "loss": n_loss, "draw": n_draw, "timeout": n_to,
        },
        "episode_results": episode_results,
        "episode_rewards": [round(float(r), 3) for r in episode_rewards],
    }

    # ── Write outputs ────────────────────────────────────────────────────────
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    json_path = os.path.join(out_dir, f"unreal_eval_metrics_{ts}.json")
    with open(json_path, "w") as f:
        json.dump(summary, f, indent=2)

    csv_path = os.path.join(out_dir, f"unreal_eval_stepwise_{ts}.csv")
    if step_rows:
        import csv as _csv
        fields = list(step_rows[0].keys())
        with open(csv_path, "w", newline="") as f:
            w = _csv.DictWriter(f, fieldnames=fields)
            w.writeheader()
            w.writerows(step_rows)

    print("\n" + "=" * 70)
    print("STAGE 2 COMPLETE")
    print(f"  episodes={total_eps}  elapsed={elapsed:.0f}s")
    print(f"  duplicate_rate (nearest)  = {summary['metrics']['duplicate_rate_nearest']}")
    print(f"  duplicate_rate (assigned) = {summary['metrics']['duplicate_rate_assigned']}")
    print(f"  objective_coverage (nearest)  = {summary['metrics']['objective_coverage_nearest']}")
    print(f"  objective_coverage (assigned) = {summary['metrics']['objective_coverage_assigned']}")
    print(f"  win_rate = {summary['metrics']['win_rate']}  "
          f"(W{n_win}/L{n_loss}/D{n_draw}/T{n_to})")
    print(f"  mean_episode_reward = {summary['metrics']['mean_episode_reward']}")
    print(f"\n  metrics → {json_path}")
    print(f"  stepwise → {csv_path}")
    return summary


def main():
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--checkpoint", default=os.path.join(
        here, "..", "..", "DE_Training", "training_results", "20260328_032546", "best"))
    ap.add_argument("--episodes", type=int, default=24)
    ap.add_argument("--host", default="localhost")
    ap.add_argument("--port", type=int, default=50051)
    ap.add_argument("--num-envs", type=int, default=2)
    ap.add_argument("--agents-per-env", type=int, default=5)
    ap.add_argument("--out-dir", default=os.path.join(here, "outputs"))
    args = ap.parse_args()
    run(args)


if __name__ == "__main__":
    main()
