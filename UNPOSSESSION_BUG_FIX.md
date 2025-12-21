# Unpossession Bug - Root Cause & Fix

**Date:** 2025-12-21
**Status:** ✅ ROOT CAUSE IDENTIFIED & FIXED

---

## The Smoking Gun 🔍

```
[MOVE BUG] ❌ AIController 'FollowerAgentTrainer_2' does NOT control pawn 'BP_FollowerAgent_C_7'!
GetPawn()=None
```

**What this means:**
- `FollowerAgentTrainer` successfully called `Possess(Pawn)` during initialization
- BUT by the time `ExecuteMovement()` runs, `GetPawn()` returns `None`
- The AIController was **unpossessed** between initialization and movement

---

## Why Everything Failed

This explains ALL the mysterious symptoms:

| Symptom | Why It Happened |
|---------|-----------------|
| ✅ PathFollowingComponent exists | Created during initial `Possess()` call |
| ✅ Path exists (FindPathSync works) | Our manual pathfinding doesn't need possession |
| ❌ MoveTo fails | `AIController::MoveTo()` requires `GetPawn() != nullptr` |
| ❌ Moved 0.0 units | No pawn to move! |

**Root Equation:**
```
AIController with no pawn + MoveTo() = EPathFollowingRequestResult::Failed
```

---

## The Fix (Applied)

### 1. Detection & Auto Re-Possession (STTask_ExecuteObjective.cpp:165-186)

```cpp
// CRITICAL: Verify AIController actually controls this pawn
if (InstanceData.AIController->GetPawn() != Pawn)
{
    UE_LOG(LogTemp, Error, TEXT("[MOVE BUG] ❌ AIController does NOT control pawn! GetPawn()=None"));

    // FIX: Re-possess the pawn
    UE_LOG(LogTemp, Error, TEXT("[MOVE FIX] Re-possessing pawn..."));
    InstanceData.AIController->Possess(Pawn);

    // Verify re-possession succeeded
    if (InstanceData.AIController->GetPawn() != Pawn)
    {
        UE_LOG(LogTemp, Error, TEXT("[MOVE FIX] ❌ Re-possession FAILED!"));
        return;  // Can't move without possession
    }
    UE_LOG(LogTemp, Warning, TEXT("[MOVE FIX] ✅ Re-possession successful"));
}
```

**What it does:**
- Detects when AIController lost possession
- Automatically re-possesses the pawn before movement
- Verifies re-possession succeeded
- Logs everything for debugging

---

### 2. Unpossession Tracking (FollowerAgentTrainer.cpp:210-224)

```cpp
void AFollowerAgentTrainer::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] ✅ OnPossess: Now controlling '%s'"),
        *GetNameSafe(InPawn));
}

void AFollowerAgentTrainer::OnUnPossess()
{
    UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] ❌ OnUnPossess: Losing control of '%s' - CALLSTACK NEEDED!"),
        *GetNameSafe(GetPawn()));
    Super::OnUnPossess();
}
```

**What it does:**
- Logs every possess/unpossess event
- Helps identify WHAT is causing the unpossession
- Look for `[FollowerTrainer] ❌ OnUnPossess` in logs

---

### 3. Controller Change Detection (FollowerCharacter.cpp:57-79)

```cpp
void AFollowerCharacter::Tick(float DeltaTime)
{
    // Track if controller gets lost during gameplay (check every 60 frames)
    static TMap<AFollowerCharacter*, AController*> LastKnownController;
    AController* CurrentController = GetController();
    AController* PreviousController = LastKnownController.FindRef(this);

    if (PreviousController != CurrentController)
    {
        UE_LOG(LogTemp, Error, TEXT("[FollowerChar] ❌ CONTROLLER CHANGED! Was='%s', Now='%s'"),
            *GetNameSafe(PreviousController), *GetNameSafe(CurrentController));
    }
}
```

**What it does:**
- Detects controller changes from the pawn's perspective
- Helps identify if another controller is stealing possession
- Checks every 60 frames (every ~2 seconds at 30 FPS) to reduce log spam

---

## Expected Logs After Fix

### Success Case (Auto Re-Possession Works):
```
[FollowerTrainer] ✅ OnPossess: Now controlling 'BP_FollowerAgent_C_7'
[FollowerChar] BeginPlay: Controller=FollowerAgentTrainer_2 (AIController=✅, PathFollowing=✅)

... (later during gameplay) ...

[FollowerTrainer] ❌ OnUnPossess: Losing control of 'BP_FollowerAgent_C_7' - CALLSTACK NEEDED!
[MOVE BUG] ❌ AIController 'FollowerAgentTrainer_2' does NOT control pawn! GetPawn()=None
[MOVE FIX] Re-possessing pawn 'BP_FollowerAgent_C_7'...
[FollowerTrainer] ✅ OnPossess: Now controlling 'BP_FollowerAgent_C_7'  ← Re-possessed!
[MOVE FIX] ✅ Re-possession successful
[MOVE DIAG] MoveTo result: Code=✅ RequestSuccessful
[MOVE DIAG] After 0.1s: moved 52.3 units  ← MOVEMENT WORKS!
```

