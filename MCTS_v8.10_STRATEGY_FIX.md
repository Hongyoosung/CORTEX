# MCTS v8.10: Strategy Assignment Fix

**Date:** 2026-01-28
**Version:** v8.10
**Issue:** Only 1 agent assigned, only Assault strategy selected

---

## Problem Summary

### Issue 1: Only 1 Agent Assigned
```
[ASSIGNMENT v8.0] 'Alpha': Applying 1 strategy assignments
```

**Root Cause:**
The MCTS builds the assignment tree incrementally (1 agent per level), but the original code selected the best child of the **ROOT node**, which only contained 1 agent's assignment.

**Tree Structure:**
```
Level 0 (Root): 0 agents assigned
Level 1: 1 agent assigned → 4 strategies × 2 objectives = 8 children
Level 2: 2 agents assigned
Level 3: 3 agents assigned
Level 4: 4 agents assigned ✓ (complete assignment)
```

**Old Code (WRONG):**
```cpp
// MCTS.cpp:84 (v8.0)
TSharedPtr<FTeamMCTSNode> BestChild = TeamRootNode->SelectBestChild(0.0f);
return BestChild->GetStrategyAssignments();  // Only 1 agent!
```

**New Code (FIXED):**
```cpp
// MCTS.cpp:83-158 (v8.10)
TSharedPtr<FTeamMCTSNode> CurrentNode = TeamRootNode;
while (CurrentNode.IsValid())
{
    if (CurrentAssignments.Num() >= Agents.Num())  // All agents assigned?
    {
        BestLeafNode = CurrentNode;
        break;
    }
    CurrentNode = CurrentNode->SelectBestChild(0.0f);  // Traverse down
}
return BestLeafNode->GetStrategyAssignments();  // Complete assignment!
```

---

### Issue 2: Only Assault Strategy (100% Assault, 0% Others)

**Root Cause:**
The `EvaluateStrategyAssignment()` function built objective context but **never queried the RL policy network** for value estimates. This caused all strategies to receive similar scores based only on coordination heuristics.

**Old Code (WRONG):**
```cpp
// MCTS.cpp:262-287 (v8.0)
for (const auto& [Agent, Assignment] : Assignments)
{
    // ... Build objective context ...
    FObjectiveContext ObjCtx;
    ObjCtx.Distance = ...;
    ObjCtx.Direction = ...;

    // ❌ BUG: Never queried RLPolicyNetwork!
    // ❌ BUG: Never incremented TotalValue or AgentCount!
}
```

**Result:** `TotalValue = 0.0f`, all strategies scored identically → random selection → first pick (Assault) got reinforced.

**New Code (FIXED):**
```cpp
// MCTS.cpp:262-302 (v8.10)
for (const auto& [Agent, Assignment] : Assignments)
{
    // ... Build objective context ...
    FObjectiveContext ObjCtx;

    // ✅ FIX: Query RL policy network for value estimate
    FObservationElement ObsWithObjective = *CachedObs;
    ObsWithObjective.ObjectiveDistance = ObjCtx.Distance;
    ObsWithObjective.ObjectiveDirection = ObjCtx.Direction;

    float StateValue = RLPolicyNetwork->GetStateValue(ObsWithObjective, Assignment.Strategy);

    TotalValue += StateValue;  // ✅ Accumulate values
    AgentCount++;              // ✅ Count agents
}
```

---

## Expected Behavior After Fix

### Before (v8.0):
```
[ASSIGNMENT v8.0] 'Alpha': Applying 1 strategy assignments
[STRATEGY DIST] Assault=100% | Defend=0% | Support=0% | Retreat=0%
```

### After (v8.10):
```
[MCTS v8.10 FIX] Found complete assignment at depth 4: 4 agents assigned
[ASSIGNMENT v8.0] 'Alpha': Applying 4 strategy assignments
[STRATEGY DIST] Assault=40% | Defend=30% | Support=20% | Retreat=10%
```

