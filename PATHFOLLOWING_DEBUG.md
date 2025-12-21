# PathFollowing Debug - Second Round Diagnostics

**Date:** 2025-12-21
**Status:** Investigating why MoveTo fails despite valid path

---

## Current Mystery

**From logs:**
```
PathExists=✅ (NavMesh path is valid)
PathFollowing=✅ (component exists)
MoveToLocation result: ❌ Failed (PathExists=Yes) ← WHY?!
After 0.1s: moved 0.0 units
```

**This is contradictory!** Path exists, all components valid, but movement fails.

---

## New Diagnostics Added

### 1. PathFollowingComponent Status Check (STTask_ExecuteObjective.cpp:224-244)

```cpp
[PATHFOLLOW DIAG] Status=Idle/Moving/Paused/Waiting, HasValidPath=0/1, CurrentRequestID=X
```

**What to look for:**

#### ❌ **FAILURE:** Status=Moving
- **Meaning:** PathFollowingComponent is already executing a move request
- **Why it fails:** Can't accept new requests while moving
- **Fix:** Must call `StopMovement()` before new `MoveTo()`
- **Root cause:** StateTree calling ExecuteMovement every tick, spamming move requests

#### ❌ **FAILURE:** Status=Paused
- **Meaning:** PathFollowingComponent is paused
- **Why it fails:** Paused components reject new requests
- **Fix:** Call `PathFollowingComp->ResumeMove()` before MoveTo

#### ✅ **EXPECTED:** Status=Idle
- **Meaning:** Ready to accept new move requests
- **If still failing:** Problem is elsewhere (see below)

---

### 2. Detailed MoveTo Result (STTask_ExecuteObjective.cpp:271-298)

```cpp
[MOVE DIAG] MoveTo result: Code=Failed, RequestID=0, Path=INVALID
[MOVE BUG] ❌❌ CRITICAL: FindPathSync succeeded but MoveTo failed!
[MOVE BUG] PathFollowingComponent details: Status=X, HasPath=0
[MOVE BUG] NavData: RecastNavMesh-Default, CanBeMainNavData=1
```

**What to look for:**

#### ❌ **FAILURE:** Path=INVALID
- **Meaning:** MoveTo's internal pathfinding failed (even though our FindPathSync succeeded)
- **Why:** Different pathfinding query parameters or NavAgent configuration mismatch
- **Fix:** Check NavAgent properties on FollowerCharacter match NavMesh settings

#### ❌ **FAILURE:** RequestID=0
- **Meaning:** PathFollowingComponent rejected the request immediately
- **Why:** Usually means PathFollowingComponent isn't properly initialized or is locked
- **Fix:** Check PathFollowingComponent creation in FollowerAgentTrainer

#### ❌ **FAILURE:** CanBeMainNavData=0
- **Meaning:** NavMesh doesn't support this agent type
- **Why:** NavAgent radius/height mismatch with NavMesh settings
- **Fix:** Adjust Project Settings → Navigation Mesh → Supported Agents

---

### 3. AIController Possession Check (STTask_ExecuteObjective.cpp:166-173)

```cpp
[MOVE BUG] ❌ AIController 'FollowerAgentTrainer_3' does NOT control pawn 'BP_FollowerAgent_C_6'!
```

**What to look for:**

#### ❌ **FAILURE:** AIController doesn't control pawn
- **Meaning:** Possession relationship is broken
- **Why:** FollowerAgentTrainer possessed but then unpossessed, or pawn changed
- **Fix:** Verify FollowerAgentTrainer.cpp:50 Possess() call succeeds and persists

---

## Expected Log Flow (Success Case)

```
[FollowerTrainer] After Possess('BP_FollowerAgent_C_6'): PathFollowingComponent=✅ Valid
[FollowerChar] BeginPlay: 'BP_FollowerAgent_C_6' - AIController=✅, PathFollowing=✅

... (StateTree execution starts) ...

[MOVE DIAG] ✅ Path exists: 800.0 units, 2 points
[MOVE DIAG] 'BP_FollowerAgent_C_6': PathFollowing=✅, PathExists=✅
[PATHFOLLOW DIAG] Status=Idle, HasValidPath=0, CurrentRequestID=0  ← Ready for new request
[MOVE DIAG] MoveTo result: Code=✅ RequestSuccessful, RequestID=1, Path=Valid
[MOVE DIAG] After 0.1s: 'BP_FollowerAgent_C_6' moved 52.3 units  ← SUCCESS!
```

