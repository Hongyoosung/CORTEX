# Entity-Centric Architecture Refactor Plan
**Project:** CORTEX / MOC v10.2
**Goal:** Make the AI system environment-agnostic — variable numbers of allies, enemies, and bases, with any base layout
**Date:** 2026-03-10

---

## 1. Problem Statement

The current observation design hard-codes three assumptions into both the C++ struct and the trained ONNX model:

| Assumption | Hard-coded value | Impact if violated |
|---|---|---|
| Ally count | 4 slots | Extra allies silently truncated; fewer allies → zero-pad (distribution shift) |
| Enemy count | 5 slots | Same |
| Capture point count | 5 slots | Same |
| Capture point positions | Not encoded | Model has no spatial awareness of base layout |

Changing any of these during inference causes either silent data loss or out-of-distribution inputs that degrade policy quality. Strategic cooperation (spreading across bases, guarding specific points) is also impossible without base position information.

---

## 2. Target Architecture

### 2.1 Overview

Replace the flat 54-dim observation vector with a **set of typed entity tokens**. Use a **permutation-invariant attention encoder** in the policy network so that entity count and order do not affect the output. Add base-position awareness and cooperative reward shaping to drive strategic base occupation.

```
Observation (variable-length sets)
  ├── Self token        (7-dim, fixed)
  ├── Ally tokens       (5-dim each, N_allies)
  ├── Enemy tokens      (5-dim each, N_enemies)
  └── Base tokens       (7-dim each, N_bases)
          │
          ▼
Attention Encoder (per entity type)
  ├── Ally attention  → 64-dim ally context
  ├── Enemy attention → 64-dim enemy context
  └── Base attention  → 64-dim base context
          │
     Concat + MLP
          │
          ▼
Policy Output: 7-dim EQS weights
```

### 2.2 Entity Token Formats

**Self Token (7-dim)**
```
[pos_x/7500, pos_y/7500, pos_z/1000, health, vel_x/600, vel_y/600, vel_z/600]
```

**Ally Token (5-dim)**
```
[rel_pos_x/8000, rel_pos_y/8000, rel_pos_z/8000, health, alive]
```

**Enemy Token (5-dim)**
```
[rel_pos_x/8000, rel_pos_y/8000, rel_pos_z/8000, visible, confidence]
```

**Base Token (7-dim)**
```
[rel_pos_x/15000, rel_pos_y/15000, rel_pos_z/1000, ownership, capture_progress, is_assigned_target, strategic_value]
```

- `ownership`: +1.0 = friendly, 0.0 = neutral, −1.0 = enemy
- `capture_progress`: [0.0–1.0] current cap progress (direction implied by ownership)
- `is_assigned_target`: 1.0 if this base is the agent's current assignment from Squad Commander
- `strategic_value`: normalized score (e.g. distance to center, connectivity) — can be static per map

### 2.3 Attention Encoder (Python)

```python
class EntityCentricPolicy(nn.Module):
    def __init__(self, hidden=64, heads=4):
        self.self_enc   = nn.Linear(7, hidden)
        self.ally_enc   = nn.Linear(5, hidden)
        self.enemy_enc  = nn.Linear(5, hidden)
        self.base_enc   = nn.Linear(7, hidden)

        self.ally_attn  = nn.MultiheadAttention(hidden, heads, batch_first=True)
        self.enemy_attn = nn.MultiheadAttention(hidden, heads, batch_first=True)
        self.base_attn  = nn.MultiheadAttention(hidden, heads, batch_first=True)

        self.policy_head = nn.Sequential(
            nn.Linear(hidden * 4, 256), nn.ReLU(),
            nn.Linear(256, 128),        nn.ReLU(),
            nn.Linear(128, 7)           # 7 EQS weights
        )

    def forward(self, self_obs, allies, enemies, bases,
                ally_mask=None, enemy_mask=None, base_mask=None):
        s = self.self_enc(self_obs)                      # (B, hidden)
        q = s.unsqueeze(1)                               # (B, 1, hidden)

        a_enc = self.ally_enc(allies)                    # (B, N_a, hidden)
        e_enc = self.enemy_enc(enemies)                  # (B, N_e, hidden)
        b_enc = self.base_enc(bases)                     # (B, N_b, hidden)

        a_ctx, _ = self.ally_attn(q, a_enc, a_enc, key_padding_mask=ally_mask)
        e_ctx, _ = self.enemy_attn(q, e_enc, e_enc, key_padding_mask=enemy_mask)
        b_ctx, _ = self.base_attn(q, b_enc, b_enc, key_padding_mask=base_mask)

        combined = torch.cat([s,
                              a_ctx.squeeze(1),
                              e_ctx.squeeze(1),
                              b_ctx.squeeze(1)], dim=-1)  # (B, hidden*4)
        return self.policy_head(combined)
```

