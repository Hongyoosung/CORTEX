# MAPPO Implementation — Completion Document

**Project:** DE v10.2 | **Date:** 2026-03-23 | **Branch:** `refactor-scriptai`

---

## 1. Objective

Transition the RL training pipeline from independent PPO (IPPO) with decentralized per-agent critics to **MAPPO with a centralized team-level critic**, enabling proper credit assignment for cooperative rewards (base capture, co-occupation penalties) while retaining the existing entity-centric attention-based actor architecture.

---

## 2. Architecture Decision Record

| Decision | Choice | Rationale |
|---|---|---|
| Value estimation | **Dual** — local + centralized | Local attention-based critic (256-dim features via self/cross-attention) captures per-agent spatial nuance; centralized MLP critic (71-dim global state) captures team-level credit assignment. Combined via learnable mixing coefficient α. |
| Policy count | **Three** — Strike / Vanguard / Support | Roles have fundamentally opposing spatial objectives (Strike maintains range, Vanguard closes distance, Support stays near wounded allies). A single policy conditioned on role would produce conflicting EQS weight gradients. |
| Global state dimension | **71** (not 70) | Actual `FDETeamWorldState::ToTensor()` output: Friendly(35D) + Enemy(30D) + Map(6D) = 71D. The original plan document had an off-by-one error. |
| Critic sharing mechanism | **Class-level reference** within RLlib worker | Safe for `num_workers=0` (single process). For multi-worker, each worker gets its own copy with gradients synced by RLlib automatically. |
| Obs space layout | **297-dim** = agent(226) + global(71) | Global state appended as suffix; actor slices `[:226]`, centralized critic slices `[226:]`. ONNX export unchanged at 226-dim. |
| Info channel format | **Comma-separated float string** | Schola's `FAgentState::Info` is `TMap<FString, FString>`. 71 floats × ~8 chars = ~568 bytes per agent per step — negligible overhead. |

---

## 3. Files Modified

### 3.1 C++ (UE5)

| File | Change |
|---|---|
| `Public/Team/DETeamWorldState.h` | Added `static constexpr TENSOR_DIM = 71` with full dimension breakdown comment. Fixed `Reserve(70)` → `Reserve(TENSOR_DIM)`. |
| `Public/Schola/Trainers/DETrainer.h` | Added `#include "Team/DETeamWorldState.h"`. Declared `FDETeamWorldState BuildTeamWorldState() const`. |
| `Private/Schola/Trainers/DETrainer.cpp` | Implemented `BuildTeamWorldState()`: reads 5 friendly agents, 5 enemy agents, and 5 capture points from `CachedMatchManager`, maps capture ownership relative to agent's team (+1/-1/0), computes time remaining from episode step ratio. Updated `GetInfo()` to serialize the 71-dim tensor as comma-separated floats under key `"global_state"`. |

### 3.2 Python (Training)

| File | Change |
|---|---|
| `DE_Training/training/policy.py` | Added `CentralizedCritic` module (71→256→256→1 MLP with LayerNorm, ~85K params). Added constants `GLOBAL_STATE_DIM = 71`, `MAPPO_OBS_DIM = 297`. |
| `DE_Training/training/env_wrapper.py` | Observation space expanded from 226 to 297. Added `AGENT_OBS_DIM = 226`, `GLOBAL_STATE_DIM = 71`. Added per-env global state cache (`_global_state`). Added `_extract_global_state()` — parses comma-separated float string from Schola info channel, falls back to zeros. Added `_append_global_state()` — concatenates 71-dim global state to each agent's 226-dim obs. Both `reset()` and `step()` now extract and append global state. |
| `DE_Training/training/train.py` | `EntityCentricRLlibModel` updated: `forward()` slices `obs[:226]` for actor, `obs[226:]` for centralized critic. `value_function()` computes `α·V_local + (1-α)·V_central` where α = sigmoid(learnable logit), initialized to 0.5. Shared `CentralizedCritic` across three role policies via class-level reference. TensorBoard logging of `{role}/mappo/value_mix_alpha`. Added validation tests 11–13 (CentralizedCritic shape, MAPPO dimension arithmetic, dual value estimation with RLlib, shared critic identity check). |
| `Docs/MAPPO_Transition.md` | Updated §4 (file summary) and §5 (risk table) to reflect actual implementation. |

