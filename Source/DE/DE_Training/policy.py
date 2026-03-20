"""
Entity-Centric Policy Network
DE v10.2 — Phase 6 (entity-centric-refactor-plan.md §2.3)

Replaces the flat LinearPolicy with a permutation-invariant attention encoder.
Input: variable-length entity token sets (self, allies, enemies, bases)
Output: 7-dim EQS weight vector

Token shapes:
  self_obs : (B, 7)      — pos(3) + health(1) + vel(3)
  allies   : (B, Na, 5)  — rel_pos(3) + health(1) + alive(1)
  enemies  : (B, Ne, 5)  — rel_pos(3) + visible(1) + confidence(1)
  bases    : (B, Nb, 7)  — rel_pos(3) + ownership(1) + progress(1) + assigned(1) + strategic(1)

Padding masks: True = absent slot (suppresses attention), matching nn.MultiheadAttention convention.

ONNX export supports dynamic axes for Na/Ne/Nb — see export_policy_onnx().
For NNE (UE5) with fixed-shape requirement, pad to MAX_ALLIES=8, MAX_ENEMIES=8, MAX_BASES=8.
"""

import torch
import torch.nn as nn


# ── Capacity constants (must match DEObservationTypes.h) ──────────────────────
MAX_ALLIES  = 8
MAX_ENEMIES = 8
MAX_BASES   = 8

# Token dimensions (must match entity-centric-refactor-plan.md §2.2)
DIM_SELF   = 7
DIM_ALLY   = 5
DIM_ENEMY  = 5
DIM_BASE   = 7

# Output: EQS weight indices (§2.4)
DIM_EQS    = 7   # [EnemyObj, AllyObj, Cover, EnemyVis, AllyProx, CombatRange, AssignedBase]


class EntityCentricPolicy(nn.Module):
    """
    Permutation-invariant policy for variable entity sets.

    Architecture:
        Per-type linear projections → cross-attention (self as query) →
        concat(self, ally_ctx, enemy_ctx, base_ctx) → MLP head → 7 EQS weights
    """

    def __init__(self, hidden: int = 64, heads: int = 4):
        super().__init__()
        assert hidden % heads == 0, "hidden must be divisible by heads"

        # Entity projections
        self.self_enc  = nn.Linear(DIM_SELF,  hidden)
        self.ally_enc  = nn.Linear(DIM_ALLY,  hidden)
        self.enemy_enc = nn.Linear(DIM_ENEMY, hidden)
        self.base_enc  = nn.Linear(DIM_BASE,  hidden)

        # Cross-attention: query = self embedding, key/value = entity embeddings
        self.ally_attn  = nn.MultiheadAttention(hidden, heads, batch_first=True)
        self.enemy_attn = nn.MultiheadAttention(hidden, heads, batch_first=True)
        self.base_attn  = nn.MultiheadAttention(hidden, heads, batch_first=True)

        # Layer norms on each context vector
        self.norm_ally  = nn.LayerNorm(hidden)
        self.norm_enemy = nn.LayerNorm(hidden)
        self.norm_base  = nn.LayerNorm(hidden)

        # MLP policy head: hidden*4 → 7
        self.policy_head = nn.Sequential(
            nn.Linear(hidden * 4, 256), nn.ReLU(),
            nn.Linear(256, 128),        nn.ReLU(),
            nn.Linear(128, DIM_EQS),
        )

    def forward(
        self,
        self_obs:    torch.Tensor,            # (B, 7)
        allies:      torch.Tensor,            # (B, Na, 5)
        enemies:     torch.Tensor,            # (B, Ne, 5)
        bases:       torch.Tensor,            # (B, Nb, 7)
        ally_mask:   torch.Tensor | None = None,   # (B, Na)  True = pad
        enemy_mask:  torch.Tensor | None = None,   # (B, Ne)
        base_mask:   torch.Tensor | None = None,   # (B, Nb)
    ) -> torch.Tensor:                        # (B, 7)

        # --- self encoding ---
        s = self.self_enc(self_obs)           # (B, hidden)
        q = s.unsqueeze(1)                    # (B, 1, hidden)

        # --- entity encodings ---
        a_enc = self.ally_enc(allies)         # (B, Na, hidden)
        e_enc = self.enemy_enc(enemies)       # (B, Ne, hidden)
        b_enc = self.base_enc(bases)          # (B, Nb, hidden)

        # --- cross-attention: self queries each entity set ---
        a_ctx, _ = self.ally_attn(q, a_enc, a_enc,  key_padding_mask=ally_mask)
        e_ctx, _ = self.enemy_attn(q, e_enc, e_enc, key_padding_mask=enemy_mask)
        b_ctx, _ = self.base_attn(q, b_enc, b_enc,  key_padding_mask=base_mask)

        # Squeeze sequence dim + layer norm
        a_ctx = self.norm_ally(a_ctx.squeeze(1))    # (B, hidden)
        e_ctx = self.norm_enemy(e_ctx.squeeze(1))   # (B, hidden)
        b_ctx = self.norm_base(b_ctx.squeeze(1))    # (B, hidden)

        # --- concat and MLP ---
        combined = torch.cat([s, a_ctx, e_ctx, b_ctx], dim=-1)  # (B, hidden*4)
        return self.policy_head(combined)                        # (B, 7)


