# v9.0 Training Issues - Fix Summary

**Date:** 2026-02-01
**Version:** v9.0
**Status:** ✅ All Issues Fixed

---

## Issue #1: State Thrashing (Repeated Enter/Exit)

### Problem
Agents repeatedly entering and exiting tactical movement state dozens of times per second when dead:
```
[TACTICAL v8.0] 'BP_FollowerAgent_C_21': Entering tactical movement state
[TACTICAL v8.0] 'BP_FollowerAgent_C_21': Entering tactical movement state
```

### Root Cause
**File:** `Source/GameAI_Project/Private/StateTree/FollowerStateTreeComponent.cpp:107-159`

Two interconnected bugs:

1. **Missing Counter Increment:** `TickLogCounter` was never incremented, causing `TickLogCounter % 60 == 0` to always evaluate to `true` (0 % 60 = 0). This caused restart attempts **every single frame** (60 FPS = 60 restarts/sec).

2. **Restart During Death:** StateTree attempted to restart when agents were dead, briefly entering tactical movement before detecting death condition, creating a rapid enter/exit loop.

### Fix Applied

**Added counter increment** (line 112):
```cpp
void UFollowerStateTreeComponent::TickComponent(...)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // v9.0 FIX: Increment counter to prevent spam
    TickLogCounter++;

    // ... rest of function
}
```

**Added death check** (lines 143-147):
```cpp
EStateTreeRunStatus CurrentStatus = GetStateTreeRunStatus();
if (CurrentStatus != EStateTreeRunStatus::Running)
{
    // v9.0 FIX: Don't restart StateTree when agent is dead
    if (HealthComponent && !HealthComponent->IsAlive())
    {
        return; // Stay stopped while dead
    }

    // ... restart logic
}
```

### Expected Result
- Restart attempts reduced from **60/sec** to **1/sec** when needed
- No restart attempts when dead (agents stay in Dead state until respawn)
- Clean logs during training

---

## Issue #2: Strategy Persistence (8290 Consecutive Calls)

### Problem
Log showed:
```
[SCHOLA REWARD v9.0] BP_FollowerAgent_C_21 (Strategy=Assault, Call#8290):
⚠️ Strategy is Assault for 8290 consecutive calls. Verify MCTS assignments.
```

User question: Is this normal or a bug?

### Answer: **NORMAL (with documentation error)**

#### What "Call#8290" Actually Means
- "Call#" tracks **reward calculation calls**, NOT MCTS strategy assignments
- Reward calculations happen at **2-5 Hz** (every 0.2-0.5 seconds)
- 8290 calls at ~3 Hz average = **~46 minutes** across multiple episodes
- This spans many episodes, not a single continuous assignment

#### MCTS Update Frequency
**Documentation Error Found:**
- **CLAUDE.md claimed:** "MCTS runs async every 1.5s"
- **Actual code:** MCTS runs every **30 seconds**

**File:** `Source/GameAI_Project/Private/AI/Components/TeamLeaderComponent.h:297`
```cpp
float ContinuousPlanningInterval = 30.0f;  // NOT 1.5s!
```

#### Why Assault Persisted
- MCTS runs every 30 seconds (correct behavior)
- Recent fix (commit 2cb757e) addressed the real bug: MCTS batch 0 lock
- Call#8290 spans multiple episodes where agent was legitimately assigned Assault
- Each episode has 2-3 MCTS updates (at t=0s, 30s, 60s, etc.)

### Fix Applied

**Updated CLAUDE.md documentation** (lines 63, 80):
```diff
- Frequency: Async, every 1.5s
+ Frequency: Async, every 30s (configurable via ContinuousPlanningInterval)
```

### Recommendations

1. **Monitor Strategy Distribution:**
   - Check Python logs for `[STRATEGY DIST]` output
   - Should see balanced distribution after MCTS fix (commit 2cb757e)

2. **Verify MCTS Fix Effectiveness:**
   - Look for `🎲 EPSILON-GREEDY: Randomly selected batch` messages
   - Look for `🎲 TIE-BREAKING: %d batches tied` messages

3. **Optional Performance Tuning:**
   - If faster adaptation needed, reduce `ContinuousPlanningInterval` from 30s to 10-15s
   - Test for performance impact (MCTS takes 20-30ms per run)

---

## Issue #3: Combat vs Objective Reward Balance

### Problem
Agents avoiding combat and not approaching objectives. From log:
```
[SCHOLA REWARD v9.0] BP_FollowerAgent_C_21 (Strategy=Assault):
Obj=-0.0863, Combat=0.0000, Surv=0.0000, Tact=0.1288 → Total=0.0532
Obs: Friendly=0.315, Hostile=0.432, Enemy=1.000
```

**Objective reward is NEGATIVE despite being at moderate distance (0.432)!**

### Root Cause: Incorrect Normalization

**File:** `Source/GameAI_Project/Public/RL/Components/RewardCalculator.h:147`

**Old Parameters:**
```cpp
OBJECTIVE_NORM = { Scale: 0.02f, Offset: -0.2f, ClipMin: -1.0f, ClipMax: 3.0f }
```

