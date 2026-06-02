"""
Attention extraction + metric computation for the Stage 1 probe.

`probe_forward` re-implements EntityCentricPolicy._encode so we can (a) return
every attention map directly and (b) optionally ablate the intra-set
Self-Attention while reusing the *same trained weights*. This gives a clean
Self+Cross vs Cross-only comparison from a single checkpoint: the only thing
removed is the self-attention residual update.
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
    EQS_LABELS,
    SELF_START, ALLY_START, ENEMY_START, BASE_START,
    ALLY_MASK_START, ENEMY_MASK_START, BASE_MASK_START, STRATEGY_START,
    MAX_ALLIES, MAX_ENEMIES, MAX_BASES, ALLY_DIM, ENEMY_DIM, BASE_DIM,
)

# EQS index used as the "go toward where allies/objectives are" preference.
EQS_ALLY_OBJ_PROX = EQS_LABELS.index("AllyObjectiveProximity")  # 1
EQS_ALLY_PROX = EQS_LABELS.index("AllyProximity")               # 4


@torch.no_grad()
def probe_forward(model, flat, ablate_self=False):
    """Run the relational encoder and return EQS output + all attention maps.

    Returns dict with:
        eqs            : (7,) numpy
        ally_self/enemy_self/base_self  : (8,8) numpy  (query x key)
        ally_cross/enemy_cross/base_cross : (8,) numpy (self-query over keys)
        *_mask         : (8,) bool (True = padding)
    """
    B = flat.shape[0]
    assert B == 1, "probe runs one observation at a time"

    self_obs = flat[:, SELF_START:ALLY_START]
    allies = flat[:, ALLY_START:ENEMY_START].view(B, MAX_ALLIES, ALLY_DIM)
    enemies = flat[:, ENEMY_START:BASE_START].view(B, MAX_ENEMIES, ENEMY_DIM)
    bases = flat[:, BASE_START:ALLY_MASK_START].view(B, MAX_BASES, BASE_DIM)

    ally_mask = flat[:, ALLY_MASK_START:ENEMY_MASK_START] > 0.5
    enemy_mask = flat[:, ENEMY_MASK_START:BASE_MASK_START] > 0.5
    base_mask = flat[:, BASE_MASK_START:STRATEGY_START] > 0.5

    def _safe(m):
        all_masked = m.all(dim=1, keepdim=True)
        return m & ~all_masked

    ally_mask, enemy_mask, base_mask = _safe(ally_mask), _safe(enemy_mask), _safe(base_mask)

    s = model.self_enc(self_obs)
    q = s.unsqueeze(1)

    a_enc = model.ally_enc(allies)
    e_enc = model.enemy_enc(enemies)
    b_enc = model.base_enc(bases)

    # Intra-set self-attention (+ residual + LayerNorm), optionally ablated.
    a_rel, a_self_w = model.ally_self_attn(a_enc, a_enc, a_enc, key_padding_mask=ally_mask)
    e_rel, e_self_w = model.enemy_self_attn(e_enc, e_enc, e_enc, key_padding_mask=enemy_mask)
    b_rel, b_self_w = model.base_self_attn(b_enc, b_enc, b_enc, key_padding_mask=base_mask)

    if ablate_self:
        a_enc = model.ally_ln(a_enc)
        e_enc = model.enemy_ln(e_enc)
        b_enc = model.base_ln(b_enc)
    else:
        a_enc = model.ally_ln(a_enc + a_rel)
        e_enc = model.enemy_ln(e_enc + e_rel)
        b_enc = model.base_ln(b_enc + b_rel)

    a_ctx, a_cross_w = model.ally_attn(q, a_enc, a_enc, key_padding_mask=ally_mask)
    e_ctx, e_cross_w = model.enemy_attn(q, e_enc, e_enc, key_padding_mask=enemy_mask)
    b_ctx, b_cross_w = model.base_attn(q, b_enc, b_enc, key_padding_mask=base_mask)

    combined = torch.cat([s, a_ctx.squeeze(1), e_ctx.squeeze(1), b_ctx.squeeze(1)], dim=-1)
    eqs = model.action_head(combined)

    return {
        "eqs": eqs[0].cpu().numpy(),
        "ally_self": a_self_w[0].cpu().numpy(),
        "enemy_self": e_self_w[0].cpu().numpy(),
        "base_self": b_self_w[0].cpu().numpy(),
        "ally_cross": a_cross_w[0, 0].cpu().numpy(),
        "enemy_cross": e_cross_w[0, 0].cpu().numpy(),
        "base_cross": b_cross_w[0, 0].cpu().numpy(),
        "ally_mask": ally_mask[0].cpu().numpy(),
        "enemy_mask": enemy_mask[0].cpu().numpy(),
        "base_mask": base_mask[0].cpu().numpy(),
    }


# ── Metric helpers ───────────────────────────────────────────────────────────

def padding_suppression(result):
    """Mean/max attention assigned to padded KEY slots across all groups.

    For self-attention we average attention received by padded keys over the
    *valid* query rows; for cross-attention we read padded keys directly.
    Near zero means the key_padding_mask is working.
    """
    vals = []
    for grp in ("ally", "enemy", "base"):
        mask = result[f"{grp}_mask"]            # True = padding
        pad = mask
        valid = ~mask
        if pad.sum() == 0:
            continue
        # cross-attention: (8,)
        cross = result[f"{grp}_cross"]
        vals.extend(cross[pad].tolist())
        # self-attention: (8,8) -> attention from valid queries to padded keys
        self_w = result[f"{grp}_self"]
        if valid.sum() > 0:
            sub = self_w[np.ix_(valid, pad)]
            vals.extend(sub.flatten().tolist())
    vals = np.array(vals) if vals else np.array([0.0])
    return float(vals.mean()), float(vals.max())


def _valid_cross(result, grp):
    mask = result[f"{grp}_mask"]
    valid = ~mask
    w = result[f"{grp}_cross"].copy()
    return w, valid


def top_cross_slot(result, grp):
    w, valid = _valid_cross(result, grp)
    w = w.copy()
    w[~valid] = -np.inf
    return int(np.argmax(w))


def top_self_key(result, grp):
    """Most-attended key in self-attention, averaged over valid query rows."""
    mask = result[f"{grp}_mask"]
    valid = ~mask
    self_w = result[f"{grp}_self"]            # (q, k)
    if valid.sum() == 0:
        return 0
    col_mean = self_w[valid][:, :].mean(axis=0)  # (k,)
    col_mean = col_mean.copy()
    col_mean[~valid] = -np.inf
    return int(np.argmax(col_mean))


def self_attn_deltamax(res_a, res_b, grp):
    """Max abs difference in per-key self-attention (averaged over valid queries)
    between two scenarios. Compared only over slots valid in both."""
    valid = (~res_a[f"{grp}_mask"]) & (~res_b[f"{grp}_mask"])
    if valid.sum() == 0:
        return 0.0
    a = res_a[f"{grp}_self"][valid][:, valid].mean(axis=0)
    b = res_b[f"{grp}_self"][valid][:, valid].mean(axis=0)
    return float(np.max(np.abs(a - b)))


def cross_attn_tv_shift(res_a, res_b, grp):
    """Total-variation distance (0.5 * L1) between cross-attention distributions
    of two scenarios, over slots valid in both. Captures distribution shift even
    when the argmax slot does not change. Range [0, 1]."""
    valid = (~res_a[f"{grp}_mask"]) & (~res_b[f"{grp}_mask"])
    if valid.sum() == 0:
        return 0.0
    a = res_a[f"{grp}_cross"][valid]
    b = res_b[f"{grp}_cross"][valid]
    return float(0.5 * np.abs(a - b).sum())


def self_cross_consistency(result, grp):
    """1.0 if the top self-attention key equals the top cross-attention slot."""
    return float(top_self_key(result, grp) == top_cross_slot(result, grp))


def crowded_objective_preference(result):
    """Proxy for 'go toward where allies are' = AllyObjectiveProximity EQS weight."""
    return float(result["eqs"][EQS_ALLY_OBJ_PROX])
