# Validation Project — Agent Brief

## Purpose

This document is the complete specification for a new standalone Python project that validates the research claims in the paper:

> **"Structured Role Differentiation via Label-Conditioned Intra-Set Self-Attention in Cooperative MARL"**

You will build a self-contained training and analysis codebase. The project is a research validation study, not a production system. Code should be clean and reproducible, not over-engineered.

---

## Research Claim to Validate

In heterogeneous cooperative MARL with role-specific rewards, Intra-Set Self-Attention uses role labels as **routing signals for entity selection** more effectively than MLP policies — because role labels condition Q (from `self_tok`) and K/V (from `ally_tok`) simultaneously, enabling role-to-role relational routing.

**Three testable predictions:**
1. Strike agents attend more heavily to Support ally slots than to other role slots
2. Vanguard shows the highest formation sensitivity (Δmax) in self-attention weights
3. Support shows the lowest formation sensitivity — uniformly distributes attention across ally slots regardless of formation

These patterns should be **absent or unstructured** in the MLP baseline (Condition 3).

---

## Phase 1: PoC Environment (MiniGrid)

### Environment Spec

Use `minigrid` (pip: `minigrid`) as a lightweight grid-world base. Build a **custom 3-role cooperative environment** on top of it — do not use an existing MiniGrid scenario.

**Map:** 15×15 grid with 3 capture zones (top-left, top-right, center)

**Team:** 3 agents, one per role — Strike, Vanguard, Support

**Roles:**

| Role | Behavior | Reward Shape |
| :--- | :--- | :--- |
| Strike | Ranged DPS — capture zones, avoid close contact | Approach bonus + capture bonus + too-close penalty (dist < 2) |
| Vanguard | Melee tank — capture zones, engage close | Approach bonus + capture bonus + melee bonus (dist < 2 in zone) |
| Support | Healer — track lowest-HP ally, stay behind them | Approach-to-target bonus + heal bonus + rear-arc bonus |

**Team reward mixing:** `final = 0.8 * individual + 0.2 * team_avg` (α=0.2, matching UE5 project)

**Episode:** 200 steps. Win condition: team captures and holds 2 of 3 zones for 20 consecutive steps.

**Observation per agent (entity-centric vector):**

```
self_tok    [0:7]    — pos(2) + vel(2) + health(1) + role_onehot(3) = 8 dims
                       (role_onehot is [1,0,0]=Strike, [0,1,0]=Vanguard, [0,0,1]=Support)
ally_tok    [8:23]   — 2 allies × 8 dims each: rel_pos(2) + health(1) + alive(1) + role_onehot(3) + padding(1)
enemy_tok   [23:38]  — 2 enemies × 8 dims (same layout, no role_onehot — replace with class_confidence(3))
zone_tok    [38:56]  — 3 zones × 6 dims: rel_pos(2) + occupancy(1) + progress(1) + assigned(1) + value(1)
ally_mask   [56:58]  — 0=valid, 1=padding (2 ally slots)
enemy_mask  [58:60]  — 0=valid, 1=padding
zone_mask   [60:63]  — 0=valid, 1=padding (3 zone slots)
TOTAL: 63 dims
```

**Critical:** Role one-hot must appear in BOTH `self_tok` AND each `ally_tok` slot. This is load-bearing for the theoretical claim.

---

## Policy Network Architecture

Implement all 5 conditions as swappable model classes sharing the same interface. All models take the 63-dim observation and output actions.

### Condition 1 — Full (Proposed)

```python
# Encoder per entity set
self_enc   : Linear(8, 64)
ally_enc   : Linear(8, 64)
enemy_enc  : Linear(8, 64)
zone_enc   : Linear(6, 64)

# Intra-Set Self-Attention (one per set)
ally_self_attn   : MultiheadAttention(64, num_heads=2, batch_first=True)
enemy_self_attn  : MultiheadAttention(64, num_heads=2, batch_first=True)
zone_self_attn   : MultiheadAttention(64, num_heads=2, batch_first=True)
ally_ln, enemy_ln, zone_ln : LayerNorm(64)  # post-residual

# Cross-Attention (self_tok as Query, each set as Key/Value)
ally_cross_attn  : MultiheadAttention(64, num_heads=2, batch_first=True)
enemy_cross_attn : MultiheadAttention(64, num_heads=2, batch_first=True)
zone_cross_attn  : MultiheadAttention(64, num_heads=2, batch_first=True)

# Output MLP
mlp : Linear(64*3 + 64, 128) → ReLU → Linear(128, action_dim)
# input = [ally_ctx, enemy_ctx, zone_ctx, self_emb] concatenated
```

