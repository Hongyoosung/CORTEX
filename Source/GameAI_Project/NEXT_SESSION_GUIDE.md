# Next Session Guide - SBDAPM v3.2 Priorities

**Last Review:** v3.1 Production Review (2025-12-09)
**Status:** ✅ Production Ready - Minor cleanups recommended
**Priority:** Training Evaluation + Code Quality

---

## Quick Status Summary

### ✅ What's Working
- MCTS + PPO critic integration fully functional
- Hierarchical rewards aligned across all layers
- Crouch action (and all 8D actions) properly transmitted
- Performance targets met (MCTS: 30-50ms, RL: 1-3ms, StateTree: <0.5ms/agent)
- Real-time RLlib training operational

### ⚠️ Minor Issues (Non-Blocking)
- Excessive diagnostic logging (spam at Warning level)
- Outdated documentation (REWARD_OBSERVATION_DIAGNOSTIC.md)
- Misleading placeholder comments in STEvaluator_UpdateObservation
- Commented-out dead code in RLReplayBuffer

---

## Immediate Tasks (Next Session)

### 🎯 Priority 1: Training Evaluation (HIGH)
**Goal:** Validate that training converges and meets baseline metrics

**Steps:**
1. **Start Unreal Editor** with SBDAPM project
2. **Run Training Session:**
   ```bash
   cd Source/GameAI_Project/Scripts
   python train_rllib.py --iterations 100 --checkpoint-freq 10
   ```
3. **Monitor Metrics:**
   - Episode reward mean (should increase over time)
   - Episode length (stability indicator)
   - Agent coordination rate (target: ≥30% coordinated kills)
   - MCTS execution time (should stay 30-50ms)

4. **Baseline Targets:**
   - Training convergence within 50-100 iterations
   - Reward trend: positive slope
   - No crashes or memory leaks during 100+ episodes

**Expected Duration:** 30-60 minutes (depending on UE performance)

**Output:**
- Training checkpoints in `training_results/[timestamp]/`
- TensorBoard logs (optional: `tensorboard --logdir training_results`)
- Exported ONNX model: `rl_policy_network.onnx`

---

### 🧹 Priority 2: Code Cleanup (MEDIUM)
**Goal:** Reduce console spam and improve code clarity

#### Task 2.1: Reduce Logging Verbosity
**Files to Edit:**
- `Source/GameAI_Project/Private/AI/MCTS/MCTS.cpp:71-72`
- `Source/GameAI_Project/Private/Schola/TacticalActuator.cpp:124-131`

**Changes:**
```cpp
// BEFORE (MCTS.cpp:71)
UE_LOG(LogTemp, Warning, TEXT("[MCTS] FormationCoherence=%.3f ..."));

// AFTER
UE_LOG(LogTemp, Verbose, TEXT("[MCTS] FormationCoherence=%.3f ..."));
// OR
#if !UE_BUILD_SHIPPING
UE_LOG(LogTemp, Display, TEXT("[MCTS] FormationCoherence=%.3f ..."));
#endif
```

**Expected Impact:** Reduce console spam from ~60 logs/sec to occasional debug output

#### Task 2.2: Remove Misleading Comments
**File:** `Source/GameAI_Project/Private/StateTree/Evaluators/STEvaluator_UpdateObservation.cpp`

**Lines 157-170:** Remove or update "Placeholder" comments
- **Reality:** Observation building delegated to `FollowerAgentComponent::BuildLocalObservation()` (lines 420-499)
- **Fix:** Either remove placeholder methods or add comment explaining delegation

#### Task 2.3: Clean Up Dead Code
**File:** `Source/GameAI_Project/Private/RL/RLReplayBuffer.cpp`

**Lines 361-363, 397-398:** Remove commented-out code
```cpp
// REMOVE THESE:
// Experience.State.FromFlatArray(ObsFeatures);  // Not implemented
```

**Expected Duration:** 15-30 minutes

---

### 📝 Priority 3: Documentation Update (LOW)
**Goal:** Sync documentation with current implementation

**File:** `Source/GameAI_Project/REWARD_OBSERVATION_DIAGNOSTIC.md`

**Issue:** Document claims cover rewards "NOT IMPLEMENTED" but they're actually in RewardCalculator.cpp:197-219

**Fix:** Update section to reflect:
```markdown
✅ **Cover Rewards (IMPLEMENTED):**
- CrouchInCoverReward: +5.0 (RewardCalculator.cpp:163)
- CoverUnderFireReward: +10.0 (RewardCalculator.cpp:156)
- ExposedPenalty: -5.0 (RewardCalculator.cpp:159)
```

