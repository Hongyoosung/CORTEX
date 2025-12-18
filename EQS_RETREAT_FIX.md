# EQS_RetreatCover Configuration Fix

## Problem Summary
Current behavior: Agent moves toward enemy behind cover instead of retreating away from enemy.

**Root Cause:** Cover detection (Trace test) has higher effective weight than retreat direction (Dot Product test).

---

## Solution: Rebalance Test Weights

Open **Content/Game/Blueprints/AI/EQS/EQS_RetreatCover.uasset** and reconfigure tests with these exact settings:

### Test 1: Dot Product (HIGHEST PRIORITY - Retreat Direction)

**Purpose:** Ensure positions are BEHIND the querier, away from enemies

```
Test Purpose: Score Only ⭐ (DO NOT use Filter and Score)
Test Mode: Dot 3D
Absolute Value: ❌ DISABLED (critical!)

Line A:
  - From: Querier
  - To: Item (the test position)

Line B:
  - From: Querier
  - To: VisibleEnemies (your context)

Scoring Equation: Inverse Linear
  - Why: Negative dot product (backward) should score high
  - Clamp Min Type: Normalized (-1.0)
  - Clamp Max Type: Normalized (1.0)
  - Normalization Type: Relative to Min/Max

Scoring Factor: 2.0 ⭐⭐⭐ CRITICAL - Highest weight
  - This makes retreat direction 2x more important than other factors

Filter Type: Match (don't filter, just score)
```

**Expected behavior:**
- Dot product = -1.0 (directly behind querier, away from enemy) → Score = 1.0 × 2.0 = 2.0
- Dot product = 0.0 (perpendicular) → Score = 0.5 × 2.0 = 1.0
- Dot product = 1.0 (toward enemy) → Score = 0.0 × 2.0 = 0.0

---

### Test 2: Distance to VisibleEnemies (Secondary Priority)

**Purpose:** Prefer farther positions from enemies

```
Test Purpose: Score Only
Test Mode: Distance 3D
Distance To: VisibleEnemies (your context)

Scoring Equation: Linear
  - Clamp Min Type: Absolute
  - Clamp Min: 500.0 (5m minimum retreat distance)
  - Clamp Max Type: Absolute
  - Clamp Max: 2000.0 (20m maximum - beyond this all equal)

Scoring Factor: 1.0 ⭐⭐ Medium weight

Filter Type: Match
```

**Expected behavior:**
- Distance = 500 units → Score = 0.0
- Distance = 1250 units → Score = 0.5
- Distance = 2000+ units → Score = 1.0

---

### Test 3: Trace (Cover Quality - Lowest Priority)

**Purpose:** Prefer positions with cover, but ONLY if they're in retreat direction

```
Test Purpose: Score Only ⭐ (Change from "Filter Only"!)
Test Mode: Visibility - Trace to VisibleEnemies
Trace Mode: Geometry by Channel
Trace From Context: Item (the test position)
Item Height Offset: 150.0 (eye level)
Context Height Offset: 150.0

Bool Match: False ⭐ CRITICAL
  - This means: NO line of sight to enemy = High score (behind cover)
  - Has line of sight to enemy = Low score (exposed)

Scoring Factor: 0.5 ⭐ Lowest weight
  - Cover is nice-to-have, not required

Filter Type: Match (don't filter)
```

**Expected behavior:**
- Behind solid wall (no LOS to enemy) → Score = 1.0 × 0.5 = 0.5
- Exposed to enemy (has LOS) → Score = 0.0 × 0.5 = 0.0

---

### Test 4: PathExist (Keep as Filter Only)

**Purpose:** Remove unreachable positions

```
Test Purpose: Filter Only ⭐ Keep this
Path From Context: Querier
Filter Type: Match
```

**This removes positions with no valid path - keep as-is.**

---

## Test Execution Order (Important!)

EQS executes tests in the order they appear. Optimal order:

1. **PathExist** (Filter Only) - Remove unreachable positions first
2. **Dot Product** (Score Only, weight 2.0) - Prioritize retreat direction
3. **Distance** (Score Only, weight 1.0) - Prefer farther positions
4. **Trace** (Score Only, weight 0.5) - Bonus for cover

**To reorder tests in Unreal:**
- Select test node → Details panel → "Test Order" property
- Or drag/drop tests in the graph

---

## Why This Works

### Score Calculation Example

**Scenario:** Wall in front (toward enemy) and wall behind (retreat direction)

**Position A: Behind front wall (toward enemy)**
```
Dot Product: 0.8 (toward enemy) → Score = 0.1 × 2.0 = 0.2
Distance: 400 units → Score = 0.0 × 1.0 = 0.0
Trace: Behind wall (no LOS) → Score = 1.0 × 0.5 = 0.5
---
Final Score: 0.2 + 0.0 + 0.5 = 0.7
```

**Position B: Behind rear wall (retreating away)**
```
Dot Product: -0.9 (away from enemy) → Score = 0.95 × 2.0 = 1.9
Distance: 1200 units → Score = 0.47 × 1.0 = 0.47
Trace: Behind wall (no LOS) → Score = 1.0 × 0.5 = 0.5
---
Final Score: 1.9 + 0.47 + 0.5 = 2.87 ⭐ MUCH HIGHER
```

