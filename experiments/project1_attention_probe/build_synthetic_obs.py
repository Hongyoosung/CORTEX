"""
Synthetic observation builder for the Stage 1 attention probe.

Design principle (from the experiment plan): keep the self token and the base
(objective) tokens FIXED across scenarios, and vary only the entity group under
test. This makes any attention change attributable to the formation change.

Observation layout (226-dim, must match DEObservationTypes.h / policy.py):
    self  (7):  [pos_x, pos_y, pos_z, vel_x, vel_y, vel_z, health]
    ally  (9):  [rel_x/8000, rel_y/8000, rel_z/8000, health, hp_delta, alive,
                 is_strike, is_vanguard, is_support]
    enemy (8):  [rel_x/8000, rel_y/8000, rel_z/8000, health, visible,
                 is_strike, is_vanguard, is_support]
    base  (7):  [rel_x/15000, rel_y/15000, rel_z/1000, ownership(+1/0/-1),
                 capture_progress, is_assigned_target, strategic_value]
    masks: 0.0 = present, 1.0 = padding.
    strategy one-hot [strike, vanguard, support] at [223:226].

Positions below are already in the normalized space the network sees.
"""

import sys
import os

import numpy as np
import torch

sys.path.insert(
    0,
    os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "DE_Training", "training")),
)

from policy import (  # noqa: E402
    OBS_DIM, ALLY_DIM, ENEMY_DIM, BASE_DIM,
    SELF_START, ALLY_START, ENEMY_START, BASE_START,
    ALLY_MASK_START, ENEMY_MASK_START, BASE_MASK_START, STRATEGY_START,
    MAX_ALLIES, MAX_ENEMIES, MAX_BASES,
)

ROLE_TO_STRATEGY_IDX = {"strike": 0, "vanguard": 1, "support": 2}

# ── Fixed scene elements (shared by all scenarios) ───────────────────────────

# Three objectives. Base 0 is the "contested / crowded" objective and the
# self agent's assigned target.
_BASE_POS = [
    (0.30, 0.00),   # base 0  — crowded objective / assigned target
    (-0.30, 0.00),  # base 1
    (0.00, 0.30),   # base 2
]

# Two fixed enemies used when the ally group is the variable under test.
_FIXED_ENEMIES = [
    (0.10, -0.25),
    (-0.05, -0.30),
]


def _empty_obs():
    return torch.zeros(1, OBS_DIM)


def _set_self(obs):
    obs[0, SELF_START + 0:SELF_START + 3] = torch.tensor([0.0, 0.0, 0.0])  # position
    obs[0, SELF_START + 3:SELF_START + 6] = torch.tensor([0.0, 0.0, 0.0])  # velocity
    obs[0, SELF_START + 6] = 1.0                                            # health


def _set_bases(obs, n=3):
    for i in range(n):
        s = BASE_START + i * BASE_DIM
        x, y = _BASE_POS[i]
        obs[0, s + 0] = x
        obs[0, s + 1] = y
        obs[0, s + 2] = 0.0                 # rel_z
        obs[0, s + 3] = 0.0                 # ownership = neutral
        obs[0, s + 4] = 0.2 * i             # capture_progress
        obs[0, s + 5] = float(i == 0)       # is_assigned_target -> base 0
        obs[0, s + 6] = 0.5                 # strategic_value
    obs[0, BASE_MASK_START:BASE_MASK_START + n] = 0.0
    obs[0, BASE_MASK_START + n:STRATEGY_START] = 1.0


def _set_ally(obs, slot, pos, role_flags=(1, 0, 0), health=1.0, hp_delta=0.0):
    s = ALLY_START + slot * ALLY_DIM
    obs[0, s + 0] = pos[0]
    obs[0, s + 1] = pos[1]
    obs[0, s + 2] = 0.0
    obs[0, s + 3] = health
    obs[0, s + 4] = hp_delta
    obs[0, s + 5] = 1.0                      # alive
    obs[0, s + 6] = float(role_flags[0])     # is_strike
    obs[0, s + 7] = float(role_flags[1])     # is_vanguard
    obs[0, s + 8] = float(role_flags[2])     # is_support


def _set_enemy(obs, slot, pos, health=0.8, visible=1.0, role_flags=(1, 0, 0)):
    s = ENEMY_START + slot * ENEMY_DIM
    obs[0, s + 0] = pos[0]
    obs[0, s + 1] = pos[1]
    obs[0, s + 2] = 0.0
    obs[0, s + 3] = health
    obs[0, s + 4] = visible
    obs[0, s + 5] = float(role_flags[0])
    obs[0, s + 6] = float(role_flags[1])
    obs[0, s + 7] = float(role_flags[2])


def _set_masks(obs, n_ally, n_enemy):
    obs[0, ALLY_MASK_START:ALLY_MASK_START + n_ally] = 0.0
    obs[0, ALLY_MASK_START + n_ally:ENEMY_MASK_START] = 1.0
    obs[0, ENEMY_MASK_START:ENEMY_MASK_START + n_enemy] = 0.0
    obs[0, ENEMY_MASK_START + n_enemy:BASE_MASK_START] = 1.0


