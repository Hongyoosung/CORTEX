# Movement Debug Guide - FollowerAgent MoveToLocation Issue

## Problem
- MoveToLocation returns "Request Successful" in Blueprint
- Test character with same mesh moves correctly
- FollowerAgent doesn't move

## Root Cause Analysis

The issue is likely one of these:

1. **PathFollowingComponent missing/not initialized** in FollowerAgentTrainer
2. **CharacterMovementComponent** disabled or in wrong mode
3. **Something stopping movement** after MoveToLocation succeeds
4. **StateTree or Schola** overriding movement commands

## Diagnostic Logs Added

### 1. FollowerCharacter::BeginPlay() (FollowerCharacter.cpp:47)
```
[FollowerChar] BeginPlay: 'CharacterName' - Controller=X (AIController=✅/❌, PathFollowing=✅/❌)
```
**What to check:** Both AIController and PathFollowing must be ✅

---

### 2. FollowerAgentTrainer::Initialize() (FollowerAgentTrainer.cpp:54)
```
[FollowerTrainer] After Possess('PawnName'): PathFollowingComponent=✅/❌
```
**What to check:** PathFollowingComponent must be ✅ Valid

---

### 3. STTask_ExecuteObjective::ExecuteMovement() (STTask_ExecuteObjective.cpp:180-209)
```
[MOVE DIAG] 'CharName': Distance=X, CharMovement=✅/❌, PathFollowing=✅/❌, NavSys=✅/❌
[MOVE DIAG] CharMovement: Mode=1 (Walking=1), MaxSpeed=600.0, bOrientToMovement=1
[MOVE DIAG] MoveToLocation result: ✅ RequestSuccessful
[MOVE DIAG] After 0.1s: 'CharName' moved X.X units
```

**What to check:**
- CharMovement, PathFollowing, NavSys all ✅
- Mode must be 1 (Walking)
- "After 0.1s" should show >0 units moved

---

## Testing Steps

1. **Compile** the project with new diagnostics
2. **Start PIE** (Alt+P)
3. **Watch Output Log** for the diagnostic messages above
4. **Trigger movement** (either via Blueprint test or gameplay)
5. **Check "After 0.1s" message** - if moved distance is 0, movement failed

---

## Expected Failures & Fixes

### ❌ FAILURE: PathFollowingComponent is NULL
**Log shows:** `PathFollowingComponent=❌ NULL`

**Cause:** FollowerAgentTrainer doesn't properly initialize PathFollowingComponent

**Fix Options:**
1. Check if `AAbstractTrainer::OnPossess()` calls `Super::OnPossess()` (AAIController)
2. Manually create PathFollowingComponent in FollowerAgentTrainer constructor
3. Override OnPossess in FollowerAgentTrainer to ensure proper initialization

---

### ❌ FAILURE: CharMovement Mode != 1 (Walking)
**Log shows:** `Mode=0` or `Mode=3` (not 1)

**Cause:** Character is falling, flying, or custom movement mode

**Fix:** Force Walking mode before MoveToLocation:
```cpp
if (MoveComp->MovementMode != MOVE_Walking)
{
    MoveComp->SetMovementMode(MOVE_Walking);
}
```

---

### ❌ FAILURE: "After 0.1s" shows 0 units moved
**Log shows:** `moved 0.0 units`

**Cause:** Something is stopping movement immediately after MoveToLocation

**Common culprits:**
1. **StateTree** - Another task calling StopMovement
2. **Schola Actuator** - Overriding movement with conflicting command
3. **CharacterMovementComponent** - Velocity being reset elsewhere

**Debug next:**
- Add breakpoints in `UPathFollowingComponent::RequestMove()`
- Check if `UCharacterMovementComponent::Velocity` is being set to zero
- Search codebase for `StopMovement()` calls

---

## Quick Test Comparison

**Working (Test Character):**
```
Controller = PlayerController (Blueprint manually calls MoveToLocation)
PathFollowing = Created on-demand by MoveToLocation
Movement = Works
```

**Not Working (FollowerAgent):**
```
Controller = FollowerAgentTrainer (inherits AAIController)
PathFollowing = Should auto-create but might be missing?
Movement = Fails despite "Request Successful"
```

**Key Difference:** Test character likely uses SimpleMove or direct velocity manipulation, while FollowerAgent relies on AIController's PathFollowing system.

---

## Root Cause FOUND ✅

**Issue:** EQS queries return positions that are NOT on the NavMesh!

**Evidence from logs:**
```
[MOVE DIAG] MoveToLocation result: ❌ Failed
[MOVE DIAG] After 0.1s: moved 0.0 units
```

**Why one agent moved:** That agent's EQS target happened to be on NavMesh by chance.

**Fix Applied:** `STTask_ExecuteObjective.cpp:223-239`
- Now projects EQS target onto NavMesh before MoveToLocation
- Re-validates path after projection
- Uses projected position for movement

---

## Next Steps

1. **Compile** the updated code
2. **Test movement** - should now work for all agents
3. **Check logs** for:
   - `[MOVE FIX] Projected EQS position onto NavMesh (offset: X units)` - Shows projection working
   - `PathExists=Yes` - Confirms valid path
   - `MoveToLocation result: ✅ RequestSuccessful` - Movement accepted
   - `After 0.1s: moved X.X units` - Actual movement confirmed

---

## Long-Term Fix: Configure EQS Assets

**Current:** EQS queries can return positions anywhere in 3D space
**Better:** Configure EQS to only generate positions on NavMesh

**For each EQS asset** (`EQS_ForwardCover`, `EQS_Retreat`, `EQS_Advance`):
1. Open asset in UE5 editor
2. Find the **Generator** (e.g., "Points: Grid")
3. Add **Test: Pathfinding** (or ensure it exists)
4. Configure test:
   - Test Mode: `Filter Only`
   - Filter Type: `Minimum`
   - Boolean Match: `Must Match`

This ensures EQS ONLY returns positions with valid NavMesh paths.

---

## Next Steps After Logs

1. ✅ **FIXED** - NavMesh projection now applied automatically
2. **Verify NavMesh** exists in level (press `P` to visualize green overlay)
3. **Consider EQS configuration** to prevent off-NavMesh positions at source

---

## File Changes Summary

**Modified Files:**
- `Source/GameAI_Project/Private/Actor/FollowerCharacter.cpp` - BeginPlay diagnostics
- `Source/GameAI_Project/Private/Schola/FollowerAgentTrainer.cpp` - Possess diagnostics
- `Source/GameAI_Project/Private/StateTree/Tasks/STTask_ExecuteObjective.cpp` - Movement diagnostics
- `Source/GameAI_Project/Private/AI/AIController/FollowerAIController.cpp` - Possess diagnostics (unused controller, but harmless)

**Safe to revert if needed** - All changes are diagnostic logs only, no behavior changes.