Padding masks set `True` for absent entity slots, so attention is suppressed over padding. This allows variable-length batches to be collated with standard padding.

### 2.4 EQS Weights (7-dim, up from 6)

| Index | Name | Description |
|---|---|---|
| 0 | `EnemyObjectiveProximity` | Approach enemy base |
| 1 | `AllyObjectiveProximity` | Stay near friendly base |
| 2 | `CoverDensity` | Seek cover |
| 3 | `EnemyVisibility` | Maintain LoS to enemies |
| 4 | `AllyProximity` | Stay near allies |
| 5 | `CombatRange` | Preferred engagement distance |
| **6** | **`AssignedBaseProximity`** | **Pull toward assigned base — NEW** |

---

## 3. Reward Shaping for Cooperative Base Occupation

Added to `UDERewardCalculator` on top of existing per-strategy rewards:

| Reward Term | Value | Condition |
|---|---|---|
| `BaseOccupationReward` | +2.0/step | Agent is the *only* ally within 2000cm of an uncontrolled base |
| `CoOccupationPenalty` | −0.5/step | 2+ allies stacking on the same base |
| `BaseCaptureCreditReward` | +5.0 (sparse) | Agent that flipped the base ownership |
| `UndefendedBasePenalty` | −1.0/step | Each friendly base with no ally within 2000cm (shared across team) |
| `AssignedBaseReachReward` | +1.0 (sparse) | First time reaching the assigned base in an episode |

These terms create pressure to spread, not stack.

---

## 4. Squad Commander Changes

The Squad Commander (`ASquadManager`) must assign each agent a **target base** in addition to a role. This assignment is:

1. Computed during MCTS tactical play selection
2. Written to each agent's Blackboard as `AssignedBaseLocation` (FVector key)
3. Read by `UEnvQueryContext_DEAssignedBase` for EQS weight index 6
4. Re-assigned on base flip events (same critical-event trigger as role changes)

Base assignment logic (initial): distribute agents across uncontrolled/threatened bases, prioritizing:
- Nearest unoccupied neutral/enemy base for Assault agents
- Nearest friendly base without a Defend agent for Defend agents
- Nearest injured ally's base for Support agents

---

## 5. Implementation Phases

### Phase 0 — Preparation (no functional change)
- [x] Add `FVector` positions of capture points to `FDEObservation` (currently only ownership status is stored)
- [x] Add `CaptureProgress` float per point to `FDEObservation`
- [ ] Add `AssignedBaseIndex` int32 to Blackboard (`BB_DEAgent`) — **manual editor step**: open `BB_DEAgent` and add an Int key named `AssignedBaseIndex` with default -1
- [x] Verify `UEnvQueryContext_DECapturePoints` returns positions (not just statuses) — confirmed, already returns `CP->GetActorLocation()`

### Phase 1 — New C++ Observation Types
**File:** `Public/Types/DEObservationTypes.h`

- [x] Define `FDEEntityToken` struct (variable-dim via TArray<float>)
- [x] Define `FDEObservationV2` with `TArray<FDEEntityToken>` for allies, enemies, bases
- [x] Implement `FDEObservationV2::ToDict()` → returns named TMap for Schola gRPC serialization
- [x] Implement `FDEObservationV2::ToFlatArray()` → 167-dim padded flat (NNE compatible)
- [x] Keep `FDEObservation` (v1) intact — do not delete until training is validated

**File:** `Public/Types/DEEQSTypes.h`

- [x] Add `AssignedBaseProximity` as 7th field to `FDEEQSWeightParameters`
- [x] Update clamp/scale logic for 7 weights

