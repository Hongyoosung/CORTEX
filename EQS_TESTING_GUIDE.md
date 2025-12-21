# EQS Asset Testing Guide

**Purpose:** Verify your 5 EQS queries work correctly BEFORE integrating with RLlib training.

**Date:** 2025-12-18

---

## Method 1: Using EQS Testing Pawn (Recommended)

### Step 1: Enable EQS Testing Pawn in Editor

1. **Place EQS Testing Pawn in Level:**
   - Open your test level (or create a new one)
   - In Modes panel (Window > Modes), search for "EQS Testing Pawn"
   - Drag it into your level
   - This pawn is specifically designed to test EQS queries visually

2. **Configure the Testing Pawn:**
   - Select the EQS Testing Pawn in the level
   - In Details panel, find "EQS" section
   - Set "Query Template" to one of your queries (e.g., EQS_ForwardCover)
   - Enable "Should be visible in game"
   - Set "Step to Debug Draw": -1 (shows all steps)

### Step 2: Set Up Test Environment

For each query type, you need different environmental setups:

#### Test Setup for EQS_ForwardCover

**Environment Requirements:**
```
[Querier]-----(5-10m)-----[Cover Walls]-----(10-15m)-----[Objective]

                          [Enemy Actors]
                          (placed between querier and objective)
```

**Setup Steps:**
1. Place EQS Testing Pawn at world origin (0, 0, 0)
2. Place static mesh cubes (200x200x150 units) as cover walls:
   - 500-1000 units in front of the testing pawn
   - Create multiple cover options (3-5 walls)
3. Place an actor with tag "Objective" at 1500 units forward
4. Place dummy enemy actors (with tag "Enemy") at 800-1200 units forward

**Expected Visualization:**
- **Green spheres:** Should cluster around cover walls BETWEEN pawn and objective
- **Red spheres:** Should appear in open areas without cover
- **Yellow lines:** Show trace tests checking for cover
- **Best position (brightest green):** Closest cover wall toward objective

---

#### Test Setup for EQS_RetreatCover

**Environment Requirements:**
```
[Objective]-----(10m)-----[Querier]-----(5-10m)-----[Cover Walls]

[Enemy Actors]                                    (safe area)
(in front of querier)
```

**Setup Steps:**
1. Place EQS Testing Pawn at (0, 0, 0)
2. Place enemy actors at -500 to -800 units (in front/negative Y)
3. Place cover walls at +500 to +1000 units (behind pawn)
4. Place objective at -1500 units (same direction as enemies)

**Expected Visualization:**
- **Green spheres:** Should cluster BEHIND the pawn (away from enemies)
- **Red spheres:** Positions closer to enemies
- **Best position:** Farthest cover wall from enemies with good protection

---

#### Test Setup for EQS_Advance

**Environment Requirements:**
```
[Querier]-----(no cover)----->[Objective]

                    [Enemy Actors]
                    (optional)
```

**Setup Steps:**
1. Place EQS Testing Pawn at (0, 0, 0)
2. Face pawn toward +X
3. Place objective at (800, 0, 0)
4. NO cover walls needed (aggressive advance)
5. Optional: Place enemies at (400, 0, 0)

**Expected Visualization:**
- **Green spheres:** Form forward arc toward objective (120-degree cone)
- **No cover filtering:** Positions in open areas are valid
- **Best position:** Closest point toward objective within arc

---

## Method 2: Runtime Testing with Your FollowerAgent

### Step 1: Create Test Level

1. **Level Setup:**
   - Create simple test geometry (floor, walls, cover objects)
   - Place NavMesh Bounds Volume (covers entire playable area)
   - Press 'P' to visualize NavMesh (should be green)

2. **Place Your Agent:**
   - Spawn your FollowerAgent pawn with TacticalActuator component
   - Ensure it has StateTree asset assigned
   - Place at known starting position

3. **Configure Contexts:**
   - Verify EQS_ObjectiveContext Blueprint returns valid location
   - Verify EQS_EnemiesContext returns SharedContext.VisibleEnemies
   - Test contexts independently if possible

### Step 2: Enable EQS Debugging

**In PIE (Play In Editor):**