**Expected Duration:** 10 minutes

---

## Optional Tasks (If Time Permits)

### 🔍 Task 4: Performance Profiling
**Goal:** Identify optimization opportunities

**Steps:**
1. Enable UE Insights or use Unreal's Stat commands
2. Run 10-episode session
3. Profile MCTS execution time breakdown:
   - Selection phase
   - Expansion phase
   - Simulation (PPO critic queries)
   - Backpropagation
4. Identify bottlenecks for future parallelization

**Tool:** UE5 Console Commands
```
stat startfile
stat stopfile
```

### 🎨 Task 5: Visualize MCTS Tree (Debug)
**Goal:** Add editor visualization for MCTS decisions

**Approach:**
- Draw debug lines showing objective assignments
- Color-code by confidence (green=high, yellow=medium, red=low)
- Display MCTS stats as on-screen debug text

**File to Edit:** `Source/GameAI_Project/Private/Team/TeamLeaderComponent.cpp`

**Reference:** CLAUDE.md mentions this as v4.0 "Explainability" feature

---

## Success Criteria

### ✅ Session Complete When:
1. Training completes 100+ iterations without crashes
2. Reward trend shows positive slope (learning is happening)
3. Logging verbosity reduced (no spam at Warning level)
4. Documentation updated to reflect current implementation
5. Baseline metrics recorded for future comparison

### 📊 Metrics to Record:
- **Win Rate:** % of episodes won by trained team
- **Coordination Rate:** % of kills with combined fire bonus
- **MCTS Efficiency:** Average tree search time (target: 30-50ms)
- **Training Stability:** Episode reward standard deviation

---

## Troubleshooting Guide

### Issue: Training Hangs or Crashes
**Check:**
1. Schola gRPC connection (should see "Schola Env Num Envs: 1" in console)
2. Agent IDs filtering (should have exactly 4 agents, no CDO)
3. UE editor not frozen (check for modal dialogs)

**Fix:**
- Restart UE editor and Python script
- Check `test_connection.py` to verify Schola connectivity

### Issue: Rewards Always Zero
**Check:**
1. `RewardCalculator::CalculateTotalReward()` being called
2. Combat events firing (OnKillEnemy, OnDealDamage)
3. FollowerAgentComponent properly registered

**Debug:**
```cpp
// Add to RewardCalculator.cpp:59
UE_LOG(LogTemp, Warning, TEXT("TotalReward=%.2f (Ind=%.2f, Coord=%.2f, Obj=%.2f)"),
    TotalReward, IndividualReward, CoordinationReward, ObjectiveReward);
```

### Issue: MCTS Timeout Warnings
**Check:**
1. PPO critic inference time (should be <3ms per query)
2. ONNX model loaded correctly (`RLPolicyNetwork.cpp:39-55`)
3. Simulation count too high (default: 500-1000)

**Fix:**
- Reduce MaxSimulations in TeamLeaderComponent (e.g., 500 → 250)
- Verify ONNX model exists at `Content/Models/rl_policy_network.onnx`

---

## Files Quick Reference

### To Edit This Session:
```
Source/GameAI_Project/
├── Private/AI/MCTS/MCTS.cpp                           # Line 71: Logging
├── Private/Schola/TacticalActuator.cpp                # Line 124: Logging
├── Private/StateTree/Evaluators/STEvaluator_UpdateObservation.cpp  # Lines 157-170: Comments
├── Private/RL/RLReplayBuffer.cpp                      # Lines 361-398: Dead code
└── REWARD_OBSERVATION_DIAGNOSTIC.md                   # Cover rewards section
```

### To Monitor During Training:
```
Source/GameAI_Project/Scripts/
├── train_rllib.py          # Training loop
├── sbdapm_env.py           # Environment wrapper
└── training_results/       # Output directory
```

---

## Next Session After v3.2

Once training evaluation completes and cleanups are done, focus shifts to:

1. **Curriculum Learning:** Progressive difficulty (v3.0 Sprint 3 feature)
2. **MCTS Parallelization:** Multi-threaded tree search (2-4x speedup)
3. **Opponent Modeling:** Predict enemy strategy
4. **Model Export Pipeline:** Automated ONNX export after checkpoints

See CLAUDE.md "Next Steps (Post v3.1)" for full v4.0 roadmap.

---

**Prepared:** 2025-12-09
**For:** v3.2 Development Session
**Estimated Session Duration:** 1-2 hours