Padding masks: apply `key_padding_mask` to both self-attention and cross-attention using the mask slots. Use `_safe_mask()` to force-unmask slot 0 if all slots are masked (prevents NaN).

### Condition 2 — No Self-Attn (MLP encoder + Cross-Attn)

Same as Condition 1, but **remove the three Intra-Set Self-Attention blocks**. The encoded ally/enemy/zone tensors feed directly into Cross-Attention without intra-set contextualization.

### Condition 3 — Role Label + Pure MLP

Remove ALL attention. Architecture:

```python
# Flatten full 63-dim observation
mlp: Linear(63, 256) → ReLU → Linear(256, 128) → ReLU → Linear(128, action_dim)
```

Role label is still present in the input (concatenated naturally as part of the flat vector).

### Condition 4 — No Role Label + Self-Attn + Cross-Attn (Shared Policy)

Same architecture as Condition 1, but:
- Zero out the role_onehot dimensions in `self_tok` and all `ally_tok` slots before passing to the network
- Use a **single shared policy** for all three roles (one network handles Strike, Vanguard, Support)
- Role-specific rewards still apply during training

### Condition 5 — Flat MLP Baseline

Same as Condition 3 but also zero out role_onehot dimensions. Pure flat MLP, no labels, no attention.

---

## Training Setup

**Algorithm:** MAPPO (Multi-Agent PPO)
- Use `torch` directly — no RLlib dependency for PoC
- Implement a lightweight MAPPO trainer or use `torchrl` if preferred
- Centralized critic: takes concatenated observations of all agents (63×3 = 189 dims) → MLP → value
- GAE λ=0.95, γ=0.99, clip ε=0.2, entropy coeff=0.01
- Adam optimizer, lr=3e-4
- Batch size: 512, mini-batch: 64, epochs per update: 4
- Training: 2M environment steps per condition

**Independent policies (Conditions 1, 2, 3, 5):** Three separate policy networks, one per role. Each network is updated only on trajectories from its own role.

**Shared policy (Condition 4):** One network updated on all trajectories. Role identity is NOT available in the observation.

**Logging:** Log episode win rate, episode reward per role, and entropy per 10k steps. Save checkpoints every 200k steps.

---

## Track 2 Analysis Module

This is the primary scientific contribution. Implement as a separate analysis script `analyze_attention.py` that loads a trained checkpoint and runs the following.

### Scenario Construction

Programmatically construct two fixed scenarios (not sampled from env):

**Clustered:** All 2 ally agents placed within radius 1 of the same zone center. Enemies placed at the opposite corner.

**Spread:** Each ally placed at a different zone (3 zones, 2 allies + self spread across map). Enemies placed at map center.

### Measurements

**1. Self-Attention Weight Extraction**

For each role (Strike, Vanguard, Support) and each scenario (Clustered, Spread):
- Run a forward pass, extract `attn_weight` output from each `MultiheadAttention` call
- Average across heads → shape `(num_ally_slots, num_ally_slots)` for ally_self_attn
- Report the weight each slot assigns to each other slot
- Compute Δmax = max weight difference between Clustered and Spread for each role

**2. Cross-Role Slot Analysis**

For each role, in both scenarios:
- Extract ally cross-attention weights → which ally slots does this role focus on?
- Tag each occupied slot with the ally's role (Strike/Vanguard/Support)
- Aggregate: for each (agent_role, ally_role) pair, record the mean attention weight
- This produces a 3×3 role-routing matrix — rows = agent role, cols = attended ally role
- **Expected:** Strike→Support weight should be notably higher than Strike→Vanguard

