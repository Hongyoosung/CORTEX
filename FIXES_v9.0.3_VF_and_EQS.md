# CORTEX v9.0.3: VF Clip + EQS Fixes

**Quick reference for applying v9.0.3 fixes**

---

## Fix 1: VF Clip Param (Python - 2 min)

**File:** `CORTEX_Training/train_rllib.py`

**Line 268:**
```python
# BEFORE (v9.0.2)
VF_CLIP_PARAM = 2.0   # ❌ Too restrictive for [-5, 5] rewards

# AFTER (v9.0.3)
VF_CLIP_PARAM = 10.0  # ✅ Allows VF to learn from full reward range
```

**Rationale:**
- C++ outputs rewards in [-5, 5] via `tanh(sum/4) * 5`
- Typical episode returns: 200-500
- TD errors can exceed ±2.0 regularly
- Clipping at 2.0 prevents VF from learning → negative explained variance

**Expected impact:**
- VF explained variance: -0.02 → 0.7+ within 20 iterations
- VF loss: 0.804 → <0.1 within 30 iterations

---

## Fix 2: EQS Blueprint (UE5 Editor - 30 min)

**File:** `Content/Game/Blueprints/AI/EQS/EQS_TacticalPositionQuery.uasset`

### Step 1: Open Blueprint
1. Launch UE5 Editor
2. Navigate to: `Content/Game/Blueprints/AI/EQS/`
3. Open: `EQS_TacticalPositionQuery`

### Step 2: Check Parameters
Verify FloatParam exists:
- Name: `ObjectiveWeight`
- Default Value: `0.0`

If missing, add it:
- Right-click canvas → Add Parameter → Float → Name: "ObjectiveWeight"

### Step 3: Add/Fix DistanceToObjective Test

**Current issue:** EQS likely missing this test or using wrong scoring.

**Add new test:**
1. In Query canvas, right-click → Add Test → Distance
2. Configure:
   ```
   Test Name: DistanceToObjective
   Test Purpose: Prefer Closer
   Filtering Type: Minimum Threshold (optional: 0.0)
   Scoring Type: Inverse Linear
   Context: EnvQueryContext_ObjectiveLocation  ← CRITICAL: Use custom context!
   ```

3. **Set weight multiplication:**
   - In test properties, find "Score Weight" or "Weight"
   - Set to: `ObjectiveWeight` (parameter reference)
   - **NOT** a hardcoded value like 8.0!

### Step 4: Verify Other Tests

Check existing tests don't have hardcoded weights that dominate:

**Acceptable:**
```
Test: DistanceToCover
Weight: CoverWeight (parameter)  ✅ Good

Test: DistanceToAllies
Weight: FormationWeight (parameter)  ✅ Good
```

**NOT acceptable:**
```
Test: DistanceToCover
Weight: 10.0 (hardcoded)  ❌ Will always dominate ObjectiveWeight=8.0!
```

**Fix:** Replace all hardcoded weights with parameters:
- CoverWeight
- AggressionWeight
- FormationWeight
- ObjectiveWeight

### Step 5: Save and Test

1. Save blueprint
2. Compile (if needed)
3. Close editor
4. Run test scenario with Defend strategy
5. Check logs for EQS diagnostic:
   ```
   ✅ EXPECTED (80%+ should be closer):
   [EQS DIAGNOSTIC] Agent (Defend): Delta=-250cm | ✅ CLOSER to objective
   [EQS DIAGNOSTIC] Agent (Defend): Delta=-180cm | ✅ CLOSER to objective
   [EQS DIAGNOSTIC] Agent (Defend): Delta=-95cm  | ✅ CLOSER to objective
   ```

---

## Fix 3: Optional Diagnostic Logging (C++ - 5 min)

**File:** `Source/GameAI_Project/Private/StateTree/Tasks/STTask_ExecuteTacticalMovement_v8.cpp`

**Add after line 372:**
```cpp
// AFTER: QueryRequest.SetFloatParam(TEXT("ObjectiveWeight"), ObjectiveWeight);

// v9.0.3 DEBUG: Verify parameter passing
static int32 DebugLogCounter = 0;
if (++DebugLogCounter % 50 == 0)
{
    UE_LOG(LogTemp, Warning, TEXT("🔧 [EQS PARAMS v9.0.3] %s (%s): ObjWeight=%.1f, AggrWeight=%.1f, CoverWeight=%.1f, FormWeight=%.1f"),
        *Pawn->GetName(),
        StrategyName,
        ObjectiveWeight,
        AggressionWeight,
        CoverWeight,
        FormationWeight);
}
```

**Compile and run.** Expected log:
```
🔧 [EQS PARAMS v9.0.3] BP_FollowerAgent_C_4 (Defend): ObjWeight=8.0, AggrWeight=2.3, CoverWeight=3.5, FormWeight=2.8
```

If ObjectiveWeight shows `0.0`, the parameter name mismatch between C++ and blueprint!

---

## Apply Fixes and Restart Training

### 1. Apply Fixes
```bash
# Edit train_rllib.py
VF_CLIP_PARAM = 10.0

# Open UE5 and fix EQS blueprint (as above)

# (Optional) Add C++ diagnostic logging and recompile
```

### 2. Restart Training (Fresh)
```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\CORTEX_Training

# Delete old broken checkpoint or start fresh
python train_rllib.py --iterations 100
```