# ── ONNX export ───────────────────────────────────────────────────────────────

def export_policy_onnx(
    model: EntityCentricPolicy,
    output_path: str,
    device: str = "cpu",
    fixed_shape: bool = False,
) -> None:
    """
    Export policy to ONNX.

    Args:
        fixed_shape: If True, pad to MAX_ALLIES/MAX_ENEMIES/MAX_BASES (required for UE5 NNE).
                     If False, use dynamic axes (for pure-Python / non-NNE inference).
    """
    model.eval()
    B = 1

    if fixed_shape:
        Na, Ne, Nb = MAX_ALLIES, MAX_ENEMIES, MAX_BASES
    else:
        Na, Ne, Nb = 3, 4, 5  # representative sizes for tracing

    dummy_self    = torch.zeros(B, DIM_SELF,  device=device)
    dummy_allies  = torch.zeros(B, Na, DIM_ALLY,  device=device)
    dummy_enemies = torch.zeros(B, Ne, DIM_ENEMY, device=device)
    dummy_bases   = torch.zeros(B, Nb, DIM_BASE,  device=device)
    dummy_amask   = torch.zeros(B, Na,  dtype=torch.bool, device=device)
    dummy_emask   = torch.zeros(B, Ne,  dtype=torch.bool, device=device)
    dummy_bmask   = torch.zeros(B, Nb,  dtype=torch.bool, device=device)

    inputs = (dummy_self, dummy_allies, dummy_enemies, dummy_bases,
              dummy_amask, dummy_emask, dummy_bmask)

    input_names  = ["self_obs", "allies", "enemies", "bases",
                    "ally_mask", "enemy_mask", "base_mask"]
    output_names = ["eqs_weights"]

    if fixed_shape:
        dynamic_axes = {
            "self_obs":    {0: "batch"},
            "allies":      {0: "batch"},
            "enemies":     {0: "batch"},
            "bases":       {0: "batch"},
            "ally_mask":   {0: "batch"},
            "enemy_mask":  {0: "batch"},
            "base_mask":   {0: "batch"},
            "eqs_weights": {0: "batch"},
        }
    else:
        dynamic_axes = {
            "self_obs":    {0: "batch"},
            "allies":      {0: "batch", 1: "n_allies"},
            "enemies":     {0: "batch", 1: "n_enemies"},
            "bases":       {0: "batch", 1: "n_bases"},
            "ally_mask":   {0: "batch", 1: "n_allies"},
            "enemy_mask":  {0: "batch", 1: "n_enemies"},
            "base_mask":   {0: "batch", 1: "n_bases"},
            "eqs_weights": {0: "batch"},
        }

    torch.onnx.export(
        model,
        inputs,
        output_path,
        export_params=True,
        opset_version=17,
        do_constant_folding=True,
        input_names=input_names,
        output_names=output_names,
        dynamic_axes=dynamic_axes,
    )
    print(f"[policy] ONNX exported → {output_path}  (fixed_shape={fixed_shape})")
