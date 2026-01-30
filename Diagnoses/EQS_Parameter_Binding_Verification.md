# EQS Parameter Binding Verification

**Status:** ⚠️ **MISMATCH DETECTED**
**Date:** 2026-01-27
**File:** `STTask_ExecuteTacticalMovement_v8.cpp:247-280`

---

## Current Implementation Analysis

### C++ Parameter Mapping (STTask_ExecuteTacticalMovement_v8.cpp)

**Lines 254-267:**
```cpp
// 1. Aggression → Distance to enemy scoring
float MinDistanceToEnemy = FMath::Lerp(1000.0f, 200.0f, Params.Aggression);
float AggressionWeight = Params.Aggression * 2.0f; // [0, 2]

// 2. CoverPreference → Cover weight
float CoverWeight = FMath::Lerp(0.5f, 5.0f, Params.CoverPreference);
float ExposureWeight = (1.0f - Params.CoverPreference) * 3.0f;

// 3. SpreadDistance → Formation spread
float IdealSpreadDistance = FMath::Lerp(200.0f, 1000.0f, Params.SpreadDistance);
float FormationWeight = 3.0f; // Constant
```

**Lines 274-279 (EQS Parameter Setting):**
```cpp
QueryRequest.SetFloatParam(TEXT("MinDistanceToEnemy"), MinDistanceToEnemy);
QueryRequest.SetFloatParam(TEXT("AggressionWeight"), AggressionWeight);
QueryRequest.SetFloatParam(TEXT("CoverWeight"), CoverWeight);
QueryRequest.SetFloatParam(TEXT("ExposureWeight"), ExposureWeight);
QueryRequest.SetFloatParam(TEXT("FormationSpread"), IdealSpreadDistance);
QueryRequest.SetFloatParam(TEXT("FormationWeight"), FormationWeight);
```

### User's EQS Asset Configuration

**Generator:**
- OnCircle (radius=750, spacing=50)

**Tests:**
1. **Distance to Visible Enemies**
   - Mode: Score Only
   - Score Parameter: `QueryParam.AggressionWeight`
   - Data Binding: Query Parameter

2. **Trace (Cover Check)**
   - Mode: Score Only
   - Trace Mode: Geometry by channel
   - Context: EnvQueryContext_Querier
   - Score Parameter: `QueryParam.CoverWeight`
   - Data Binding: Query Parameter

3. **Distance to Teammates**
   - Mode: Score Only
   - Score Parameter: `QueryParam.FormationWeight`
   - Normalization Type: Absolute
   - Reference Value: `QueryParam.FormationSpread`
   - Data Binding: Query Parameter

4. **PathExists from Querier**
   - Mode: Filter Only
   - Bool Match: true

---

## Issue 1: Unused Parameters

### ❌ MinDistanceToEnemy

**C++ Sets:** `QueryRequest.SetFloatParam(TEXT("MinDistanceToEnemy"), MinDistanceToEnemy);`
**EQS Uses:** ❌ **NOT REFERENCED** in any test

**Problem:** The parameter maps aggression to min distance (200-1000cm) but the EQS "Distance to Visible Enemies" test doesn't use it. The test only uses `AggressionWeight` as a score multiplier.

**Expected Behavior:**
- High Aggression (0.9) → Prefer positions 200cm from enemies
- Low Aggression (0.1) → Prefer positions 1000cm from enemies

**Actual Behavior:**
- `MinDistanceToEnemy` is computed but never used
- Distance test uses only `AggressionWeight` [0, 2] as score multiplier
- No distance normalization occurs

**Fix Required:**
The "Distance to Visible Enemies" test needs to use `MinDistanceToEnemy` as either:
1. **Normalization Reference Value** (preferred)
2. **Min Clamp** in test configuration
3. **Ideal Distance** parameter

### ❌ ExposureWeight

**C++ Sets:** `QueryRequest.SetFloatParam(TEXT("ExposureWeight"), ExposureWeight);`
**EQS Uses:** ❌ **NOT REFERENCED** in any test

**Problem:** Computed as inverse of cover preference but unused.

**Suggested Use:**
Add a test for "Exposure to Enemy Fire" (inverse of cover) if you want agents with low cover preference to actively seek open positions. Otherwise, remove from C++ code.

---

## Issue 2: Parameter Scaling Mismatch

### ⚠️ AggressionWeight Scaling

**C++ Output:**
```cpp
float AggressionWeight = Params.Aggression * 2.0f; // [0, 2]
```

