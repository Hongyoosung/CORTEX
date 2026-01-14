# Unreal Editor Tasks for v8.0 Integration

**Status:** 🔴 REQUIRED - Code is complete, but Editor setup needed
**Estimated Time:** ~1-2 hours
**Priority:** P0 - Blocking for v8.0 testing

---

## 📋 Task Checklist

### ✅ Task 1: Create EQS Query Asset (30-45 minutes)

**Why:** The new v8.0 tactical movement system needs an EQS query with RL-controlled parameters.

**Steps:**

1. **Open Unreal Editor** on the CORTEX project

2. **Navigate to Content Browser:**
   - Go to: `Content/AI/EQS/` (create folder if doesn't exist)

3. **Create New EQS Asset:**
   - Right-click in folder → **Environment Query** → **New Environment Query**
   - Name it: `TacticalPositionQuery`

4. **Open the EQS asset** (double-click)

5. **Add Parameters (Critical - must match code):**
   Click "Add Parameter" button 6 times and configure:

   | # | Parameter Name | Type | Default Value | Notes |
   |---|----------------|------|---------------|-------|
   | 1 | `MinDistanceToEnemy` | Float | 600.0 | Min safe distance (cm) |
   | 2 | `AggressionWeight` | Float | 1.0 | Scoring weight for aggressive positions |
   | 3 | `CoverWeight` | Float | 2.5 | Scoring weight for cover |
   | 4 | `ExposureWeight` | Float | 1.5 | Scoring weight for open positions |
   | 5 | `FormationSpread` | Float | 600.0 | Ideal team spacing (cm) |
   | 6 | `FormationWeight` | Float | 3.0 | Scoring weight for formation |

   **⚠️ CRITICAL:** Parameter names must match exactly (case-sensitive)!

6. **Configure Generator:**
   - Click "Add Generator" → **Points: Circle**
   - Settings:
     - Circle Center: `Querier`
     - Circle Radius: `1500.0` (15m search radius)
     - Points on Circle Percentage: `1.0` (full circle)
     - Number of Points: `12` (good balance of coverage vs performance)
     - Project Down: `1000.0` (snap to ground)
     - Trace Data: `Navigation`

7. **Add Query Tests (4 tests):**

   **Test 1: Distance to Enemies**
   - Add Test → **Distance**
   - Settings:
     - Distance To: `All Actors of Class`
     - Test Purpose: `Filter Max` (avoid enemies)
     - Filter Type: `Minimum`
     - Float Value Param: `MinDistanceToEnemy` (use parameter!)
     - Scoring Equation: `Constant` (weight from `AggressionWeight`)
     - Scoring Factor: `1.0`
     - Weight: **Use Parameter** → `AggressionWeight`

   **Test 2: Cover Detection**
   - Add Test → **Trace**
   - Settings:
     - Trace Data: `Visibility`
     - Trace Mode: `Discard Hit`
     - Test Purpose: `Score`
     - Scoring Equation: `Linear` (more cover = higher score)
     - Scoring Factor: `1.0`
     - Weight: **Use Parameter** → `CoverWeight`

   **Test 3: Team Formation**
   - Add Test → **Distance**
   - Settings:
     - Distance To: `All Actors of Class` (teammates)
     - Test Purpose: `Score`
     - Scoring Equation: `InverseLinear` (ideal distance scoring)
     - Normalization Type: `Absolute`
     - Reference Value: **Use Parameter** → `FormationSpread`
     - Weight: **Use Parameter** → `FormationWeight`

   **Test 4: NavMesh Pathfinding**
   - Add Test → **Pathfinding**
   - Settings:
     - Test Purpose: `Filter Only` (discard unreachable positions)
     - Filter Type: `Match`
     - Pathfinding Mode: `Regular`
     - Use Hierarchical Pathfinding: `True`

8. **Save and Compile** the EQS asset

9. **Test the Query:**
   - Create test actor in level
   - Add `UEnvQueryManager::RunEQSQuery()` call in Blueprint
   - Verify it returns valid positions

---

### ✅ Task 2: Update StateTree Asset (15-20 minutes)

**Why:** Replace v7.0 movement task with v8.0 tactical movement task.

**Steps:**

1. **Locate StateTree Asset:**
   - Navigate to: `Content/AI/StateTree/`
   - Find: `FollowerStateTree` (or your custom StateTree asset)

2. **Open StateTree Editor** (double-click asset)

3. **Find Movement State:**
   - Look for state named "ExecuteMission" or "Movement" or similar
   - This state should currently contain `STTask_ExecuteMovement` (v7.0)

4. **Replace Task:**
   - Select `STTask_ExecuteMovement` task node
   - Press Delete to remove
   - Right-click → **Add Task** → Search: `Execute Tactical Movement v8.0`
   - Select `STTask_ExecuteTacticalMovement_v8`

5. **Configure Task Parameters:**
   In the Details panel for the new task:

   | Parameter | Binding/Value | Notes |
   |-----------|---------------|-------|
   | `StateTreeComp` | **Bind** → `FollowerStateTreeComponent` | Context binding |
   | `AgentComponent` | **Bind** → `FollowerAgentComponent` | Context binding |
   | `ControlledPawn` | **Bind** → `Pawn` | Context binding |
   | `AIController` | **Bind** → `AIController` | Context binding |
   | `TacticalPositionQuery` | **Select Asset** → `TacticalPositionQuery` | EQS asset from Task 1 |
   | `UpdateFrequencyHz` | **5.0** | 5 Hz (recommended) |
   | `MovementSpeedMultiplier` | **1.0** | Default speed |

6. **Verify Context Bindings:**
   - Ensure all bindings show component names (not "None")
   - If "None", click binding dropdown and select correct component

7. **Save StateTree Asset**

8. **Compile StateTree:**
   - Click "Compile" button in toolbar
   - Verify no errors in Output Log

---

### ✅ Task 3: Verify Actor Setup (10-15 minutes)

**Why:** Ensure all agents have required components.

**Steps:**

1. **Open Agent Blueprint:**
   - Navigate to: `Content/Characters/` or wherever AI agent blueprint is
   - Example: `BP_GameAICharacter` or `BP_FollowerAgent`

2. **Verify Components Exist:**
   In Components panel, ensure these exist:

   - ✅ `FollowerAgentComponent` (v8.0 combat + tactical params)
   - ✅ `FollowerStateTreeComponent` (executes StateTree)
   - ✅ `AgentPerceptionComponent` (enemy detection)
   - ✅ `HealthComponent` (combat integration)
   - ✅ `WeaponComponent` (firing system)
   - ✅ `CharacterMovementComponent` (UE5 default)

   **If any missing:** Add Component → Search component name

3. **Configure FollowerAgentComponent:**
   - Select component in hierarchy
   - In Details panel:
     - `TeamLeaderActor`: Set to team leader reference (or use `TeamLeaderTag`)
     - `bUseRLPolicy`: **True** (even without trained model, uses fallback)
     - `bCollectExperiences`: **True** (for future training)
     - `bEnableDebugDrawing`: **True** (for testing, disable in production)

4. **Configure StateTree Component:**
   - Select component
   - Details panel:
     - `State Tree Asset`: Select `FollowerStateTree` (updated in Task 2)
     - `Start Logic Automatically`: **True**

5. **Save Blueprint**

6. **Compile Blueprint**

---

### ✅ Task 4: Test in PIE (Play In Editor) (20-30 minutes)

**Why:** Validate v8.0 system works before running full simulations.

**Steps:**

1. **Create Simple Test Level:**
   - Place 1 friendly agent (with FollowerAgentComponent)
   - Place 1 enemy agent (for combat testing)
   - Ensure NavMesh covers the area (Build → Build Paths)

2. **Enable Debug Visualization:**
   - Select friendly agent in World Outliner
   - Set `bEnableDebugDrawing = True` on FollowerAgentComponent

3. **Play in Editor (PIE):**
   - Press Play button
   - Watch Output Log for v8.0 messages

4. **Verify Movement:**
   Look for logs:
   ```
   [TACTICAL v8.0] 'Agent0': Entered tactical movement state
   [TACTICAL v8.0] 'Agent0': Updated tactical params - Aggression=0.50, Cover=0.50, Spread=0.50, Risk=0.50
   [TACTICAL v8.0] EQS returned 12 positions (Aggression=0.50 → MinDist=600cm, Cover=0.50 → Weight=2.5)
   [TACTICAL v8.0] 'Agent0': Moving to tactical position (X=..., Y=..., Z=...)
   ```

   **Expected Behavior:**
   - Agent should move every ~200ms (5 Hz updates)
   - Movement should be smooth (NavMesh pathfinding)
   - Debug sphere shows strategy color (Red=Assault, Blue=Defend, etc.)

5. **Verify Combat:**
   Move enemy within perception range, look for logs:
   ```
   [COMBAT v8.0] 'Agent0': Targeting Enemy0 (Priority: Closest)
   ```

   **Expected Behavior:**
   - Agent should aim at enemy (SetFocus)
   - Weapon should fire if has LOS (STTask_ExecuteFire)
   - Target should switch if enemy HP drops (LowestHP priority)

6. **Check for Errors:**
   Common errors to watch for:
   - `[TACTICAL v8.0] TacticalPositionQuery not assigned` → Go back to Task 2
   - `[TACTICAL v8.0] EQS returned 0 positions` → Check NavMesh, EQS query settings
   - `StateTreeComp is null` → Check context bindings in Task 2

7. **Performance Check:**
   - Open "Stat Unit" console command
   - Frame time should be <16.6ms (60 FPS)
   - Game thread should be <10ms

---

### ✅ Task 5: Profile with Unreal Insights (30-45 minutes)

**Why:** Validate performance targets (<20ms/sec inference latency).

**Steps:**

1. **Start Unreal Insights:**
   - Close Unreal Editor
   - Run: `UnrealEditor.exe CORTEX -trace=cpu,frame,log -tracehost=127.0.0.1`
   - This launches editor with profiling enabled

2. **Start Recording:**
   - Unreal Insights window should open
   - Click "Start Recording"

3. **Run Test Scenario:**
   - Play in Editor (4v4 scenario recommended)
   - Let run for 60 seconds
   - Perform various actions (movement, combat, strategy switches)

4. **Stop Recording:**
   - Stop PIE
   - Stop recording in Insights
   - Save trace file

5. **Analyze Trace:**
   - Open trace in Unreal Insights
   - Navigate to "Timing View"

6. **Check Key Metrics:**

   **FollowerAgentComponent::TickComponent:**
   - Filter timeline by "FollowerAgentComponent"
   - Expected: 2-5ms per agent per tick
   - Red flag: >10ms (indicates performance issue)

   **RLPolicyNetwork::GetMacroAction:**
   - Should show ~20 calls/second (4 agents × 5 Hz)
   - Expected: 2-4ms per call (batched)
   - Red flag: >10ms (check batching is working)

   **EnvQueryManager::RunInstantQuery:**
   - Should show ~20 calls/second (same as RL)
   - Expected: <2ms per call
   - Red flag: >5ms (simplify EQS query, reduce tests)

   **ExecuteCombat:**
   - Should show 60 calls/second per agent (every tick)
   - Expected: <0.5ms per call
   - Red flag: >1ms (too many enemies detected?)

7. **Generate Report:**
   - Screenshot flame graph showing AI frame breakdown
   - Export CSV of timing data
   - Document any performance issues

---

## 🎯 Success Criteria

After completing all tasks, you should see:

### Functional:
- ✅ Agent moves to tactical positions (EQS working)
- ✅ Tactical parameters update every ~200ms (5 Hz)
- ✅ Agent targets enemies (closest or lowestHP)
- ✅ Weapon fires when has LOS
- ✅ Strategy switches dynamically (health drops, new enemies)

### Performance:
- ✅ 60 FPS stable (frame time <16.6ms)
- ✅ AI frame <10ms (FollowerAgentComponent tick)
- ✅ Inference latency <20ms/sec (4 agents batched)
- ✅ EQS query <2ms per call
- ✅ Combat execution <0.5ms per call

### Logs (No Errors):
```
✅ [TACTICAL v8.0] 'Agent0': Entered tactical movement state
✅ [TACTICAL v8.0] 'Agent0': Updated tactical params - Aggression=0.XX, Cover=0.XX, ...
✅ [TACTICAL v8.0] EQS returned 12 positions (Aggression=0.XX → MinDist=XXXcm, ...)
✅ [TACTICAL v8.0] 'Agent0': Moving to tactical position (X=..., Y=..., Z=...)
✅ [COMBAT v8.0] 'Agent0': Targeting Enemy0 (Priority: Closest)

❌ [TACTICAL v8.0] StateTreeComp is null
❌ [TACTICAL v8.0] TacticalPositionQuery not assigned
❌ [TACTICAL v8.0] EQS returned 0 positions
```

---

## 🚨 Common Issues & Solutions

### Issue 1: "TacticalPositionQuery not assigned"

**Symptom:**
```
[TACTICAL v8.0] TacticalPositionQuery not assigned
```

**Cause:** Task 2 incomplete - EQS asset not linked in StateTree

**Solution:**
1. Open StateTree asset
2. Select `STTask_ExecuteTacticalMovement_v8` node
3. In Details → `TacticalPositionQuery` → Select `TacticalPositionQuery` asset
4. Save and recompile StateTree

### Issue 2: "EQS returned 0 positions"

**Symptom:**
```
[TACTICAL v8.0] 'Agent0': EQS returned 0 positions, holding position
```

**Causes:**
1. NavMesh not built or too small
2. EQS generator radius too small
3. EQS tests too strict (filtering out all positions)

**Solutions:**
1. Build → Build Paths (rebuild NavMesh)
2. Increase generator radius to 2000cm
3. Check EQS test weights (some might be zero)
4. Temporarily disable filter tests to isolate issue

### Issue 3: "StateTreeComp is null"

**Symptom:**
```
[TACTICAL v8.0] StateTreeComp is null
```

**Cause:** Context binding incorrect in Task 2

**Solution:**
1. Open StateTree asset
2. Select `STTask_ExecuteTacticalMovement_v8` node
3. Details → `StateTreeComp` → Click binding dropdown
4. Select `FollowerStateTreeComponent` from context
5. Ensure it shows "FollowerStateTreeComponent" not "None"

### Issue 4: Agent doesn't move

**Symptoms:** Agent stands still, no EQS queries

**Causes:**
1. StateTree not starting
2. Agent not in correct state (e.g., stuck in "Dead" state)
3. FollowerAgentComponent not ticking

**Solutions:**
1. Check `bStartLogicAutomatically = True` on StateTree component
2. Verify agent is alive: `bIsAlive = True`
3. Check `PrimaryComponentTick.bCanEverTick = True`
4. Add breakpoint in `STTask_ExecuteTacticalMovement_v8::Tick()` to verify execution

### Issue 5: Performance degradation

**Symptoms:** Frame rate drops below 60 FPS

**Causes:**
1. EQS query too complex (too many tests, high generator density)
2. RL inference not batched (each agent runs separately)
3. Debug drawing enabled in production

**Solutions:**
1. Reduce EQS generator points (12 → 8)
2. Verify batching: Check `RLPolicyNetwork::GetMacroActionBatched()` is called
3. Disable `bEnableDebugDrawing` on all agents
4. Lower update frequency (5 Hz → 2 Hz)

---

## 📊 Validation Checklist

Before marking Week 2 as complete:

- [ ] All 5 tasks completed
- [ ] EQS asset created with 6 named parameters
- [ ] StateTree updated to use v8.0 task
- [ ] Agent blueprint has all required components
- [ ] PIE test shows correct logs (no errors)
- [ ] Movement is smooth and responsive
- [ ] Combat targeting works (Closest and LowestHP)
- [ ] Unreal Insights profile captured
- [ ] Performance targets met (<20ms/sec, 60 FPS)
- [ ] Screenshots/videos of working system captured

---

## 📁 Deliverables

When tasks are complete, save:

1. **EQS Asset:** `Content/AI/EQS/TacticalPositionQuery.uasset`
2. **StateTree Asset:** `Content/AI/StateTree/FollowerStateTree.uasset` (updated)
3. **Insights Trace:** `Saved/Profiling/v8.0_week2_test.utrace`
4. **Screenshots:**
   - In-game debug visualization (strategy colors, targeting)
   - Unreal Insights flame graph (AI frame breakdown)
   - Output log showing v8.0 messages
5. **Test Video:** 60-second clip of 4v4 scenario with v8.0 working

---

## 🎓 Learning Resources

**EQS Documentation:**
- UE5 Docs: [Environment Query System](https://docs.unrealengine.com/5.0/en-US/environment-query-system-in-unreal-engine/)
- Epic Learning: [EQS Quick Start](https://dev.epicgames.com/community/learning/tutorials/8OWY/unreal-engine-environment-query-system-eqs-quick-start)

**StateTree Documentation:**
- UE5 Docs: [StateTree](https://docs.unrealengine.com/5.0/en-US/statetree-in-unreal-engine/)
- Epic Learning: [StateTree Overview](https://dev.epicgames.com/community/learning/tutorials/l96/unreal-engine-statetree-overview)

**Unreal Insights:**
- UE5 Docs: [Unreal Insights](https://docs.unrealengine.com/5.0/en-US/unreal-insights-in-unreal-engine/)
- Epic Learning: [Profiling with Insights](https://dev.epicgames.com/community/learning/tutorials/4R5j/unreal-engine-profiling-with-unreal-insights)

---

**Document Version:** 1.0
**Created:** 2026-01-14
**Estimated Completion Time:** 2-3 hours (first time), 1 hour (if familiar with tools)