1. Press **`** (backtick key) to open console
2. Type: `eqs` and press Enter to see EQS commands
3. Enable debugging:
   ```
   eqs debug [YourAgentName]
   ```
   Replace `[YourAgentName]` with your pawn's name in the World Outliner

**Alternative: Use Gameplay Debugger**
1. Press **'** (apostrophe key) in PIE
2. Navigate to EQS category (press 3)
3. Visual overlay will show query results

### Step 3: Manual Query Testing

**Test each tactical position manually:**

1. **Method A: Console Commands (if you've exposed functions)**
   ```
   ExecuteMacroAction 1 0 0 0 0  // TacticalPos=ForwardCover
   ExecuteMacroAction 2 0 0 0 0  // TacticalPos=Retreat
   ExecuteMacroAction 5 0 0 0 0  // TacticalPos=Advance
   ```

2. **Method B: Behavior Tree Test**
   - Create simple test Behavior Tree
   - Add your STTask_ExecuteObjective task
   - Manually set macro action values in blackboard
   - Run tree and observe movement

### Step 4: Check Logs

**Enable verbose logging in DefaultEngine.ini:**
```ini
[Core.Log]
LogStateTree=Verbose
LogAINavigation=Verbose
LogEQS=Verbose
```

**Expected Log Output (Success):**
```
LogStateTree: [ExecuteObjective] Executing TacticalPosition: ForwardCover
LogEQS: Running EQS Query: EQS_ForwardCover
LogEQS: Query returned 12 positions
LogStateTree: Moving to EQS position: X=450.0 Y=320.0 Z=100.0
```

**Error Log Output (Missing Asset):**
```
LogStateTree: Error: Failed to load EQS asset: /Game/AI/EQS/EQS_ForwardCover
LogStateTree: [ExecuteObjective] No valid EQS positions found for ForwardCover
```

---

## Verification Checklist

For each EQS query, verify:

### EQS_ForwardCover
- [ ] Returns 3-10 positions (depends on environment)
- [ ] Green spheres cluster near cover objects
- [ ] Positions are BETWEEN querier and objective
- [ ] Best position (brightest) is closest cover toward objective
- [ ] No positions in wide-open areas (filtered by Trace test)

### EQS_RetreatCover
- [ ] Green spheres appear BEHIND querier (away from enemies)
- [ ] Positions have cover (not in open)
- [ ] Best position is farthest from enemies
- [ ] Dot product test ensures backward movement

### EQS_Advance
- [ ] Positions form forward arc (120 degrees)
- [ ] NO cover filtering (accepts open positions)
- [ ] Best position is closest to objective within arc
- [ ] Radius smaller than other queries (~800 units)

---

## Common Issues & Fixes

| Issue | Symptom | Solution |
|-------|---------|----------|
| **No green spheres** | All spheres red or none visible | Check generator settings (Grid Size, Circle Radius) - may be too small |
| **Query returns empty** | Log: "No valid results" | Filter tests too strict - disable PathFinding test temporarily |
| **Wrong direction** | Retreat goes forward, flank goes backward | Check Dot Product test line directions and context setup |
| **Context errors** | Log: "Failed to get context" | Verify EQS_ObjectiveContext and EQS_EnemiesContext return valid data |
| **Agent doesn't move** | Query succeeds but no movement | Check STTask_ExecuteObjective MoveToLocation call (line 247-252) |
| **Crashes on query** | Editor crashes when query runs | Verify all contexts return valid actors/locations (null checks) |

---

## Advanced: Debugging Specific Tests

### To isolate which test is failing:

1. Open EQS query asset in editor
2. Disable tests one by one (uncheck "Enabled" in Details panel)
3. Run query again with EQS Testing Pawn
4. Re-enable tests gradually to find culprit

### To visualize test contributions:

1. Select EQS Testing Pawn in PIE
2. Open Details panel
3. Find "Query Params" section
4. Check "Draw Failed Items" - shows why positions were filtered
5. Adjust "Step to Debug Draw" to see individual test results

---

## Performance Validation

After testing correctness, verify performance:

1. **Query Execution Time:**
   - Should be < 5ms for instant queries
   - Check log: `LogEQS: Query completed in 2.34ms`

2. **Position Count:**
   - Optimal: 5-15 positions per query
   - Too few (< 3): Increase generator density
   - Too many (> 30): Increase Space Between or add filter tests

3. **NavMesh Validation:**
   - All returned positions should be on NavMesh (green)
   - If positions float or sink, check generator's "Project to NavMesh" setting

---

## Next Steps After Validation

Once all 5 queries pass these tests:

1. **Integration Test:** Run your FollowerAgent with all queries in sequence
2. **Multi-Agent Test:** Spawn 2-3 agents and verify queries don't interfere
3. **Python RLlib Test:** Run `train_rllib.py` and verify MultiDiscrete actions trigger correct queries
4. **Training:** Begin full training session and monitor for:
   - No EQS errors in logs
   - Agents move to sensible positions
   - Tactical behavior emerges (not random movement)

---

## Quick Test Procedure (5 Minutes)

1. Place EQS Testing Pawn in empty level
2. Set Query Template = EQS_ForwardCover
3. Place 3 cube walls in front (500 units away)
4. PIE and check for green spheres near walls
5. Repeat for other 4 queries with appropriate environment
6. If all show green spheres in expected locations → Ready for integration!

**Done? Your EQS assets are production-ready for v4.0 RLlib training!**