**Expected in EQS:**
The "Distance to Visible Enemies" test multiplies the distance score by `AggressionWeight`.

**Problem:** EQS score normalization is unclear without seeing the test configuration details.

**Questions to verify:**
1. Does the distance test normalize scores to [0, 1] before applying weight?
2. Is `AggressionWeight` applied multiplicatively or additively?
3. What is the base distance scoring function? (Linear, Inverse, Constant?)

**Recommendation:** Change to [0, 5] range to match other weights:
```cpp
float AggressionWeight = Params.Aggression * 5.0f; // [0, 5]
```

### ⚠️ FormationWeight Constant

**C++ Output:**
```cpp
float FormationWeight = 3.0f; // Constant
```

**EQS Uses:** Score parameter for "Distance to Teammates" test

**Problem:** FormationWeight is constant (3.0), meaning spread behavior is ONLY controlled by `FormationSpread` (the reference distance). This might be intentional, but it means the IMPORTANCE of formation is fixed.

**Possible Issue:**
If an agent has high SpreadDistance (0.9 → 1000cm ideal spread), they may still be penalized heavily for being too far if FormationWeight is high. Consider making FormationWeight inversely proportional to SpreadDistance:

```cpp
// High spread (0.9) → Low weight (1.0) - formation less important
// Low spread (0.1) → High weight (5.0) - formation very important
float FormationWeight = FMath::Lerp(5.0f, 1.0f, Params.SpreadDistance);
```

---

## Issue 3: Missing Test - Cover Trace Configuration

### ⚠️ Trace Test Details Unclear

**User Description:**
```
Trace: to Querier on visibility
  - Mode: Score Only
  - Trace Mode: Geometry by channel
  - Context: EnvQueryContext_Querier
  - Score Parameter: QueryParam.CoverWeight
```

**Questions:**
1. **Trace Direction:** Is it tracing FROM potential position TO querier? Or TO enemies?
2. **Boolean Scoring:** Trace tests are typically binary (hit/no hit). How is this converted to a continuous score?
3. **Score Inversion:** Is a HIT (cover blocked) scored HIGH or LOW?

**Expected Behavior:**
- High `CoverWeight` (4.5) → Strongly prefer positions with cover
- Low `CoverWeight` (0.5) → Weakly prefer cover (almost ignore)

**Potential Issue:**
If the trace test returns binary results (0 or 1), then `CoverWeight` acts as a multiplier:
- Cover position: score = 1.0 * CoverWeight = [0.5, 5.0]
- Exposed position: score = 0.0 * CoverWeight = 0.0

This is correct IF:
- ✅ Trace hits → Score = 1.0 (position has cover)
- ✅ Trace misses → Score = 0.0 (position exposed)

But if it's inverted:
- ❌ Trace hits → Score = 0.0 (blocked line of sight = bad)
- ❌ Trace misses → Score = 1.0 (clear line of sight = good)

**Verification Required:** Check the trace test's "Boolean Match" setting. Should be `true` if you want to prioritize cover.

---

## Recommended Fixes

### Fix 1: Add MinDistanceToEnemy to EQS Asset

**In EQS Asset "Distance to Visible Enemies" test:**
1. Open test properties
2. Set **Normalization Type**: Relative
3. Set **Reference Value**: `QueryParam.MinDistanceToEnemy` (Data Binding: Query Parameter)

**Result:** Positions closer to `MinDistanceToEnemy` get higher scores, scaled by `AggressionWeight`.

**Alternative (Simpler):** Change C++ to use fixed min distance and only vary weight:
```cpp
// Remove MinDistanceToEnemy parameter
// Use AggressionWeight to control scoring intensity only
float AggressionWeight = FMath::Lerp(0.5f, 5.0f, Params.Aggression); // [0.5, 5.0]
```

### Fix 2: Remove Unused ExposureWeight

**In C++ (lines 261, 277):**
```cpp
// DELETE:
float ExposureWeight = (1.0f - Params.CoverPreference) * 3.0f;
QueryRequest.SetFloatParam(TEXT("ExposureWeight"), ExposureWeight);
```

Unless you add an "Exposure" test to EQS, this parameter is dead code.

### Fix 3: Scale AggressionWeight to [0, 5]

**In C++ (line 255):**
```cpp
// CHANGE FROM:
float AggressionWeight = Params.Aggression * 2.0f; // [0, 2]

// TO:
float AggressionWeight = FMath::Lerp(0.5f, 5.0f, Params.Aggression); // [0.5, 5.0]
```

