"""
DE Live Evaluation — Best vs Latest Checkpoint Head-to-Head

Connects to UE5 with exactly 2 sub-environments:
  env 0  →  "best"   checkpoint  (strike / vanguard / support policies)
  env 1  →  "latest" checkpoint  (strike / vanguard / support policies)

Both envs run RL (Blue team, TeamID=1) vs ScriptedAI (Red team, TeamID=0).
UE5 must be configured with bUseScriptedOpponent=true on both MatchManagers.

Win / loss / draw / timeout results are recorded per checkpoint and written
to <output_dir>/eval_results_<timestamp>.json.

Usage (standalone):
  python training/eval_live.py \\
      --checkpoint-best   /app/training_results/20260324_031510/best \\
      --checkpoint-latest /app/training_results/20260324_031510/latest \\
      --episodes 50

Usage (Docker via eval_live.bat / docker compose):
  Set CHECKPOINT_BEST, CHECKPOINT_LATEST, EVAL_EPISODES env vars.
"""

from __future__ import annotations

import sys
import os
import time
import pickle
import json
import numpy as np
import torch
from datetime import datetime
from typing import Dict, List, Optional

# Allow both "python training/eval_live.py" and "python eval_live.py" invocations
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

try:
    from training.policy import EntityCentricPolicy, EQS_DIM
    from training.env_wrapper import (
        DEEntityCentricEnv,
        AGENT_STRATEGY_REGISTRY,
        AGENT_OBS_DIM,
    )
except ImportError:
    from policy import EntityCentricPolicy, EQS_DIM
    from env_wrapper import (
        DEEntityCentricEnv,
        AGENT_STRATEGY_REGISTRY,
        AGENT_OBS_DIM,
    )

# Role index → name (must match UE5 EStrategyType enum order)
STRATEGY_NAMES: Dict[int, str] = {0: "strike", 1: "vanguard", 2: "support"}
ROLE_LIST = ("strike", "vanguard", "support")
RolePolicies = Dict[str, EntityCentricPolicy]


# ── Checkpoint loading ────────────────────────────────────────────────────────

def load_policies_from_checkpoint(checkpoint_dir: str) -> RolePolicies:
    """
    Load 3 role policies directly from an RLlib checkpoint directory.

    Extracts actor weights from policy_state.pkl files without instantiating
    a full RLlib Algorithm — significantly faster and avoids Ray overhead.

    Expected layout:
        <checkpoint_dir>/policies/strike_policy/policy_state.pkl
        <checkpoint_dir>/policies/vanguard_policy/policy_state.pkl
        <checkpoint_dir>/policies/support_policy/policy_state.pkl

    Returns:
        {"strike": EntityCentricPolicy, "vanguard": ..., "support": ...}
        All networks are in eval() mode on CPU.
    """
    policies: RolePolicies = {}

    for role in ROLE_LIST:
        pkl_path = os.path.join(
            checkpoint_dir, "policies", f"{role}_policy", "policy_state.pkl"
        )
        if not os.path.exists(pkl_path):
            raise FileNotFoundError(
                f"Policy state not found: {pkl_path}\n"
                f"  Expected RLlib checkpoint under: {checkpoint_dir}\n"
                f"  Run training first or check --checkpoint-best / --checkpoint-latest paths."
            )

        with open(pkl_path, "rb") as f:
            state = pickle.load(f)

        # RLlib TorchPolicy stores weights as {param_name: np.ndarray}
        weights_raw: Dict[str, np.ndarray] = state["weights"]

        # EntityCentricRLlibModel wraps the actor net as self.policy
        # All actor parameters are therefore prefixed "policy."
        actor_weights = {
            k[len("policy."):]: torch.from_numpy(v.copy())
            for k, v in weights_raw.items()
            if k.startswith("policy.")
        }

        if not actor_weights:
            present = list(weights_raw.keys())[:10]
            raise ValueError(
                f"No 'policy.*' weights found in {pkl_path}\n"
                f"  Keys present (first 10): {present}"
            )

        net = EntityCentricPolicy()
        missing, unexpected = net.load_state_dict(actor_weights, strict=False)
        if missing:
            print(f"  [WARN] {role}: missing keys in state_dict: {missing}")
        if unexpected:
            print(f"  [WARN] {role}: unexpected keys in state_dict: {unexpected}")
        net.eval()
        policies[role] = net

    return policies