**Position C: Open area behind (retreating, no cover)**
```
Dot Product: -0.8 (away from enemy) → Score = 0.9 × 2.0 = 1.8
Distance: 1000 units → Score = 0.33 × 1.0 = 0.33
Trace: Exposed (has LOS) → Score = 0.0 × 0.5 = 0.0
---
Final Score: 1.8 + 0.33 + 0.0 = 2.13 (Still better than Position A!)
```

**Result:** Agent prioritizes retreat direction even without cover, but gets bonus if cover exists in retreat path.

---

## Critical Configuration Checklist

### ❌ Common Mistakes to Avoid

1. **Dot Product Test:**
   - ❌ Using "Filter and Score" → This filters out retreat positions!
   - ❌ "Absolute Value" enabled → Loses directional information
   - ❌ Scoring Factor too low (< 1.5) → Cover overrides retreat direction
   - ✅ Must use "Score Only" with factor 2.0+

2. **Trace Test:**
   - ❌ Using "Filter Only" → Removes ALL positions without cover
   - ❌ Bool Match = True → Rewards exposed positions (opposite of cover)
   - ✅ Must use "Score Only" with low weight (0.5)

3. **Distance Test:**
   - ❌ Using "Inverse Linear" → Prefers closer to enemies (wrong!)
   - ✅ Must use "Linear" (farther = better)

---

## Testing the Fix

### Visual Validation with EQS Testing Pawn

**Setup:**
```
[Enemy]----[Front Wall]----[Querier]----[Rear Wall]
   ↑            ↑             ↑            ↑
 -1500        -800            0           +800
```

**Expected Results:**

1. **Brightest spheres (blue/cyan):** Behind rear wall (+600 to +1000 range)
   - High dot product score (retreating)
   - High distance score (far from enemy)
   - High trace score (behind cover)

2. **Medium spheres (green):** Open areas behind querier (+400 to +600)
   - High dot product score (retreating)
   - Medium distance score
   - Low trace score (no cover) ← But still preferred over Position 3!

3. **Dim spheres (yellow/orange):** Behind front wall (-600 to -800)
   - Low dot product score (toward enemy)
   - Low distance score (close to enemy)
   - High trace score (behind cover) ← Not enough to compensate!

4. **Red spheres:** In front of querier (-400 to 0) or perpendicular
   - Very low/zero scores

### Log Validation

Enable verbose logging:
```ini
# Config/DefaultEngine.ini
[Core.Log]
LogEQS=Verbose
```

**Expected output:**
```
LogEQS: Running Query 'EQS_RetreatCover'
LogEQS: Test 'Dot Product' scored 15 items (avg: 0.75, factor: 2.0)
LogEQS: Test 'Distance' scored 15 items (avg: 0.45, factor: 1.0)
LogEQS: Test 'Trace' scored 15 items (avg: 0.30, factor: 0.5)
LogEQS: Best position: X=250.0, Y=-850.0, Z=100.0 (score: 2.65)
```

**Verify:**
- Best position has NEGATIVE Y (behind querier, away from enemy)
- Score > 2.0 (indicates high dot product contribution)

---

## Alternative Approach: Add Distance to Self Test

If you still see issues, add this test BEFORE the Dot Product test:

### Optional Test: Distance from Querier

**Purpose:** Prefer positions farther from current location (forces retreat)

```
Test Purpose: Score Only
Test Mode: Distance 3D
Distance To: Querier

Scoring Equation: Linear
  - Clamp Min: 200.0 (minimum retreat distance)
  - Clamp Max: 1000.0 (maximum retreat in one step)

Scoring Factor: 0.8

Filter Type: Minimum Threshold
  - Float Value Min: 200.0 ⭐ Filter positions too close to current location
```

**This ensures agent moves at least 2m away from current position.**

---

## Summary of Changes

| Test | Old Config | New Config | Reason |
|------|------------|------------|--------|
| **Dot Product** | Weight ~1.0, maybe Filter+Score | Weight 2.0, Score Only, Inverse Linear | Make retreat direction PRIMARY factor |
| **Distance to Enemies** | Weight ~1.0 | Weight 1.0 (keep) | Secondary factor |
| **Trace (Cover)** | Filter Only | Score Only, Weight 0.5 | Tertiary factor - don't eliminate non-cover positions |
| **PathExist** | Filter Only (keep) | Filter Only (keep) | Remove unreachable only |

**Key principle:** Direction > Distance > Cover

---

## After Applying Fix

1. Open **EQS_RetreatCover** in Unreal Editor
2. Apply all test configuration changes above
3. **Save asset**
4. Place EQS Testing Pawn in level with the enemy-wall-querier-wall setup
5. PIE and verify brightest spheres are BEHIND querier
6. Check logs to confirm best position has negative dot product and high score

**Expected behavior:** Agent always retreats away from enemy, preferring cover if available in that direction, but not sacrificing retreat direction for cover.