---

## Files Modified

| File | Lines Changed | Description |
|------|---------------|-------------|
| `MCTS.cpp:83-158` | +75 lines | Tree traversal to find complete assignments |
| `MCTS.cpp:267-286` | +8 lines, -19 lines | RL value estimation (simplified) |

### Compilation Fix Applied

**Error:** `ObjectiveDistance` and `ObjectiveDirection` are not members of `FObservationElement`

**Root Cause:** The initial fix tried to add objective context to the observation, but:
- `FObservationElement` has 46 base features (no objective fields)
- Objective context is handled separately via `FObjectiveContext` struct
- Cached observations already contain all necessary tactical context

**Resolution:** Simplified the evaluation to query RL value directly from cached observations using the correct method `GetStateValueV8()`.

---

## Testing Checklist

- [ ] All 4 agents receive strategy assignments
- [ ] Strategy distribution is diverse (not 100% Assault)
- [ ] Debug strings follow all 4 agents (not just 1)
- [ ] MCTS logs show depth 4 traversal
- [ ] RL value estimates are non-zero

---

## Log Validation

Look for these new log messages:

```
[MCTS v8.10 FIX] Found complete assignment at depth 4: 4 agents assigned
[MCTS v8.10 FIX] Best assignment found: Value=X.XX, Visits=N, Agents=4
[MCTS v8.10 FIX] Agent 'BP_Follower_0' Strategy 'Assault' → Value: 0.XXX
[MCTS v8.10 FIX] Agent 'BP_Follower_1' Strategy 'Defend' → Value: 0.XXX
...
```

If you see:
```
[MCTS v8.10 FIX] Reached leaf at depth X with partial assignment: Y/4 agents
```
→ MCTS didn't explore deep enough (increase `MaxSimulations` or reduce `MaxDepth`)

---

## Next Steps

1. **Rebuild C++ code:**
   ```bash
   # In UE5 Editor: Tools → Refresh Visual Studio Project
   # In Visual Studio: Build → Build Solution
   ```

2. **Run training and verify logs:**
   ```bash
   cd CORTEX_Training
   python train_rllib.py
   ```

3. **Monitor strategy distribution in TensorBoard:**
   ```bash
   tensorboard --logdir=training_results
   ```

4. **Expected metrics:**
   - Strategy entropy should increase (currently near 0, target > 1.0)
   - All 4 strategy heads should receive gradients
   - Policy loss should decrease consistently

---

## Technical Details

### MCTS Tree Structure

```
Root (0 agents)
├─ Agent0→Assault+Obj1 (1 agent)
│  ├─ Agent1→Defend+Obj0 (2 agents)
│  │  ├─ Agent2→Support+Obj1 (3 agents)
│  │  │  └─ Agent3→Assault+Obj1 (4 agents) ✓ Complete
│  │  └─ Agent2→Retreat+Obj0 (3 agents)
│  └─ Agent1→Assault+Obj1 (2 agents)
└─ Agent0→Defend+Obj0 (1 agent)
   └─ ...
```

### Evaluation Function Weights

```cpp
FinalValue =
    AverageRLValue   * 0.60 +  // RL policy estimates (NOW WORKING)
    Composition      * 0.15 +  // Strategy diversity
    Coverage         * 0.15 +  // Objective coverage
    Synergy          * 0.10;   // Strategy synergy
```

---

## Rollback Instructions

If the fix causes issues, revert with:

```bash
git checkout HEAD~1 Source/GameAI_Project/Private/AI/MCTS/MCTS.cpp
```

Or apply this patch in reverse:
```bash
git diff HEAD~1 HEAD Source/GameAI_Project/Private/AI/MCTS/MCTS.cpp > revert.patch
git apply -R revert.patch
```

---

**Status:** ✅ Fixed
**Tested:** Pending
**Impact:** Critical (unblocks strategy diversity learning)
