# v4.0 Macro Actions - Next Steps

**Status:** Phase 1 (Python + C++ Interface) Complete ✅
**Date:** 2025-12-17

---

## What Was Done (Phase 1)

### 1. Architecture Documentation ✅
- Updated `CLAUDE.md` to v4.0 with macro action architecture
- Added comparison table (v3.1 atomic vs v4.0 macro)
- Updated architecture rules and key features

### 2. C++ Data Structures ✅
- Added new enums in `RLTypes.h`:
  - `ETacticalPosition` (6 options: Hold, ForwardCover, Retreat, FlankLeft, FlankRight, Advance)
  - `EFireMode` (3 options: HoldFire, Fire, Suppress)
  - `EStance` (3 options: Stand, Crouch, Prone)
- Added `FMacroAction` struct with macro action fields
- Modified `FTacticalAction` to include `MacroAction` field (backwards compatible)

### 3. Schola Actuator ✅
- Changed `TacticalActuator` from `UBoxActuator` → `UMultiDiscreteActuator`
- Updated `GetActionSpace()` to return `MultiDiscrete([6, MaxEnemies+1, 3, 3])`
- Updated `TakeAction()` to parse discrete indices and build `FMacroAction`
- Removed legacy smoothing/masking logic (no longer needed for macro actions)

### 4. Python Environment ✅
- Updated `sbdapm_env.py` action space:
  - Changed from `Box(7)` → `MultiDiscrete([6, 11, 3, 3])`
  - Updated docstrings and comments
  - Modified `step()` to log discrete action names
- Updated `SBDAPMScholaEnv` and `SBDAPMMultiAgentEnv` classes
- Changed action batching from `(num_envs, 7)` → `(num_envs, 4)` with `int32` dtype

---

## What Still Needs To Be Done

### Phase 2: C++ Execution Logic (CRITICAL)

The Python environment now sends `MultiDiscrete([6, 11, 3, 3])` actions, and `TacticalActuator` correctly parses them into `FMacroAction` structs. However, **`STTask_ExecuteObjective` still executes atomic actions** (velocity, aiming). You need to:

#### A. Update `STTask_ExecuteObjective.cpp` ✅ (Partial)
**File:** `Source/GameAI_Project/Private/StateTree/Tasks/STTask_ExecuteObjective.cpp`

**Current Problem:**
- `ExecuteMovement()` uses `AddMovementInput(velocity)` (line 198-267)
- `ExecuteAiming()` uses manual rotation calculations (line 269-299)
- Expects `FTacticalAction` with atomic fields (MoveDirection, LookDirection, etc.)

**Required Changes:**
1. **Replace `ExecuteMovement()` with EQS + NavMesh:**
   ```cpp
   void FSTTask_ExecuteObjective::ExecuteMovement(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const
   {
       const FMacroAction& Macro = Action.MacroAction;

       // Query EQS for tactical positions based on PositionChoice
       TArray<FVector> CandidatePositions = QueryEQSPositions(Context, Macro.PositionChoice);

       if (CandidatePositions.Num() > 0)
       {
           FVector TargetLocation = CandidatePositions[0]; // Best EQS result

           // Use AIController::MoveToLocation (NavMesh pathfinding)
           if (AAIController* AI = InstanceData.AIController)
           {
               AI->MoveToLocation(TargetLocation, AcceptanceRadius);
           }
       }
   }
   ```

2. **Replace `ExecuteAiming()` with SetFocus:**
   ```cpp
   void FSTTask_ExecuteObjective::ExecuteAiming(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const
   {
       const FMacroAction& Macro = Action.MacroAction;

       if (Macro.TargetIndex >= 0)
       {
           // Get enemy actor from observation system
           AActor* TargetEnemy = GetEnemyByIndex(Context, Macro.TargetIndex);

           if (TargetEnemy && InstanceData.AIController)
           {
               // Engine handles aiming automatically
               InstanceData.AIController->SetFocus(TargetEnemy);
           }
       }
       else
       {
           // No target - clear focus
           if (InstanceData.AIController)
           {
               InstanceData.AIController->ClearFocus(EAIFocusPriority::Gameplay);
           }
       }
   }
   ```

3. **Update `ExecuteFire()` to use FireMode enum:**
   ```cpp
   void FSTTask_ExecuteObjective::ExecuteFire(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const
   {
       const FMacroAction& Macro = Action.MacroAction;

       switch (Macro.FireMode)
       {
       case EFireMode::HoldFire:
           // Don't fire
           break;
       case EFireMode::Fire:
           // Fire at focused target (if within aim tolerance)
           if (InstanceData.AIController && InstanceData.AIController->GetFocusActor())
           {
               // Fire weapon
               if (UWeaponComponent* Weapon = FindWeaponComponent(Context))
               {
                   Weapon->StartFiring();
               }
           }
           break;
       case EFireMode::Suppress:
           // Fire near enemy cover location (even if not visible)
           // TODO: Implement suppressive fire logic
           break;
       }
   }
   ```

4. **Update `ExecuteCrouch()` to use Stance enum:**
   ```cpp
   void FSTTask_ExecuteObjective::ExecuteCrouch(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const
   {
       const FMacroAction& Macro = Action.MacroAction;

       if (UCharacterMovementComponent* Movement = FindCharacterMovement(Context))
       {
           switch (Macro.Stance)
           {
           case EStance::Stand:
               Movement->bWantsToCrouch = false;
               // TODO: Exit prone if needed
               break;
           case EStance::Crouch:
               Movement->bWantsToCrouch = true;
               break;
           case EStance::Prone:
               // TODO: Implement prone stance (custom movement mode)
               break;
           }
       }
   }
   ```