# ── Inference ─────────────────────────────────────────────────────────────────

@torch.no_grad()
def compute_actions(
    obs_dict:        Dict[str, np.ndarray],
    best_policies:   RolePolicies,
    latest_policies: RolePolicies,
) -> Dict[str, np.ndarray]:
    """
    Compute deterministic (mean) actions for all agents in obs_dict.

    Routing rule:
      agent_id prefix "env0_*"  →  best_policies
      agent_id prefix "env1_*"  →  latest_policies

    The policy forward() returns tanh-squashed means in [-1, 1].
    """
    actions: Dict[str, np.ndarray] = {}

    for agent_id, obs in obs_dict.items():
        if agent_id == "__all__":
            continue

        # Parse env index from agent ID (format: "env{i}_agent{j}")
        try:
            env_idx = int(agent_id.split("_")[0][3:])
        except (IndexError, ValueError):
            env_idx = 0

        policy_set = best_policies if env_idx == 0 else latest_policies

        strategy_idx = AGENT_STRATEGY_REGISTRY.get(agent_id, 0)
        role = STRATEGY_NAMES.get(strategy_idx, "strike")
        policy = policy_set[role]

        # Policy input: first AGENT_OBS_DIM (226) dims of the 297-dim MAPPO obs
        agent_obs = torch.from_numpy(obs[:AGENT_OBS_DIM]).float().unsqueeze(0)  # (1, 226)
        action = policy(agent_obs).squeeze(0).numpy()                           # (7,)
        actions[agent_id] = np.clip(action, -1.0, 1.0).astype(np.float32)

    return actions


# ── Result extraction ─────────────────────────────────────────────────────────

def extract_episode_result(
    env_idx:  int,
    env:      DEEntityCentricEnv,
    info_dict: Dict,
) -> str:
    """
    Determine the outcome of the episode that just ended for env_idx.

    Reads WinnerTeamID from the terminal info channel:
      TeamID 1 (Blue / RL)   → "win"
      TeamID 0 (Red / Script) → "loss"
      TeamID -1 / absent      → "draw"
    Timeout (step limit exceeded without UE5 match-end signal) → "timeout"

    Returns one of: "win" | "loss" | "draw" | "timeout"
    """
    agents = env._agents_for_env(env_idx)
    winner_team_id: Optional[int] = None
    match_ended = False

    for aid in agents:
        info = info_dict.get(aid, {})
        if str(info.get("MatchEnded", "false")).lower() == "true":
            match_ended = True
            raw = info.get("WinnerTeamID")
            if raw is not None:
                try:
                    winner_team_id = int(raw)
                except (ValueError, TypeError):
                    pass
            break

    # Timeout: reached step limit without a UE5 MatchEnded signal
    steps    = env._env_episode_steps.get(env_idx, 0)
    max_step = env._max_episode_steps
    if not match_ended and steps >= max_step:
        return "timeout"

    if winner_team_id == 1:
        return "win"
    elif winner_team_id == 0:
        return "loss"
    else:
        return "draw"


# ── Stats accumulator ─────────────────────────────────────────────────────────

class EvalStats:
    def __init__(self, label: str, checkpoint_path: str):
        self.label           = label
        self.checkpoint_path = checkpoint_path
        self.wins    = 0
        self.losses  = 0
        self.draws   = 0
        self.timeouts = 0
        self._episode_log: List[str] = []

    # ── Derived ───────────────────────────────────────────────────────────

    @property
    def decided(self) -> int:
        return self.wins + self.losses + self.draws

    @property
    def total(self) -> int:
        return self.decided + self.timeouts

    @property
    def win_rate(self) -> float:
        return self.wins / self.decided if self.decided > 0 else 0.0

    # ── Mutation ──────────────────────────────────────────────────────────

    def record(self, result: str):
        self._episode_log.append(result)
        if result == "win":
            self.wins += 1
        elif result == "loss":
            self.losses += 1
        elif result == "draw":
            self.draws += 1
        elif result == "timeout":
            self.timeouts += 1

    # ── Serialisation ─────────────────────────────────────────────────────

    def to_dict(self) -> dict:
        return {
            "checkpoint":     self.checkpoint_path,
            "wins":           self.wins,
            "losses":         self.losses,
            "draws":          self.draws,
            "timeouts":       self.timeouts,
            "total_episodes": self.total,
            "decided_episodes": self.decided,
            "win_rate":       round(self.win_rate, 4),
            "episode_log":    self._episode_log,
        }

    def one_line(self) -> str:
        return (
            f"{self.label:<8}  "
            f"ep={self.total:<4}  "
            f"W={self.wins:<4} L={self.losses:<4} D={self.draws:<4} T={self.timeouts:<4}  "
            f"win_rate={self.win_rate:.1%}"
        )


