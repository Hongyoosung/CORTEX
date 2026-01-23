# Episode Mismatch Issue - Root Cause & Fix

**Date**: 2026-01-22
**Version**: v8.5 Vectorized Training
**Status**: Issue Identified + Fix Provided

---

## Issue Summary

**Observed Behavior:**
- UE5 environment logs show Episodes 2 and 3 starting after Episode 1 times out
- Python script only shows Episode 2
- Episode numbers are not synchronized between environments

**Expected Behavior:**
- Each of the 4 environments should maintain independent episode counters
- Env 0: Episode 1 → Episode 2 → Episode 3...
- Env 1: Episode 1 → Episode 2 → Episode 3...
- Env 2: Episode 1 → Episode 2 → Episode 3...
- Env 3: Episode 1 → Episode 2 → Episode 3...

---

## Root Cause Analysis

### Problem 1: Shared Episode Counter in SimulationManager

**File**: `Source/GameAI_Project/Private/Core/SimulationManagerGameMode.cpp:957-987`

```cpp
void ASimulationManagerGameMode::StartNewEpisode(int32 EnvironmentEpisodeNumber)
{
    // ...
    if (EnvironmentEpisodeNumber >= 0)
    {
        // Multi-environment mode: Use the environment's episode counter
        CurrentEpisode = EnvironmentEpisodeNumber;  // ❌ SHARED COUNTER!
        // ...
    }
```

**The Issue:**
1. All 4 `ScholaCombatEnvironment` actors share the SAME `SimulationManagerGameMode` instance
2. `SimulationManagerGameMode::CurrentEpisode` is a SINGLE shared variable (line 579 in .h)
3. When Env 0 calls `StartNewEpisode(1)`, it sets `CurrentEpisode = 1`
4. When Env 1 calls `StartNewEpisode(1)`, it OVERWRITES `CurrentEpisode = 1`
5. When Env 0 finishes Episode 1 and starts Episode 2, it sets `CurrentEpisode = 2`
6. When Env 1 ALSO finishes Episode 1 and starts Episode 2, it sets `CurrentEpisode = 2` (again)
7. When Env 0 finishes Episode 2 and starts Episode 3, the counter jumps to 3
8. This causes the episode counter to increment incorrectly

### Problem 2: Shared Event Broadcasts

**File**: `Source/GameAI_Project/Private/Schola/ScholaCombatEnvironment.cpp:500-501`

```cpp
SimulationManager->OnEpisodeStarted.AddUniqueDynamic(this, &AScholaCombatEnvironment::OnEpisodeStarted);
SimulationManager->OnEpisodeEnded.AddUniqueDynamic(this, &AScholaCombatEnvironment::OnEpisodeEnded);
```

**The Issue:**
- ALL 4 environments bind to the SAME `OnEpisodeStarted` event on the shared `SimulationManagerGameMode`
- When ANY environment calls `StartNewEpisode()`, the event broadcasts to ALL environments
- This causes cross-environment contamination of episode events

---

## Solution

### Option A: Per-Environment SimulationManager (Recommended)

**Approach:** Each environment gets its own `SimulationManagerGameMode` instance (or equivalent episode manager).

**Changes Required:**
1. Create a new `UEpisodeManagerComponent` that is attached to each `ScholaCombatEnvironment`
2. Move episode lifecycle logic from `SimulationManagerGameMode` to `UEpisodeManagerComponent`
3. Keep team registration and enemy relationship management in `SimulationManagerGameMode` (since those are global)

**Pros:**
- Clean separation of concerns
- Each environment has fully independent episode tracking
- No risk of cross-environment contamination

**Cons:**
- Requires refactoring existing code
- Need to split episode logic from team management

### Option B: Multi-Environment Support in SimulationManager (Current Approach)

**Approach:** Make `SimulationManagerGameMode` support multiple environments properly.

**Changes Required:**
1. ✅ **Already Added**: Per-environment episode tracking maps in `.h` file:
   ```cpp
   TMap<int32, int32> EnvironmentEpisodes;  // EnvironmentID → Episode Number
   TMap<int32, int32> EnvironmentSteps;     // EnvironmentID → Current Step
   ```

2. **Update `StartNewEpisode()`** to accept both EnvironmentID and Episode number:
   ```cpp
   void StartNewEpisode(int32 EnvironmentID, int32 EnvironmentEpisodeNumber);
   ```

3. **Update `ScholaCombatEnvironment::ResetEnvironment()`** to pass EnvironmentID:
   ```cpp
   SimulationManager->StartNewEpisode(EnvironmentID, CurrentEpisode);
   ```