#### B. Implement EQS Position Query Helper
**New Method:** Add to `STTask_ExecuteObjective.cpp`

```cpp
TArray<FVector> FSTTask_ExecuteObjective::QueryEQSPositions(FStateTreeExecutionContext& Context, ETacticalPosition PositionType) const
{
    TArray<FVector> Results;

    // Get agent's current location
    APawn* Pawn = InstanceData.ControlledPawn;
    if (!Pawn) return Results;

    FVector AgentLocation = Pawn->GetActorLocation();

    switch (PositionType)
    {
    case ETacticalPosition::Hold:
        // Stay at current location
        Results.Add(AgentLocation);
        break;

    case ETacticalPosition::ForwardCover:
        // Query EQS for cover points closer to objective
        Results = RunEQSQuery(Pawn, "EQS_ForwardCover");
        break;

    case ETacticalPosition::Retreat:
        // Query EQS for cover points away from enemies
        Results = RunEQSQuery(Pawn, "EQS_RetreatCover");
        break;

    case ETacticalPosition::FlankLeft:
        // Query EQS for left flank positions
        Results = RunEQSQuery(Pawn, "EQS_FlankLeft");
        break;

    case ETacticalPosition::FlankRight:
        // Query EQS for right flank positions
        Results = RunEQSQuery(Pawn, "EQS_FlankRight");
        break;

    case ETacticalPosition::Advance:
        // Move toward objective without cover requirement
        Results = RunEQSQuery(Pawn, "EQS_Advance");
        break;
    }

    return Results;
}

TArray<FVector> FSTTask_ExecuteObjective::RunEQSQuery(APawn* Pawn, FName QueryName) const
{
    TArray<FVector> Results;

    // TODO: Implement EQS query execution
    // Use UEnvQueryManager::RunInstantEQS or similar

    return Results;
}

AActor* FSTTask_ExecuteObjective::GetEnemyByIndex(FStateTreeExecutionContext& Context, int32 EnemyIndex) const
{
    // TODO: Query observation system for visible enemies
    // Return the Nth enemy actor from sorted list (e.g., by distance or threat)

    return nullptr;
}
```

---

### Phase 3: RLlib Training Configuration (Optional)

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

## StateTree vs Behavior Tree?

**Answer:** **Keep StateTree** ✅

| Aspect | StateTree | BehaviorTree |
|--------|-----------|--------------|
| **Performance** | 2-5x faster (data-driven) | Slower (node-based ticking) |
| **UE5.6 Integration** | Native, first-class | Legacy, still supported |
| **Hot Reload** | Fast (asset-based) | Slow (recompilation) |
| **Debugging** | Built-in visual debugger | Third-party tools needed |
| **Migration Cost** | N/A (already using it) | 2-3 weeks of rewrite |

**Verdict:** StateTree is the right choice. It's more modern, performant, and already integrated with your system. Switching to BehaviorTree would provide **zero benefit** while costing significant time.

---

## How to Test Phase 2 Implementation

### 1. Compile C++ Changes
```bash
# Build UE project
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX
"C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" GameAI_ProjectEditor Win64 Development
```

### 2. Test in UE Editor
1. Open `GameAI_Project.uproject`
2. Place follower pawns in level
3. Attach `ScholaAgentComponent` + `TacticalActuator`
4. Configure `MaxEnemies = 10` in TacticalActuator details panel
5. PIE (Play In Editor) - check logs for `[MACRO ACTION]` messages

### 3. Test with Python
```bash
cd CORTEX_Training
python train_rllib.py
```

**Expected Output:**
```
[MACRO ACTION] 'BP_Follower_1': Position=ForwardCover, Target=2, Fire=Fire, Stance=Crouch
Agent moves to cover via NavMesh (not velocity input)
Agent auto-aims at enemy index 2 via SetFocus
Agent fires weapon at target
```

---

## Priority Order

1. **CRITICAL:** Implement Phase 2 (EQS + NavMesh + SetFocus) - Without this, the system won't work
2. **Medium:** Test Phase 2 in UE Editor first (before Python training)
3. **Low:** Adjust RLlib hyperparameters (only after basic execution works)

---

## Estimated Effort

| Phase | Complexity | Time | Priority |
|-------|-----------|------|----------|
| Phase 2A (Movement) | Medium | 4-6 hours | **CRITICAL** |
| Phase 2B (Aiming) | Low | 1-2 hours | **CRITICAL** |
| Phase 2C (Fire/Stance) | Low | 1-2 hours | High |
| Phase 3 (RLlib config) | Low | 30 mins | Low |
| **Total** | - | **8-10 hours** | - |

---

## Questions?

- **EQS Queries:** Do you have existing EQS queries for cover/flank? If not, need to create them first.
- **Enemy Tracking:** Does `FollowerAgentComponent` expose a sorted enemy list? If not, need to add this.
- **Prone Stance:** UE5 CharacterMovement doesn't support prone natively - may need custom movement mode.

**Next:** Focus on Phase 2A (Movement with EQS + NavMesh). This is the highest-impact change.
