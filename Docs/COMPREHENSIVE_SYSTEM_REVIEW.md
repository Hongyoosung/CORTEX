# CORTEX Hierarchical AI System: Comprehensive Review Report

**Date:** 2025-12-28
**Version Reviewed:** v4.0 (Macro Actions)
**Reviewer:** Claude Opus 4.5

---

## Executive Summary

The CORTEX system implements a sophisticated **hierarchical AI architecture** combining:
- **MCTS (Monte Carlo Tree Search)** for strategic team-level decisions
- **PPO (Proximal Policy Optimization)** for tactical individual agent actions
- **StateTree** for action execution and behavior orchestration

**Overall Assessment: Well-Structured with Room for Improvement**

| Component | Implementation Quality | Maturity |
|-----------|----------------------|----------|
| Observations | Good (7/10) | Production-Ready |
| MCTS Strategy | Good (7.5/10) | Production-Ready |
| RL + StateTree Tactics | Good (7/10) | Integration Testing |
| Python Training | Fair (6/10) | Needs Refinement |
| System Integration | Good (7/10) | Integration Testing |

---

## Table of Contents

1. [Observation System Analysis](#1-observation-system-analysis)
2. [MCTS Strategy Layer Analysis](#2-mcts-strategy-layer-analysis)
3. [RL + StateTree Tactics Layer Analysis](#3-rl--statetree-tactics-layer-analysis)
4. [Python Training Structure Analysis](#4-python-training-structure-analysis)
5. [Integration Assessment](#5-integration-assessment)
6. [Identified Issues and Recommendations](#6-identified-issues-and-recommendations)
7. [Recommended Methodologies](#7-recommended-methodologies)
8. [Priority Action Items](#8-priority-action-items)

---

## 1. Observation System Analysis

### 1.1 Structure Overview

**Individual Observation (70 features):**
```
Agent State (11):      Position(3), Velocity(3), Rotation(3), Health(1), Shield(1)
Combat State (3):      WeaponCooldown(1), Ammunition(1), WeaponType(1)
Environment (32):      RaycastDistances(16), RaycastHitTypes(16)
Enemies (16):          VisibleEnemyCount(1), NearbyEnemies(5x3=15)
Tactical (5):          HasCover(1), CoverDistance(1), CoverDirection(2), Terrain(1)
Temporal (2):          TimeSinceLastAction(1), LastActionType(1)
Combat Proximity (1):  DistanceToNearestEnemy(1)
```

**Objective Embedding (4 features):** One-hot encoding for Assault/Defend/Support/Retreat

### 1.2 Strengths

1. **Proper Normalization**: All features normalized to `[-1, 1]` or `[0, 1]` range - critical for neural network stability
2. **Comprehensive Coverage**: Captures agent state, environment, enemies, and tactical context
3. **Efficient Feature Count**: 74 total features is reasonable for a policy network
4. **MCTS Tree Reuse**: `CalculateSimilarity()` enables efficient observation comparison

### 1.3 Issues Identified

| Issue | Severity | Location | Description |
|-------|----------|----------|-------------|
| **Inconsistent Position Normalization** | Medium | `ObservationElement.cpp:13-15` | Position divided by 10000.0f but map sizes may vary significantly |
| **Fixed Raycast Count** | Low | `ObservationElement.h:65-69` | Hardcoded 16 rays - may be insufficient for complex environments |
| **Missing Ally Information** | Medium | `ObservationElement.h` | No ally positions/health in individual observation |
| **Static Enemy Slot Padding** | Low | `ObservationElement.cpp:75-79` | Always pads to 5 enemies even if fewer exist |

### 1.4 Recommendations

```cpp
// RECOMMENDATION 1: Dynamic position normalization
UPROPERTY(EditDefaultsOnly)
float MapSize = 10000.0f;  // Configurable per level

Features.Add(Position.X / MapSize);

// RECOMMENDATION 2: Add ally observation (critical for coordination)
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Allies")
TArray<FAllyObservation> NearbyAllies;  // 3×4 = 12 features (position, health, objective)
```

---

## 2. MCTS Strategy Layer Analysis

### 2.1 Architecture Overview

```
TeamLeaderComponent
    ├── RunTeamMCTSWithObjectives()
    │   ├── GenerateObjectiveAssignments() - UCB-based progressive widening
    │   ├── RunSingleSimulation() (500-1000 iterations)
    │   │   ├── SelectNode() - UCT traversal
    │   │   ├── ExpandNode() - Tree expansion with priors
    │   │   └── SimulateNode() - Strategic heuristic evaluation
    │   └── Backpropagate() - Value updates
    └── Output: TMap<AActor*, UObjective*>
```

### 2.2 Strategic Heuristic Evaluation

**Formula:** `Value = ObjectiveProgress(±0.6) + TeamStrength(±0.3) + PositionalAdvantage(±0.1)`

| Component | Weight | Evaluation Logic |
|-----------|--------|------------------|
| ObjectiveProgress | ±0.6 | Distance to objective, type-specific scoring |
| TeamStrength | ±0.3 | Survival rate × 0.15 + Health × 0.1 + Ammo × 0.05 |
| PositionalAdvantage | ±0.1 | Cover usage, formation spread |

### 2.3 Strengths

1. **Appropriate Heuristic Choice**: Using handcrafted heuristics instead of neural network for MCTS leaf evaluation is correct - 50x faster (~0.1ms vs ~5ms)
2. **UCB-Based Progressive Widening**: Smart action generation with synergy bonuses (`CalculateObjectiveSynergy()`)
3. **Parallel Execution**: Root parallelization enabled for multi-core speedup
4. **Statistics Export**: Value variance, policy entropy for curriculum learning
5. **RL Integration**: Uses `GetObjectivePriors()` to guide MCTS search

### 2.4 Issues Identified

| Issue | Severity | Location | Description |
|-------|----------|----------|-------------|
| **Priors Mismatch** | High | `RLPolicyNetwork.cpp:393-500` | `GetObjectivePriors()` returns 7 elements but v4.0 uses 4 objectives |
| **Limited Depth** | Medium | `MCTS.cpp` | No progressive deepening - always expands first untried action |
| **No RAVE/AMAF** | Low | N/A | Missing Rapid Action Value Estimation for faster convergence |
| **Objective Assignment Coupling** | Medium | `MCTS.cpp:346-412` | Target selection in assignment generation may not match game state |

### 2.5 Recommendations

```cpp
// RECOMMENDATION 1: Fix objective priors to match v4.0 (4 types)
TArray<float> URLPolicyNetwork::GetObjectivePriors(const FTeamObservation& TeamObs)
{
    TArray<float> Priors;
    Priors.Init(0.1f, 4);  // v4.0: Only 4 objective types

    // Map: [0]=Assault, [1]=Defend, [2]=Support, [3]=Retreat
    // ... simplified logic for 4 types
}

// RECOMMENDATION 2: Add progressive deepening
void UMCTS::ExpandNode(TSharedPtr<FTeamMCTSNode> Node)
{
    // Only expand if parent has sufficient visits
    if (Node->Parent && Node->Parent->VisitCount < MinVisitsBeforeExpand)
    {
        return nullptr;  // Wait for more simulations at current depth
    }
    // ...existing logic
}

// RECOMMENDATION 3: RAVE value estimation (optional enhancement)
float RAVEValue = (RaveWeight * RaveQ + (1 - RaveWeight) * NodeQ);
```

---

## 3. RL + StateTree Tactics Layer Analysis

### 3.1 Action Space (v4.0 Macro Actions)

```
MultiDiscrete([4, N+1, 3])
├── Position: Hold(0), ForwardCover(1), Retreat(2), Advance(3)
├── Target: None(-1), Enemy_0(0), ..., Enemy_N(N)
└── FireMode: HoldFire(0), Fire(1), Suppress(2)
```

**Total Combinations:** 4 × 6 × 3 = 72 discrete states (vs. infinite continuous in v3.x)

### 3.2 Execution Pipeline

```
RLlib PPO Policy
    ↓ gRPC (Schola)
TacticalActuator (receives FMacroAction)
    ↓
STTask_ExecuteObjective
    ├── ExecuteMovement() → EQS Query → NavMesh Pathfinding
    ├── ExecuteAiming() → AIController.SetFocus()
    └── ExecuteFire() → WeaponComponent.FireInDirection()
```

### 3.3 Strengths

1. **Clean Separation**: v4.0 macro actions correctly delegate physics to UE5 engine
2. **EQS Integration**: Proper use of Environment Query System for tactical positions
3. **Action Throttling**: Both UE5-side (`DecisionInterval`) and StateTree-side (`ActionApplicationInterval`)
4. **Fallback Handling**: Advance action has direct pathfinding fallback if EQS fails

### 3.4 Issues Identified

| Issue | Severity | Location | Description |
|-------|----------|----------|-------------|
| **Target Index Overflow** | High | `sbdapm_env.py:89-90` | Action space `[4, 11, 3, 3]` but network expects `[4, 6, 3]` |
| **Missing Action Masking** | Medium | `RLPolicyNetwork.cpp:308` | `GetActionWithMask()` ignores mask - invalid actions possible |
| **Movement Interruption** | Medium | `STTask_ExecuteObjective.cpp:244-249` | Stops movement on every action change, even if destination unchanged |

### 3.5 Recommendations

```cpp
// RECOMMENDATION 1: Implement action masking for dynamic target count
FTacticalAction URLPolicyNetwork::GetActionWithMask(...)
{
    // Get visible enemy count from observation
    int32 VisibleEnemies = FMath::Min(Observation.VisibleEnemyCount, 5);

    // Mask invalid target indices
    for (int32 i = VisibleEnemies + 1; i < TargetLogits.Num(); ++i)
    {
        TargetLogits[i] = -1e9f;  // Effectively zero after softmax
    }
    // ...sample from masked logits
}

// RECOMMENDATION 2: Validate EQS queries on BeginPlay
EStateTreeRunStatus FSTTask_ExecuteObjective::EnterState(...)
{
    if (!InstanceData.ForwardCoverQuery || !InstanceData.RetreatQuery || !InstanceData.AdvanceQuery)
    {
        UE_LOG(LogTemp, Error, TEXT("EQS queries not assigned! Check StateTree editor."));
        return EStateTreeRunStatus::Failed;
    }
    // ...existing logic
}
```

---

## 4. Python Training Structure Analysis

### 4.1 Training Pipeline

```python
train_rllib.py
    ├── create_ppo_config() - PPO hyperparameters
    ├── register_env("sbdapm_env") - Multi-agent environment
    ├── Training Loop (100 iterations default)
    │   ├── algo.train() - RLlib PPO step
    │   └── Checkpoint every 10 iterations
    └── export_onnx() - Dual-head model export

sbdapm_env.py (SBDAPMMultiAgentEnv)
    ├── reset() → schola_env.hard_reset()
    ├── step(action_dict)
    │   ├── Format actions for all DictSpace keys
    │   ├── send_actions() via gRPC
    │   └── poll() - blocking wait for UE5 observations
    └── Episode termination logic
```

### 4.2 Hyperparameters

| Parameter | Value | Assessment |
|-----------|-------|------------|
| Learning Rate | 3e-4 | Good (standard PPO) |
| Train Batch Size | 4000 | May be too small for multi-agent |
| Entropy Coefficient | 0.5 | High - good for exploration |
| Gamma | 0.99 | Standard |
| GAE Lambda | 0.95 | Standard |
| Network | [128, 128, 64] | Reasonable for 74-dim input |

### 4.3 Issues Identified

| Issue | Severity | Location | Description |
|-------|----------|----------|-------------|
| **Action Space Mismatch** | Critical | `sbdapm_env.py:90` | `MultiDiscrete([4, 11, 3, 3])` but v4.0 uses `[4, 6, 3]` (no stance) |
| **Observation Size Mismatch** | High | `sbdapm_env.py:89,337` | Uses 78 (old) instead of 74 (v4.0) features |
| **Small Batch Size** | Medium | `train_rllib.py:63` | 4000 may be insufficient for 4+ agents |
| **No Curriculum Learning** | Medium | N/A | Mentioned in docs but not implemented |
| **Episode Timeout** | Low | `sbdapm_env.py:52` | 180s timeout may be too long for fast training |

### 4.4 Recommendations

```python
# RECOMMENDATION 1: Fix action/observation spaces for v4.0
self._obs_space = spaces.Box(low=-np.inf, high=np.inf, shape=(74,), dtype=np.float32)
self._action_space = spaces.MultiDiscrete([4, 6, 3])  # 4 pos × 6 target × 3 fire

# RECOMMENDATION 2: Increase batch size for multi-agent
TRAIN_BATCH_SIZE = 8000  # Double for 4-agent training
SGD_MINIBATCH_SIZE = 256  # Also double

# RECOMMENDATION 3: Add curriculum learning
class CurriculumCallback(DefaultCallbacks):
    def on_train_result(self, algorithm, result, **kwargs):
        # Increase difficulty as reward improves
        mean_reward = result.get("episode_reward_mean", 0)
        if mean_reward > 50:
            algorithm.workers.foreach_worker(
                lambda w: w.set_episode_difficulty(2)
            )

# RECOMMENDATION 4: Shorter initial episodes for faster iteration
MAX_EPISODE_DURATION = 60.0  # 1 minute episodes initially
```

---

## 5. Integration Assessment

### 5.1 Data Flow Verification

```
✅ MCTS → Objective Assignment → FollowerAgent
✅ FollowerAgent → Observation → TacticalObserver → Schola gRPC
✅ RLlib → Action → TacticalActuator → FMacroAction
✅ StateTree → EQS/NavMesh/Weapon → Action Execution
✅ RewardCalculator → Hierarchical Rewards → Schola gRPC
```

### 5.2 Integration Issues

| Flow | Issue | Impact |
|------|-------|--------|
| MCTS → RL | Priors use 7 objectives, v4.0 has 4 | Incorrect action distribution |
| Python → C++ | Action space size mismatch | Training instability |
| EQS → NavMesh | Queries not pre-assigned | Runtime failures |
| Reward → Policy | Cover reward depends on v3.x velocity | Always zero in v4.0 |

---

## 6. Identified Issues and Recommendations

### 6.1 Critical Issues (Must Fix)

| # | Issue | Location | Recommended Fix |
|---|-------|----------|-----------------|
| 1 | Action space mismatch `[4,11,3,3]` vs `[4,6,3]` | `sbdapm_env.py:90` | Update to `MultiDiscrete([4, 6, 3])` |
| 2 | Observation size 78 vs 74 | `sbdapm_env.py:89,337` | Change to 74 features |
| 3 | Objective priors return 7, need 4 | `RLPolicyNetwork.cpp:400` | Rewrite for 4 objective types |
| 4 | EQS queries must be assigned | StateTree editor | Document setup, add validation |

### 6.2 High Priority Issues

| # | Issue | Location | Recommended Fix |
|---|-------|----------|-----------------|
| 5 | No action masking for targets | `RLPolicyNetwork.cpp:308` | Mask invalid target indices |
| 6 | Cover reward broken in v4.0 | `RewardCalculator.cpp:241-243` | Use position-based cover check |
| 7 | Small training batch size | `train_rllib.py:63` | Increase to 8000+ |

### 6.3 Medium Priority Issues

| # | Issue | Recommendation |
|---|-------|----------------|
| 8 | No ally information in observations | Add 12 features for 3 nearest allies |
| 9 | Missing curriculum learning | Implement difficulty scaling |
| 10 | No model version tracking | Add version metadata to ONNX |

---

## 7. Recommended Methodologies

### 7.1 For Better MCTS

1. **PUCT (Polynomial UCT)**: Already using UCB1 + priors, consider tuning exploration constant
2. **Virtual Loss**: For parallel MCTS, prevent tree lock contention
3. **Transposition Tables**: Cache evaluations for similar game states

### 7.2 For Better RL Training

1. **Population-Based Training (PBT)**: Auto-tune hyperparameters during training
2. **Behavior Cloning Warmstart**: Pre-train on expert demonstrations before RL
3. **Self-Play**: Train against past versions for robust policies
4. **Reward Shaping Automation**: Use inverse RL to discover optimal reward weights

### 7.3 For Better Integration

1. **Separate Actor-Critic Networks**: Allow different learning rates for policy/value
2. **Recurrent Policies (LSTM)**: For temporal reasoning in partially observable environments
3. **Attention Mechanisms**: For variable-length enemy/ally observations

### 7.4 Alternative Architectures to Consider

| Architecture | Pros | Cons | When to Use |
|--------------|------|------|-------------|
| **Hierarchical RL (HRL)** | Natural abstraction | Complex training | Many sub-goals |
| **Multi-Agent PPO (MAPPO)** | Centralized critic | Communication overhead | Cooperative teams |
| **QMIX** | Factorized value function | Discrete actions only | Tight coordination needed |
| **Feudal Networks** | Explicit manager/worker | Double the parameters | Long time horizons |

---

## 8. Priority Action Items

### Phase 1: Fix Critical Mismatches (1-2 days)

```bash
# 1. Update Python environment spaces
# sbdapm_env.py
self._obs_space = spaces.Box(low=-np.inf, high=np.inf, shape=(74,), dtype=np.float32)
self._action_space = spaces.MultiDiscrete([4, 6, 3])

# 2. Update all references to 78 → 74 in sbdapm_env.py
# Lines: 89, 337, 397

# 3. Fix RLPolicyNetwork::GetObjectivePriors() for 4 objectives
# RLPolicyNetwork.cpp:393-500
```

### Phase 2: Complete Integration Testing (2-3 days)

1. Run full training loop with fixed spaces
2. Verify EQS queries work in PIE mode
3. Check reward values are non-zero
4. Confirm ONNX export loads in UE5

### Phase 3: Optimization (1 week)

1. Implement action masking for targets
2. Add ally observation features
3. Increase batch size and tune hyperparameters
4. Add curriculum learning callback

### Phase 4: Long-term Improvements (2+ weeks)

1. Consider MAPPO for centralized training
2. Add LSTM for temporal reasoning
3. Implement self-play for robustness
4. Build visualization/debugging tools

---

## Conclusion

The CORTEX hierarchical AI system demonstrates a **well-thought-out architecture** that correctly separates strategic planning (MCTS) from tactical execution (RL + StateTree). The v4.0 macro action design is a significant improvement over continuous control, enabling faster learning.

**Key Strengths:**
- Clean separation of concerns (strategy vs tactics)
- Proper use of game engine features (NavMesh, EQS, SetFocus)
- Handcrafted strategic heuristics (faster than neural network)
- Hierarchical reward structure with alignment

**Critical Next Steps:**
1. Fix action/observation space mismatches between Python and C++
2. Update objective priors from 7 to 4 types
3. Complete integration testing with EQS queries assigned
4. Increase training batch size for multi-agent stability

With the identified fixes implemented, the system should be ready for effective training and deployment.

---

*Generated by Claude Opus 4.5 - 2025-12-28*