### Phase 2 — Observer Update
**File:** `Private/Schola/Observers/DETacticalObserver.cpp`

- [x] Replace `FDEObservation` population with `FDEObservationV2`
- [x] Populate `Allies` tokens from `AMatchManager` ally list (variable N, capped at DE_MAX_ALLIES=8)
- [x] Populate `Enemies` tokens from inline LOS check (variable N, capped at DE_MAX_ENEMIES=8)
- [x] Populate `Bases` tokens from `AMatchManager` capture point list (variable N)
  - Set `is_assigned_target = 1.0` for the index matching `AssignedBaseIndex` on the character
- [x] Update `CollectObservations()` to serialize via `ToFlatArray()` (167-dim padded flat)
- Note: `GatherBaseObservation()` retained for reward subsystem (v1 untouched)

### Phase 3 — EQS Context and Query Update
**File:** `Public/AI/EQS/DEEQSContext.h` + `Private/AI/EQS/DEEQSContext.cpp`

- [x] Add `UEnvQueryContext_DEAssignedBase` — reads `AssignedBaseIndex` from Blackboard, looks up position in MatchManager
- [ ] Update existing EQS query template (`EQS_DEAgent`) to include the 7th test using `UEnvQueryContext_DEAssignedBase` — **manual editor step**

**File:** `Public/AI/EQS/DEEQSExecutor.h`

- [x] `ExecuteTacticalQuery()` already accepts `FDEEQSWeightParameters` by struct — 7th field added, no signature change needed

### Phase 4 — Squad Commander Base Assignment
**File:** `Private/Team/DEMatchManager.cpp` (or `SquadManager` if separate)

- [x] Add `AssignBasesToAgents()` method — distributes bases to agents by role and proximity (Defend=unique, Assault=nearest enemy/neutral, Support=nearest any)
- [x] Call on base flip event (`OnPointCaptured`) for both affected teams
- [x] Call after initial `SpawnTeams()` on episode start
- [x] Write `AssignedBaseIndex` to each agent's Blackboard via `UBlackboardComponent::SetValueAsInt`
- Note: also mirrored to `ADECharacter::AssignedBaseIndex` for immediate EQS context access

### Phase 5 — Reward Update
**File:** `Public/Data/DERewardData.h`

- [x] Add cooperative base occupation reward parameters: `BaseOccupationReward`, `CoOccupationPenalty`, `BaseCaptureCreditReward`, `UndefendedBasePenalty`, `AssignedBaseReachReward`, `BaseOccupationRadius`

**File:** `Private/Core/Subsystems/DERewardSubsystem.cpp`

- [x] Implement `ComputeBaseCooperationReward()` — queries ally positions vs. base positions per capture point
- [x] Integrate into `ComputeStepReward()` pipeline before time penalty and clamping

### Phase 6 — Python Policy Rewrite
**File:** `DE_Training/` (new training directory)

- [x] Replace flat `LinearPolicy` with `EntityCentricPolicy` (see Section 2.3) — `DE_Training/policy.py`
- [x] Update environment wrapper to handle dict observations from Schola — `DE_Training/env_wrapper.py`
- [x] Implement `collate_fn` with padding and mask generation for variable-length sets — `env_wrapper.py::collate_fn`
- [x] Update ONNX export: export with dynamic axes for ally/enemy/base sequence dims — `policy.py::export_policy_onnx(fixed_shape=False)`
  - Also exports fixed-shape variant (`fixed_shape=True`) for UE5 NNE compatibility
- [x] Update reward config for new cooperative terms — `train.py::REWARD_CONFIG` + `env_wrapper.py::process_reward()`

### Phase 7 — Inference Integration
**File:** `Public/Schola/Agents/DEScholaAgent.h` + `.cpp`

- [x] Update `DETacticalParameterActuator` to read/write 7 weights instead of 6 (`GetActionSpace`, `TakeAction`, `ActionToEQSWeights`)
- [ ] Verify ONNX model dynamic axes work with UE5 NNE runtime
  - NNE may require fixed shapes — if so, pad to `MAX_ALLIES`, `MAX_ENEMIES`, `MAX_BASES` with mask input

