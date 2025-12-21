# v4.0 Macro Actions - Clean Implementation (No Fallbacks)

**Status:** Phase 4C Complete - Ready for Integration Testing ✅
**Date:** 2025-12-20 (Latest Update)
**Policy:** NO FALLBACK FUNCTIONALITY - EQS is required for v4.0 to function

---

## Phase 3: EQS Integration & Testing ✅ COMPLETE

### Implementation Status

All Phase 3 tasks have been completed with production-quality EQS integration and zero fallback code.

#### A. EQS Query Implementation ✅ COMPLETE
**File:** `Source/GameAI_Project/Private/StateTree/Tasks/STTask_ExecuteObjective.cpp`

**Implementation:** (`STTask_ExecuteObjective.cpp:514-595`)
- ✅ `RunEQSQuery()` implemented with UEnvQueryManager integration
- ✅ Loads EQS assets from `/Game/AI/EQS/{QueryName}.uasset`
- ✅ Executes synchronous instant queries via `UEnvQueryManager::RunInstantQuery()`
- ✅ Validates query results and extracts positions
- ✅ Returns empty array on failure with detailed error logging
- ✅ **NO FALLBACK CODE** - Errors handled by stopping movement and logging

**Helper Methods:**
1. **`QueryEQSPositions()`** - Maps tactical positions to EQS query names (line 465-512)
   - `Hold`: Returns current position (no EQS needed)
   - `ForwardCover`: Calls `RunEQSQuery("EQS_ForwardCover")`
   - `Retreat`: Calls `RunEQSQuery("EQS_RetreatCover")`
   - `FlankLeft`: Calls `RunEQSQuery("EQS_FlankLeft")`
   - `FlankRight`: Calls `RunEQSQuery("EQS_FlankRight")`
   - `Advance`: Calls `RunEQSQuery("EQS_Advance")`

2. **`RunEQSQuery()`** - Full EQS integration (line 514-595)
   - Loads query asset by name
   - Validates World, QueryManager, and results
   - Returns sorted positions (best score first)
   - Logs detailed errors on failure (missing asset, query failure, no results)

3. **`GetEnemyByIndex()`** - Enemy lookup from VisibleEnemies array (line 597+)
   - Validates index bounds and actor validity


---

### Phase 4: RLlib Training Configuration (Optional)

**File:** `CORTEX_Training/train_rllib.py`

No critical changes needed - RLlib automatically handles MultiDiscrete spaces. However, you may want to adjust:

1. **Entropy Coefficient:** Discrete actions may need different exploration settings
   ```python
   "entropy_coeff": 0.05,  # Lower than continuous (was 0.1)
   ```

2. **Network Architecture:** Consider separate embedding layers per action head
   ```python
   "model": {
       "fcnet_hiddens": [128, 128, 64],
       "fcnet_activation": "relu",
       "use_attention": False,  # Could help with variable enemy count
   }
   ```

---


## Phase 4: EQS Asset Creation & Testing

### ✅ COMPLETE: EQS Query Assets Created
- ✅ `Content/Game/Blueprints/AI/EQS/EQS_ForwardCover.uasset` - Cover toward objective
- ✅ `Content/Game/Blueprints/AI/EQS/EQS_RetreatCover.uasset` - Cover away from enemies
- ✅ `Content/Game/Blueprints/AI/EQS/EQS_Advance.uasset` - Forward positions (no cover)

**Note:** FlankLeft/FlankRight removed from action space (6→4 positions) for faster learning convergence.

---

## 🚨 CRITICAL: Next Steps (Required for Training)

### ✅ COMPLETED: Phase 4A, 4B & 4C
- ✅ **EQS Assets Created:** ForwardCover, Retreat, Advance (Content/Game/Blueprints/AI/EQS/)
- ✅ **Code Refactored:** UPROPERTY-based query system implemented
- ✅ **C++ Contexts Exist:** `UEnvQueryContext_ObjectiveLocation` and `UEnvQueryContext_VisibleEnemies`
- ✅ **C++ Project Compiled:** UPROPERTY changes registered in editor
- ✅ **EQS Contexts Configured:** All 3 queries use C++ contexts
- ✅ **StateTree Configured:** Queries assigned to `STTask_ExecuteObjective`
- ✅ **EQS Queries Tested:** Individual query testing completed

---

## 🎯 NEXT PHASE: Integration Testing & Training

### Phase 4D: Integration Testing (CURRENT - 1-2 hours)

**Goal:** Verify the complete pipeline works with follower agents in gameplay

#### **Test 1: Agent Movement with EQS (HIGH PRIORITY)**
1. **Setup:**
   - Open training level (e.g., `Training_BasicCombat_2v2_v01.umap`)
   - Place 2+ follower agents with `BP_TestCharacter`
   - Place 2+ enemies with `BP_TestEnemy`
   - Verify NavMesh coverage (press `P` in viewport - should see green overlay)

