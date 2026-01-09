# CORTEX: Real-Time Multi-Agent Combat AI with MCTS-RL Coordination

**Engine:** UE5.6 | **Language:** C++17 | **Platform:** Windows | **Version:** v7.0 (Durability-Based Objectives + MCTS-RL Coordination)

---

## Quick Reference

### Decision Tree
```
Task Type?
├─ Add Feature → Read affected files → Check design patterns → Implement → Test
├─ Fix Bug → Reproduce → Read stack trace → Locate file:line → Fix → Verify
├─ Optimize → Profile first → Identify bottleneck → Apply pattern → Benchmark
└─ Refactor → Read dependencies → Plan backwards from usage → Implement → Validate
```

### Critical Constraints
| Component | Max Latency | Memory | Notes |
|-----------|-------------|--------|-------|
| MCTS | 30-50ms | 1MB | Mission assignment (async, 1.5s intervals) |
| RL Inference | 2-4ms | 400KB | **Batched inference** (4 agents → single forward pass) |
| StateTree | <0.5ms/agent | 100KB | Rule-based execution |
| **Total (4 agents)** | **5-10ms** | **4MB** | Real-time requirement |

**Performance Optimization Strategies:**
- **Batched Inference:** All 4 agents processed in single network call → 2-4ms total (not 4×1-3ms)
- **Event-Driven Updates:** Only recompute strategy on significant events (health delta >20%, new enemy, Mission change)
- **Async MCTS:** Runs on separate thread, doesn't block RL execution
- **Fallback Strategy:** Use last valid strategy if inference delayed

### File Locations (Quick Jump)
| Feature | Path | Key Methods |
|---------|------|-------------|
| MCTS | `AI/MCTS/MCTS.cpp` | `RunMCTS():71`, `EvaluateAssignment():180` |
| RL Policy | `RL/RLPolicyNetwork.cpp` | `GetStrategy():320`, `GetStateValue():380` |
| Team Leader | `Team/TeamLeaderComponent.cpp` | `RunMissionAssignment():220` |
| Follower | `Team/FollowerAgentComponent.cpp` | `UpdateStrategy():150` |
| Types & Config | `RL/RLTypes.h` | `EStrategyType`, `FMissionAssignment` |

### Search Terms (When Unfamiliar)
- **Hierarchical RL:** "options framework Sutton", "hierarchical reinforcement learning"
- **MCTS:** "UCB1 algorithm 2002", "PUCT AlphaGo", "UE5 async task graph"
- **PPO:** "Proximal Policy Optimization Schulman 2017", "actor-critic methods"
- **Task Assignment:** "multi-agent task allocation", "combinatorial optimization"
- **UE5 APIs:** "UE5.6 NNE ONNX runtime", "FTimerManager", "EQS"

---

## Architecture (v6.0 MCTS-RL Coordination)

### System Flow
```
Team Leader (1 per team, async planning every 1.5s)
  ├─ MCTS: Solves combinatorial Mission assignment
  │   ├─ Action Space: Which agents → which Missions?
  │   ├─ Evaluation: RL value estimates + coordination heuristics
  │   └─ Output: Agent-to-Mission mapping
  │
  └─ Broadcasts assignments to followers
                    ↓
Followers (N agents, reactive strategy adaptation every tick)
  ├─ RL Policy: Decides current strategy (Assault/Defend/Support/Retreat)
  │   ├─ Input: Observation (64) + Mission embedding (4) = 68 features
  │   ├─ Output: EStrategyType (4 discrete actions)
  │   └─ Value: Provides state value to MCTS
  │
  ├─ Rule-Based Execution:
  │   ├─ Strategy → Position (EQS query type)
  │   ├─ Position → Movement (NavMesh pathfinding)
  │   ├─ Strategy → Targeting (LOS + threat priority)
  │   └─ Targeting → Firing (Engine weapon system)
  │
  └─ Schola Integration → RLlib Environment → Real-Time PPO Updates
```

### Key Architectural Principles (v6.0)

**Three-Layer Hierarchy:**
1. **MCTS (Coordination Layer)** - "Who does what?" (every 1.5s)
2. **RL (Adaptation Layer)** - "How should I approach this?" (every tick)
3. **Rules (Execution Layer)** - "Execute the approach" (every tick)