**3. MLP Gradient Attribution (Condition 3 baseline)**

For the Role+MLP condition:
- Compute input gradients w.r.t. action output for each scenario
- Separately attribute gradient magnitude to: spatial features vs. role_onehot dims
- If role_onehot gradient magnitude is low relative to spatial features → labels are underutilized in MLP
- Compare to the attention routing matrix from Condition 1

**4. Visualizations**

Generate and save as PNG:
- Heatmap per role: self-attention weight matrix (slots × slots), Clustered vs. Spread side-by-side with difference map
- 3×3 role-routing matrix heatmap for Condition 1 and Condition 3 (gradient attribution version)
- Bar chart: Δmax per role across conditions 1 and 2 (to show self-attention's contribution)

---

## Project Structure

```
validation_poc/
├── env/
│   └── cooperative_grid.py       # Custom MiniGrid environment
├── models/
│   ├── base_policy.py            # Abstract interface all conditions share
│   ├── full_model.py             # Condition 1
│   ├── no_self_attn.py           # Condition 2
│   ├── role_mlp.py               # Condition 3
│   ├── no_role_label.py          # Condition 4 (shared policy)
│   └── flat_mlp.py               # Condition 5
├── training/
│   ├── mappo.py                  # MAPPO trainer
│   ├── rollout.py                # Trajectory collection
│   └── train.py                  # Entry point — accepts --condition [1-5]
├── analysis/
│   ├── analyze_attention.py      # Track 2 analysis entry point
│   ├── scenario_builder.py       # Constructs Clustered / Spread scenarios
│   └── visualize.py              # All plot generation
├── results/                      # Auto-created, gitignored large files
│   ├── checkpoints/
│   └── plots/
├── configs/
│   └── default.yaml              # All hyperparameters in one place
└── requirements.txt
```

---

## Output Expectations

After training all 5 conditions and running analysis, the project should produce:

**Track 1 outputs** (in `results/`):
- `win_rate_curves.png` — all 5 conditions on one plot
- `convergence_table.csv` — steps to 50% win rate per condition

**Track 2 outputs** (in `results/plots/`):
- `self_attn_heatmap_{role}_clustered.png` (×3 roles)
- `self_attn_heatmap_{role}_spread.png` (×3 roles)
- `self_attn_diff_{role}.png` (×3 roles)
- `role_routing_matrix_cond1.png` — 3×3 attention routing matrix
- `role_routing_matrix_cond3_gradient.png` — MLP gradient attribution version
- `delta_max_comparison.png` — bar chart per role, conditions 1 vs. 2

**Claim confirmation checklist** (print to console from `analyze_attention.py`):
```
[PASS/FAIL] Vanguard Δmax is highest among all roles
[PASS/FAIL] Support Δmax is lowest among all roles
[PASS/FAIL] Strike→Support routing weight > Strike→Vanguard routing weight
[PASS/FAIL] Role routing pattern is weaker/unstructured in Condition 3 (MLP)
[PASS/FAIL] No_RoleLabel (shared) win rate < Full win rate by >5pp
```

---

## Dependencies

```
torch>=2.0
minigrid>=2.3
numpy
matplotlib
seaborn
pyyaml
tqdm
```

No RLlib, no Ray, no heavy frameworks. Keep it self-contained.

---

## Notes for the Agent

- The `_safe_mask()` pattern is critical: if all ally slots are padded, force-unmask slot 0 to prevent NaN in attention. Implement this in the base attention utility.
- Condition 4 (shared policy, no role label) is the methodologically tricky one. Double-check that role_onehot is zeroed in BOTH `self_tok` and `ally_tok` before encoding.
- The role-routing matrix (3×3) is the single most important output. If the diagonal is dominant (each role attends to same-role allies), that is also an interesting result — document it. If Strike→Support is highest off-diagonal for Strike, that confirms the primary prediction.
- Do not tune hyperparameters per condition. All 5 conditions use identical hyperparameters from `configs/default.yaml`. The point is structural comparison, not performance maximization.