### Phase 8 — Testing
- [x] Unit test: `FDEObservationV2::ToDict()` produces correct token counts for 3v3, 5v5, 3v8
      → `Private/Tests/DEEntityCentricTests.cpp` — `CORTEX.EntityCentric.ObsV2.ToDict.TokenCounts`
- [x] Unit test: `AssignBasesToAgents()` covers all agents, no two Defend agents on same base
      → `Private/Tests/DEEntityCentricTests.cpp` — `CORTEX.EntityCentric.AssignBases.PureLogic`
      (mirrors greedy algorithm; 3 scenarios: StandardComp/3-base, TurtleFormation/3-base, AllOutRush/5-base)
- [x] Unit test: `FDEObservationV2::ToFlatArray()` dimension == 167 for all entity-count combinations
      → `Private/Tests/DEEntityCentricTests.cpp` — `CORTEX.EntityCentric.ObsV2.ToFlatArray.Dimension`
- [x] Unit test: reward data cooperative defaults match plan spec (Section 3)
      → `Private/Tests/DEEntityCentricTests.cpp` — `CORTEX.EntityCentric.RewardData.CoopDefaults`
- [ ] Integration test: EQS query #6 (`AssignedBase`) fires with non-zero weight
      **Manual / PIE**: place agent in level, trigger `AssignBasesToAgents(0)`, verify BB key
      `AssignedBaseIndex` ≥ 0, then run EQS with weight[6] > 0 and confirm score > 0 in EQS debugger.
- [ ] Integration test: agents spread across 3 bases (no stacking) within 60 seconds
      **Manual / PIE**: run 5v5 match for 60 s, check that no base has >1 Defend agent in
      `ADEMatchManager::AssignBasesToAgents` log output (`[DEMatchManager] AssignBasesToAgents`).
- [ ] Training smoke test: 10k steps without NaN reward or observation shape errors
      **Python**: run `DE_Training/train.py` for 10 000 steps; confirm no NaN in reward log and
      obs shape is (167,) at every step.

---

## 6. File Change Summary

| File | Change Type | Notes |
|---|---|---|
| `Public/Types/DEObservationTypes.h` | Extend | Add `FDEEntityToken`, `FDEObservationV2`; keep v1 |
| `Public/Types/DEEQSTypes.h` | Modify | Add 7th EQS weight field |
| `Private/Schola/Observers/DETacticalObserver.cpp` | Rewrite | Populate v2 observation |
| `Public/AI/EQS/DEEQSContext.h/.cpp` | Extend | Add `UEnvQueryContext_DEAssignedBase` |
| `Public/AI/EQS/DEEQSExecutor.h` | Modify | Accept 7-weight struct |
| `Public/Data/DERewardData.h` | Extend | Add cooperative reward params |
| `Private/Core/Subsystems/DERewardSubsystem.cpp` | Extend | Add `ComputeBaseCooperationReward()` |
| `Private/Team/DEMatchManager.cpp` | Extend | Add `AssignBasesToAgents()` |
| `Public/Team/DEMatchManager.h` | Extend | Declare new method |
| `DE_Training/policy.py` | Rewrite | Attention-based policy |
| `DE_Training/env_wrapper.py` | Modify | Dict observation handling |
| `DE_Training/train.py` | Modify | New reward terms, dynamic collation |

---

## 7. Risks and Mitigations

| Risk | Mitigation |
| --- | --- |
| UE5 NNE has limited or undocumented support for dynamic ONNX axes (sequence dims) | Treat NNE as fixed-shape for now: pad to `MAX_ALLIES=8`, `MAX_ENEMIES=8`, `MAX_BASES=8` and feed boolean masks. Keep PyTorch/export pipeline using dynamic axes so non-UE inference can still exploit true variable lengths. Revisit this once Epic officially documents dynamic-shape support in NNE. |
| Schola gRPC protocol change (flat → dict) | Keep v1 flat observer as fallback; switch via config flag |
| Reward shaping causes degenerate behavior (e.g., agents chasing assignment over winning) | Start `AssignedBaseProximity` weight at 0.0, anneal up during training; cap `AssignedBaseReachReward` at one per episode |
| Base assignment conflicts (two Defend agents same base) | `AssignBasesToAgents()` uses greedy assignment with uniqueness constraint |