**DO NOT resume from 20260201_112620** - it has 53 iterations of broken VF weights.

### 3. Monitor TensorBoard
```bash
# In separate terminal
tensorboard --logdir=training_results --port=6006
```

**Watch:**
- `vf_explained_var`: Should go 0.0 → 0.5+ by iteration 10
- `vf_loss`: Should decrease steadily (1.0 → 0.3 → 0.1)
- `entropy`: Should stay ~7.0 for first 20 iterations, then start dropping

### 4. Check UE5 Logs

**Search for:**
```
🔍 [EQS DIAGNOSTIC]
```

**Expect:**
```
Iteration 5-10:  40-60% movements closer (VF still learning)
Iteration 10-20: 60-80% movements closer (strategies emerging)
Iteration 20+:   80%+ movements closer (converged behavior)
```

---

## Validation Checklist

### Before Training:
- [ ] `train_rllib.py:268` has `VF_CLIP_PARAM = 10.0`
- [ ] EQS blueprint has "DistanceToObjective" test
- [ ] EQS test uses "EnvQueryContext_ObjectiveLocation" context
- [ ] EQS test scoring: "Prefer Closer" or "Inverse Linear"
- [ ] EQS test weight: `ObjectiveWeight` parameter (not hardcoded!)
- [ ] All other EQS tests use parameterized weights

### Iteration 10:
- [ ] VF explained variance: >0.4
- [ ] VF loss: <0.4
- [ ] No "microscopic reward" warnings in logs
- [ ] EQS diagnostics show >50% movements closer to objective

### Iteration 20:
- [ ] VF explained variance: >0.7
- [ ] VF loss: <0.15
- [ ] Entropy: 6.5-7.0 (starting to decline)
- [ ] EQS diagnostics show >70% movements closer to objective

### Iteration 40:
- [ ] VF explained variance: >0.85
- [ ] VF loss: <0.05
- [ ] Entropy: 5.5-6.5 (declining steadily)
- [ ] Episode rewards: Clear strategy differentiation

---

## Troubleshooting

### VF Explained Variance Still Negative After 10 Iterations

**Check:**
1. Did you restart from scratch or resume old checkpoint?
   - If resumed, start fresh: `python train_rllib.py --iterations 100` (no --resume)

2. Is VF_CLIP_PARAM actually 10.0?
   - Check TensorBoard: `ray/tune/info/learner/default_policy/config/vf_clip_param`
   - Should show 10.0

3. Are rewards still microscopic?
   - Check UE5 logs for `[REWARD v9.0]`
   - Should see rewards in range [-5, 5], NOT [-0.01, 0.01]

### EQS Still Not Moving Toward Objectives

**Check:**
1. Parameter name matches in C++ and blueprint:
   - C++ passes: `"ObjectiveWeight"`
   - Blueprint should have parameter: `ObjectiveWeight` (exact match, case-sensitive)

2. Context is correct:
   - Test should use `EnvQueryContext_ObjectiveLocation`
   - NOT `EnvQueryContext_Querier` or other contexts

3. Scoring direction:
   - "Prefer Closer" or "Inverse Linear"
   - NOT "Prefer Further" or "Linear" (would move AWAY from objective)

4. Weight is actually being used:
   - Check EQS blueprint: Weight field should reference `ObjectiveWeight` parameter
   - NOT empty, NOT hardcoded to 1.0

5. Add C++ diagnostic logging (see Fix 3) to verify parameter passing

### Entropy Not Declining After 30 Iterations

**This is OK if:**
- VF explained variance is improving (0.7+)
- Episode rewards are improving
- Agents are following strategies (EQS diagnostics show >70% movements toward objectives)

**Entropy decline lags behind strategy learning.** It will drop once:
1. VF is stable (explained_var >0.85)
2. Strategies are clearly rewarded
3. Policy commits to learned behaviors

**Expect entropy drop around iteration 40-60.**

---

## Quick Reference: Expected Metrics Timeline

| Metric | Iter 5 | Iter 10 | Iter 20 | Iter 40 | Iter 80 |
|--------|--------|---------|---------|---------|---------|
| VF Expl Var | 0.2 | 0.5 | 0.75 | 0.85 | 0.88 |
| VF Loss | 0.6 | 0.3 | 0.12 | 0.04 | 0.015 |
| Entropy | 7.1 | 7.0 | 6.7 | 6.0 | 5.2 |
| Ep Reward (mean) | 220 | 340 | 580 | 1100 | 1800 |
| EQS Objective% | 30% | 55% | 75% | 85% | 90% |

---

## Summary

**Two critical fixes:**
1. **VF_CLIP_PARAM: 2.0 → 10.0** (prevents VF collapse)
2. **EQS Blueprint: Add DistanceToObjective test** (enables objective-driven movement)

**Timeline:**
- Apply fixes: 30 min
- Restart training: 100 iterations (~3-4 hours with Docker)
- Expected convergence: Iteration 40-60

**Success metric:**
By iteration 20, you should see:
- VF explained variance >0.7
- 70%+ EQS movements toward objectives
- Clear strategy differentiation in episode rewards

If not, recheck EQS blueprint configuration (most common issue: wrong context or scoring direction).

---

**Document Version:** v9.0.3 Fixes
**Last Updated:** 2026-02-01