---

## Expected Log Flow (Failure Cases)

### Case A: Already Moving (StateTree tick spam)
```
[PATHFOLLOW DIAG] Status=Moving - BLOCKING NEW REQUESTS!, CurrentRequestID=1  ← PROBLEM!
[PATHFOLLOW FIX] Stopping existing movement before new request
[MOVE DIAG] MoveTo result: Code=✅ RequestSuccessful, RequestID=2, Path=Valid
[MOVE DIAG] After 0.1s: moved 48.2 units  ← Fixed by StopMovement()
```

**If this is the issue:** StateTree is calling ExecuteMovement every tick. Need to check if movement is already in progress before issuing new MoveTo.

---

### Case B: Path Invalid (NavAgent mismatch)
```
[MOVE DIAG] ✅ Path exists: 800.0 units, 2 points  ← Our FindPathSync works
[PATHFOLLOW DIAG] Status=Idle, HasValidPath=0, CurrentRequestID=0
[MOVE DIAG] MoveTo result: Code=❌ Failed, RequestID=0, Path=INVALID  ← MoveTo's pathfinding fails
[MOVE BUG] ❌❌ CRITICAL: FindPathSync succeeded but MoveTo failed!
[MOVE BUG] NavData: RecastNavMesh-Default, CanBeMainNavData=0  ← NavAgent not supported!
```

**If this is the issue:** NavAgent configuration on FollowerCharacter doesn't match NavMesh supported agents.

**Fix:**
1. Open Project Settings → Navigation Mesh → Agents
2. Check "Default" agent radius/height
3. Open BP_FollowerAgent → CapsuleComponent → verify radius/height match
4. Or adjust NavAgent properties in FollowerCharacter.cpp constructor

---

### Case C: PathFollowingComponent Not Initialized
```
[FollowerTrainer] After Possess('BP_FollowerAgent_C_6'): PathFollowingComponent=❌ NULL
```

**If this is the issue:** AAbstractTrainer (Schola plugin) is breaking PathFollowingComponent creation.

**Fix:** Override constructor in FollowerAgentTrainer:
```cpp
AFollowerAgentTrainer::AFollowerAgentTrainer()
{
    // Force PathFollowingComponent creation if Schola breaks it
    if (!GetPathFollowingComponent())
    {
        CreatePathFollowingComponent();
    }
}
```

---

## Next Steps

1. **Compile** with new diagnostics
2. **Start PIE** and trigger movement
3. **Check logs** for the patterns above
4. **Identify failure case** (A, B, or C)
5. **Apply corresponding fix**
6. **Report back** with logs - I'll refine the fix

---

## Most Likely Issue (Prediction)

**Case A: StateTree tick spam** (90% confidence)

**Reasoning:**
- PathFollowingComponent shows as ✅ Valid (not Case C)
- Path exists (probably not Case B)
- Logs show multiple agents all failing simultaneously (suggests systemic issue, not configuration)
- StateTree calls ExecuteMovement on every tick by default
- PathFollowingComponent rejects new requests while Status=Moving

**If confirmed:** Add movement state tracking to prevent redundant MoveTo calls:
```cpp
// Only call MoveTo if not already moving to this destination
if (PathComp->GetStatus() != EPathFollowingStatus::Moving ||
    FVector::Dist(SharedContext.MovementDestination, NavTargetPos) > 100.0f)
{
    MoveTo(NavTargetPos);
}
```

---

## Files Modified (This Round)

- `STTask_ExecuteObjective.cpp` (Lines 11, 166-173, 224-298)
  - Added AITypes.h include
  - Added NavMeshPath.h include
  - Added possession verification
  - Added PathFollowingComponent status check
  - Added detailed MoveTo result logging
  - Added NavData support check
  - Auto-stops movement if Status=Moving before new request

---

## Rollback If Needed

If diagnostics cause compilation errors, remove lines:
- 166-173 (possession check)
- 224-244 (PathFollowing status)
- 264-298 (detailed MoveTo)

Keep original simple logging:
```cpp
EPathFollowingRequestResult::Type MoveResult = InstanceData.AIController->MoveToLocation(TargetPos, AcceptanceRadius);
UE_LOG(LogTemp, Warning, TEXT("[MOVE] Result: %s"), MoveResult == ... ? ... : ...);
```
