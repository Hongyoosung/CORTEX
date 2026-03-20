"""
Entity-Centric Environment Wrapper
DE v10.2 — Phase 6 (entity-centric-refactor-plan.md §6)

Wraps the Schola gRPC environment to:
  - Accept dict observations from FDEObservationV2::ToDict()
  - Produce padded tensors + boolean masks for EntityCentricPolicy
  - Apply cooperative reward scaling for new base-occupation terms

Expected observation dict keys (from DETacticalObserver):
    "self"         : float32[7]
    "allies"       : float32[Na * 5]   (flattened)
    "enemies"      : float32[Ne * 5]   (flattened)
    "bases"        : float32[Nb * 7]   (flattened)
    "n_allies"     : int
    "n_enemies"    : int
    "n_bases"      : int

Expected reward dict keys (new cooperative terms from DERewardSubsystem):
    "base_occupation"     : float  (+2.0/step, sole ally near uncontrolled base)
    "co_occupation"       : float  (-0.5/step, 2+ allies stacking)
    "base_capture_credit" : float  (+5.0 sparse, agent that flipped)
    "undefended_base"     : float  (-1.0/step per unguarded friendly base, shared)
    "assigned_base_reach" : float  (+1.0 sparse, first reach of assigned base)
"""

from __future__ import annotations

import numpy as np
import torch
from torch import Tensor
from typing import Any

from policy import (
    MAX_ALLIES, MAX_ENEMIES, MAX_BASES,
    DIM_SELF, DIM_ALLY, DIM_ENEMY, DIM_BASE,
)


# ── Observation parsing ────────────────────────────────────────────────────────

def parse_obs_dict(obs: dict[str, Any]) -> tuple[Tensor, Tensor, Tensor, Tensor,
                                                  Tensor, Tensor, Tensor]:
    """
    Convert a raw observation dict (from Schola) into padded tensors + masks.

    Returns:
        self_t    : (7,)
        ally_t    : (MAX_ALLIES, 5)
        enemy_t   : (MAX_ENEMIES, 5)
        base_t    : (MAX_BASES, 7)
        ally_m    : (MAX_ALLIES,)   bool  — True = padded slot
        enemy_m   : (MAX_ENEMIES,)  bool
        base_m    : (MAX_BASES,)    bool
    """
    self_t = torch.tensor(obs["self"], dtype=torch.float32)  # (7,)

    n_a = int(obs["n_allies"])
    n_e = int(obs["n_enemies"])
    n_b = int(obs["n_bases"])

    ally_t, ally_m   = _pad_entities(obs["allies"],  n_a, MAX_ALLIES,  DIM_ALLY)
    enemy_t, enemy_m = _pad_entities(obs["enemies"], n_e, MAX_ENEMIES, DIM_ENEMY)
    base_t, base_m   = _pad_entities(obs["bases"],   n_b, MAX_BASES,   DIM_BASE)

    return self_t, ally_t, enemy_t, base_t, ally_m, enemy_m, base_m


def _pad_entities(flat: Any, n: int, max_n: int, dim: int) -> tuple[Tensor, Tensor]:
    """
    Reshape a flat array of n*dim floats to (max_n, dim) with zero-padding.
    Returns (tensor, mask) where mask[i]=True means slot i is padding.
    """
    arr = np.array(flat, dtype=np.float32).reshape(n, dim) if n > 0 else np.zeros((0, dim), np.float32)
    padded = np.zeros((max_n, dim), dtype=np.float32)
    if n > 0:
        take = min(n, max_n)
        padded[:take] = arr[:take]
    mask = torch.zeros(max_n, dtype=torch.bool)
    mask[min(n, max_n):] = True   # True = pad
    return torch.from_numpy(padded), mask


# ── Reward post-processing ─────────────────────────────────────────────────────