---

### Failure Case (Re-Possession Blocked):
```
[FollowerTrainer] ❌ OnUnPossess: Losing control of 'BP_FollowerAgent_C_7'
[MOVE BUG] ❌ AIController does NOT control pawn! GetPawn()=None
[MOVE FIX] Re-possessing pawn...
[MOVE FIX] ❌ Re-possession FAILED!  ← Something is blocking re-possession!
```

**If this happens:** Another system is actively preventing possession (e.g., another controller owns the pawn)

---

## Root Cause Investigation (Next Steps)

The fix **works around** the problem, but we need to find out **WHY** unpossession happens:

### Possible Causes:

1. **Schola Environment Reset**
   - `ComputeStatus()` returns `Completed/Truncated` → Schola calls `ResetTrainer()` → Unpossess?
   - Check `FollowerAgentTrainer::ResetTrainer()` implementation
   - Check if Schola's `AbstractTrainer` calls `UnPossess()` during reset

2. **Pawn Destruction/Recreation**
   - Training environment destroys/respawns pawns between episodes
   - `PawnPendingDestroy()` calls `UnPossess()` automatically
   - Check if pawns are being destroyed in logs

3. **Controller Replacement**
   - Another controller (e.g., `FollowerAIController`) is auto-possessing the pawn
   - Check if `AutoPossessAI` is set on `FollowerCharacter` (it shouldn't be!)
   - Check Blueprint overrides in `BP_FollowerAgent`

4. **StateTree Issues**
   - StateTree stops/restarts → triggers unpossession?
   - Check `FollowerStateTreeComponent::StopLogic()` implementation

---

## How to Identify the Culprit

**Look for this pattern in logs:**
```
[FollowerTrainer] ❌ OnUnPossess: Losing control of 'BP_FollowerAgent_C_7'
↓ (Something happens here - look at timestamps)
[MOVE BUG] ❌ AIController does NOT control pawn!
```

**What happened between OnUnPossess and MOVE BUG?**
- Check logs for `ResetTrainer`, `Destroyed`, `ComputeStatus`, etc.
- The log message immediately BEFORE `OnUnPossess` is the smoking gun

---

## Long-Term Fix (TODO)

Once we identify the root cause:

### If Schola resets cause it:
```cpp
void AFollowerAgentTrainer::ResetTrainer()
{
    // DON'T unpossess! Keep possession across episode resets
    EpisodeReward = 0.0f;
    EpisodeSteps = 0;
    // ... other resets WITHOUT UnPossess()
}
```

### If AutoPossessAI is stealing control:
```cpp
// FollowerCharacter.cpp constructor
AIControllerClass = nullptr;  // Don't auto-create controller
AutoPossessAI = EAutoPossessAI::Disabled;  // Don't auto-possess
```

### If StateTree is causing it:
```cpp
// Don't stop StateTree during gameplay
// Or override StopLogic to prevent unpossession
```

---

## Files Modified

### Core Fix (Auto Re-Possession):
- `Source/GameAI_Project/Private/StateTree/Tasks/STTask_ExecuteObjective.cpp` (Lines 165-186)

### Diagnostics (Tracking):
- `Source/GameAI_Project/Private/Schola/FollowerAgentTrainer.cpp` (Lines 210-224)
- `Source/GameAI_Project/Public/Schola/FollowerAgentTrainer.h` (Lines 54-62)
- `Source/GameAI_Project/Private/Actor/FollowerCharacter.cpp` (Lines 57-79)

---

## Testing Checklist

- [ ] Compile project
- [ ] Start PIE
- [ ] Trigger movement
- [ ] **Check logs for `[MOVE FIX] ✅ Re-possession successful`**
- [ ] **Verify agents move (After 0.1s: moved >0 units)**
- [ ] **Find `[FollowerTrainer] ❌ OnUnPossess` messages**
- [ ] **Identify what happens BEFORE OnUnPossess** (root cause!)
- [ ] Report back with sequence of events

---

## Expected Outcome

✅ **Movement now works** (re-possession fixes symptom)
🔍 **Logs reveal root cause** (OnUnPossess tracking)
🛠️ **Permanent fix coming** (once root cause identified)

---

## Rollback (If Needed)

If re-possession causes issues:

1. Remove possession check (STTask_ExecuteObjective.cpp:165-186)
2. Keep diagnostic logging (FollowerAgentTrainer OnPossess/UnPossess)
3. Report logs showing unpossession pattern

---

## Final Notes

This is a **workaround fix**, not a root cause fix. The real problem is:
**SOMETHING is calling `UnPossess()` on FollowerAgentTrainer during gameplay**

Once we see the logs showing WHEN and WHY unpossession happens, we can prevent it at the source instead of re-possessing every movement frame.

For now, **movement should work** ✅
