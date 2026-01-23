# CORTEX v8.0: Tactical Parameters Architecture

**Engine:** UE5.6 | **Language:** C++17 | **Platform:** Windows | **Version:** v8.0 (Low-Level Action Space)

---

## Executive Summary

CORTEX v8.0 implements a hierarchical multi-agent AI system where **MCTS assigns strategies** and **RL controls tactical parameters**. This architecture eliminates the redundancy of v7.0 (where both layers selected similar high-level decisions) by moving RL to tactical parameter control that modulates EQS spatial reasoning.

**Core Innovation:**
```
MCTS (Strategic) → Assigns Strategies [Assault, Defend, Support, Retreat]
   ↓
RL (Tactical) → Outputs Tactical Parameters + Combat Choices
   ↓
EQS (Execution) → Uses parameters as query weights + NavMesh movement
   ↓
Rules (Combat) → Auto-targeting + Auto-firing
```

**Key Advantages:**
- Lower risk: Leverages proven EQS spatial reasoning
- Faster training: 4 continuous parameters easier than discrete movement
- Graceful degradation: Random parameters still produce valid EQS movement
- Guaranteed differentiation: Separate policy heads per strategy

---

## Quick Reference

### Decision Tree
```
Task Type?
├─ Add Feature → Read affected files → Check v8.0 patterns → Implement → Test
├─ Fix Bug → Reproduce → Read stack trace → Locate file:line → Fix → Verify
├─ Optimize → Profile first → Identify bottleneck → Apply pattern → Benchmark
└─ Refactor → Read dependencies → Plan backwards from usage → Implement → Validate
```

### Performance Targets
| Component | Max Latency | Memory | Notes |
|-----------|-------------|--------|-------|
| MCTS | 30-50ms | 1MB | Strategy assignment (async, 1.5s intervals) |
| RL Inference | 2-4ms | 458KB | Batched inference (4 agents → single forward pass) |
| EQS Queries | 1-2ms | 100KB | Tactical position selection (2-5 Hz) |
| StateTree | <0.5ms/agent | 100KB | Rule-based execution |
| **Total (4 agents)** | **10-20ms/sec** | **4MB** | Real-time requirement (50% reduction vs v7.0) |

### File Locations (Quick Jump)
| Feature | Path | Key Methods |
|---------|------|-------------|
| MCTS | `AI/MCTS/MCTS.cpp` | `RunMCTS()`, `EvaluateAssignment()` |
| RL Policy | `RL/RLPolicyNetwork.cpp` | `GetTacticalParameters()`, `GetCombatChoice()` |
| Tactical Movement | `StateTree/Tasks/STTask_ExecuteTacticalMovement_v8.cpp` | `ApplyTacticalParameters()` |
| Reward Calculator | `RL/RewardCalculator.cpp` | `CalculateUnifiedReward()` |
| Team Leader | `Team/TeamLeaderComponent.cpp` | `RunStrategyAssignment()` |
| Follower | `Team/FollowerAgentComponent.cpp` | `UpdateTacticalParameters()` |
| Types & Config | `RL/RLTypes.h` | `FTacticalParameters`, `FCombatParameters` |

---

## Architecture Overview

### Three-Layer Hierarchy

**Layer 1: MCTS (Strategic Decision)**
- **Responsibility:** Team-level strategy assignment
- **Frequency:** Async, every 1.5s
- **Output:** Strategy + Target Objective for each agent
- **Action Space:** Agent-to-Strategy-to-Objective mapping

**Layer 2: RL (Tactical Parameter Control)**
- **Responsibility:** Modulate EQS behavior via tactical parameters + combat choices
- **Frequency:** 2-5 Hz (event-driven or periodic)
- **Input:** 68 observation features + 4 strategy features (one-hot)
- **Output:** 4 continuous tactical parameters + 2 discrete combat choices

**Layer 3: EQS + Rules (Execution)**
- **Responsibility:** Spatial reasoning (EQS) + combat execution (rules)
- **Frequency:** 2-5 Hz (EQS), 60 Hz (combat)
- **Logic:** Environmental Query System with RL-modulated weights + auto-targeting

### System Flow

