# EQS Troubleshooting - Common Issues

## Problem: Only Blue and Red Spheres (No Green), Scores are 0.0 or 1.0

### Diagnosis
Your symptoms indicate:
- ✅ Trace test is working (positions behind walls are red = filtered)
- ❌ No scoring tests configured (all passing positions get score 1.0 = blue)
- ❌ Missing gradient scoring (need tests to differentiate "good" from "best")

### Why This Happens

**EQS Visualization Colors:**
```
Red (0.0)        = Filtered out OR lowest score
Yellow (0.3-0.5) = Low score
Green (0.5-0.8)  = Medium-high score
Blue (1.0)       = Perfect score
```

**When you see only Blue + Red:**
- All positions that pass filters get score 1.0 (blue)
- All positions that fail filters get score 0.0 (red)
- **No gradient** = No scoring tests differentiating positions

### Root Cause: Test Purpose Settings

Each EQS test has a "Test Purpose" dropdown:
- **Filter Only** - Binary pass/fail (contributes to 0.0 or 1.0, no gradient)
- **Score Only** - Adds gradient scoring (creates green/yellow spheres)
- **Filter and Score** - Both filter AND score

**Your current setup (likely):**
```
Simple Grid Generator
  └─ Trace Test (Filter Only) ✅ Working
     ❌ Missing: Distance Test (Score Only)
     ❌ Missing: Dot Product Test (Score Only)
```

---

## Solution: Add Scoring Tests

Open your **EQS_ForwardCover** asset and follow these steps:

### Step 1: Verify Trace Test Configuration

1. Select your **Trace Test** node
2. In Details panel, find "Test Purpose"
3. Set to: **Filter Only** ✅ (Keep this - it removes invalid positions)
4. Keep your current settings (Trace Mode: Geometry by Channel, etc.)

### Step 2: Add Distance Test (To Objective)

1. **Right-click your Generator node** (Simple Grid)
2. Select **Add Test > Distance**
3. Configure in Details panel:

```
Test Purpose: Score Only  ⭐ CRITICAL
Test Mode: Distance 3D
Distance To: EQS_ObjectiveContext  (your custom context)

Scoring Equation: Inverse Linear
  - Clamp Min Type: Normalized
  - Clamp Min: 0.0
  - Clamp Max Type: Normalized
  - Clamp Max: 1.0

Scoring Factor: 1.0

Filter Type: Match (don't filter, just score)
```

**What this does:** Positions closer to objective get higher scores (inverse = closer is better)

### Step 3: Add Distance Test (To Enemies)

1. **Right-click Generator** → Add Test > Distance
2. Configure:

```
Test Purpose: Score Only  ⭐ CRITICAL
Test Mode: Distance 3D
Distance To: EQS_EnemiesContext  (your custom context)

Scoring Equation: Linear
  - Clamp Min Type: Absolute
  - Clamp Min: 500.0  (5m minimum safe distance)
  - Clamp Max Type: Absolute
  - Clamp Max: 1500.0 (15m maximum)

Scoring Factor: 0.8  (slightly less weight than objective distance)

Filter Type: Match
```

**What this does:** Positions at safe distance from enemies (5-15m) get higher scores

### Step 4: Add Dot Product Test (Forward Direction)

1. **Right-click Generator** → Add Test > Dot
2. Configure:

```
Test Purpose: Score Only  ⭐ CRITICAL

Line A:
  - From: Querier
  - To: Item (test position)

Line B:
  - From: Querier
  - To: EQS_ObjectiveContext

Test Mode: Dot 3D
Absolute Value: ❌ Disabled

Scoring Equation: Linear
  - Clamp Min: -1.0
  - Clamp Max: 1.0

Scoring Factor: 0.6

Filter Type: Match
```

**What this does:** Positions in forward direction (toward objective) get higher scores

---

## Expected Result After Fix

After adding these scoring tests, you should see:

### Visualization:
```
    [Red]      [Yellow]    [Green]     [Blue]

    (Behind    (Far from   (Near       (Perfect:
     wall,      objective,  cover,      cover + close
     no LOS)    open)       forward)    + forward)
```

### Score Distribution:
- **Blue (0.9-1.0):** Cover walls closest to objective, in forward direction
- **Green (0.6-0.9):** Cover walls at medium distance or slightly off-angle
- **Yellow (0.3-0.6):** Open areas or far positions
- **Red (0.0-0.3):** Behind walls (no LOS) or very far from objective

### Position Selection:
Your agent should move to the **brightest blue sphere** (highest score)

---

## Step-by-Step Diagnostic Checklist

### 1. Check Test Purpose Settings

Open EQS_ForwardCover → Select each test node → Verify:

| Test Type | Test Purpose | Expected Result |
|-----------|--------------|-----------------|
| Trace Test | Filter Only | Removes positions behind walls |
| Distance (Objective) | Score Only | Closer = higher score |
| Distance (Enemies) | Score Only | Safe distance = higher score |
| Dot Product | Score Only | Forward direction = higher score |

**Common Mistake:** All tests set to "Filter Only" → No gradient scoring

### 2. Verify Context Setup

**If contexts are missing, tests won't work:**