2. **PIE Test (Play In Editor):**
   - Start PIE (Alt+P)
   - Open console (`` ` `` backtick key)
   - Type: `eqs debug [AgentName]` (e.g., `eqs debug BP_TestCharacter_0`)
   - Watch for green/red spheres when agent executes tactical positions

3. **Expected Success Indicators:**
   - ✅ Green spheres appear at cover positions when agent moves
   - ✅ Agent navigates to positions via NavMesh (no clipping through walls)
   - ✅ Different tactical positions trigger different EQS queries
   - ✅ Output Log shows: `[EQS v4.0] ✅ Query 'EQS_ForwardCover' returned N positions`

4. **Expected Failure Indicators (if issues exist):**
   - ❌ Red spheres or no spheres
   - ❌ Agent stuck or not moving
   - ❌ Output Log shows: `[EQS v4.0] ❌ Failed to load EQS asset` or `Failed to get context`
   - ❌ Agent clips through walls (NavMesh not configured)

---

#### **Test 2: Verify Full Macro Action Pipeline**
1. **Trigger each tactical position manually (if possible):**
   - `Hold`: Agent should stay at current position
   - `ForwardCover`: Agent moves to cover toward objective
   - `Retreat`: Agent moves to cover away from enemies
   - `Advance`: Agent moves forward (no cover requirement)

2. **Verify other macro actions:**
   - **Target Selection:** Agent aims at enemies via `SetFocus`
   - **Fire Mode:** Agent fires weapon when `FireMode = Fire` or `Suppress`
   - **Stance:** Agent crouches when `Stance = Crouch`

3. **Check Output Log for:**
   - ✅ `[EQS v4.0] ✅ Query returned N positions (best score: X.X)`
   - ✅ `[StateTree] Executing macro action: Position=ForwardCover, Target=Enemy_0, Fire=Fire, Stance=Crouch`
   - ❌ Any `[EQS v4.0] ❌` error messages

---

#### **Test 3: Verify Level Configuration**
1. **NavMesh:**
   - Press `P` in viewport - green overlay should cover walkable areas
   - If no green overlay: Add **Nav Mesh Bounds Volume** covering playable area
   - Build > Build Paths to rebuild NavMesh

2. **Objective Markers:**
   - Verify actors with tag "Objective" exist in level (for C++ context fallback)
   - Check `FollowerStateTreeContext` has valid `CurrentObjective.TargetLocation`

3. **Enemy Detection:**
   - Verify enemies have tag "Enemy" (for C++ context fallback)
   - Check `VisibleEnemies` array is populated by perception system

---

---

### Phase 4E: Python RLlib Training Integration (NEXT - 30 mins)

**Prerequisites:** Phase 4D integration testing passed

**Steps:**
1. **Start UE5 in PIE** (with Schola plugin enabled)
2. **Run Python training script:**
   ```bash
   cd CORTEX_Training
   python train_rllib.py
   ```
3. **Monitor logs for:**
   - ✅ `[Schola] Connected to UE5 at localhost:50051`
   - ✅ `[RLlib] Action space: MultiDiscrete([4, 11, 3, 3])`
   - ✅ `[EQS v4.0] Query returned N positions`
   - ❌ Any EQS errors (missing contexts, no results)

4. **Watch agent behavior:**
   - Agents should move to cover positions (not random wandering)
   - Movement should be NavMesh-based (no clipping through walls)
   - Different tactical positions should trigger different EQS queries

5. **If successful:**
   - Let training run for 10-15 minutes
   - Monitor TensorBoard for reward trends
   - Verify policy updates are occurring

---

### Phase 4F: RLlib Hyperparameter Tuning (OPTIONAL)

Only adjust after basic execution works:

**Recommended Changes for Discrete Actions:**
```python
# train_rllib.py
config = PPOConfig()
config.training(
    entropy_coeff=0.05,  # Lower than continuous (was 0.1) - reduce random exploration
    lr=3e-4,            # Standard for discrete actions
)
config.resources(
    num_gpus=1 if torch.cuda.is_available() else 0,
)
```

**Network Architecture (optional):**
```python
config.model = {
    "fcnet_hiddens": [128, 128, 64],  # Match v3.1 architecture
    "fcnet_activation": "relu",
    "use_attention": False,           # Could help with variable enemy count
}
```

---

## Time Tracking

| Phase | Estimated | Actual | Status |
|-------|-----------|--------|--------|
| Phase 1 (Python + C++ Interface) | N/A | N/A | ✅ Complete |
| Phase 2 (C++ Execution Logic) | 8-10 hours | ~2 hours | ✅ Complete |
| Phase 3 (EQS Integration) | 1-2 hours | ~30 mins | ✅ Complete |
| Phase 3 (Legacy Removal) | 30 mins | ~15 mins | ✅ Complete |
| Phase 3 (Documentation) | 30 mins | ~15 mins | ✅ Complete |
| Phase 4A (EQS Assets Creation) | 2-3 hours | ~1 hour | ✅ Complete |
| Phase 4B (Code Refactor: UPROPERTY) | 30 mins | ~15 mins | ✅ Complete |
| Phase 4C (Editor Configuration) | 10-15 mins | ~10 mins | ✅ Complete |
| Phase 4D (Integration Testing) | 1-2 hours | - | 🎯 **CURRENT** |
| Phase 4E (RLlib Integration) | 30 mins | - | ⏳ Next |
| Phase 4F (Hyperparameter Tuning) | 30 mins | - | ⏳ Optional |
| **Total (Code)** | **10-13 hours** | **~3 hours** | **✅ 100% Complete** |
| **Total (Assets)** | **2-3 hours** | **~1 hour** | **✅ 100% Complete** |
| **Total (Configuration)** | **10-15 mins** | **~10 mins** | **✅ 100% Complete** |
| **Total (Testing)** | **1-2 hours** | **-** | **🎯 In Progress** |

---

## ✅ COMPLETE: No-Fallback Policy Enforced

### EQS Integration (IMPLEMENTED)
- **Current Status:** `RunEQSQuery()` fully implemented with UEnvQueryManager
- **Fallback Behavior:** ❌ **NONE** - Agents STOP and log errors if EQS fails
- **Next Step:** Create EQS assets in UE Editor (Content/AI/EQS/)
- **Impact:** System will log detailed errors without EQS assets, agents will hold position

### Enemy Tracking
- **Current Status:** Uses `SharedContext.VisibleEnemies` array
- **Assumption:** `FollowerAgentComponent` or perception system populates this array
- **Validation Needed:** Verify VisibleEnemies is updated each tick
- **Sorting:** Currently uses array order (may want to sort by distance/threat)

### Prone Stance
- **Current Status:** Logs warning, not implemented
- **Reason:** UE5 CharacterMovement doesn't support prone natively
- **Options:**
  1. Leave as Stand/Crouch only (3 → 2 stance options)
  2. Implement custom movement mode (significant effort)
  3. Use animation-only prone (visual only, no collision change)

### No Backwards Compatibility
- **v3.x Support:** ❌ **REMOVED** - All atomic action fields deleted
- **Migration Path:** v3.x code will NOT compile - must migrate to v4.0
- **Breaking Change:** This is a clean break from v3.x architecture

### Design Philosophy
- **Fail Fast:** If EQS is not implemented, system logs errors immediately
- **No Hidden Behavior:** No fallback code that silently masks missing functionality
- **Force Implementation:** Developer must implement EQS to make system work

**Completed:** ✅ Phase 4A (EQS Assets) + ✅ Phase 4B (UPROPERTY Refactor) + ✅ Phase 4C (Editor Config)
**Current:** 🎯 Phase 4D - Integration Testing (verify full pipeline with agents)
**Next:** Phase 4E - RLlib Training Integration
**Note:** All code, assets, and editor configuration complete! Now testing the complete system.

---

## Configuration Checklist (Pre-Training)

Before running Python training, verify:

### ✅ Code & Assets (COMPLETE)
- [x] C++ RunEQSQuery() implementation (`STTask_ExecuteObjective.cpp:504-587`)
- [x] Python action space updated to `MultiDiscrete([4, 11, 3, 3])`
- [x] EQS query assets created (ForwardCover, Retreat, Advance)
- [x] EQS Plugin enabled in UE5

### ✅ Contexts & Environment (Phase 4C - COMPLETE)
- [x] **C++ Contexts Exist:** `UEnvQueryContext_ObjectiveLocation` and `UEnvQueryContext_VisibleEnemies`
- [x] **Code Refactored:** UPROPERTY-based query system in `STTask_ExecuteObjective.h`
- [x] **C++ Project Compiled:** UPROPERTY changes registered in editor
- [x] **EQS query tests configured:** Using C++ contexts (not Blueprint)
- [x] **EQS queries assigned:** In StateTree `STTask_ExecuteObjective` properties
- [x] **Individual EQS queries tested:** Each query validated separately

### 🎯 Integration Testing (Phase 4D - CURRENT)
- [ ] **NavMesh configured** in training level (press 'P' to verify green overlay)
- [ ] **PIE test with agents:** Verify agents move using EQS queries
- [ ] **EQS debug visualization:** `eqs debug [AgentName]` shows green spheres
- [ ] **Output Log verification:** Check for `[EQS v4.0] ✅` success messages
- [ ] **Full macro action pipeline:** Movement, aiming, firing, stance all working

### ⏳ Training Pipeline (Phase 4E - NEXT)
- [ ] Python training script connects via Schola (localhost:50051)
- [ ] RLlib recognizes action space: MultiDiscrete([4, 11, 3, 3])
- [ ] Agents move to cover positions (not stuck or random wandering)
- [ ] No EQS errors in Output Log during training