```
┌─────────────────────────────────────────────────────────────┐
│  Team Leader (1 per team, async every 1.5s)                 │
│  ├─ MCTS: Solves Strategy assignment problem               │
│  │   ├─ Action Space: Which agents → which Strategies?     │
│  │   ├─ Evaluation: RL value estimates + coordination      │
│  │   └─ Output: FStrategyAssignment per agent              │
│  └─ Broadcasts assignments to followers                     │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│  Followers (N agents, tactical control 2-5 Hz)              │
│  ├─ RL Policy Network (Multi-Head Architecture):           │
│  │   ├─ Shared Feature Extractor: [68] → [128] → [128] → [64] │
│  │   ├─ Strategy-Specific Policy Heads (4 heads):         │
│  │   │   ├─ Assault Head → [Aggression, Cover, Spread, Risk] │
│  │   │   ├─ Defend Head → [Aggression, Cover, Spread, Risk]  │
│  │   │   ├─ Support Head → [Aggression, Cover, Spread, Risk] │
│  │   │   └─ Retreat Head → [Aggression, Cover, Spread, Risk] │
│  │   ├─ Combat Head → [TargetPriority: Closest/LowestHP]  │
│  │   └─ Critic Head → State Value                         │
│  ├─ EQS Execution:                                         │
│  │   ├─ Tactical parameters → EQS weights                 │
│  │   ├─ EQS → Optimal tactical position                   │
│  │   └─ NavMesh → Movement execution                      │
│  └─ Combat Execution:                                      │
│      ├─ Target priority → Target selection                │
│      └─ Auto-aim + Auto-fire                              │
└─────────────────────────────────────────────────────────────┘
```

---

## Core Components

### 1. RL Policy Network (`RL/RLPolicyNetwork.cpp`)

**Purpose:** Strategy-specific tactical parameter generation + combat decision making

**Network Architecture:**
```
Input: 72 features (68 observation + 4 strategy one-hot)
   ↓
Shared Feature Extractor:
   [72] → FC(128, ReLU) → FC(128, ReLU) → FC(64, ReLU)
   ↓
   Shared Features [64]
   ↓
   ┌─────────────┬──────────────┬──────────────┬──────────────┬──────────────┐
   ↓             ↓              ↓              ↓              ↓              ↓
Assault Head  Defend Head  Support Head  Retreat Head  Combat Head    Critic Head
FC(64→32)     FC(64→32)    FC(64→32)     FC(64→32)     FC(64→2)       FC(64→1)
FC(32→4)      FC(32→4)     FC(32→4)      FC(32→4)      Softmax        Linear
Sigmoid       Sigmoid      Sigmoid       Sigmoid
   ↓             ↓              ↓              ↓              ↓              ↓
[4 params]    [4 params]   [4 params]    [4 params]    [Priority]     Value
```

**Key Methods:**

```cpp
// Get tactical parameters for assigned strategy
FTacticalParameters GetTacticalParameters(
    const FObservationElement& Obs,
    EStrategyType AssignedStrategy);

// Get combat choice
FCombatParameters GetCombatChoice(const FObservationElement& Obs);

// Get state value (for MCTS evaluation)
float GetStateValue(const FObservationElement& Obs, EStrategyType Strategy);
```

**Why Separate Heads?**
- **Guaranteed Differentiation:** Each strategy has dedicated output layers
- **Easier Debugging:** Can visualize each strategy's learned parameters independently
- **Faster Convergence:** No competition between strategies for shared output weights
- **Parameter Cost:** +34% model size (24KB → 33KB, negligible)

**Files:** `RL/RLPolicyNetwork.h/cpp`, `RL/RLTypes.h`

---

### 2. Tactical Movement (`StateTree/Tasks/STTask_ExecuteTacticalMovement_v8.cpp`)

**Purpose:** Apply RL tactical parameters to EQS queries

**EQS Parameter Modulation:**

**Files:** `StateTree/Tasks/STTask_ExecuteTacticalMovement_v8.h/cpp`

---

### 3. Combat Execution (`Team/FollowerAgentComponent.cpp`)

**Purpose:** Learned target selection + auto-firing

**Combat Logic:**

void UFollowerAgentComponent::ExecuteCombat(FCombatParameters Combat)


