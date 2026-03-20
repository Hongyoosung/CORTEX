"""
Entity-Centric Policy Training Script
DE v10.2 — Phase 6 (entity-centric-refactor-plan.md §6)

Trains EntityCentricPolicy using PPO-style rollouts from Schola gRPC.
New cooperative reward terms (base occupation, co-occupation penalty, etc.)
are automatically handled by EntityCentricEnvWrapper.process_reward().

Usage (inference / ONNX export only):
    python train.py --export_only --checkpoint policy.pth

Usage (full training):
    python train.py --episodes 5000 --steps_per_ep 512 --lr 3e-4

Key Phase-6 items implemented:
    [x] Replace flat LinearPolicy with EntityCentricPolicy
    [x] Environment wrapper handles dict observations from Schola
    [x] collate_fn with padding and mask generation
    [x] ONNX export with dynamic axes (+ fixed-shape option for NNE)
    [x] Reward config includes new cooperative terms
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
from collections import deque

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, TensorDataset

from policy import EntityCentricPolicy, export_policy_onnx, DIM_EQS
from env_wrapper import EntityCentricEnvWrapper, collate_fn


# ── Reward config (mirrors DERewardData.h cooperative params) ─────────────────
REWARD_CONFIG = {
    # Cooperative base occupation (§3 of refactor plan)
    "base_occupation_reward":  2.0,
    "co_occupation_penalty":  -0.5,
    "base_capture_credit":     5.0,
    "undefended_base_penalty": -1.0,
    "assigned_base_reach":     1.0,
    "base_occupation_radius":  2000.0,  # cm, informational only

    # AssignedBaseProximity weight annealing (risk §7)
    # Start at 0.0, linearly anneal to 1.0 over anneal_steps
    "assigned_base_weight_start": 0.0,
    "assigned_base_weight_end":   1.0,
    "assigned_base_anneal_steps": 50_000,
}


# ── PPO loss ───────────────────────────────────────────────────────────────────

def ppo_loss(
    policy: EntityCentricPolicy,
    batch: dict[str, torch.Tensor],
    old_log_probs: torch.Tensor,
    advantages: torch.Tensor,
    returns: torch.Tensor,
    value_net: nn.Module,
    clip_eps: float = 0.2,
    vf_coef: float = 0.5,
    entropy_coef: float = 0.01,
) -> tuple[torch.Tensor, dict]:
    """
    Clipped PPO objective.
    Policy outputs raw EQS weights; we treat them as a diagonal Gaussian action
    distribution (mean=output, fixed log_std) to compute log-probs.
    """
    # Forward pass
    eqs_weights = policy(
        batch["self_obs"], batch["allies"], batch["enemies"], batch["bases"],
        batch["ally_mask"], batch["enemy_mask"], batch["base_mask"],
    )  # (B, 7)

    # Gaussian log-prob with fixed std=0.1 (EQS weights are bounded actions)
    log_std = torch.full_like(eqs_weights, -2.303)  # ln(0.1)
    dist = torch.distributions.Normal(eqs_weights, log_std.exp())
    log_probs = dist.log_prob(batch["action"]).sum(dim=-1)   # (B,)
    entropy   = dist.entropy().sum(dim=-1).mean()

    # PPO clipped ratio
    ratio = (log_probs - old_log_probs).exp()
    surr1 = ratio * advantages
    surr2 = ratio.clamp(1 - clip_eps, 1 + clip_eps) * advantages
    policy_loss = -torch.min(surr1, surr2).mean()

    # Value loss
    values = value_net(batch["self_obs"]).squeeze(-1)   # simple baseline
    value_loss = nn.MSELoss()(values, returns)

    total = policy_loss + vf_coef * value_loss - entropy_coef * entropy
    return total, {
        "policy_loss":  policy_loss.item(),
        "value_loss":   value_loss.item(),
        "entropy":      entropy.item(),
    }


# ── Value network (simple MLP baseline) ───────────────────────────────────────

class ValueNet(nn.Module):
    def __init__(self, self_dim: int = 7):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(self_dim, 128), nn.ReLU(),
            nn.Linear(128, 64),      nn.ReLU(),
            nn.Linear(64, 1),
        )

    def forward(self, self_obs: torch.Tensor) -> torch.Tensor:
        return self.net(self_obs)


# ── Rollout buffer ─────────────────────────────────────────────────────────────

class RolloutBuffer:
    """Stores on-policy transitions for one PPO update."""

    def __init__(self):
        self._samples: list[dict] = []

    def push(self, obs: dict, action: torch.Tensor,
             reward: float, done: bool, log_prob: float):
        sample = dict(obs)
        sample["action"]   = action
        sample["reward"]   = torch.tensor([reward], dtype=torch.float32)
        sample["done"]     = torch.tensor([float(done)], dtype=torch.float32)
        sample["log_prob"] = torch.tensor([log_prob], dtype=torch.float32)
        self._samples.append(sample)

    def compute_returns(self, gamma: float = 0.99) -> tuple[torch.Tensor, torch.Tensor]:
        """Compute discounted returns and simple advantages (return - 0)."""
        rewards = [s["reward"].item() for s in self._samples]
        dones   = [s["done"].item()   for s in self._samples]
        G, returns = 0.0, []
        for r, d in zip(reversed(rewards), reversed(dones)):
            G = r + gamma * G * (1 - d)
            returns.insert(0, G)
        returns_t   = torch.tensor(returns, dtype=torch.float32)
        advantages_t = (returns_t - returns_t.mean()) / (returns_t.std() + 1e-8)
        return returns_t, advantages_t

    def as_dataloader(self, batch_size: int = 64) -> DataLoader:
        return DataLoader(
            self._samples,
            batch_size=batch_size,
            shuffle=True,
            collate_fn=collate_fn,
        )

    def clear(self):
        self._samples.clear()

    def __len__(self):
        return len(self._samples)


# ── Training loop ──────────────────────────────────────────────────────────────

def train(args: argparse.Namespace):
    device = torch.device(args.device)

    # Models
    policy    = EntityCentricPolicy(hidden=64, heads=4).to(device)
    value_net = ValueNet().to(device)

    optimizer = optim.AdamW(
        list(policy.parameters()) + list(value_net.parameters()),
        lr=args.lr,
        weight_decay=1e-4,
    )
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.episodes)

    # Load checkpoint if provided
    if args.checkpoint and Path(args.checkpoint).exists():
        ckpt = torch.load(args.checkpoint, map_location=device)
        policy.load_state_dict(ckpt["policy"])
        value_net.load_state_dict(ckpt["value_net"])
        optimizer.load_state_dict(ckpt["optimizer"])
        start_episode = ckpt.get("episode", 0)
        global_step   = ckpt.get("global_step", 0)
        print(f"[train] Resumed from {args.checkpoint} (ep={start_episode})")
    else:
        start_episode, global_step = 0, 0

    # Environment  (Schola must be running — skip if export_only)
    env = None
    if not args.export_only:
        try:
            from schola.envs import ScholaEnv          # type: ignore
            env = EntityCentricEnvWrapper(ScholaEnv(args.schola_port))
            print(f"[train] Connected to Schola on port {args.schola_port}")
        except ImportError:
            raise RuntimeError(
                "schola package not found. "
                "Ensure the Schola Python package is installed and UE5 is running."
            )

    buffer = RolloutBuffer()
    ep_rewards: deque[float] = deque(maxlen=50)
    os.makedirs(args.output_dir, exist_ok=True)

    for episode in range(start_episode, args.episodes):
        obs    = env.reset()
        ep_ret = 0.0

        # --- rollout ---
        for _ in range(args.steps_per_ep):
            policy.eval()
            with torch.no_grad():
                eqs = policy(
                    obs["self_obs"].unsqueeze(0).to(device),
                    obs["allies"].unsqueeze(0).to(device),
                    obs["enemies"].unsqueeze(0).to(device),
                    obs["bases"].unsqueeze(0).to(device),
                    obs["ally_mask"].unsqueeze(0).to(device),
                    obs["enemy_mask"].unsqueeze(0).to(device),
                    obs["base_mask"].unsqueeze(0).to(device),
                ).squeeze(0)  # (7,)

                # Anneal AssignedBaseProximity weight (risk mitigation §7)
                anneal_frac = min(global_step / REWARD_CONFIG["assigned_base_anneal_steps"], 1.0)
                base_w_max  = REWARD_CONFIG["assigned_base_weight_end"]
                eqs[6] = eqs[6] * (anneal_frac * base_w_max)

                log_std  = torch.full_like(eqs, -2.303)
                dist     = torch.distributions.Normal(eqs, log_std.exp())
                action   = dist.sample()
                log_prob = dist.log_prob(action).sum().item()

            obs_next, reward, done, info = env.step(action)
            buffer.push(obs, action.cpu(), reward, done, log_prob)

            ep_ret    += reward
            global_step += 1
            obs = obs_next

            if done:
                break

        ep_rewards.append(ep_ret)

        # --- PPO update ---
        if len(buffer) >= args.batch_size:
            policy.train()
            returns, advantages = buffer.compute_returns(args.gamma)
            old_log_probs = torch.tensor(
                [s["log_prob"].item() for s in buffer._samples],
                dtype=torch.float32, device=device,
            )
            returns_d    = returns.to(device)
            advantages_d = advantages.to(device)

            for _ in range(args.ppo_epochs):
                loader = buffer.as_dataloader(args.batch_size)
                for i, batch in enumerate(loader):
                    batch = {k: v.to(device) for k, v in batch.items()}
                    old_lp_batch = old_log_probs[i * args.batch_size: (i + 1) * args.batch_size]
                    adv_batch    = advantages_d[i * args.batch_size: (i + 1) * args.batch_size]
                    ret_batch    = returns_d[i * args.batch_size: (i + 1) * args.batch_size]

                    optimizer.zero_grad()
                    loss, info_dict = ppo_loss(
                        policy, batch, old_lp_batch, adv_batch, ret_batch, value_net,
                        clip_eps=args.clip_eps,
                    )
                    loss.backward()
                    nn.utils.clip_grad_norm_(policy.parameters(), max_norm=0.5)
                    optimizer.step()

            buffer.clear()
            scheduler.step()

        # --- logging ---
        if (episode + 1) % args.log_interval == 0:
            mean_ret = np.mean(ep_rewards) if ep_rewards else 0.0
            print(f"[train] ep={episode+1:5d}  mean_ret={mean_ret:8.3f}  "
                  f"step={global_step:7d}  lr={scheduler.get_last_lr()[0]:.2e}")

        # --- checkpoint ---
        if (episode + 1) % args.save_interval == 0:
            ckpt_path = os.path.join(args.output_dir, f"policy_ep{episode+1}.pth")
            torch.save({
                "policy":       policy.state_dict(),
                "value_net":    value_net.state_dict(),
                "optimizer":    optimizer.state_dict(),
                "episode":      episode + 1,
                "global_step":  global_step,
            }, ckpt_path)
            print(f"[train] Checkpoint saved → {ckpt_path}")

    # --- ONNX export ---
    _export(policy, args, device)


def _export(policy: EntityCentricPolicy, args: argparse.Namespace, device: torch.device):
    """Export both a dynamic-axis version and a fixed-shape (NNE) version."""
    policy.eval()

    dyn_path   = os.path.join(args.output_dir, "policy_dynamic.onnx")
    fixed_path = os.path.join(args.output_dir, "policy_fixed.onnx")

    export_policy_onnx(policy, dyn_path,   str(device), fixed_shape=False)
    export_policy_onnx(policy, fixed_path, str(device), fixed_shape=True)

    print(f"\n[train] Dynamic-axis ONNX → {dyn_path}")
    print(f"[train] Fixed-shape ONNX  → {fixed_path}  (copy this to Content/AI/Models/ for NNE)")


# ── CLI ────────────────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Train EntityCentricPolicy (DE v10.2 Phase 6)")

    # Mode
    p.add_argument("--export_only",   action="store_true",
                   help="Skip training; load checkpoint and export ONNX only")

    # Schola
    p.add_argument("--schola_port",   type=int,   default=50051)

    # Training
    p.add_argument("--episodes",      type=int,   default=5000)
    p.add_argument("--steps_per_ep",  type=int,   default=512)
    p.add_argument("--batch_size",    type=int,   default=64)
    p.add_argument("--ppo_epochs",    type=int,   default=4)
    p.add_argument("--lr",            type=float, default=3e-4)
    p.add_argument("--gamma",         type=float, default=0.99)
    p.add_argument("--clip_eps",      type=float, default=0.2)

    # I/O
    p.add_argument("--output_dir",    type=str,   default="./output")
    p.add_argument("--checkpoint",    type=str,   default="",
                   help="Path to .pth checkpoint to resume from")

    # Logging
    p.add_argument("--log_interval",  type=int,   default=10)
    p.add_argument("--save_interval", type=int,   default=100)

    # Device
    p.add_argument("--device",        type=str,
                   default="cuda" if torch.cuda.is_available() else "cpu")

    return p.parse_args()


if __name__ == "__main__":
    args = parse_args()

    if args.export_only:
        if not args.checkpoint:
            raise ValueError("--checkpoint required with --export_only")
        device = torch.device(args.device)
        policy = EntityCentricPolicy(hidden=64, heads=4).to(device)
        ckpt   = torch.load(args.checkpoint, map_location=device)
        policy.load_state_dict(ckpt["policy"])
        os.makedirs(args.output_dir, exist_ok=True)
        _export(policy, args, device)
    else:
        train(args)