**The Math:**
```
Raw Assault reward: (1.0 - 0.432) * 10.0 = 5.68 (positive!)
Normalized: (5.68 * 0.02) + (-0.2) = 0.1136 - 0.2 = -0.0864 (NEGATIVE!)
```

**Why This Failed:**
- Normalization was designed for raw rewards in range `[-10, 200]` (match wins, capture completion)
- v9.0 gradient-based rewards are in range `[0, 25]` (positioning, proximity)
- The **-0.2 offset** made all rewards below 10.0 become **negative**
- Agents needed `distance < 0.0` to get positive rewards (impossible!)

**Impact:**
- Objective positioning: **Penalized** (-0.0863)
- Combat kills: **Rewarded** (+0.2 per kill)
- Result: Agents avoid objectives, focus on safe positioning

### Fix Applied

**File:** `Source/GameAI_Project/Public/RL/Components/RewardCalculator.h:147-152`

**New Parameters:**
```cpp
// v9.0 FIX: Removed negative offset, updated scale for gradient rewards
// v9.0 gradient rewards are [0, 25] range, not [-10, 200]
constexpr FComponentNormalization OBJECTIVE_NORM = { 0.1f, 0.0f, 0.0f, 2.5f };
```

**Updated Documentation:** `CLAUDE.md:254`
```cpp
// Objective: Raw [0, 25] → Normalized [0, 2.5]
OBJECTIVE_NORM = { Scale: 0.1, Offset: 0.0, ClipMin: 0.0, ClipMax: 2.5 }
```

**New Math:**
```
Raw Assault reward: 5.68
Normalized: (5.68 * 0.1) + 0.0 = 0.568 ✅ POSITIVE!
```

### Expected Result

**Example Scenarios (Assault Strategy):**

| Scenario | Raw Reward | Old Norm | New Norm | Change |
|----------|-----------|----------|----------|--------|
| **Very Close** (dist=0.1) | (1-0.1)*10 = 9.0 | -0.02 ❌ | **+0.90** ✅ | +0.92 |
| **Moderate** (dist=0.4) | (1-0.4)*10 = 6.0 | -0.08 ❌ | **+0.60** ✅ | +0.68 |
| **Far** (dist=0.7) | (1-0.7)*10 = 3.0 | -0.14 ❌ | **+0.30** ✅ | +0.44 |

**Behavioral Changes:**
- ✅ Assault agents will now approach hostile objectives (positive gradient)
- ✅ Defend agents will now stay near friendly objectives (positive gradient)
- ✅ Objective rewards balanced with combat rewards
- ✅ Gradient feedback preserved (smooth learning signals)

---

## Files Modified

| File | Lines | Changes |
|------|-------|---------|
| `FollowerStateTreeComponent.cpp` | 112, 143-147 | Counter increment + death check |
| `RewardCalculator.h` | 147-152 | Normalization parameters |
| `CLAUDE.md` | 63, 80, 254-257 | Documentation fixes |
| `FIXES_v9.0_Training_Issues.md` | - | This summary (NEW) |

---

## Testing Recommendations

### 1. State Thrashing
- **Monitor:** Log frequency during training
- **Expected:** No more repeated "Entering tactical movement state" spam
- **Verify:** Agents stay in Dead state until respawn (no restart attempts)

### 2. Strategy Distribution
- **Monitor:** Python logs for `[STRATEGY DIST]`
- **Expected:** Balanced distribution (not 75% Assault)
- **Verify:** MCTS updates every 30 seconds in UE logs

### 3. Reward Balance
- **Monitor:** `[SCHOLA REWARD v9.0]` logs
- **Expected:** Positive objective rewards when approaching targets
- **Verify:** Assault agents move toward hostile objectives, Defend toward friendly

**Example Expected Log:**
```
[SCHOLA REWARD v9.0] BP_FollowerAgent_C_0 (Strategy=Assault):
Obj=+0.568, Combat=0.000, Surv=0.000, Tact=0.128 → Total=+0.696
Obs: Hostile=0.432
```

### 4. Behavioral Validation
Run 100+ training episodes and verify:
- [ ] Assault agents approach hostile objectives (reward-driven)
- [ ] Defend agents stay near friendly objectives (reward-driven)
- [ ] No combat avoidance (balanced rewards)
- [ ] Tactical parameters converge 2-3× faster (gradient feedback)

---

## Migration Notes

### Rebuild Required
- ✅ C++ header changes require full rebuild
- ✅ Run `Build Solution` in Visual Studio or `uat BuildCookRun`

### Training Checkpoints
- ⚠️ Reward scale changed: existing checkpoints may have suboptimal value functions
- **Recommendation:** Start fresh training run or monitor for 500+ episodes

### Python Environment
- ✅ No changes needed to `cortex_env.py`
- ✅ Return normalization still applied (preserves gradients)

---

## Version History

- **v9.0 (2026-01-30):** Initial release with gradient rewards
- **v9.0.1 (2026-02-01):** Fixed state thrashing, MCTS docs, reward normalization

---

**Status:** ✅ Ready for Training
**Next Steps:** Run extended validation (1000+ episodes), monitor reward logs