**Why Learned Targeting but Not Aiming?**
- **Positioning Dominance:** In tactical shooters, positioning accounts for 70-80% of combat effectiveness
- **Complexity Budget:** Learning tactical parameters + target priority is sufficient for v8.0
- **Incremental Path:** v8.0 establishes movement foundation, v8.5 can add learned aiming

**Files:** `Team/FollowerAgentComponent.h/cpp`

---

### 4. Unified Reward System (`RL/RewardCalculator.cpp`)

**Purpose:** Strategy-specific reward calculation via weight profiles

**Architecture:**

class URewardCalculator

**Strategy-Specific Weight Profiles:**

```cpp
// Assault: High objective progress, high combat, medium survival
ASSAULT_WEIGHTS = {
    ObjectiveProgress: 1.0,
    CombatEffectiveness: 0.8,
    Survival: 0.6,
    CoverUsage: 0.3,
    TeamCoordination: 0.4
};

// Defend: Low objective progress, medium combat, high survival
DEFEND_WEIGHTS = {
    ObjectiveProgress: 0.2,
    CombatEffectiveness: 0.6,
    Survival: 1.0,
    CoverUsage: 0.9,
    TeamCoordination: 0.5
};

// Support: Medium objective, low combat, high coordination
SUPPORT_WEIGHTS = {
    ObjectiveProgress: 0.5,
    CombatEffectiveness: 0.4,
    Survival: 0.7,
    CoverUsage: 0.5,
    TeamCoordination: 1.0
};

// Retreat: High objective (reach safe zone), zero combat, highest survival
RETREAT_WEIGHTS = {
    ObjectiveProgress: 0.8,
    CombatEffectiveness: 0.0,
    Survival: 1.2,
    CoverUsage: 0.7,
    TeamCoordination: 0.3
};
```

**Reward Components:**

```cpp
// Objective progress (strategy-dependent interpretation)
float CalculateObjectiveReward(const FAgentState& State)
{
    if (State.DistanceToObjective < State.LastDistanceToObjective)
        return 0.1f;  // Moving closer (scaled by strategy weight)
    return 0.0f;
}

// Combat effectiveness (kills, damage dealt)
float CalculateCombatReward(const FAgentState& State)
{
    float Reward = 0.0f;
    if (State.DealtDamage)
        Reward += State.DamageAmount / 10.0f;
    if (State.KilledEnemy)
        Reward += 10.0f;  // Base kill reward
    if (State.TargetWasLowestHP)
        Reward += 2.0f;   // Bonus for efficient targeting
    return Reward;
}

// Survival (death penalty)
float CalculateSurvivalReward(const FAgentState& State)
{
    if (State.Died)
        return -10.0f;  // Base death penalty (scaled by strategy weight)
    return 0.0f;
}

// Cover usage (tactical positioning)
float CalculateCoverReward(const FAgentState& State)
{
    if (State.InCover)
        return 0.1f;  // Continuous cover bonus
    return 0.0f;
}

// Team coordination (proximity to allies, formation)
float CalculateCoordinationReward(const FAgentState& State)
{
    float Reward = 0.0f;
    if (State.AllyState)
    {
        float Distance = GetDistance(State.Position, State.AllyState.Position);
        if (200 < Distance && Distance < 800)  // Optimal support range
            Reward += 0.1f;
    }
    return Reward;
}
```

**Volume-Based Objective Rewards (v7.0 Legacy):**

```cpp
// Defense role (friendly objective)
if (AgentInFriendlyVolume)
    Reward += 0.05f;  // +0.05 per step (~0.5/sec at 10Hz)

if (KilledEnemy && AgentInFriendlyVolume)
    Reward += 5.0f;   // +5.0 for defending kills

// Assault role (hostile objective)
if (AgentInEnemyVolume)
    Reward += 0.1f;   // +0.1 per step (~1.0/sec at 10Hz)

if (EnemyObjective.IsDefeated())
    Reward += 100.0f; // +100.0 for mission success
```

**Files:** `RL/RewardCalculator.h/cpp`

---

### 5. Team Leader (`Team/TeamLeaderComponent.cpp`)

**Purpose:** MCTS-based strategy assignment

**MCTS Action Space:**