---

## 4. Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│  C++ (UE5) — per step, per agent                                │
│                                                                  │
│  ADETrainer::GetInfo()                                           │
│    └─ BuildTeamWorldState()                                      │
│         ├─ CachedMatchManager->GetTeamAgents(TeamID)    → 5 friendly
│         ├─ CachedMatchManager->GetEnemyAgents(TeamID)   → 5 enemy
│         └─ CachedMatchManager->GetCapturePoints()       → 5 CPs
│         ↓                                                        │
│       FDETeamWorldState::ToTensor() → float[71]                  │
│         ↓                                                        │
│       Info["global_state"] = "0.012,0.345,...,0.789"  (71 vals)  │
│         ↓                                                        │
│       Schola gRPC → Python                                       │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  Python (env_wrapper.py) — per step                              │
│                                                                  │
│  _extract_global_state(info, env_idx)                            │
│    └─ np.fromstring(info["global_state"], sep=',')  → float32[71]
│                                                                  │
│  _append_global_state(obs_d)                                     │
│    └─ per agent: np.concatenate([agent_obs_226, global_71])      │
│    → obs_297 to RLlib                                            │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  Python (train.py) — EntityCentricRLlibModel                     │
│                                                                  │
│  forward(obs[B, 297]):                                           │
│    agent_obs    = obs[:, :226]   → actor → means[B, 7]          │
│    global_state = obs[:, 226:]   → cached                        │
│    return [means, log_stds] → (B, 14)                            │
│                                                                  │
│  value_function():                                               │
│    α = sigmoid(learnable_logit)       # init 0.5                 │
│    V_local   = policy.get_value(agent_obs)        # attention    │
│    V_central = centralized_critic(global_state)   # MLP          │
│    return α · V_local + (1-α) · V_central                       │
└─────────────────────────────────────────────────────────────────┘
```

---

## 5. Global State Tensor Layout (71-dim)

| Index | Dim | Field | Normalization |
|---|---|---|---|
| 0–14 | 15 | Friendly positions (5×3D) | X,Y / 10000, Z / 1000 |
| 15–19 | 5 | Friendly health | [0, 1] |
| 20–24 | 5 | Friendly cooldowns | [0, 1] |
| 25–29 | 5 | Friendly alive | 0 or 1 |
| 30–34 | 5 | Friendly classes | 0=Strike, 1=Vanguard, 2=Support |
| 35–49 | 15 | Enemy positions (5×3D) | X,Y / 10000, Z / 1000 |
| 50–54 | 5 | Enemy confidences | [0, 1] (1.0 = direct observation) |
| 55–59 | 5 | Enemy health | [0, 1] |
| 60–64 | 5 | Enemy alive | 0 or 1 |
| 65–69 | 5 | Capture point ownership | +1=friendly, 0=neutral, −1=enemy |
| 70 | 1 | Time remaining | [0, 1] (1.0 = start, 0.0 = end) |

---

## 6. Dual Value Estimation

The value function combines two critics:

$$V(s) = \alpha \cdot V_{\text{local}}(o_i) + (1 - \alpha) \cdot V_{\text{central}}(g)$$

| Component | Input | Architecture | Captures |
|---|---|---|---|
| V_local | 226-dim agent obs | Entity-centric attention → 256-dim → Linear(1) | Per-agent spatial context, entity relationships |
| V_central | 71-dim global state | MLP 71→256→256→1 with LayerNorm | Team composition, all agent positions, credit assignment |
| α | — | sigmoid(learnable scalar), init=0.5 | Automatic balancing; logged per-role as `mappo/value_mix_alpha` |

**Why dual, not replacement:** The local critic processes rich entity-level features (8 allies × 9D, 8 enemies × 8D) through self-attention and cross-attention, providing fine-grained spatial reasoning. The centralized critic sees the compressed team summary (71D) but from all agents simultaneously. Neither alone is strictly superior — combining them lets the network learn the optimal blend.

---

## 7. Validation Results

```
28 passed, 0 failed / 28 total
```

| Test | Description | Status |
|---|---|---|
| 1 | forward() shape (B, 7) and range [-1, 1] | PASS |
| 2 | get_value() shape (B,) | PASS |
| 3 | sample_action() shapes and range | PASS |
| 4 | compute_log_prob() finite | PASS |
| 5 | All-padding mask suppression | PASS |
| 6 | OBS_DIM == 226, layout arithmetic | PASS |
| 7 | ONNX export + ORT reload (err < 1e-4) | PASS |
| 8 | PPOTrainer.update() runs | PASS |
| 9 | Reward config matches plan §3 | PASS |
| 10 | collate_fn shape | PASS |
| **11** | **CentralizedCritic output shape (B,) and finite** | **PASS** |
| **12** | **MAPPO_OBS_DIM=297, GLOBAL_STATE_DIM=71, arithmetic** | **PASS** |
| **13** | **Dual value estimation: RLlib model (B,14) output, (B,) value, shared critic identity** | **PASS** |

Tests 11–13 are new MAPPO-specific validations.

---

## 8. Monitoring & Diagnostics

### TensorBoard Tags (new)

| Tag | Description | Expected Range |
|---|---|---|
| `{role}/mappo/value_mix_alpha` | Learned mixing coefficient per role | [0, 1]; starts at 0.5 |

### What to Watch

| Signal | Healthy | Action if Unhealthy |
|---|---|---|
| `value_mix_alpha` | Stays between 0.2–0.8 | If saturates to 0 or 1, clamp `_value_mix_logit` range |
| `vf_explained_var` (per role) | Increases over training | If drops after MAPPO, check global state is non-zero in TensorBoard |
| Global state all-zeros | Only before C++ build is deployed | After UE5 rebuild, verify non-zero via env_wrapper debug logs |
| Per-role reward divergence | Expected (roles have different rewards) | If one role collapses, check shared critic isn't dominating (α→0) |

---

## 9. Backward Compatibility

| Concern | Status |
|---|---|
| Existing actor weights (226-dim input) | **Unchanged** — actor slices `obs[:226]`, same architecture |
| ONNX export for UE5 NNE inference | **Unchanged** — exports 226→7, global state not included |
| Existing RLlib checkpoints | Actor weights restore cleanly; centralized critic + mixing param initialize fresh |
| Pre-MAPPO UE5 builds (no `global_state` in info) | Falls back to `zeros(71)` — training runs but centralized critic learns nothing until C++ is rebuilt |

---

## 10. Remaining Work

| Item | Priority | Description |
|---|---|---|
| UE5 build verification | **High** | Compile the C++ changes (`DETeamWorldState.h`, `DETrainer.h/.cpp`) in the UE5 editor to verify no build errors |
| End-to-end integration test | **High** | Run one training iteration with UE5 connected to verify `global_state` flows through info channel correctly |
| Fog of War integration | Medium | `BuildTeamWorldState()` currently reads enemy positions directly (confidence=1.0). Integrate with `DEFogOfWarManager` for realistic confidence decay on unseen enemies |
| Per-role critic fallback | Low | If shared critic causes gradient interference (visible as `vf_explained_var` dropping for one role), implement 3 separate `CentralizedCritic` instances instead of sharing |
| α clamp guard | Low | If `value_mix_alpha` saturates, add logit clamping (e.g., `[-3, 3]`) to prevent dead critic branch |