**Clear Separation of Concerns:**
- **MCTS owns:** Team composition, Mission assignment, multi-agent coordination
- **RL owns:** Strategy selection, dynamic adaptation, learned tactics
- **Rules own:** Position generation (EQS), movement (NavMesh), targeting, firing

**Synergy Mechanisms:**
1. **RL guides MCTS:** Value estimates replace hand-coded heuristics
2. **MCTS guides RL:** Provides Missions that shape RL observations
3. **Rules execute cleanly:** No decisions, pure deterministic execution

---

## Core Components

### 1. Team Leader (`Team/TeamLeaderComponent.cpp`)
**Role:** MCTS-based Mission assignment

**v6.0 Features:**
- Runs MCTS to solve agent-to-Mission assignment problem
- Uses RL value function for leaf evaluation
- Considers team cohesion, Mission coverage, agent capabilities
- Async execution (doesn't block gameplay)
- Continuous replanning (every 1.5s)

**MCTS Action Space:**
```cpp
// Each node in MCTS tree represents an assignment
struct FMissionAssignment {
    TMap<AActor*, UMission*> AgentToMission;
    // Example:
    // Agent1 → CapturePointA
    // Agent2 → CapturePointA  (2 agents on same Mission)
    // Agent3 → DefendPointB
    // Agent4 → SupportAgent1
};
```

**Evaluation Function:**
```cpp
float EvaluateAssignment(FMissionAssignment& Assignment) {
    float totalValue = 0;

    // 1. Query RL value for each agent
    for (auto& [agent, Mission] : Assignment.AgentToOMission) {
        FObservation obs = BuildObservation(agent, Mission);
        totalValue += RLPolicy->GetStateValue(obs);
    }

    // 2. Add coordination heuristics
    totalValue += TeamCohesionScore(Assignment);     // Agents near each other?
    totalValue += MissionCoverageScore(Assignment); // All Missions covered?
    totalValue += CapabilityMatchScore(Assignment);   // Right agent for job?

    return totalValue;
}
```

**Files:** `Team/TeamLeaderComponent.h/cpp`, `AI/MCTS/MCTS.h/cpp`

---

### 2. RL Policy Network (`RL/RLPolicyNetwork.cpp`) ✅ v6.0
**Purpose:** Strategy selection based on observation + Mission

**Architecture:**
```
Input: 68 features
  ├─ Agent State (7): pos(3), vel(3), health(1)
  ├─ Combat (1): enemy_dist(1)
  ├─ Perception (32): raycasts(16), hit_types(16)
  ├─ Enemy Info (16): count(1), nearby(15)
  ├─ Tactical (4): has_cover(1), cover_dist(1), cover_dir(2)
  ├─ Support Context (4): ally_needs(1), ally_health(1), ally_dist(1), ally_dir(1)
  └─ Mission Context (4): type(1), distance(1), direction(2)
                    ↓
┌──────────────────────────────────────────────────────────┐
│  Network: [128 → 128 → 64] ReLU                          │
│  (Learns when to Assault/Defend/Support/Retreat)         │
└──────────────────────────────────────────────────────────┘
        ↓                               ↓
   ┌─────────┐                     ┌─────────┐
   │ Policy  │                     │  Critic │
   │ Head(4) │                     │ Head(1) │
   └─────────┘                     └─────────┘
        ↓                               ↓
   [A,D,S,R]                         Value
   Logits                           Estimate
```

**Strategy Selection:**
```cpp
EStrategyType GetStrategy(Observation, Mission) {
    Features = BuildFeatures(Observation, Mission);
    Logits = RunNetwork(Features);  // [4 logits]
    return SampleFromLogits(Logits); // Assault/Defend/Support/Retreat
}
```

**Value Function (for MCTS):**
```cpp
float GetStateValue(Observation, Mission) {
    Features = BuildFeatures(Observation, Mission);
    return RunCriticHead(Features);  // [-1, 1] value estimate
}
```

**Files:** `RL/RLPolicyNetwork.h/cpp`, `RL/RLTypes.h`

---

### 3. Rule-Based Execution (`StateTree/Tasks/`)

**Strategy → Position Mapping (Deterministic):**
```cpp
ETacticalPosition StrategyToPosition(EStrategyType Strategy, FAllyContext AllyCtx) {
    switch (Strategy) {
        case Assault:  return ETacticalPosition::ForwardCover;
        case Defend:   return ETacticalPosition::Hold;
        case Support:  return ETacticalPosition::ForwardCover; // Toward ally
        case Retreat:  return ETacticalPosition::Retreat;
    }
}
```

**Position → Movement (EQS + NavMesh):**
- `STTask_ExecuteMovement.cpp` queries appropriate EQS
- Takes top-ranked position
- Issues MoveToLocation via AIController

**Strategy → Targeting (Priority-Based):**
```cpp
AActor* SelectTarget(EStrategyType Strategy, TArray<AActor*> Enemies) {
    if (Strategy == Retreat) return nullptr; // Hold fire

    // Priority: Closest threatening enemy
    AActor* target = GetClosestEnemyInLOS(Enemies);
    return target;
}
```

**Targeting → Firing (Engine-Driven):**
- SetFocus(target) for auto-aim
- Fire() if has LOS
- Handled by `STTask_ExecuteFire.cpp`

**Files:** `StateTree/Tasks/STTask_Execute*.cpp`

---

### 4. Observations (`Observation/TacticalObserver.cpp`)

**v6.0 Observation Space (68 features):**
```
Agent State (7): pos(3), vel(3), health(1)
Combat (1): enemy_dist(1)
Perception (32): raycasts(16), hit_types(16)
Enemy Info (16): count(1), nearby(15)
Tactical (4): has_cover(1), cover_dist(1), cover_dir(2)
Support Context (4): ally_needs(1), ally_health(1), ally_dist(1), ally_dir(1)
Mission Context (4):
  - type(1): Encoded as [0=None, 0.33=Capture, 0.66=Defend, 1.0=Support]
  - distance(1): Normalized distance to Mission
  - direction(2): Normalized 2D direction vector to Mission
```

---

**Files:** `RL/RewardCalculator.h/cpp`

---

### 6. Types & Configuration (`RL/RLTypes.h`)

**Key Structs (v6.0):**
```cpp
// Strategy types (RL output)
enum class EStrategyType : uint8 {
    Assault,   // Push toward Mission aggressively
    Defend,    // Hold position, suppress threats
    Support,   // Protect ally, draw aggro
    Retreat    // Disengage, survive, regroup
};

// Mission types (MCTS assigns these)
enum class EMissionType : uint8 {
    None,
    Capture,   // Offensive: Capture a point/zone
    Defend,    // Defensive: Hold a point/zone
    Support,   // Auxiliary: Protect specific ally
    Eliminate, // Offensive: Destroy enemy squad
    Retreat    // Fallback: Disengage to safe zone
};

// MCTS assignment result
struct FMissionAssignment {
    TMap<AActor*, UMission*> AgentToMission;
    float ExpectedValue;  // MCTS-estimated value
    int32 VisitCount;     // MCTS confidence
};

// Mission context for RL observation
struct FOMissionContext {
    EMissionType Type;
    float Distance;           // Normalized [0,1]
    FVector2D Direction;      // Normalized 2D direction
    AActor* TargetActor;      // Target (e.g., enemy, capture point, ally)
    int32 Priority;           // Mission priority [0-10]

    TArray<float> ToFeatureVector() const {
        float typeEncoded = static_cast<float>(Type) / 4.0f; // [0, 0.25, 0.5, 0.75, 1.0]
        return {
            typeEncoded,
            Distance,
            Direction.X,
            Direction.Y
        };
    }
};
```

---

## Design Patterns & Principles

### Hierarchical Decision Making
| Layer | Responsibility | Update Frequency | Latency |
|-------|----------------|------------------|---------|
| **MCTS** | Team coordination | 1.5s | 30-50ms |
| **RL** | Strategy adaptation | Every tick | 1-3ms |
| **Rules** | Execution | Every tick | <0.5ms |

### Clear Interfaces
```cpp
// MCTS → RL (value query)
interface IValueEstimator {
    float GetStateValue(Observation, Mission);
};

// Leader → Follower (command)
interface ICommandReceiver {
    void ReceiveMission(Mission);
};

// Follower → Rules (execution)
interface IStrategyExecutor {
    void ExecuteStrategy(EStrategyType);
};
```

### Key Patterns
- **Strategy Pattern:** RL selects strategy, rules execute
- **Observer Pattern:** Leader broadcasts assignments
- **Hierarchical Policy:** MCTS high-level, RL low-level
- **Facade Pattern:** TeamLeaderComponent wraps MCTS complexity

---

## 7. Objective System (v7.0 - Durability-Based Capture)

### Overview

v7.0 introduces a **unified durability-based objective system** that replaces the previous type-based (Defend/Capture) distinction. Both teams now have identical objective mechanics: defend your base, capture the enemy's base.

**Key Innovation:**
- Objectives are physical actors in the world with durability (health) that declines when hostile agents are present
- Team ownership determines friendly vs hostile (no more explicit Defend/Capture types)
- Volume-based rewards incentivize correct behavior (stay in capture zone)
- Symmetric gameplay with clear win conditions

### ObjectiveActor Class

**File:** `Team/ObjectiveActor.h/cpp`

```cpp
// Core properties
int32 OwnerTeamID;              // Team that owns this objective (0 or 1)
float MaxDurability = 100.0f;    // Maximum durability
float CurrentDurability;         // Current durability (0.0 to 100.0)

// Components
UStaticMeshComponent* PillarMesh;         // Visual pillar representation
USphereComponent* CaptureVolume;          // Trigger volume for agent tracking

// Team association helpers
bool IsFriendlyTo(int32 AgentTeamID);    // OwnerTeamID == AgentTeamID
bool IsHostileTo(int32 AgentTeamID);     // OwnerTeamID != AgentTeamID
bool IsAgentInVolume(AActor* Agent);     // Check if agent in capture zone
```

### Durability Mechanics

**Decline Logic (Timer-based, 10Hz):**
```cpp
if (HostileAgentsInVolume.Num() > 0)
{
    float DeclineRate = HostileAgentsInVolume.Num() * DamagePerAgentPerSecond;
    CurrentDurability -= DeclineRate * DeltaTime;

    // Default: 2.0 damage/sec per hostile agent
    // Example: 3 hostiles = 6.0 durability/sec decline
}
```

**Recovery Logic:**
```cpp
if (HostileAgentsInVolume.Num() == 0 && CurrentDurability < MaxDurability)
{
    CurrentDurability += RecoveryPerSecond * DeltaTime;

    // Default: 1.0 durability/sec recovery
}
```

**Defeat Condition:**
```cpp
if (CurrentDurability <= 0.0f)
{
    OnDefeat(); // Broadcast to GameMode → Episode reset
}
```

### Capture Volume

**Overlap Detection:**
- Spherical trigger volume (default radius: 1000cm = 10m)
- Automatically tracks hostile agents via `OnComponentBeginOverlap` / `EndOverlap`
- Maintains `TSet<AActor*> HostileAgentsInVolume` for efficient counting
- Only hostile agents contribute to durability decline (friendly agents ignored)

**Visual Feedback:**
- Translucent green sphere in editor/debug mode
- Team color material (Blue = Team 0, Red = Team 1)
- Emission strength pulses as durability drops
- Debug text shows durability %, hostile count

### Volume-Based Rewards (v7.0)

**Replaced distance-based rewards with volume presence rewards:**

**Defense Role (Friendly Objective):**
```cpp
// Continuous retention reward
if (AgentInFriendlyVolume)
{
    Reward += 0.05f;  // +0.05 per step (~0.5/sec at 10Hz)
}

// Kill bonus
if (KilledEnemy && AgentInFriendlyVolume)
{
    Reward += 5.0f;   // +5.0 for defending kills
}
```

**Assault Role (Hostile Objective):**
```cpp
// Continuous retention reward
if (AgentInEnemyVolume)
{
    Reward += 0.1f;   // +0.1 per step (~1.0/sec at 10Hz)
}

// Base destruction reward
if (EnemyObjective.IsDefeated())
{
    Reward += 100.0f; // +100.0 for mission success
}
```

**Why Volume-Based Rewards?**
- **Clear incentives:** "Stay in the zone" is unambiguous
- **No distance ambiguity:** Either inside (rewarded) or outside (not rewarded)
- **Encourages commitment:** Agents must enter dangerous zones to progress
- **Symmetric design:** Both teams use identical reward structure

### ObjectiveManager Integration

**File:** `Team/ObjectiveManager.h/cpp`

```cpp
// Find all objective actors in the world
TArray<AObjectiveActor*> FindAllObjectiveActors();

// Find friendly objective (OwnerTeamID matches team)
AObjectiveActor* FindFriendlyObjective(int32 TeamID);

// Find hostile objective (OwnerTeamID doesn't match)
AObjectiveActor* FindHostileObjective(int32 TeamID);
```

**Usage in MCTS Assignment:**
```cpp
// TeamLeaderComponent assigns agents to objectives
AObjectiveActor* FriendlyBase = ObjectiveManager->FindFriendlyObjective(TeamID);
AObjectiveActor* EnemyBase = ObjectiveManager->FindHostileObjective(TeamID);

// MCTS explores:
// - All agents attack EnemyBase (full assault)
// - Split: 2 attack, 2 defend (balanced)
// - Split: 3 attack, 1 defend (aggressive)

// Assignment based on RL value estimates + coordination heuristics
```

## Architecture Rules (Invariants - v7.0)

1. **ONLY Leaders run MCTS** (followers NEVER touch MCTS)
2. **MCTS solves assignment problem** (agent-to-objective mapping)
3. **RL uses MCTS assignments as context** (objective embedding in observation)
4. **RL selects strategies dynamically** (can deviate from objective if needed)
5. **Rules are deterministic** (no learning, no randomness in execution)
6. **MCTS uses RL value estimates** (learned heuristics for leaf evaluation)
7. **Async MCTS, sync RL** (MCTS doesn't block RL execution)
8. **Objective types ≠ Strategy types** (Hostile objective might trigger Retreat strategy if low health)
9. **Single-head RL network** (simpler, faster than multi-head)
10. **EQS handles spatial reasoning** (RL focuses on when/what, not where)
11. **Objectives are physical actors** (v7.0: AObjectiveActor with durability, not abstract UObjects)
12. **Team ownership determines role** (v7.0: Friendly vs hostile by OwnerTeamID, not type enum)
13. **Volume-based rewards only** (v7.0: No distance-based rewards, only capture zone presence)

---

### Self-Play Loop (MCTS ↔ RL Synergy)

```
┌─────────────────────────────────────────────────────────┐
│  Episode Start                                           │
└─────────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────────┐
│  1. MCTS Assignment                                      │
│     - Queries RL value for each agent-Mission pair    │
│     - Selects best assignment: Agent1→Capture A, etc.   │
└─────────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────────┐
│  2. RL Execution                                         │
│     - Receives Mission embedding in observation       │
│     - Selects strategies (Assault/Defend/Support/...)   │
│     - Collects rewards (Mission-conditioned)          │
└─────────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────────┐
│  3. PPO Training                                         │
│     - Updates policy to maximize Mission returns      │
│     - Updates value function to predict better          │
└─────────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────────┐
│  4. Improved MCTS (Next Episode)                        │
│     - Now has better value estimates                    │
│     - Makes smarter assignments                         │
└─────────────────────────────────────────────────────────┘
                    ↓
              (Curriculum Learning)
```

**Curriculum Progression:**
```
Early Training (Episodes 0-1000):
  - Simple assignments (all agents → same Mission)
  - RL learns basic strategies
  - MCTS gets coarse value estimates

Mid Training (Episodes 1000-5000):
  - Mixed assignments (2-2 splits, 3-1 splits)
  - RL learns coordination
  - MCTS learns team composition

Late Training (Episodes 5000+):
  - Complex assignments (individual, dynamic reassignment)
  - RL learns adaptive strategies
  - MCTS optimizes fine-grained tactics
```

---

### Event-Driven Inference (Performance Optimization)

**Problem:** Running RL inference every tick (60 FPS) = 60 × 2-4ms = 120-240ms/sec → Too expensive

**Solution:** Only update strategy on significant events

```cpp
// Follower: Event-driven strategy update
bool UFollowerAgentComponent::ShouldUpdateStrategy() const {
    // Check significant state changes
    bool healthChanged = FMath::Abs(CurrentHealth - LastStrategyHealth) > 0.2f;
    bool newEnemyDetected = PerceivedEnemies.Num() > LastEnemyCount;
    bool MissionChanged = CurrentMission != LastMission;
    bool timeout = TicksSinceLastUpdate > 10;  // Fallback every 10 ticks

    return healthChanged || newEnemyDetected || MissionChanged || timeout;
}

void UFollowerAgentComponent::TickComponent(float DeltaTime, ...) {
    if (ShouldUpdateStrategy()) {
        // Run RL inference (2-4ms batched)
        CurrentStrategy = RLPolicy->GetStrategy(Observation, Mission);

        // Cache for next check
        LastStrategyHealth = CurrentHealth;
        LastEnemyCount = PerceivedEnemies.Num();
        LastMission = CurrentMission;
        TicksSinceLastUpdate = 0;
    }

    // Always execute current strategy (cheap, <0.5ms)
    ExecuteStrategy(CurrentStrategy);

    TicksSinceLastUpdate++;
}
```

**Performance Impact:**
```
Before (every tick):  60 FPS × 2-4ms = 120-240ms/sec
After (event-driven): ~6-10 updates/sec × 2-4ms = 12-40ms/sec
Reduction: 75-83% lower inference cost
```

---

### Batched Inference (4 Agents → 1 Network Call)

**Problem:** Naïve approach = 4 agents × 1-3ms = 4-12ms per tick

**Solution:** Batch all agents into single forward pass


**ONNX Runtime Optimization:**

**Performance:**
```
Naive (sequential): 4 × 2ms = 8ms
Batched (parallel):  1 × 3ms = 3ms
Speedup: 2.6× faster
```

---

### Sim2Real Synchronization (Training ↔ Runtime Consistency)

**Problem:** If Python training environment and C++ runtime differ (e.g., movement speed, perception radius), trained model breaks in-game.

**Solution:** Single source of truth in C++ header

**C++ (RL/RLTypes.h):**
```cpp
namespace RLConfig {
    // === CRITICAL: These values MUST match Python training environment ===

    // Movement
    constexpr float AGENT_WALK_SPEED = 600.0f;      // cm/s
    constexpr float AGENT_RUN_SPEED = 900.0f;
    constexpr float AGENT_SPRINT_SPEED = 1200.0f;

    // Perception
    constexpr float PERCEPTION_RADIUS = 3000.0f;    // cm
    constexpr int32 RAYCAST_COUNT = 16;
    constexpr float RAYCAST_LENGTH = 2000.0f;       // cm
    constexpr float RAYCAST_ANGLE_SPREAD = 180.0f;  // degrees

    // Combat
    constexpr float BASE_DAMAGE = 10.0f;
    constexpr float MAX_HEALTH = 100.0f;
    constexpr float FIRE_RATE = 0.1f;               // seconds per shot

    // Observation Normalization
    constexpr float MAX_DISTANCE_NORMALIZATION = 5000.0f;  // cm
    constexpr float MAX_VELOCITY_NORMALIZATION = 1200.0f;

    // Action Space
    constexpr int32 NUM_STRATEGIES = 4;  // Assault, Defend, Support, Retreat
    constexpr int32 NUM_TARGETS = 11;    // 10 enemies + 1 no-target

    // === END CRITICAL SECTION ===
}
```

**Python (training_env/config.py):**
```python
# GENERATED FROM C++ RL/RLTypes.h - DO NOT EDIT MANUALLY
# Run: python tools/sync_config_from_cpp.py


**Sync Script (tools/sync_config_from_cpp.py):**


**Validation:**

// Unit test to catch drift
TEST(RLConfigTest, PythonCppConsistency) {
    // Load exported ONNX model metadata
    auto metadata = LoadONNXMetadata("cortex_policy.onnx");

    // Check critical values
    EXPECT_FLOAT_EQ(metadata["walk_speed"], RLConfig::AGENT_WALK_SPEED);
    EXPECT_FLOAT_EQ(metadata["perception_radius"], RLConfig::PERCEPTION_RADIUS);
    EXPECT_EQ(metadata["num_strategies"], RLConfig::NUM_STRATEGIES);
}
```

---

### Debug Visualization (Essential for Development)

**In-World MCTS Tree Visualization:**

```cpp
void ATeamLeaderComponent::DebugDrawMCTSAssignments() {
```

**Strategy State Visualization:**

```cpp
void UFollowerAgentComponent::DebugDrawStrategyState() {
```

**Console Commands:**

```cpp
// Add to GameMode
UFUNCTION(Exec)
void ToggleMCTSDebug() {
    for (auto* leader : TeamLeaders) {
        leader->bShowDebugVisualization = !leader->bShowDebugVisualization;
    }
}

UFUNCTION(Exec)
void ToggleRLDebug() {
    for (auto* follower : AllAgents) {
        follower->bShowDebugVisualization = !follower->bShowDebugVisualization;
    }
}

UFUNCTION(Exec)
void PrintMCTSStats() {
    for (auto* leader : TeamLeaders) {
        UE_LOG(LogTemp, Display, TEXT("MCTS Iterations: %d"), leader->LastMCTSIterations);
        UE_LOG(LogTemp, Display, TEXT("Best Value: %.2f"), leader->BestAssignmentValue);
    }
}
```

**Visual Result:**
```
In-Game View:
  - Yellow arrows: MCTS assignments (Agent → Mission)
  - Green text: RL value estimates ("V=0.73")
  - Colored spheres: Current strategy (Red=Assault, Blue=Defend, etc.)
  - Health bars: Agent health visualization
  - Cyan text: Mission types ("Capture", "Defend")
```

---

### Profiling Requirements (Production Validation)

**Unreal Insights Benchmarks:**

**Target Metrics (4v4 scenario):**
| Component | Target | Measurement Method |
|-----------|--------|-------------------|
| MCTS Assignment | < 50ms | Insights: `MCTSComponent::RunMCTS` |
| Batched RL Inference | < 4ms | Insights: `RLPolicyNetwork::GetStrategiesBatched` |
| StateTree Execution | < 2ms (4 agents) | Insights: `StateTreeComponent::Tick` |
| **Total AI Frame** | **< 10ms** | Insights: `GameMode::TickAI` |

**Memory Budget:**
| Component | Target | Measurement Method |
|-----------|--------|-------------------|
| MCTS Tree | < 1MB | Memory Insights: `MCTS::TreeNodes` |
| RL Network Weights | < 400KB | Memory Insights: `ONNXRuntime::ModelData` |
| Observations (4 agents) | < 20KB | Memory Insights: `TacticalObserver::ObservationBuffer` |
| **Total AI Memory** | **< 2MB** | Memory Insights: `AISubsystem` |

**Profiling Checklist:**
- [ ] Capture 60s gameplay session with Unreal Insights
- [ ] Verify MCTS async execution (no main thread blocking)
- [ ] Verify batched inference (4 agents in single call)
- [ ] Verify event-driven updates (not every tick)
- [ ] Screenshot flame graph showing <10ms AI frame
- [ ] Screenshot memory timeline showing <2MB allocation

---

### Scalability Considerations (Future: 50v50)

**Current Architecture (4v4):**
```
1 TeamLeader × MCTS(50ms) + 4 Agents × RL(batched 4ms) = 54ms total
Acceptable for 4v4
```

**Scaling Challenge (50v50):**
```
1 TeamLeader × MCTS(?) + 50 Agents × RL(batched ?ms) = ???ms
MCTS action space explodes: 50 agents × 10 Missions = 500^10 combinations
```

**Proposed Solution: Hierarchical MCTS**

```
Commander (1)
  ├─ MCTS assigns squads to Missions (5 squads × 5 Missions = 5^5 = 3125 combinations)
  │  └─ Latency: ~100ms (async, every 5s)
  │
Squad Leaders (5)
  ├─ MCTS assigns agents within squad (10 agents × 3 tactics = 10^3 = 1000 combinations)
  │  └─ Latency: ~50ms each (async, every 2s)
  │
Agents (50)
  └─ RL selects strategies (batched inference: 50 agents / 5 squads = 10 per batch)
     └─ Latency: ~8ms per squad (5 squads × 8ms = 40ms total if sequential)
```

**Optimizations:**
1. **LOD AI:** Distant agents use cheaper heuristics (no RL inference)
2. **Async Squad MCTS:** 5 squad leaders run MCTS in parallel
3. **Staggered Updates:** Update 10 agents per tick (50 agents / 5 ticks = 10 FPS update rate per agent)
4. **Hierarchical Batching:** Batch inference per squad (10 agents) instead of all 50

**Future Work:**
- Implement squad-based MCTS (v7.0)
- Add LOD system for agent AI (v7.1)
- Benchmark 50v50 scenario (v7.2)

---

## v6.0 Core Innovation: MCTS-RL Synergy

### Problem Solved
v6.0 separates: **MCTS = coordination, RL = adaptation, Rules = execution**.

### Synergy Mechanisms

**1. RL as MCTS Heuristic:**
```cpp
// MCTS doesn't need hand-coded evaluation
float EvaluateNode(Assignment) {
    // RL provides learned value estimate
    return SumOf(RLPolicy->GetStateValue(agent, assignment));
}
```

**2. MCTS as RL Curriculum:**
```cpp
// MCTS explores different team compositions
Early training:  Simple (all agents → same Mission)
Mid training:    Splits (2-2, 3-1)
Late training:   Complex (individual assignments, dynamic)
```

## Implementation Status (v7.0 Objective System Complete)

### 🎯 CURRENT STATUS: v7.0 Durability-Based Objectives Implemented

**v7.0 COMPLETED (2026-01-09):**
- ✅ **ObjectiveActor System** - Durability-based capture with physical actors
- ✅ **Capture Volume** - Spherical trigger with automatic hostile tracking
- ✅ **Volume-Based Rewards** - Replaced distance-based with retention rewards
- ✅ **ObjectiveManager Integration** - Team-objective mapping helpers
- ✅ **Legacy Code Removal** - Removed Capture enum, deprecated CheckObjectiveType
- ✅ **Reward Calculator Update** - Unified volume-based reward logic
- ✅ **Type Cleanup** - All Capture references replaced with Assault

**v6.0 Design Complete:**
- ✅ Architecture redesign (MCTS coordination + RL adaptation)
- ✅ Clear separation of concerns (3-layer hierarchy)
- ✅ Synergy mechanisms identified
- ✅ Academic merit validated
- ✅ Performance optimization strategies (batching, event-driven)
- ✅ Training & value alignment framework
- ✅ Sim2Real synchronization process
- ✅ Debug visualization system
- ✅ Profiling requirements & scalability roadmap

**Implementation Needed (v6.0 → v7.0 Transition):**
| Component | Status | Priority | Notes |
|-----------|--------|----------|-------|
| **ObjectiveActor** | ✅ COMPLETE | P0 | Durability system with capture volume |
| **Reward Calculator** | ✅ COMPLETE | P0 | Volume-based rewards implemented |
| **Legacy Cleanup** | ✅ COMPLETE | P1 | Removed Capture type, deprecated conditions |
| **ObjectiveManager** | ✅ COMPLETE | P1 | Team-objective mapping methods added |
| **MCTS Assignment** | 🔄 Needs integration | P0 | Update to use ObjectiveActor |
|

**v7.0 Next Steps:**
1. Place ObjectiveActors in level (Team 0 & Team 1 bases)
2. Update MCTS assignment to use ObjectiveManager->FindFriendly/HostileObjective()
3. Create material with TeamColor/EmissionStrength parameters
4. Add GameMode::OnObjectiveDefeated() handler for episode reset
5. Test 4v4 scenario with durability-based capture

**See next_step.md for detailed integration guide.**

---