1. Check if `EQS_ObjectiveContext` exists in Content/AI/EQS/
2. Check if `EQS_EnemiesContext` exists
3. Open each context blueprint → Verify logic returns valid locations
4. Test contexts independently:
   - Place "ObjectiveMarker" actor in level
   - Place enemy actors with "Enemy" tag
   - PIE and check console for errors

### 3. Check Generator Settings

**If generator is too small/sparse, you won't see variation:**

Select **Simple Grid** generator → Verify:
```
Grid Size: 1500 units (15m radius)  ✅ Large enough
Space Between: 200 units (2m)       ✅ Dense enough
Generate Around: Querier            ✅ Correct
Project to Navigation: ✅ Enabled   ⭐ Critical for NavMesh
```

**Common Issue:** Grid Size too small (e.g., 500) → Only generates a few positions

### 4. Enable Detailed EQS Debugging

**In PIE, press ` (backtick) and type:**
```
eqs debug [YourPawnName] -details
```

**This shows:**
- Test execution order
- Score contribution per test
- Final scores after normalization
- Why positions were filtered

**Check for:**
- "Test X failed: [reason]" - Test is filtering positions
- "Test X scored: 0.XX" - Test is contributing to gradient
- "Final score: 1.00" for all positions - Missing scoring tests

### 5. Check Test Weights

**If all tests have equal weight, dominant test wins:**

Select each scoring test → Details panel → Scoring section:
```
Scoring Factor:
  - Distance (Objective): 1.0   (highest priority)
  - Distance (Enemies): 0.8     (important but not critical)
  - Dot Product: 0.6            (nice to have)
```

**Normalization Method:** All tests are normalized 0-1, then multiplied by factor

---

## Quick Fix: Import Template Settings

### EQS_ForwardCover (Complete Configuration)

**Generator: Simple Grid**
- Grid Size: 1500
- Space Between: 200
- Generate Around: Querier
- Project to Navigation: ✅

**Test 1: Trace (Filter Only)**
- Test Purpose: **Filter Only**
- Trace Mode: Geometry by Channel
- Trace From Context: Querier
- Item Height Offset: 150
- Context: Querier

**Test 2: Distance to Objective (Score Only)**
- Test Purpose: **Score Only** ⭐
- Distance To: EQS_ObjectiveContext
- Scoring Equation: Inverse Linear
- Scoring Factor: 1.0

**Test 3: Distance to Enemies (Score Only)**
- Test Purpose: **Score Only** ⭐
- Distance To: EQS_EnemiesContext
- Scoring Equation: Linear
- Clamp Min: 500 (Absolute)
- Scoring Factor: 0.8

**Test 4: Dot Product (Score Only)**
- Test Purpose: **Score Only** ⭐
- Line A: Querier → Item
- Line B: Querier → EQS_ObjectiveContext
- Test Mode: Dot 3D
- Scoring Factor: 0.6

---

## Common Mistakes Summary

| Symptom | Cause | Fix |
|---------|-------|-----|
| Only blue/red, no green | All tests "Filter Only" | Change Distance/Dot to "Score Only" |
| All positions blue (1.0) | No scoring tests at all | Add Distance and Dot tests |
| No positions returned | Filter too strict | Temporarily disable Trace test |
| Positions behind walls | Trace test not configured | Add Trace test (Filter Only) |
| Positions far from objective | Distance test wrong direction | Use "Inverse Linear" (closer = better) |
| Symmetrical scoring | No directional test | Add Dot Product test (forward bias) |

---

## Verification Steps After Fix

1. **Open EQS_ForwardCover** in editor
2. **Count tests:** Should have 4 tests (1 filter + 3 scoring)
3. **PIE with EQS Testing Pawn**
4. **Expected:**
   - Mix of colors (blue, green, yellow, red)
   - Brightest spheres near cover walls toward objective
   - Gradual color transition (not binary)
5. **Check logs:**
   ```
   LogEQS: Query 'EQS_ForwardCover' returned 12 positions
   LogEQS: Best score: 0.87 at location (X=450.0, Y=320.0)
   ```

---

## Advanced: Test Score Debugging

**To see individual test contributions:**

1. PIE with EQS Testing Pawn
2. Press ' (apostrophe) → Open Gameplay Debugger
3. Navigate to EQS category (press 3)
4. Select a test position (click sphere in viewport)
5. Details panel shows:
   ```
   Test 1 (Trace): PASSED (1.0)
   Test 2 (Distance Obj): 0.75
   Test 3 (Distance Enemy): 0.60
   Test 4 (Dot): 0.45
   ---
   Normalized Final: 0.68 (Green)
   ```

**This tells you:**
- Which tests are passing/failing
- How much each test contributes to final score
- Why one position is better than another

---

## Next Steps

1. **Add the 3 scoring tests** to EQS_ForwardCover (Distance x2, Dot x1)
2. **Set all scoring tests to "Score Only"** (critical!)
3. **PIE and verify color gradient** (should see blue/green/yellow/red mix)
4. **Repeat for other 4 queries** (EQS_Retreat, FlankLeft, FlankRight, Advance)
5. **Test with actual FollowerAgent** once visual debugging passes

**Your Trace test is working correctly - you just need to add the scoring layer!**
