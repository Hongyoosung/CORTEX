# v9.0 Phase 3 Refactoring - Validation Checklist

## Quick Start

This document provides step-by-step validation procedures for the v9.0 Phase 3 refactoring. Follow each section in order to ensure the system is working correctly.

---

## Phase 1: Compilation ✓

### 1.1 Clean Build
```powershell
# Navigate to project directory
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX

# Generate project files
"C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="$(pwd)\GameAI_Project.uproject" -game -rocket -progress

# Clean build
"C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" GameAI_ProjectEditor Win64 Development "$(pwd)\GameAI_Project.uproject" -clean

# Full rebuild
"C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" GameAI_ProjectEditor Win64 Development "$(pwd)\GameAI_Project.uproject"
```

### 1.2 Expected Output
- [ ] No compilation errors
- [ ] No warnings related to:
  - `ObservationBuilderComponent`
  - `TeamLeaderComponent`
  - `FollowerAgentComponent`
  - Missing `TeamLeader` member
  - Undefined `GetAliveFollowers()` calls

### 1.3 Troubleshooting
If compilation fails:
1. Check that all modified files are saved
2. Verify no merge conflicts in `.cpp`/`.h` files
3. Run `git diff` on modified files to see unexpected changes
4. Compare against `REFACTORING_v9.0_PHASE3_SUMMARY.md` for correct changes

---

## Phase 2: Static Analysis ✓

### 2.1 Verify Data Flow
Run these greps to confirm proper data flow:

```bash
# Should return 0 matches (TeamLeader dependency removed)
grep -n "TeamLeader->" Source/GameAI_Project/Private/Observation/Components/ObservationBuilderComponent.cpp

# Should find UpdateTacticalContext call WITH 3 parameters
grep -n "UpdateTacticalContext.*CurrentTeamObservation" Source/GameAI_Project/Private/Team/Components/TeamLeaderComponent.cpp

# Should find usage of CachedTeamObservation in ObservationBuilder
grep -n "CachedTeamObservation" Source/GameAI_Project/Private/Observation/Components/ObservationBuilderComponent.cpp
```

### 2.2 Expected Results
- [ ] `ObservationBuilderComponent.cpp` has **zero** `TeamLeader->` references
- [ ] `TeamLeaderComponent.cpp` line ~800 passes `CurrentTeamObservation` parameter
- [ ] `ObservationBuilderComponent.cpp` uses `CachedTeamObservation.FollowerObservations`

---

## Phase 3: Runtime Validation 🎮

### 3.1 Launch Game
1. Open `GameAI_Project.uproject` in Unreal Editor
2. Open map: `Content/Game/Maps/Training/Training_BasicCombat_1vs1_v9.umap`
3. Click **Play** (PIE mode)

### 3.2 Objective Discovery
**Test:** Verify objectives are discovered and propagated

**Expected Log Output:**
```
[IntelManager] Discovered friendly objective: ObjectiveFriendly_Team0 (TeamID: 0)
[IntelManager] Discovered hostile objective: ObjectiveHostile_Team1 (TeamID: 1)
✅ [IntelManager v9.0] Successfully discovered both objectives (TeamID: 0)
📡 [IntelManager v9.0] OnObjectivesDiscovered event broadcast (TeamID: 0)
```

**Validation Steps:**
1. [ ] Open Output Log window (`Window > Developer Tools > Output Log`)
2. [ ] Filter by "IntelManager"
3. [ ] Verify both objectives discovered within 0.5s of play start
4. [ ] Verify `OnObjectivesDiscovered` event broadcast

**If Test Fails:**
- Check that ObjectiveActors exist in the map
- Verify ObjectiveActor `OwnerTeamID` matches team IDs (0 and 1)
- Check `IntelManager::TeamID` is set correctly in `BP_TeamLeader` blueprint

---

### 3.3 Objective Context Propagation
**Test:** Verify followers receive objective data

**Expected Log Output:**
```
✅ [OBS CONTEXT v9.0] Follower_0: FriendlyDist=0.234, HostileDist=0.678
```

**Validation Steps:**
1. [ ] Filter Output Log by "OBS CONTEXT"
2. [ ] Verify each follower logs objective distances every ~5s
3. [ ] Check that `FriendlyDist` and `HostileDist` are NOT 1.000 (default values)
4. [ ] Verify values change as agents move

**If Test Fails:**
- Check that `TeamLeader::HandleObjectivesDiscovered()` is called
- Verify `FollowerAgent::UpdateTacticalContext()` receives non-null objectives
- Check `ObservationBuilder::SetObjectives()` is called
- Enable verbose logging: Add `-LogCmds="LogTemp Verbose"` to command line

---

### 3.4 Team Observation Building
**Test:** Verify team observation includes follower data