```cpp
// Each node in MCTS tree represents a strategy assignment
struct FStrategyAssignment
{
    TMap<AActor*, FStrategyAssignmentData> AgentAssignments;
    // Example:
    // Agent1 → {Strategy: Assault, Objective: EnemyBase}
    // Agent2 → {Strategy: Assault, Objective: EnemyBase}
    // Agent3 → {Strategy: Defend, Objective: FriendlyBase}
    // Agent4 → {Strategy: Support, Objective: Agent1}
};
```

**Evaluation Function:**

float EvaluateAssignment(FStrategyAssignment& Assignment)

**Files:** `Team/TeamLeaderComponent.h/cpp`, `AI/MCTS/MCTS.h/cpp`

---

### 6. Observation System (`Observation/TacticalObserver.cpp`)

**v8.0 Observation Space (72 features):**

```
Agent State (7): pos(3), vel(3), health(1)
Combat (1): enemy_dist(1)
Perception (32): raycasts(16), hit_types(16)
Enemy Info (16): count(1), nearby(15)
Tactical (4): has_cover(1), cover_dist(1), cover_dir(2)
Support Context (4): ally_needs(1), ally_health(1), ally_dist(1), ally_dir(1)
Objective Context (4): type(1), distance(1), direction(2)
Strategy Context (4): one-hot encoding [Assault, Defend, Support, Retreat]

Total: 68 base + 4 strategy = 72 features
```

**Feature Extraction:**

FObservationElement BuildObservation(AActor* Agent, AObjectiveActor* TargetObjective, EStrategyType Strategy)


**Files:** `Observation/TacticalObserver.h/cpp`, `Observation/ObservationElement.h`

---

### 7. Objective System (`Team/ObjectiveActor.cpp`)

**Purpose:** Durability-based capture mechanics

**ObjectiveActor Properties:**

class AObjectiveActor : public AActor

**Durability Mechanics:**

```cpp
// Decline logic (10Hz timer)
if (HostileAgentsInVolume.Num() > 0)
{
    float DeclineRate = HostileAgentsInVolume.Num() * DamagePerAgentPerSecond;
    CurrentDurability -= DeclineRate * DeltaTime;
    // Default: 2.0 damage/sec per hostile agent
}

// Recovery logic
if (HostileAgentsInVolume.Num() == 0 && CurrentDurability < MaxDurability)
{
    CurrentDurability += RecoveryPerSecond * DeltaTime;
    // Default: 1.0 durability/sec recovery
}

// Defeat condition
if (CurrentDurability <= 0.0f)
{
    OnDefeat(); // Broadcast to GameMode → Episode reset
}
```

**Files:** `Team/ObjectiveActor.h/cpp`, `Team/ObjectiveManager.h/cpp`

---

## Action Space Design

### v8.0 Action Space

**Tactical Parameters (4 continuous, strategy-specific):**
```cpp
struct FTacticalParameters
{
    float Aggression;        // [0,1] - modulates EQS aggression weights
    float CoverPreference;   // [0,1] - modulates EQS cover weights
    float SpreadDistance;    // [0,1] - modulates EQS formation weights
    float RiskTolerance;     // [0,1] - retreat threshold
};
```

**Combat Parameters (2 discrete choices):**
```cpp
struct FCombatParameters
{
    ETargetPriority Priority;    // Closest, LowestHP
};
```

**Total Action Space:**
- 4 continuous tactical parameters (movement)
- 2 target priorities (combat)
- Complexity: 4 continuous + 1 discrete (2 states)

**Expected Learned Behaviors:**

| Strategy | Aggression | CoverPref | Spread | Risk | Target Priority |
|----------|-----------|-----------|--------|------|----------------|
| **Assault** | >0.7 (high) | <0.4 (low) | 0.5-0.7 | 0.6-0.8 | LowestHP (finish kills) |
| **Defend** | <0.3 (low) | >0.7 (high) | <0.5 | <0.3 | Closest (suppress) |
| **Support** | 0.4-0.6 | 0.5 | <0.3 | 0.5 | Closest (protect ally) |
| **Retreat** | <0.2 | 0.7-0.9 | >0.8 | >0.8 | None (avoid combat) |

---

## Design Patterns & Principles

### Hierarchical Decision Making