**Rationale:** Matches the scaling of `CoverWeight` (0.5-5.0), making parameter interpretation consistent.

### Fix 4: Make FormationWeight Dynamic (Optional)

**In C++ (line 267):**
```cpp
// CHANGE FROM:
float FormationWeight = 3.0f; // Constant

// TO:
float FormationWeight = FMath::Lerp(5.0f, 1.0f, Params.SpreadDistance); // [5.0, 1.0]
```

**Rationale:**
- Low spread (tight formation) → High weight (5.0) - staying together is critical
- High spread (dispersed) → Low weight (1.0) - spread is more important than staying near ideal distance

---

## Testing Protocol

### Step 1: Verify Parameter Names Match

**Action:** In UE5 Editor, open the EQS asset `EQS_TacticalPositionQuery` and verify each test's "Score Parameter" field matches the C++ parameter names EXACTLY:
- `AggressionWeight` (not `QueryParam.AggressionWeight` - UE5 adds prefix automatically)
- `CoverWeight`
- `FormationWeight`
- `FormationSpread` (used as Reference Value in normalization)

### Step 2: Verify Trace Test Direction

**Action:** Open the "Trace" test in EQS asset and check:
1. **Trace Data:** Should be "Trace FROM Context" (not TO)
2. **Trace To Context:** Should reference enemy perception context
3. **Boolean Match:** Should be `false` (trace miss = cover)
4. **Score Configuration:** Check if it's using "Bool Match" mode or "Distance" mode

### Step 3: Test Parameter Ranges

**Create test scenarios in UE5 PIE:**

**Scenario A: Aggressive Assault**
- Set tactical params: `{Aggression: 0.9, Cover: 0.2, Spread: 0.6, Risk: 0.8}`
- Expected: Agent moves to positions 200-300cm from enemies, minimal cover
- Verify: EQS debug visualization shows close positions with high scores

**Scenario B: Defensive Hold**
- Set tactical params: `{Aggression: 0.1, Cover: 0.9, Spread: 0.3, Risk: 0.2}`
- Expected: Agent moves to positions 900-1000cm from enemies, strong cover
- Verify: EQS debug visualization shows distant, covered positions with high scores

**Scenario C: Dispersed Support**
- Set tactical params: `{Aggression: 0.5, Cover: 0.5, Spread: 0.9, Risk: 0.5}`
- Expected: Agent moves 800-1000cm away from teammates
- Verify: Distance to teammates increases to ~10m

### Step 4: Log EQS Scores

**Enable detailed EQS logging:**
```cpp
// In RunTacticalEQSQuery (line 302), uncomment:
UE_LOG(LogTemp, Log, TEXT("[TACTICAL v8.0] EQS returned %d positions (Aggression=%.2f → MinDist=%.0fcm, Cover=%.2f → Weight=%.1f)"),
    Results.Num(), Params.Aggression, MinDistanceToEnemy, Params.CoverPreference, CoverWeight);
```

**Add per-result scoring (optional):**
```cpp
for (int i = 0; i < FMath::Min(5, ItemCount); ++i)
{
    FVector ItemLocation = QueryResult->GetItemAsLocation(i);
    float ItemScore = QueryResult->GetItemScore(i);
    UE_LOG(LogTemp, Log, TEXT("  [%d] Pos=%s, Score=%.2f"),
        i, *ItemLocation.ToCompactString(), ItemScore);
}
```

---

## Summary

| Issue | Severity | Status | Fix Required |
|-------|----------|--------|--------------|
| MinDistanceToEnemy unused | 🔴 HIGH | ❌ Not used by EQS | Add as Reference Value in distance test OR remove from C++ |
| ExposureWeight unused | 🟡 MEDIUM | ❌ Not used by EQS | Remove from C++ (dead code) |
| AggressionWeight scaling | 🟡 MEDIUM | ⚠️ Inconsistent | Scale to [0.5, 5.0] to match CoverWeight |
| FormationWeight constant | 🟢 LOW | ⚠️ Suboptimal | Make dynamic based on SpreadDistance |
| Trace test direction | 🔴 HIGH | ⚠️ Unverified | Check Boolean Match and trace direction in EQS asset |

**Critical Path:**
1. Fix `MinDistanceToEnemy` usage (either add to EQS or remove from C++)
2. Verify trace test scoring (check if inversion is needed)
3. Apply recommended scaling fixes
4. Test with scenarios A, B, C

**Expected Outcome:** EQS-selected positions should differentiate clearly based on tactical parameters, with logs showing score distributions that match expected behavior.