4. **Use per-environment broadcasts** (add EnvironmentID to event signatures):
   ```cpp
   OnEpisodeStarted.Broadcast(EnvironmentID, EpisodeNumber);
   ```

5. **Filter events in listeners** to only process events for their own environment:
   ```cpp
   void AScholaCombatEnvironment::OnEpisodeStarted(int32 BroadcastEnvID, int32 EpisodeNumber)
   {
       if (BroadcastEnvID != EnvironmentID)
           return;  // Ignore events from other environments
       // ...
   }
   ```

**Pros:**
- Minimal code changes
- Keeps all episode management in one place

**Cons:**
- Still uses shared SimulationManager (potential for future issues)
- Adds complexity to event handling

### Option C: Deprecate SimulationManager Episode Tracking (Quick Fix)

**Approach:** Remove episode tracking from `SimulationManagerGameMode` entirely in multi-env mode.

**Changes Required:**
1. ✅ **Already Added**: Deprecation warning in `StartNewEpisode()`
2. **Update SimulationManager** to NOT store `CurrentEpisode` in multi-env mode
3. **Rely on each environment's local `CurrentEpisode` counter**
4. **Broadcast events with EnvironmentID** so listeners can filter

**Pros:**
- Simple, minimal changes
- Each environment fully owns its episode lifecycle

**Cons:**
- Partial solution - still doesn't fix shared event broadcasts

---

## Immediate Fix (Option C Implementation)

### Step 1: Update UE5 Event Signatures

**File**: `Source/GameAI_Project/Public/Core/SimulationManagerGameMode.h:470-473`

**Change:**
```cpp
// OLD:
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEpisodeStarted, int32, EpisodeNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEpisodeEnded, const FEpisodeResult&, Result);

// NEW:
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEpisodeStarted, int32, EnvironmentID, int32, EpisodeNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEpisodeEnded, int32, EnvironmentID, const FEpisodeResult&, Result);
```

### Step 2: Update ScholaCombatEnvironment Event Handlers

**File**: `Source/GameAI_Project/Private/Schola/ScholaCombatEnvironment.cpp:506`

**Change:**
```cpp
// OLD:
void AScholaCombatEnvironment::OnEpisodeStarted(int32 EpisodeNumber)
{
    // ...
}

// NEW:
void AScholaCombatEnvironment::OnEpisodeStarted(int32 BroadcastEnvID, int32 EpisodeNumber)
{
    // Filter events for this environment only
    if (BroadcastEnvID != EnvironmentID)
    {
        UE_LOG(LogTemp, Log, TEXT("[ScholaEnv #%d] Ignoring episode start event from Env %d"),
            EnvironmentID, BroadcastEnvID);
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════════╗"));
    UE_LOG(LogTemp, Warning, TEXT("║ [ScholaEnv #%d] Episode %d STARTED (Broadcast: %d)            ║"),
        EnvironmentID, CurrentEpisode, EpisodeNumber);
    // ...
}
```

### Step 3: Update SimulationManager Broadcasts

**File**: `Source/GameAI_Project/Private/Core/SimulationManagerGameMode.cpp:1095`

**Change:**
```cpp
// OLD:
OnEpisodeStarted.Broadcast(CurrentEpisode);

// NEW:
// Determine EnvironmentID from caller (need to pass this in StartNewEpisode signature)
OnEpisodeStarted.Broadcast(EnvironmentID, CurrentEpisode);
```

---

## Python Logging Improvements

### ✅ Already Applied:

1. **Better per-environment status visibility** (line 407):
   - Shows running vs. done environments with icons (🔄, ⚠️, ✓)
   - Displays episode number for each environment independently
   - Formatted box for episode completion

2. **Clearer episode completion logging** (line 348):
   - Box-formatted output showing which environment finished
   - Separate episode counters per environment
   - Visual separation from other log messages

### Example Output:

```
================================================================================
[STEP 100] GlobalEp=1, Time=10.0s, StepReward=0.52, Running=4/4, Done=0/4
────────────────────────────────────────────────────────────────────────────────
  🔄 Env 0: Episode 1, Steps=100, Time=10.0s
  🔄 Env 1: Episode 1, Steps=100, Time=10.0s
  🔄 Env 2: Episode 1, Steps=100, Time=10.0s
  🔄 Env 3: Episode 1, Steps=100, Time=10.0s
================================================================================

┌──────────────────────────────────────────────────────────────────────────────┐
│ [ENV 0 DONE] Episode 1 completed                                            │
│   Steps: 300        Time: 30.2s                                             │
│   Terminated: False Truncated: True                                         │
│   Status: Waiting for other environments to finish...                       │
└──────────────────────────────────────────────────────────────────────────────┘

[ASYNC STATUS] Finished: {0}, Running: [1, 2, 3]
```

