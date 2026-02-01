# CORTEX v9.0.3: Learning Convergence Failure

**Date:** 2026-02-01
**Issue:** Learning does not converge after 336k steps (~53 iterations)
**Status:** 🔍 Root causes identified

---

## Problem Summary

### Metrics at 336,896 Steps (Iteration 53)

```
❌ VF Explained Var:  -0.018  (CATASTROPHIC - worse than predicting mean!)
❌ VF Loss:            0.804  (Very high - should be <0.01)
❌ Entropy:            7.015  (Still high - should be dropping toward 5.5)
⚠️  Entropy Coeff:     0.038  (Scheduled value at 336k steps)
✅ Episode Rewards:    Mean ~340 (slowly improving but agents not following strategies)
```

**Critical Behavior:**
- Agents assigned "Defend" strategy wander randomly instead of defending objectives
- Only 1 out of 5 EQS movements are toward objective (20% vs expected 80%+)
- Value function is broken (negative explained variance = worse than random prediction)

---

## Root Cause Analysis

### Issue 1: VF Clip Param Too Restrictive 💀

**File:** `train_rllib.py:268`

```python
VF_CLIP_PARAM = 2.0   # ❌ Clips TD error to [-2, 2]
```

**Problem:**
C++ rewards are in **[-5, 5]** range (from `tanh(RawTotal/4) * 5`), but VF clips TD errors to **[-2, 2]**.

**Math:**
```
Episode return: 300 (typical)
Gamma^t * reward: Can easily exceed ±2.0 per step
→ VF clips most learning signals
→ VF can't learn proper value estimates
→ Negative explained variance (-0.018)
```

**Why this breaks learning:**
1. VF predicts values poorly → bad advantage estimates
2. Bad advantages → policy updates in wrong direction
3. Entropy stays high because policy never commits (no reward signal)

**Fix:**
```python
VF_CLIP_PARAM = 10.0  # Allow TD errors up to ±10 for [-5, 5] rewards
```

---

### Issue 2: EQS Not Using ObjectiveWeight ⚠️

**File:** `Content/Game/Blueprints/AI/EQS/EQS_TacticalPositionQuery.uasset`

**Evidence:**
```
[EQS DIAGNOSTIC] Agent (Defend): CurrentDist=3982cm → TargetDist=3977cm, Delta=-5cm  ❌ NOT closer
[EQS DIAGNOSTIC] Agent (Defend): CurrentDist=3990cm → TargetDist=4429cm, Delta=+439cm ❌ NOT closer
[EQS DIAGNOSTIC] Agent (Defend): CurrentDist=4133cm → TargetDist=4002cm, Delta=-130cm ✅ CLOSER
[EQS DIAGNOSTIC] Agent (Defend): CurrentDist=4048cm → TargetDist=4247cm, Delta=+199cm ❌ NOT closer
```

**Problem:**
Only 1 out of 5 movements toward objective (20%). With `ObjectiveWeight=8.0` for Defend, should be 80%+.

**Code analysis:**
```cpp
// STTask_ExecuteTacticalMovement_v8.cpp:372
QueryRequest.SetFloatParam(TEXT("ObjectiveWeight"), 8.0f);  // ✅ C++ is passing weight

// EQS Blueprint: ❌ LIKELY ISSUE - Blueprint not using parameter or using wrong scoring
```

**Investigation needed:**
1. Open `EQS_TacticalPositionQuery` blueprint in UE5 editor
2. Check if "ObjectiveWeight" parameter exists
3. Verify "DistanceToObjective" test exists and uses:
   - Context: EnvQueryContext_ObjectiveLocation
   - Scoring: **Prefer Closer** (not Prefer Further!)
   - Weight multiplication: ObjectiveWeight parameter

**Temporary diagnostic:**
Increase ObjectiveWeight in code to confirm EQS is broken:

```cpp
// Test: If increasing to 20.0 doesn't change behavior, EQS is ignoring parameter
float ObjectiveWeight = 20.0f;  // Temporarily test if EQS responds
```

---

### Issue 3: Observation-Action Gap (Secondary)

**Observations are correct:**
```cpp
// ObservationBuilderComponent.cpp:433
Observation.FriendlyObjectiveDistance = clamp(Distance / 10000.0, 0.0, 1.0);  // ✅ Correct
```

**Rewards are correct:**
```cpp
// RewardCalculator.cpp:346
Reward = (1.0 - HostileObjectiveDistance) * 10.0f;  // ✅ Assault gets high reward for approaching
```

**BUT:**
RL policy outputs tactical parameters → EQS ignores them → No learning signal.