# ── Main evaluation loop ──────────────────────────────────────────────────────

def run_live_eval(args) -> dict:
    print("=" * 70)
    print("DE Live Evaluation — Checkpoint Head-to-Head")
    print(f"  env 0  →  best   : {args.checkpoint_best}")
    print(f"  env 1  →  latest : {args.checkpoint_latest}")
    print(f"  target episodes  : {args.episodes} per checkpoint")
    print(f"  UE5 host         : {args.host}:{args.port}")
    print("=" * 70 + "\n")

    # ── Step 1: Load policies ─────────────────────────────────────────────
    print("[1/3] Loading checkpoint policies...")
    try:
        best_policies   = load_policies_from_checkpoint(args.checkpoint_best)
        latest_policies = load_policies_from_checkpoint(args.checkpoint_latest)
    except (FileNotFoundError, ValueError) as e:
        print(f"[ERROR] {e}")
        sys.exit(1)

    print(f"  best   checkpoint loaded: {list(best_policies.keys())}")
    print(f"  latest checkpoint loaded: {list(latest_policies.keys())}\n")

    # ── Step 2: Connect to UE5 ────────────────────────────────────────────
    print("[2/3] Connecting to UE5 (2 sub-environments required)...")
    print("  NOTE: UE5 must be running with bUseScriptedOpponent=true on both")
    print("        MatchManagers (Red team = ScriptedAI, Blue team = RL).\n")

    env = DEEntityCentricEnv(
        host=args.host,
        port=args.port,
        num_envs=2,
        agents_per_env=args.agents_per_env,
        post_reset_delay=float(os.environ.get("POST_RESET_DELAY", "4.0")),
    )
    print("Connected.\n")

    if env.num_envs != 2:
        print(
            f"[ERROR] Expected exactly 2 environments from UE5, got {env.num_envs}.\n"
            f"  Ensure exactly 2 ADEScholaEnvironment actors are active in the level."
        )
        sys.exit(1)

    # ── Step 3: Evaluation loop ───────────────────────────────────────────
    print("[3/3] Running evaluation...")
    target = args.episodes

    stats: Dict[int, EvalStats] = {
        0: EvalStats("best",   args.checkpoint_best),
        1: EvalStats("latest", args.checkpoint_latest),
    }

    obs_dict, info_dict = env.reset()

    # Baseline episode-done counters (env._env_episodes_done increments inside step())
    prev_ep_done = {
        0: env._env_episodes_done.get(0, 0),
        1: env._env_episodes_done.get(1, 0),
    }

    eval_start = time.time()

    while min(s.total for s in stats.values()) < target:
        # ── Compute actions ───────────────────────────────────────────────
        actions = compute_actions(obs_dict, best_policies, latest_policies)

        # ── Step UE5 ──────────────────────────────────────────────────────
        obs_dict, rew_dict, term_dict, trunc_dict, info_dict = env.step(actions)

        # ── Detect newly completed episodes ───────────────────────────────
        for ei in (0, 1):
            if stats[ei].total >= target:
                continue
            cur = env._env_episodes_done.get(ei, 0)
            if cur <= prev_ep_done[ei]:
                continue

            # A new episode just ended for env ei
            result = extract_episode_result(ei, env, info_dict)

            # Fallback: if no match-end info and agent was truncated by Schola
            if result == "draw":
                agents = env._agents_for_env(ei)
                if any(trunc_dict.get(a, False) for a in agents):
                    result = "timeout"

            stats[ei].record(result)
            prev_ep_done[ei] = cur

            label  = "best  " if ei == 0 else "latest"
            icon   = {"win": "✓", "loss": "✗", "draw": "~", "timeout": "⊘"}.get(result, "?")
            print(
                f"  [{icon}] env{ei} ({label}) ep {stats[ei].total:>3}/{target}  "
                f"{result.upper():<8}  |  {stats[ei].one_line()}"
            )

        # ── Reset when all envs have completed an episode batch ───────────
        if term_dict.get("__all__") or trunc_dict.get("__all__"):
            if min(s.total for s in stats.values()) < target:
                remaining = {ei: target - s.total for ei, s in stats.items()}
                print(f"\n  [RESET] Both envs finished episode batch — "
                      f"remaining: {remaining}\n")
                obs_dict, info_dict = env.reset()
                prev_ep_done = {
                    0: env._env_episodes_done.get(0, 0),
                    1: env._env_episodes_done.get(1, 0),
                }

    elapsed = time.time() - eval_start

    # ── Print summary ─────────────────────────────────────────────────────
    print("\n" + "=" * 70)
    print("EVALUATION COMPLETE")
    print(f"  elapsed: {elapsed:.1f}s  ({elapsed / 60:.1f} min)")
    print("=" * 70)
    print(
        f"\n  {'':8}  {'Ep':<6}  {'Win':>5}  {'Loss':>5}  {'Draw':>5}  "
        f"{'Timeout':>8}  {'WinRate':>8}"
    )
    print("  " + "-" * 55)
    for ei, s in stats.items():
        print(
            f"  {s.label:<8}  {s.total:<6}  {s.wins:>5}  {s.losses:>5}  "
            f"{s.draws:>5}  {s.timeouts:>8}  {s.win_rate:>7.1%}"
        )

    b = stats[0]
    l = stats[1]
    if b.win_rate > l.win_rate:
        diff = b.win_rate - l.win_rate
        print(f"\n  >> WINNER: best checkpoint  (+{diff:.1%} over latest)")
    elif l.win_rate > b.win_rate:
        diff = l.win_rate - b.win_rate
        print(f"\n  >> WINNER: latest checkpoint  (+{diff:.1%} over best)")
    else:
        print(f"\n  >> TIED  ({b.win_rate:.1%})")

    # ── Save JSON results ─────────────────────────────────────────────────
    output_dir = args.output_dir
    if not output_dir:
        # Default: parent of the best checkpoint dir (the training run root)
        output_dir = os.path.dirname(os.path.abspath(args.checkpoint_best))
    os.makedirs(output_dir, exist_ok=True)

    timestamp   = datetime.now().strftime("%Y%m%d_%H%M%S")
    result_path = os.path.join(output_dir, f"eval_results_{timestamp}.json")
    payload = {
        "timestamp":       datetime.now().isoformat(),
        "elapsed_seconds": round(elapsed, 1),
        "target_episodes": target,
        "checkpoints": {
            s.label: s.to_dict() for s in stats.values()
        },
    }
    with open(result_path, "w") as f:
        json.dump(payload, f, indent=2)
    print(f"\n  Results saved → {result_path}\n")

    return payload


# ── Entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="DE Live Evaluation — compare two checkpoints against ScriptedAI"
    )
    parser.add_argument(
        "--checkpoint-best",
        type=str,
        default=os.environ.get("CHECKPOINT_BEST"),
        help="Path to the 'best' checkpoint directory (or CHECKPOINT_BEST env var)",
    )
    parser.add_argument(
        "--checkpoint-latest",
        type=str,
        default=os.environ.get("CHECKPOINT_LATEST"),
        help="Path to the 'latest' checkpoint directory (or CHECKPOINT_LATEST env var)",
    )
    parser.add_argument(
        "--episodes",
        type=int,
        default=int(os.environ.get("EVAL_EPISODES", "50")),
        help="Target episode count per checkpoint (default: 50)",
    )
    parser.add_argument(
        "--host",
        type=str,
        default=os.environ.get("HOST", "localhost"),
        help="UE5 Schola gRPC host",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=int(os.environ.get("PORT", "50051")),
        help="UE5 Schola gRPC port",
    )
    parser.add_argument(
        "--agents-per-env",
        type=int,
        default=5,
        help="RL (Blue team) agents per UE5 environment (default: 5)",
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        default=None,
        help="Directory for eval_results_*.json (default: parent of --checkpoint-best)",
    )

    args = parser.parse_args()

    if not args.checkpoint_best:
        print("Error: --checkpoint-best required  (or set CHECKPOINT_BEST env var)")
        sys.exit(1)
    if not args.checkpoint_latest:
        print("Error: --checkpoint-latest required  (or set CHECKPOINT_LATEST env var)")
        sys.exit(1)

    run_live_eval(args)