| Layer | Responsibility | Update Frequency | Latency | Output |
|-------|----------------|------------------|---------|--------|
| **MCTS** | Team coordination | 1.5s (async) | 30-50ms | Strategy assignments |
| **RL** | Tactical parameters | 2-5 Hz | 2-4ms (batched) | Tactical + combat params |
| **EQS** | Spatial reasoning | 2-5 Hz | 1-2ms | Tactical positions |
| **Rules** | Combat execution | 60 Hz | <0.1ms | Targeting + firing |

### Architectural Invariants

1. **ONLY Leaders run MCTS** (followers NEVER touch MCTS)
2. **MCTS assigns strategies** (Assault, Defend, Support, Retreat)
3. **RL outputs tactical parameters** (modulates EQS weights)
4. **RL does NOT select strategies** (v8.0 change from v7.0)
5. **Separate policy heads per strategy** (guaranteed differentiation)
6. **EQS handles spatial reasoning** (RL focuses on how aggressive/defensive)
7. **Rules are deterministic** (no learning in combat execution)
8. **MCTS uses RL value estimates** (learned heuristics for leaf evaluation)
9. **Async MCTS, sync RL** (MCTS doesn't block RL execution)
10. **Objectives are physical actors** (durability-based capture)

### Clear Interfaces

```cpp
// MCTS → RL (value query)
interface IValueEstimator
{
    float GetStateValue(Observation, Strategy);
};

// Leader → Follower (command)
interface IStrategyReceiver
{
    void ReceiveStrategyAssignment(FStrategyAssignment);
};

// RL → EQS (parameter application)
interface ITacticalExecutor
{
    void ApplyTacticalParameters(FTacticalParameters);
};
```

---

## Performance Optimization

### Batched Inference (4 Agents → 1 Network Call)

**Problem:** Naïve approach = 4 agents × 2ms = 8ms per update

**Solution:** Batch all agents into single forward pass

```cpp
// Batch inference for all agents
TArray<FTacticalParameters> GetTacticalParametersBatched(
    TArray<FObservationElement> Observations,
    TArray<EStrategyType> Strategies)
{
    // Build batched input tensor [4, 72]
    FTensor BatchedInput = BuildBatchedTensor(Observations, Strategies);

    // Single ONNX forward pass (2-4ms for 4 agents)
    FTensor BatchedOutput = ONNXRuntime->Run(BatchedInput);

    // Parse outputs per agent
    TArray<FTacticalParameters> Results;
    for (int i = 0; i < 4; i++)
    {
        Results.Add(ParseTacticalParams(BatchedOutput, i, Strategies[i]));
    }

    return Results;
}
```

**Performance:**
```
Naive (sequential): 4 × 2ms = 8ms
Batched (parallel):  1 × 3ms = 3ms
Speedup: 2.6× faster
```

### Event-Driven Updates

**Problem:** Running RL inference every tick (60 FPS) = too expensive

**Solution:** Only update parameters on significant events or periodic timeout

```cpp
bool UFollowerAgentComponent::ShouldUpdateParameters() const
{
    bool HealthChanged = FMath::Abs(CurrentHealth - LastParamHealth) > 0.2f;
    bool NewEnemyDetected = PerceivedEnemies.Num() > LastEnemyCount;
    bool StrategyChanged = CurrentStrategy != LastStrategy;
    bool Timeout = TicksSinceLastUpdate > 12;  // 5 Hz fallback

    return HealthChanged || NewEnemyDetected || StrategyChanged || Timeout;
}
```

**Performance Impact:**
```
Before (every tick):  60 FPS × 3ms = 180ms/sec
After (event-driven): ~5 updates/sec × 3ms = 15ms/sec
Reduction: 92% lower inference cost
```

### Async MCTS Execution

```cpp
// MCTS runs on background thread
FGraphEventRef MCTSTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
    [this]()
    {
        // Run MCTS (30-50ms, doesn't block main thread)
        FStrategyAssignment BestAssignment = MCTS->RunSearch(CurrentState);

        // Store result (thread-safe)
        FScopeLock Lock(&AssignmentMutex);
        PendingAssignment = BestAssignment;
        bHasPendingAssignment = true;
    },
    TStatId(),
    nullptr,
    ENamedThreads::AnyBackgroundThreadNormalTask
);

// Main thread: Apply assignment when ready
void UTeamLeaderComponent::TickComponent(float DeltaTime, ...)
{
    if (bHasPendingAssignment)
    {
        FScopeLock Lock(&AssignmentMutex);
        ApplyAssignment(PendingAssignment);
        bHasPendingAssignment = false;
    }
}
```

---

## Training & Curriculum

### Curriculum Learning Schedule

**Phase 1: Single Strategy Training (Episodes 0-1000)**
```
All agents use same strategy
Goal: Learn basic tactical parameter meanings
Example: All agents assigned Assault
Learns: High aggression → close to enemies, low cover → exposed positions
```

**Phase 2: Mixed Strategy Training (Episodes 1000-3000)**
```
MCTS explores simple 2-2 splits
Goal: Differentiate strategy behaviors
Example: 2 agents Assault, 2 agents Defend
Learns: Different parameter profiles per strategy
```

**Phase 3: Dynamic Strategy Assignment (Episodes 3000+)**
```
MCTS freely assigns strategies based on game state
Goal: Adaptive tactics, strategy coordination
Example: 3 Assault, 1 Support (dynamic based on health)
Learns: Context-dependent parameter adaptation
```

### Reward Shaping

**Dense Intermediate Rewards:**
```python
reward_per_step = 0.0

# Progress rewards (every step, strategy-weighted)
reward_per_step += proximity_reward(strategy) * weights[strategy]['objective']
reward_per_step += cover_reward(strategy) * weights[strategy]['cover']
reward_per_step += formation_reward(strategy) * weights[strategy]['coordination']

# Combat rewards (event-based, strategy-weighted)
reward_per_step += damage_reward() * weights[strategy]['combat']
reward_per_step += kill_reward() * weights[strategy]['combat']
reward_per_step += death_penalty() * weights[strategy]['survival']

# Terminal rewards (episode end)
if episode_complete:
    reward_per_step += objective_completion_reward(team_performance)
```

### Success Criteria

**Quantitative Metrics:**
- [ ] Inference latency: <20ms/sec (4 agents batched at 5 Hz)
- [ ] Training convergence: <6,000 episodes
- [ ] Win rate vs v7.0: >60%
- [ ] Win rate vs random parameters: >90%

**Behavioral Metrics - Tactical Parameters:**
- [ ] Assault: High Aggression (>0.7), Low CoverPref (<0.4)
- [ ] Defend: Low Aggression (<0.3), High CoverPref (>0.7)
- [ ] Support: Moderate Aggression (0.4-0.6), Low Spread (<0.3)
- [ ] Retreat: Low Aggression (<0.2), High Risk (>0.8)
- [ ] Parameter differentiation: Mean absolute difference >0.3 per parameter

**Behavioral Metrics - Combat:**
- [ ] Assault: LowestHP priority >60% of time
- [ ] Defend: Closest priority >60% of time
- [ ] Target priority learning: LowestHP selection improves kill rate >10%

---

## Implementation Status

### v8.0 Implementation Progress

**Week 1: Action Space & Network Redesign ✅ COMPLETED**
- [x] Define `FTacticalParameters` struct
- [x] Define `FCombatParameters` struct
- [x] Update `FMacroAction` to include tactical and combat parameters
- [x] Implement separate strategy-specific policy heads
- [x] Update ONNX export/import logic for multi-head architecture
- [x] Add strategy routing logic
- [x] Remove v7.0 strategy selection from RL action space

**Week 2: EQS Integration & Combat Execution ✅ COMPLETED**
- [x] Implement `STTask_ExecuteTacticalMovement_v8.cpp`
- [x] Add `ApplyTacticalParameters()` function
- [x] Modify EQS queries to accept RL-controlled weights
- [x] Implement parameter-to-EQS weight mapping
- [x] Implement `ExecuteCombat()` with learned target priority
- [x] Add target selection logic (Closest, LowestHP)
- [x] Performance validation: <20ms/sec with combat

**Week 3: Unified Reward System ✅ COMPLETED**
- [x] Implement `StrategyRewardCalculator` class
- [x] Define strategy-specific weight profiles
- [x] Add combat reward components
- [x] Add TensorBoard logging for reward breakdown
- [x] Update `RewardCalculator.cpp` with new reward structure
- [x] Implement separate reward tracking for tactical vs combat
- [x] Remove v7.0 reward calculation logic

**Week 4: Training Pipeline 🔄 IN PROGRESS**
- [x] Update Python training environment (4 continuous + 2 discrete)
- [x] Implement multi-head policy network
- [x] Configure PPO for hybrid action space
- [x] Implement curriculum learning schedule
- [ ] Run initial training (1,000-2,000 episodes baseline)
- [ ] Monitor strategy-specific parameter profiles
- [ ] Monitor combat choice differentiation

**Week 5: Extended Training & Validation**
- [ ] Continue training to 4,000-6,000 episodes
- [ ] Hyperparameter sweep (learning rate, reward weights)
- [ ] Monitor convergence (loss, win rate, behavioral metrics)
- [ ] Validate strategy-specific behaviors
- [ ] Validate combat effectiveness
- [ ] Head-to-head: v8.0 vs v7.0 (100 matches)
- [ ] GO/NO-GO: If >60% win rate → Merge to main

---

## Configuration & Sim2Real

### RLConfig Namespace

**Single Source of Truth (`RL/RLTypes.h`):**

```cpp
namespace RLConfig
{
    // Movement (must match UE5 CharacterMovement)
    constexpr float AGENT_WALK_SPEED = 600.0f;      // cm/s
    constexpr float AGENT_RUN_SPEED = 900.0f;
    constexpr float AGENT_SPRINT_SPEED = 1200.0f;

    // Perception (must match UE5 AIPerception)
    constexpr float PERCEPTION_RADIUS = 3000.0f;    // cm
    constexpr int32 RAYCAST_COUNT = 16;
    constexpr float RAYCAST_LENGTH = 2000.0f;       // cm

    // Combat (must match UE5 damage system)
    constexpr float BASE_DAMAGE = 10.0f;
    constexpr float MAX_HEALTH = 100.0f;

    // Action Space (v8.0)
    constexpr int32 NUM_STRATEGIES = 4;  // MCTS-assigned
    constexpr int32 NUM_TACTICAL_PARAMS = 4;  // RL continuous outputs
    constexpr int32 NUM_COMBAT_CHOICES = 2;   // RL discrete outputs

    // Observation Space
    constexpr int32 OBSERVATION_SIZE = 68;  // 64 base + 4 strategy context
}
```

**Python Sync (Auto-Generated):**
```bash
python tools/sync_config_from_cpp.py
# Generates: CORTEX_Training/training_env/config.py
```

---

## Debug Visualization

### In-Game Debug Commands

```cpp
// Console commands
ToggleMCTSDebug     // Show MCTS strategy assignments
ToggleRLDebug       // Show RL tactical parameters
PrintMCTSStats      // Log MCTS performance metrics
PrintRLStats        // Log RL inference latency
```

### Visual Debugging

**MCTS Assignments:**
- Yellow arrows: Agent → Objective
- Text labels: Strategy name + expected value
- Color-coded spheres: Strategy type

**RL Tactical Parameters:**
```cpp
void UFollowerAgentComponent::DebugDrawTacticalState()
{
    // Draw parameter values
    DrawDebugString(World, Location + FVector(0,0,200),
        FString::Printf(TEXT("Aggression: %.2f"), TacticalParams.Aggression),
        nullptr, FColor::Red, 0.0f, true);

    DrawDebugString(World, Location + FVector(0,0,180),
        FString::Printf(TEXT("Cover: %.2f"), TacticalParams.CoverPreference),
        nullptr, FColor::Blue, 0.0f, true);

    DrawDebugString(World, Location + FVector(0,0,160),
        FString::Printf(TEXT("Spread: %.2f"), TacticalParams.SpreadDistance),
        nullptr, FColor::Green, 0.0f, true);

    DrawDebugString(World, Location + FVector(0,0,140),
        FString::Printf(TEXT("Risk: %.2f"), TacticalParams.RiskTolerance),
        nullptr, FColor::Yellow, 0.0f, true);
}
```

---

## Profiling & Benchmarking

### Unreal Insights Targets

**Performance Benchmarks (4v4 scenario):**
| Component | Target | Measurement Method |
|-----------|--------|-------------------|
| MCTS Assignment | < 50ms | Insights: `MCTSComponent::RunMCTS` |
| Batched RL Inference | < 4ms | Insights: `RLPolicyNetwork::GetTacticalParametersBatched` |
| EQS Queries | < 2ms (4 agents) | Insights: `EnvQueryManager::RunQuery` |
| StateTree Execution | < 2ms (4 agents) | Insights: `StateTreeComponent::Tick` |
| **Total AI Frame** | **< 20ms/sec** | Insights: `GameMode::TickAI` |

**Memory Budget:**
| Component | Target | Actual |
|-----------|--------|--------|
| MCTS Tree | < 1MB | TBD |
| RL Network Weights | < 500KB | 458KB ✅ |
| Observations (4 agents) | < 20KB | TBD |
| **Total AI Memory** | **< 2MB** | TBD |

---

## Future Extensions

### v8.5: Learned Targeting (Deferred)

**Motivation:** If positioning <70% predictive of win rate, add learned aiming

**Extended Action Space:**
```cpp
struct FCombatParametersExtended
{
    ETargetPriority Priority;      // Closest, LowestHP, MostThreatening
    EEngagementStyle Style;        // Aggressive, Suppressive, HoldFire
    FVector2D AimOffset;           // [-1,1] horizontal/vertical aim offset
};
```

**Complexity:** 4 continuous tactical + 2 continuous aim + 2 discrete combat = ~8 action dimensions

### v9.0: Raw Movement Control (Research)

**Motivation:** Maximum emergent behavior, no EQS constraints

**Action Space:**
```cpp
enum class EMovementPrimitive : uint8
{
    Forward, Backward, Left, Right,
    ForwardLeft, ForwardRight, BackwardLeft, BackwardRight,
    Stop
};
```

**Challenges:**
- 9× action space complexity vs v8.0
- Requires extensive exploration (10,000+ episodes)
- No graceful degradation (random movements may be invalid)

---

## Common Workflows

### Adding a New Tactical Parameter

1. Add to `FTacticalParameters` struct (`RL/RLTypes.h:220`)
2. Update network output size (`RLPolicyNetwork.cpp`)
3. Add EQS weight mapping (`STTask_ExecuteTacticalMovement_v8.cpp:594`)
4. Update reward calculation if needed (`RewardCalculator.cpp`)
5. Retrain model with new action space
6. Update expected behavioral metrics

### Tuning Strategy Reward Weights

1. Edit weight profiles in `RewardCalculator.cpp:290`
2. Run short training (500 episodes) to test
3. Check TensorBoard for parameter distributions
4. If differentiation insufficient, increase weight contrast
5. Repeat until parameter profiles converge to expectations

### Debugging Low Win Rate

1. Check `ToggleMCTSDebug` - Are strategy assignments reasonable?
2. Check `ToggleRLDebug` - Do tactical parameters match strategy?
3. Check TensorBoard reward breakdown - Which components negative?
4. Profile inference latency - Is batching working?
5. Review episode replays - Visual inspection of tactics

---

## References

### Key Files Summary

| Category | Files |
|----------|-------|
| **Action Space** | `RL/RLTypes.h` (FTacticalParameters, FCombatParameters) |
| **RL Policy** | `RL/RLPolicyNetwork.h/cpp` |
| **Tactical Movement** | `StateTree/Tasks/STTask_ExecuteTacticalMovement_v8.h/cpp` |
| **Combat** | `Team/FollowerAgentComponent.cpp` (ExecuteCombat) |
| **Rewards** | `RL/RewardCalculator.h/cpp` |
| **MCTS** | `AI/MCTS/MCTS.h/cpp`, `Team/TeamLeaderComponent.h/cpp` |
| **Observations** | `Observation/TacticalObserver.h/cpp` |
| **Objectives** | `Team/ObjectiveActor.h/cpp` |

### External Resources

- **Hierarchical RL:** "The Option Framework" (Sutton, 1999)
- **MCTS:** "UCB1 Algorithm" (Auer et al., 2002), "PUCT" (AlphaGo, 2016)
- **PPO:** "Proximal Policy Optimization" (Schulman et al., 2017)
- **UE5 APIs:** [UE5.6 NNE Documentation](https://docs.unrealengine.com)

---

**Version:** v8.0 (Tactical Parameters Architecture)
**Last Updated:** 2026-01-15
**Status:** Week 4 Training Pipeline In Progress
**Next Milestone:** Initial training (1,000-2,000 episodes) → Behavioral validation