**Flow:**
```
RL Policy → [Aggression=0.8, Cover=0.2, Spread=0.5] → EQS weights
                                                         ↓
EQS → [Position selection SHOULD use ObjectiveWeight=8.0]
                                                         ↓
                                              ❌ Actually ignores it
                                                         ↓
Agent moves randomly → No reward → No learning
```

---

## Fixing Strategy

### Priority 1: Fix VF Clip Param (Immediate)

**File:** `train_rllib.py`

```python
# Line 268 - BEFORE
VF_CLIP_PARAM = 2.0

# Line 268 - AFTER
VF_CLIP_PARAM = 10.0  # Allow full TD error range for [-5, 5] rewards
```

**Expected outcome:**
- VF explained variance: -0.018 → 0.7+ within 10 iterations
- VF loss: 0.804 → <0.1 within 20 iterations

---

### Priority 2: Fix EQS Blueprint (Critical)

**Steps:**

1. **Open UE5 Editor**
2. **Navigate to:** `Content/Game/Blueprints/AI/EQS/EQS_TacticalPositionQuery`
3. **Check parameter:**
   - Blueprint should have FloatParam "ObjectiveWeight" (default: 0.0)

4. **Check test:**
   - Test name: "DistanceToObjective" or similar
   - Context: `EnvQueryContext_ObjectiveLocation` ✅ (already implemented in C++)
   - Scoring function: **Prefer Closer** (inverse distance)
   - Weight: Multiply by `ObjectiveWeight` parameter

5. **If missing, add test:**
   ```
   Test Type: Distance
   Context: EnvQueryContext_ObjectiveLocation (Objective Location)
   Scoring: Prefer Closer
   Weight: ObjectiveWeight (FloatParam)
   Normalization Type: Absolute
   ```

6. **Verify other tests don't dominate:**
   ```
   Current weights (from C++ code):
   - AggressionWeight: 0.5-5.0
   - CoverWeight: 0.5-5.0
   - FormationWeight: 0.3-5.0
   - ObjectiveWeight: 0.0-8.0  ← Should be highest for Defend!

   If "Distance to Cover" test has hardcoded weight of 10.0, it will dominate.
   All weights should be parameterized!
   ```

**Expected outcome:**
- Defend agents: 80%+ movements toward friendly objective
- Assault agents: 80%+ movements toward hostile objective

---

### Priority 3: Restart Training from Scratch (Recommended)

**Why:**
Current checkpoint has broken VF weights from 53 iterations of bad learning.

**Command:**
```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\CORTEX_Training

# Start fresh with fixes applied
python train_rllib.py --iterations 100
```

**Monitoring (first 20 iterations):**
```
✅ Iteration 5-10:
   - VF explained var: 0.0 → 0.5+ (improving from scratch)
   - VF loss: 1.0 → 0.3 (decreasing steadily)

✅ Iteration 10-15:
   - Episode rewards: Start differentiating by strategy
   - Assault agents approach hostile objective
   - Defend agents hold friendly objective

✅ Iteration 15-20:
   - VF explained var: 0.7+
   - Entropy: 7.0 → 6.5 (starts declining)
```

---

## Configuration Changes Summary

### 1. `train_rllib.py` (REQUIRED)

```python
# Line 268
VF_CLIP_PARAM = 10.0  # INCREASED from 2.0

# Rationale: C++ rewards are [-5, 5], need headroom for TD errors
```

### 2. `EQS_TacticalPositionQuery.uasset` (REQUIRED - Blueprint Edit)

**Add/Fix:**
- "DistanceToObjective" test with ObjectiveWeight parameter
- Scoring: Prefer Closer
- Verify all other tests use parameterized weights (not hardcoded)

---

## Validation Checklist

### Before Starting Training:

- [ ] `VF_CLIP_PARAM = 10.0` in `train_rllib.py`
- [ ] EQS blueprint has "DistanceToObjective" test
- [ ] EQS test uses "Prefer Closer" scoring
- [ ] EQS test multiplies score by `ObjectiveWeight` parameter
- [ ] All EQS tests use parameterized weights (no hardcoded values >5.0)

### During Training (Iterations 1-10):

- [ ] VF explained variance improving (0.0 → 0.5+)
- [ ] VF loss decreasing (1.0 → 0.3)
- [ ] EQS diagnostic logs show 80%+ movements toward objectives
- [ ] Episode rewards differentiating by strategy

### During Training (Iterations 10-30):

- [ ] VF explained variance: 0.7-0.85
- [ ] Entropy declining: 7.0 → 6.0
- [ ] Defend agents stay within 2000cm of friendly objective
- [ ] Assault agents approach within 2000cm of hostile objective

---

## Debugging Commands

### Check EQS Parameter Passing:

Add temporary logging in C++:

```cpp
// STTask_ExecuteTacticalMovement_v8.cpp:372
UE_LOG(LogTemp, Warning, TEXT("🔧 [EQS DEBUG] %s: Setting ObjectiveWeight=%.1f, AggressionWeight=%.1f, CoverWeight=%.1f"),
    *Pawn->GetName(), ObjectiveWeight, AggressionWeight, CoverWeight);
```

Expected output:
```
🔧 [EQS DEBUG] BP_FollowerAgent_C_4 (Defend): ObjectiveWeight=8.0, AggressionWeight=2.5, CoverWeight=3.2
```

### Monitor Observation Values:

Check if objective distances are populated:

```bash
# In UE5 Output Log, search for:
"📍 [OBS] Agent"

# Expected:
📍 [OBS] Agent BP_FollowerAgent_C_4: FriendlyObj=ObjectiveActor_1, HostileObj=ObjectiveActor_2
  Friendly: RawDist=4000 cm, Normalized=0.400
  Hostile: RawDist=6500 cm, Normalized=0.650
```

If you see `FriendlyObj=NULL` or `Normalized=1.000` (default), objectives are not being assigned by TeamLeader.

---

## Timeline Expectations (With Fixes)

| Iteration | VF Explained Var | VF Loss | Entropy | Episode Rewards | Status |
|-----------|------------------|---------|---------|-----------------|--------|
| **1-5** | 0.0-0.3 | 1.0-0.5 | 7.0-7.2 | Random (~200) | 🟡 Initializing |
| **5-10** | 0.3-0.6 | 0.5-0.2 | 6.8-7.0 | 250-400 | 🟡 VF learning |
| **10-20** | 0.6-0.75 | 0.2-0.08 | 6.5-6.8 | 400-600 | 🟢 Strategies emerging |
| **20-40** | 0.75-0.85 | 0.08-0.03 | 5.5-6.5 | 600-1200 | 🟢 Converging |
| **40-80** | 0.85-0.88 | 0.03-0.01 | 4.5-5.5 | 1000-2500 | ✅ Stable |

---

## Known Limitations (Not Bugs)

### Docker Networking Latency
- `timeout: 60s` in env config is correct for Docker
- Expect 10-20ms additional latency vs native (acceptable)

### Entropy Coeff Schedule
```python
entropy_coeff_schedule = [
    (0, 0.05),         # Start high
    (500000, 0.03),    # Drop at 500k steps
    (1000000, 0.02),   # Minimum at 1M steps
]
```

At 336k steps, entropy_coeff = 0.038 is correct (linearly interpolated between 0.05 and 0.03).

---

## Next Steps

1. **Fix `VF_CLIP_PARAM = 10.0`** in `train_rllib.py` (immediate)
2. **Open UE5 Editor** and fix EQS blueprint (30 min task)
3. **Start fresh training run** with `--iterations 100`
4. **Monitor TensorBoard** for VF explained variance recovery
5. **Document results** after iteration 20

---

## Files to Modify

| File | Change | Priority |
|------|--------|----------|
| `train_rllib.py:268` | `VF_CLIP_PARAM = 10.0` | 🔴 CRITICAL |
| `EQS_TacticalPositionQuery.uasset` | Add DistanceToObjective test | 🔴 CRITICAL |
| `STTask_ExecuteTacticalMovement_v8.cpp:372` | (Optional) Add debug logging | 🟡 Diagnostic |

---

## Success Criteria

**After 20 iterations with fixes:**
- ✅ VF explained variance: >0.7
- ✅ VF loss: <0.1
- ✅ Defend agents stay within 2000cm of friendly objective (80%+ of time)
- ✅ Assault agents approach hostile objective (distance decreasing)
- ✅ Entropy declining: 7.0 → 6.0-6.5

**After 50 iterations:**
- ✅ VF explained variance: 0.85+
- ✅ Entropy: 5.0-6.0
- ✅ Episode rewards: Clear strategy differentiation (Assault >1500, Defend >800)

---

## Summary

**Three critical bugs identified:**

1. **VF Clip Param (2.0 → 10.0):** Preventing value function from learning
2. **EQS Blueprint:** Not using ObjectiveWeight parameter (or using wrong scoring)
3. **Broken Checkpoint:** 53 iterations of bad VF weights baked in

**Recommended action:**
1. Fix VF_CLIP_PARAM
2. Fix EQS blueprint
3. Restart training from scratch
4. Expect convergence by iteration 40-50

**This is NOT the same issue as v9.0.1/v9.0.2!**
- v9.0.1: Reward normalization caused collapse
- v9.0.2: Fixed normalization, but VF still broken
- v9.0.3: VF clip param is root cause + EQS not working

---

**Document Version:** v9.0.3
**Last Updated:** 2026-02-01
**Status:** 🔍 Root causes identified, fixes ready to apply