def _set_strategy(obs, role):
    obs[0, STRATEGY_START:STRATEGY_START + 3] = 0.0
    obs[0, STRATEGY_START + ROLE_TO_STRATEGY_IDX[role]] = 1.0


# Role-flag patterns for the 4 allies (mix of roles) — fixed across scenarios.
_ALLY_ROLE_FLAGS = [(1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 0, 0)]


def _fixed_enemies(obs):
    for i, p in enumerate(_FIXED_ENEMIES):
        _set_enemy(obs, i, p)
    return len(_FIXED_ENEMIES)


# ── Scenarios ────────────────────────────────────────────────────────────────

def scenario_ally_clustered(role):
    """4 allies clustered inside base-0 radius; enemies fixed."""
    obs = _empty_obs()
    _set_self(obs)
    _set_bases(obs)
    bx, by = _BASE_POS[0]
    offsets = [(0.00, 0.00), (0.02, 0.02), (-0.02, 0.01), (0.01, -0.02)]
    for i, off in enumerate(offsets):
        _set_ally(obs, i, (bx + off[0], by + off[1]), _ALLY_ROLE_FLAGS[i])
    n_enemy = _fixed_enemies(obs)
    _set_masks(obs, n_ally=4, n_enemy=n_enemy)
    _set_strategy(obs, role)
    return obs


def scenario_ally_spread(role):
    """4 allies spread across distinct objectives/corners; enemies fixed."""
    obs = _empty_obs()
    _set_self(obs)
    _set_bases(obs)
    positions = [
        _BASE_POS[0],            # base 0
        _BASE_POS[1],            # base 1
        _BASE_POS[2],            # base 2
        (0.00, -0.30),           # open corner
    ]
    for i, pos in enumerate(positions):
        _set_ally(obs, i, pos, _ALLY_ROLE_FLAGS[i])
    n_enemy = _fixed_enemies(obs)
    _set_masks(obs, n_ally=4, n_enemy=n_enemy)
    _set_strategy(obs, role)
    return obs


# Fixed ally formation (spread) used while enemies are the variable under test,
# so enemy self-attention deltas are attributable only to enemy movement.
def _spread_allies(obs):
    positions = [_BASE_POS[0], _BASE_POS[1], _BASE_POS[2], (0.00, -0.30)]
    for i, pos in enumerate(positions):
        _set_ally(obs, i, pos, _ALLY_ROLE_FLAGS[i])
    return 4


def scenario_enemy_converged(role):
    """3 enemies converging toward base 0; allies fixed (spread)."""
    obs = _empty_obs()
    _set_self(obs)
    _set_bases(obs)
    n_ally = _spread_allies(obs)
    bx, by = _BASE_POS[0]
    enemy_pos = [(bx + 0.20, by + 0.08), (bx + 0.15, by - 0.05), (bx + 0.10, by + 0.02)]
    for i, p in enumerate(enemy_pos):
        _set_enemy(obs, i, p)
    _set_masks(obs, n_ally=n_ally, n_enemy=3)
    _set_strategy(obs, role)
    return obs


def scenario_enemy_dispersed(role):
    """3 enemies dispersed across the map; allies fixed (spread).

    Baseline counterpart to enemy_converged for the enemy Self-Attention delta.
    """
    obs = _empty_obs()
    _set_self(obs)
    _set_bases(obs)
    n_ally = _spread_allies(obs)
    enemy_pos = [(0.40, 0.30), (-0.35, -0.20), (0.05, 0.45)]
    for i, p in enumerate(enemy_pos):
        _set_enemy(obs, i, p)
    _set_masks(obs, n_ally=n_ally, n_enemy=3)
    _set_strategy(obs, role)
    return obs


def scenario_padding_stress(role, n_valid):
    """k valid allies (0..4) near base 0, the rest padded; enemies fixed.

    Used to confirm padded slots receive ~zero attention regardless of how
    many entities are valid.
    """
    obs = _empty_obs()
    _set_self(obs)
    _set_bases(obs)
    bx, by = _BASE_POS[0]
    offsets = [(0.00, 0.00), (0.05, 0.03), (-0.04, 0.05), (0.03, -0.05)]
    for i in range(n_valid):
        _set_ally(obs, i, (bx + offsets[i][0], by + offsets[i][1]), _ALLY_ROLE_FLAGS[i])
    n_enemy = _fixed_enemies(obs)
    _set_masks(obs, n_ally=n_valid, n_enemy=n_enemy)
    _set_strategy(obs, role)
    return obs


# Registry of the core named scenarios (excludes the padding sweep, which is
# parameterized separately).
CORE_SCENARIOS = {
    "ally_clustered": scenario_ally_clustered,
    "ally_spread": scenario_ally_spread,
    "enemy_converged": scenario_enemy_converged,
    "enemy_dispersed": scenario_enemy_dispersed,
}

PADDING_STRESS_K = [0, 1, 2, 3, 4]


def valid_counts(scenario_name):
    """Return (n_ally, n_enemy, n_base) valid-entity counts for a core scenario."""
    if scenario_name in ("ally_clustered", "ally_spread"):
        return 4, len(_FIXED_ENEMIES), 3
    if scenario_name in ("enemy_converged", "enemy_dispersed"):
        return 4, 3, 3
    raise KeyError(scenario_name)