**Console Commands:**
```
# In PIE, press ` (tilde) to open console, then type:
DebugObservation
```

**Expected Output:**
```
[TeamLeader] Team Observation:
  Followers: 4 alive, 0 dead
  Average Health: 85.2%
  Team Centroid: X=1250.5 Y=834.2 Z=100.0
  FollowerObservations: 4 entries
    [0] Position=(1200, 800, 100) Health=0.90
    [1] Position=(1300, 800, 100) Health=0.85
    [2] Position=(1200, 900, 100) Health=0.82
    [3] Position=(1300, 900, 100) Health=0.84
```

**Validation Steps:**
1. [ ] Verify `FollowerObservations` count matches alive followers
2. [ ] Verify each entry has valid `Position` and `Health` (not zeros)
3. [ ] Check that `AverageTeamHealth` matches follower health average

**If Test Fails:**
- Check `IntelManager::BuildTeamObservation()` is called every tick
- Verify `SquadManager::GetFollowers()` returns all alive followers
- Check that each follower has valid `ObservationBuilderComponent`

---

### 3.5 Support Strategy - Ally Tracking
**Test:** Verify Support strategy agents track lowest health ally

**Setup:**
1. Start episode
2. Wait for MCTS to assign strategies (check Output Log for "ASSIGNMENT v9.0")
3. Identify a follower with `Support` strategy
4. Damage another follower to ~40% health (use console: `DamageActor <ActorName> 60`)

**Expected Behavior:**
- Support agent moves toward damaged ally
- Output Log shows:
  ```
  [OBS v9.0] Follower_2: Ally needs help (Health=0.38, Dist=542.3)
  ```

**Validation Steps:**
1. [ ] Open top-down camera view
2. [ ] Damage one follower to low health
3. [ ] Observe Support strategy agent (check via debug visualization: press `~` then `ShowDebug AI`)
4. [ ] Verify agent moves toward damaged ally
5. [ ] Check Output Log for "Ally needs help" message

**If Test Fails:**
- Verify `CachedTeamObservation` is being populated in `ObservationBuilder`
- Check `FollowerObservations` array contains all team members
- Enable verbose logging and check ally iteration logic
- Verify `AllyHealth` field is correct in `FObservationElement`

---

### 3.6 Assault/Defend Strategy - Objective Targeting
**Test:** Verify strategy-specific objective targeting

**Expected Behavior:**
- **Assault agents:** Move toward `HostileObjective`
- **Defend agents:** Stay near `FriendlyObjective`

**Validation:**
1. [ ] Start episode, wait for strategy assignment
2. [ ] Identify agents with Assault vs Defend strategies (check debug overlay)
3. [ ] Observe movement:
   - Assault → moves toward enemy objective
   - Defend → stays near friendly objective
4. [ ] Check rewards (filter Output Log by "REWARD"):
   ```
   [REWARD v9.0] Agent0 (Assault): Objective Progress: +15.2 (HostileObjectiveDistance: 0.15)
   [REWARD v9.0] Agent1 (Defend): Objective Progress: +10.5 (FriendlyObjectiveDistance: 0.22)
   ```

**If Test Fails:**
- Check that `HostileObjectiveDistance` and `FriendlyObjectiveDistance` are populated
- Verify reward calculation uses correct objective based on strategy
- Check that objective context is not using default values (1.0)

---

## Phase 4: Performance Validation ⚡

### 4.1 Profiling
**Target:** Ensure refactoring did not introduce performance regression

**Measurement:**
```
# In PIE console:
stat game
stat unit
```

**Expected Metrics (4 agents):**
- **Game Thread:** 5-8ms
- **ObservationBuilder::BuildLocalObservation():** <1ms per agent
- **TeamLeader::BuildTeamObservation():** <3ms
- **Total Frame Time:** <16ms (60 FPS)

**Validation:**
1. [ ] Run episode for 100 steps
2. [ ] Record average frame times
3. [ ] Compare to baseline (v8.20): Should be within ±5%
4. [ ] Check for new performance warnings in Output Log

**If Performance Regression Detected:**
- Profile `CachedTeamObservation` access (should be O(1) lookup)
- Check that team observation is cached, not rebuilt per follower
- Verify no redundant copies of large structs

---

### 4.2 Memory Usage
**Expected Memory (4v4 scenario):**
- **TeamLeader:** ~4.2MB
  - `CurrentTeamObservation`: ~50KB (4 followers × ~12KB each)
  - `CurrentAssignments`: ~500 bytes
- **Per Follower:** ~200KB each
  - `CachedTeamObservation`: ~50KB (shared reference preferred)
  - `CachedObjectives`: 16 bytes (2 pointers)

**Validation:**
1. [ ] Use Unreal's Memory Profiler (`stat memory`)
2. [ ] Compare to v8.20 baseline
3. [ ] Check for memory leaks (run episode 10x, check if memory grows)

**If Memory Leak Detected:**
- Check that `CachedTeamObservation` uses value semantics (struct copy)
- Verify no dangling pointers to `TeamLeader` in `ObservationBuilder`
- Use Unreal Insights to trace allocations

---

## Phase 5: Edge Cases 🔍

### 5.1 No Objectives Scenario
**Setup:**
1. Delete all ObjectiveActors from map
2. Start episode

**Expected Behavior:**
- [ ] IntelManager logs warning: "No ObjectiveActors found in world"
- [ ] `OnObjectivesDiscovered` event does NOT fire
- [ ] Followers use default objective distances (1.0)
- [ ] No crashes or errors

---

### 5.2 Single Follower Scenario
**Setup:**
1. Open `Training_BasicCombat_1vs1_v9.umap`
2. Delete 3 of 4 followers from Team 0
3. Start episode

**Expected Behavior:**
- [ ] TeamLeader builds team observation with 1 entry
- [ ] Support strategy has no ally to help (`bAllyNeedsHelp = false`)
- [ ] No crashes when iterating `FollowerObservations`

---

### 5.3 Follower Death During Episode
**Setup:**
1. Start episode
2. Kill one follower (console: `KillActor Follower_2`)

**Expected Behavior:**
- [ ] TeamLeader updates `CurrentTeamObservation` (removes dead follower)
- [ ] Support strategy no longer considers dead ally
- [ ] No null pointer crashes in `ObservationBuilder`

---

## Phase 6: Regression Tests 🧪

### 6.1 Existing Unit Tests
```bash
# Run UE automation tests
"C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$(pwd)\GameAI_Project.uproject" -ExecCmds="Automation RunTests Team.Observation; Quit" -unattended -nopause -nosplash -nullrhi -log
```

**Expected:**
- [ ] All tests pass
- [ ] No new test failures vs v8.20 baseline

---

### 6.2 Integration Test: Full Episode
**Test:** Run complete 1000-step episode

**Command:**
```bash
# In PIE console:
StartEpisode 0
WaitForCompletion 1000
CheckMetrics
```

**Expected Metrics:**
- [ ] Win rate: Within ±5% of v8.20 baseline
- [ ] Average reward: Within ±10% of v8.20 baseline
- [ ] Episode completion: No crashes or hangs
- [ ] Strategy diversity: All 4 strategies used at least once

---

## Phase 7: Code Review Checklist ✅

### 7.1 ObservationBuilderComponent
- [ ] No `TeamLeader` include or forward declaration
- [ ] No `TeamLeader` member variable
- [ ] `BuildLocalObservation()` uses only cached data:
  - `CachedFriendlyObjective`
  - `CachedHostileObjective`
  - `CachedTeamObservation`
- [ ] Support context uses `CachedTeamObservation.FollowerObservations`
- [ ] Ally iteration skips self by comparing positions

### 7.2 TeamLeaderComponent
- [ ] `PushContextToFollower()` passes 3 parameters:
  - `AObjectiveActor* Friendly`
  - `AObjectiveActor* Hostile`
  - `const FTeamObservation& CurrentTeamObservation` ← Must be present
- [ ] Delegates to manager components (no direct data access)
- [ ] Broadcasts tactical context every tick

### 7.3 FollowerAgentComponent
- [ ] `UpdateTacticalContext()` accepts 3 parameters
- [ ] Propagates data to `ObservationBuilder` without caching
- [ ] No `CachedFriendlyObjective` or `CachedHostileObjective` members

---

## Phase 8: Documentation ✍️

### 8.1 Update CLAUDE.md
- [ ] Add v9.0 Phase 3 refactoring notes
- [ ] Update data flow diagrams
- [ ] Document new injection patterns

### 8.2 Update Code Comments
- [ ] Add `v9.0 REFACTORED` comments to modified sections
- [ ] Update function headers with correct parameter descriptions
- [ ] Document SRP compliance in component headers

---

## Sign-Off

### Pre-Deployment Checklist
- [ ] All Phase 1-7 tests pass
- [ ] No performance regression
- [ ] No memory leaks
- [ ] Documentation updated
- [ ] Code review approved
- [ ] Merge conflicts resolved

### Approval
**Tested By:** _________________
**Date:** _________________
**Status:** ☐ Pass ☐ Fail ☐ Blocked

---

## Rollback Plan

If critical issues are found:

1. **Revert commits:**
   ```bash
   git reset --hard HEAD~2  # Revert last 2 commits
   ```

2. **Restore v8.20:**
   ```bash
   git checkout origin/main
   ```

3. **Document issue:**
   - Create GitHub issue with logs
   - Tag as `v9.0-phase3-regression`

---

## Support

### Logging Commands
```
# Enable verbose observation logging
LogTemp Verbose

# Enable MCTS logging
LogMCTS Verbose

# Enable reward logging
LogReward Display
```

### Debug Visualization
```
# Show AI debug info
ShowDebug AI

# Draw observation context
DebugDrawObservation

# Show team formation
DebugDrawTeam
```

### Contact
- **Technical Lead:** Claude Sonnet 4.5
- **Documentation:** `REFACTORING_v9.0_PHASE3_SUMMARY.md`
- **Architecture:** `REFACTORING_v9.0_ARCHITECTURE_DIAGRAM.md`

---

**Version:** 1.0
**Last Updated:** 2026-02-03
**Status:** Ready for Validation ✅