---

## Next Steps

### Recommended Path (Option B - Full Multi-Environment Support):

1. **Update Event Signatures** to include EnvironmentID (see Step 1 above)
2. **Update ScholaCombatEnvironment** to filter events by EnvironmentID (see Step 2 above)
3. **Update SimulationManager broadcasts** to include EnvironmentID (see Step 3 above)
4. **Test with 4 environments** to verify independent episode tracking

### Alternative Quick Fix (Option C - Deprecate Global Tracking):

1. **Remove `CurrentEpisode` usage** from SimulationManager in multi-env mode
2. **Add EnvironmentID filtering** to event handlers
3. **Rely on environment-local episode counters** exclusively

---

## Testing Checklist

- [ ] Launch UE5 with 4 environments
- [ ] Start Python training script
- [ ] Verify each environment starts at Episode 1 independently
- [ ] Force early termination in Env 0
- [ ] Verify Env 0 shows Episode 2, while Envs 1-3 remain on Episode 1
- [ ] Wait for all environments to finish
- [ ] Verify all environments increment to Episode 2 simultaneously after reset
- [ ] Check Python logs show independent episode counters per environment
- [ ] Run for 100+ episodes and verify no desync errors

---

## References

- Original Issue: Episode mismatch between UE5 and Python
- Documentation: `CORTEX_Training/ASYNC_EPISODE_HANDLING.md`
- Related Files:
  - `Source/GameAI_Project/Public/Core/SimulationManagerGameMode.h`
  - `Source/GameAI_Project/Private/Core/SimulationManagerGameMode.cpp`
  - `Source/GameAI_Project/Private/Schola/ScholaCombatEnvironment.cpp`
  - `CORTEX_Training/sbdapm_env.py`

---

## Implementation Status

**Status**: ✅ IMPLEMENTED (Option C+ - Environment-Local Management)
**Implementation Date**: 2026-01-22
**Last Updated**: 2026-01-22

### Changes Applied:

1. ✅ **Updated event signatures** (SimulationManagerGameMode.h:466-473)
   - `FOnEpisodeStarted` now includes `EnvironmentID` parameter
   - `FOnEpisodeEnded` now includes `EnvironmentID` parameter

2. ✅ **Updated ScholaCombatEnvironment event handlers** (ScholaCombatEnvironment.h/cpp)
   - `OnEpisodeStarted(int32 BroadcastEnvID, int32 EpisodeNumber)` - filters by EnvironmentID
   - `OnEpisodeEnded(int32 BroadcastEnvID, const FEpisodeResult&)` - filters by EnvironmentID

3. ✅ **Updated StartNewEpisode** (SimulationManagerGameMode.cpp:957-1106)
   - Accepts `EnvironmentID` as first parameter
   - Uses per-environment tracking (`EnvironmentEpisodes`, `EnvironmentSteps`)
   - Broadcasts with `EnvironmentID`

4. ✅ **Updated EndEpisode** (SimulationManagerGameMode.cpp:887-955)
   - Accepts `EnvironmentID` parameter (with default = 0)
   - Auto-determines EnvironmentID from team mapping if not provided
   - Broadcasts with `EnvironmentID`

5. ✅ **Added team-to-environment mapping**
   - New `TeamToEnvironmentMap` in SimulationManagerGameMode
   - New `RegisterTeamEnvironment()` method
   - Automatic registration during agent discovery in ScholaCombatEnvironment

6. ✅ **Deprecated global episode tracking**
   - Added deprecation comments to `CurrentEpisode` and `CurrentStep`
   - Kept for backward compatibility but not used in multi-env logic

### Next Steps:

**Testing Required:**
- [ ] Launch UE5 with 4 environments
- [ ] Start Python training script
- [ ] Verify independent episode tracking per environment
- [ ] Run extended training (100+ episodes) to verify stability

**Expected Behavior:**
```
[ScholaEnv #0] Episode 1 STARTED
[ScholaEnv #1] Episode 1 STARTED
[ScholaEnv #2] Episode 1 STARTED
[ScholaEnv #3] Episode 1 STARTED

... (Env 0 finishes first) ...

[ScholaEnv #0] Episode 2 STARTED
[ScholaEnv #1] Episode 1 (still running)
[ScholaEnv #2] Episode 1 (still running)
[ScholaEnv #3] Episode 1 (still running)
```