# Scale factors applied on top of C++ values (let C++ own the magnitudes;
# these scalars are for training-time normalization only)
REWARD_SCALES: dict[str, float] = {
    "base_occupation":     1.0,
    "co_occupation":       1.0,
    "base_capture_credit": 1.0,
    "undefended_base":     1.0,
    "assigned_base_reach": 1.0,
}


def process_reward(raw_reward: float | dict, info: dict | None = None) -> float:
    """
    Combine scalar Schola reward with cooperative sub-rewards from info dict.

    Schola may pass sub-rewards via the info dict (separate fields) or
    already bake them into the scalar. This function handles both cases.
    """
    if isinstance(raw_reward, dict):
        # Schola sends a reward breakdown dict
        total = 0.0
        for key, scale in REWARD_SCALES.items():
            total += raw_reward.get(key, 0.0) * scale
        # Any unlisted keys pass through unscaled
        listed = set(REWARD_SCALES.keys())
        for key, val in raw_reward.items():
            if key not in listed:
                total += float(val)
        return total

    # Scalar reward — add info sub-rewards if present
    base = float(raw_reward)
    if info:
        for key, scale in REWARD_SCALES.items():
            base += info.get(key, 0.0) * scale
    return base


# ── Collation (for batched training) ──────────────────────────────────────────

def collate_fn(samples: list[dict]) -> dict[str, Tensor]:
    """
    Collate a list of parsed sample dicts into batched tensors.

    Each sample must have keys:
        self_obs, allies, enemies, bases,
        ally_mask, enemy_mask, base_mask,
        action, reward, done

    Sequences are padded to the maximum length *in this batch*
    (or MAX_* if parse_obs_dict already pads to fixed size — then this is a no-op).
    """
    keys_simple = ["self_obs", "action", "reward", "done"]
    keys_seq    = ["allies", "enemies", "bases"]
    keys_mask   = ["ally_mask", "enemy_mask", "base_mask"]

    batch: dict[str, Tensor] = {}

    for k in keys_simple:
        batch[k] = torch.stack([s[k] for s in samples])

    for k in keys_seq:
        batch[k] = torch.stack([s[k] for s in samples])

    for k in keys_mask:
        batch[k] = torch.stack([s[k] for s in samples])

    return batch


# ── High-level wrapper ─────────────────────────────────────────────────────────

class EntityCentricEnvWrapper:
    """
    Thin wrapper around a Schola environment instance that converts raw
    observations into the (self, allies, enemies, bases, masks) format
    expected by EntityCentricPolicy.

    Usage::
        env = ScholaEnv(...)          # your existing Schola env
        wrapped = EntityCentricEnvWrapper(env)
        obs = wrapped.reset()
        obs, reward, done, info = wrapped.step(action)
    """

    def __init__(self, env):
        self.env = env

    def reset(self) -> dict[str, Tensor]:
        raw_obs = self.env.reset()
        return self._convert_obs(raw_obs)

    def step(self, action: Tensor) -> tuple[dict[str, Tensor], float, bool, dict]:
        raw_action = action.cpu().numpy()
        raw_obs, raw_reward, done, info = self.env.step(raw_action)
        obs    = self._convert_obs(raw_obs)
        reward = process_reward(raw_reward, info)
        return obs, reward, bool(done), info

    def _convert_obs(self, raw: dict | Any) -> dict[str, Tensor]:
        if not isinstance(raw, dict):
            raise TypeError(
                f"EntityCentricEnvWrapper expects a dict observation from Schola. "
                f"Got {type(raw)}. Ensure DETacticalObserver uses FDEObservationV2::ToDict()."
            )
        self_t, ally_t, enemy_t, base_t, ally_m, enemy_m, base_m = parse_obs_dict(raw)
        return {
            "self_obs":   self_t,
            "allies":     ally_t,
            "enemies":    enemy_t,
            "bases":      base_t,
            "ally_mask":  ally_m,
            "enemy_mask": enemy_m,
            "base_mask":  base_m,
        }

    def __getattr__(self, name: str):
        """Proxy everything else to the underlying env."""
        return getattr(self.env, name)
