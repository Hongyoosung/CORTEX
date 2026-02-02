# CORTEX v9.0 Refactoring Plan

**Document Version:** 1.0
**Created:** 2026-02-02
**Target Completion:** TBD
**Status:** 📋 Planning Phase

---

## Executive Summary

This refactoring plan addresses organizational debt in the CORTEX v9.0 codebase while preserving the working v9.0 architecture. The plan focuses on **clarifying responsibilities, removing deprecated code, and improving discoverability** without changing core logic.

**Key Goals:**
- ✅ Remove 40+ deprecated members (v7.0/v8.0 legacy)
- ✅ Reorganize directory structure by responsibility
- ✅ Decompose TeamLeaderComponent into 3 specialized managers
- ✅ Decouple Debug logic (VisualLogger) and StateTree dependencies(ContextBridge)
- ✅ Clarify observation scaling (52 → 56 → 71 features)
- ✅ Separate debug draw related code into a utility class
- ✅ Document initialization sequence
- ✅ Improve header dependencies

**Risk Level:** 🟡 Medium (organizational changes with minimal logic changes)

---

## Current State Assessment

### Codebase Statistics
```
Total Files: 115 (57 headers + 58 implementations)
Total Lines: ~25,000 (estimated)
Deprecated Members: 40+ (FollowerAgentComponent, TacticalStateComponent)
Shared Headers: 3 critical (RLTypes.h, TeamTypes.h, ObservationElement.h)
```

### Critical Issues Identified

| Issue | Impact | Priority |
|-------|--------|----------|
| **Deprecated code not removed** | Code pollution, confusion | 🔴 High |
| **Version suffixes (v8)** | Unclear versioning strategy | 🟡 Medium |
| **Observation scaling unclear** | 3 different sizes (52/56/71) | 🟡 Medium |
| **No initialization orchestrator** | Undocumented startup sequence | 🟠 Medium-High |
| **Schola integration scattered** | No central coordinator | 🟡 Medium |

---

## Proposed Directory Structure (Target State)

### Phase 1: Reorganize by Responsibility

```
Source/GameAI_Project/
├── Public/
│   ├── Core/                              [UNCHANGED - Game Instance, GameMode]
│   │   ├── GameAI_ProjectGameMode.h
│   │   ├── SimulationManagerGameMode.h    [Multi-env orchestrator]
│   │   ├── ScholaGameInstance.h           [Training framework entry point]
│   │   └── ProfilingMacros.h
│   │
│   ├── Team/                              [REFACTOR - Team coordination]
│   │   ├── TeamTypes.h                    [Team-related enums, structs]
│   │   ├── ObjectiveActor.h               [Friendly/Hostile markers]
│   │   ├── Components/
│   │   │   ├── TeamLeaderComponent.h      [Coordinator: Coordination and event broadcasting]
│   │   │   ├── FollowerAgentComponent.h   [Coordinator: Tuning and Life Cycle]
│   │   │   ├── SquadManagerComponent.h    [NEW: Follower Management and Registration]
│   │   │   ├── IntelManagerComponent.h    [NEW: Enemy/Target Information Management]
│   │   │   ├── StrategicPlannerComponent.h[NEW: MCTS Desicion]
│   │   │   └── TeamCommsComponent.h       [NEW: Leader Communication & Event reporting]
│   │   └── README.md                      [NEW - Team system documentation]
│   │
│   ├── AI/                                [REFACTOR - Strategic AI]
│   │   ├── MCTS/                          [Monte Carlo Tree Search]
│   │   │   ├── MCTS.h                     [v9.0: Strategy-only assignment]
│   │   │   ├── MCTSNode.h (Existing TeamMCTSNode.h)
│   │   │   ├── MCTSAsyncTask.h
│   │   │   └── README.md                  [NEW - MCTS v9.0 architecture doc]
│   │   └── AIController/
│   │       └── AIControllerBase.h
│   │
│   ├── RL/                                [REFACTOR - Reinforcement Learning]
│   │   ├── RLTypes.h                      [ENHANCED - Single source of truth]
│   │   ├── RLPolicyNetwork.h              [Policy inference interface]
│   │   ├── Components/
│   │   │   ├── RLAgentComponent.h         [RL interface layer - CLEANED]
│   │   │   ├── TacticalStateComponent.h   [State management - CLEANED]
│   │   │   └── RewardCalculator.h         [v9.0: Gradient rewards]
│   │   └── README.md                      [NEW - RL system overview]
│   │
│   ├── Observation/                       [REFACTOR - Observation system]
│   │   ├── ObservationTypes.h             [NEW - Observation enums/structs]
│   │   ├── ObservationElement.h           [v9.0: 52 base features]
│   │   ├── TeamObservation.h
│   │   ├── Components/
│   │   │   └── ObservationBuilderComponent.h  [52-feature builder]
│   │   └── README.md                      [NEW - Observation scaling guide (52→56→71)]
│   │
│   ├── Combat/                            [UNCHANGED - Combat system]
│   │   ├── ProjectileBase.h
│   │   └── Components/
│   │       ├── HealthComponent.h
│   │       ├── WeaponComponent.h
│   │       ├── CombatExecutorComponent.h  [Delete]
│   │       └── AgentPerceptionComponent.h 
│   │
│   ├── StateTree/                         [REFACTOR - Execution layer]
│   │   ├── FollowerStateTreeComponent.h
│   │   ├── Components/
│   │   │   └── ContextBridgeComponent.h   [NEW: Decouples Actor from StateTree Schema]
│   │   ├── FollowerStateTreeSchema.h
│   │   ├── FollowerStateTreeContext.h
│   │   ├── Tasks/
│   │   │   ├── STTask_ExecuteTacticalMovement.h    [RENAMED - remove v8 suffix]
│   │   │   ├── STTask_ExecuteAiming.h
│   │   │   ├── STTask_ExecuteFire.h
│   │   │   ├── STTask_Idle.h
│   │   │   └── STTask_Dead.h
│   │   ├── Evaluators/
│   │   │   └── STEvaluator_UpdateObservation.h
│   │   └── README.md                      [NEW - State Tree execution flow]
│   │
│   ├── EQS/                               [UNCHANGED - Environmental Query System]
│   │   ├── EnvQueryContext_ObjectiveLocation.h
│   │   ├── EnvQueryContext_Teammates.h
│   │   └── EnvQueryContext_VisibleEnemies.h
│   │
│   ├── Schola/                            [REFACTOR - Training integration]
│   │   ├── ScholaTypes.h                  [NEW - Schola-specific types]
│   │   ├── ScholaAgentComponent.h         [gRPC interface]
│   │   ├── ScholaCombatEnvironment.h      [Gym adapter]
│   │   ├── ScholaCoordinator.h            [NEW - Central initialization manager]
│   │   ├── Observers/                     [NEW - Observation adapters]
│   │   │   └── TacticalObserver.h         [71-feature adapter for Python]
│   │   ├── Rewards/
│   │   │   └── TacticalRewardProvider.h
│   │   ├── Actuators/
│   │   │   ├── TacticalParameterActuator.h
│   │   │   ├── CombatChoiceActuator.h
│   │   │   └── CombinedTacticalActuator.h
│   │   ├── Subsystems/
│   │   │   └── ThrottledScholaSubsystem.h
│   │   ├── Utils/
│   │   │   ├── TimeBasedBrain.h
│   │   │   └── FollowerAgentTrainer.h
│   │   └── README.md                      [NEW - Schola integration guide]
│   │
│   ├── Simulation/                        [UNCHANGED - State transition utils]
│   │   └── SimulationState.h
│   │
│   ├── Util/                              [REFACTOR - Shared utilities]
│   │   ├── Components/
│   │   │   └── VisualLoggerComponent.h    [NEW: Pure Debug Drawing Logic]
│   │   └── InitializationCoordinator.h    [NEW - Startup sequence manager]
│   │
│   ├── Debug/                             [UNCHANGED - Diagnostic tools]
│   │   └── MultiEnvDiagnostic.h
│   │
│   ├── Interfaces/                        [UNCHANGED - Abstract interfaces]
│   │   └── CombatStatsInterface.h
│   │
│   ├── Actor/                             [REFACTOR - Character definitions]
│   │   ├── CharacterBase.h                [Used TeamLeader and Follower]
│   │   
│   │
│   └── Tests/                             [REFACTOR - Testing]
│       ├── Integration/
│       │   └── TestIntegrationScenarios.cpp  [UPDATE for v9.0]
│       └── Unit/
│           └── TestMCTSAssignment.cpp        [UPDATE for strategy-only]
│
└── Private/                               [Mirror structure of Public/]
```

### Key Changes in Structure

| Change | Rationale | Impact |
|--------|-----------|--------|
| **Schola/ reorganization** | Clarify observers/rewards/actuators separation | Improved discoverability |
| **Add README.md per subsystem** | Document responsibilities and relationships | Better onboarding |
| **Remove v8 suffix** | Current version is v9.0 | Reduced confusion |
| **Add ScholaCoordinator** | Centralize training initialization | Clearer startup flow |
| **Add InitializationCoordinator** | Document startup sequence | Explicit dependency order |
| **Rename CharacterBase** | Clearer naming (was GameAIProjectCharacter) | Improved clarity |

---

## Refactoring Phases

### Phase 0: Preparation (1-2 days)

**Goal:** Set up safety measures and documentation baseline

#### Tasks
- [ ] **Create feature branch:** `refactor/v9.0-cleanup`
- [ ] **Backup current state:** Tag as `pre-refactor-v9.0`
- [ ] **Document current behavior:**
  - [ ] Record 10+ gameplay sessions (video/logs)
  - [ ] Capture reward logs (1000 episodes)
  - [ ] Export MCTS batch selection stats
- [ ] **Create test suite:**
  - [ ] Add integration test: "TeamLeader assigns 4 strategies"
  - [ ] Add integration test: "Assault agents approach hostile objective"
  - [ ] Add unit test: "ObservationElement produces 52 features"
- [ ] **Run baseline tests:**
  - [ ] All tests pass ✅
  - [ ] No compile warnings ✅

**Exit Criteria:** Feature branch created, baseline tests green, behavior documented

---

### Phase 1: Remove Deprecated Code (2-3 days)

**Goal:** Clean up v7.0/v8.0 legacy without changing behavior

**Risk:** 🟢 Low (removing unused code)

#### 1.1: Clean FollowerAgentComponent

**File:** `Public/Team/Components/FollowerAgentComponent.h`

**Remove deprecated members (lines 320-348):**
```cpp
// REMOVE:
UPROPERTY(BlueprintReadOnly, meta = (DeprecatedProperty))
EStrategyType CurrentStrategy = EStrategyType::Assault;

UPROPERTY(BlueprintReadOnly, meta = (DeprecatedProperty))
AObjectiveActor* CurrentObjective = nullptr;

// v7 DEPRECATED: Old caching strategy (pre-batch)
FStrategyAssignment CachedAssignment;
FObservationElement CachedObservation;
// ... (15+ more deprecated members)
```

**Remove deprecated getters:**
```cpp
// REMOVE:
UFUNCTION(BlueprintPure, meta = (DeprecatedFunction))
EStrategyType GetCurrentStrategy() const;

UFUNCTION(BlueprintPure, meta = (DeprecatedFunction))
AObjectiveActor* GetTargetObjective() const;
```

**Add deprecation warnings (if callers exist):**
```cpp
// KEEP TEMPORARILY (if used in Blueprints):
UFUNCTION(BlueprintPure, meta = (DeprecatedFunction, DeprecationMessage = "Use GetStrategyAssignment() instead"))
EStrategyType GetCurrentStrategy() const { return GetStrategyAssignment().Strategy; }
```

**Expected Changes:**
- ~30 lines removed
- No behavior change (deprecated members unused)

#### 1.2: Clean TacticalStateComponent

**File:** `Public/RL/Components/TacticalStateComponent.h`

**Remove deprecated methods:**
```cpp
// REMOVE:
UFUNCTION(BlueprintPure, meta = (DeprecatedFunction))
AObjectiveActor* GetTargetObjective() const { return nullptr; }  // v9.0: Objectives in rewards

UFUNCTION(BlueprintPure, meta = (DeprecatedFunction))
FVector GetObjectiveLocation() const { return FVector::ZeroVector; }
```

**Expected Changes:**
- ~5 lines removed
- Update callers (if any) to use `TeamLeaderComponent::GetFriendlyObjective()`

#### 1.3: Clean RLAgentComponent

**File:** `Public/RL/Components/RLAgentComponent.h`

**Review and remove unused reward accumulation logic (if deprecated):**
```cpp
// VERIFY USAGE:
TArray<float> RewardHistory;  // Is this used? Or just AccumulatedReward?
```

**Expected Changes:**
- ~5-10 lines removed (if unused)

#### 1.4: Clean Schola Components

**Files:** `Schola/TacticalObserver.h`, `Schola/CombinedTacticalActuator.h`

**Verify if 71-feature observer is still needed:**
- If Schola now uses 56-feature observations → Mark as deprecated
- If still used → Add comment explaining 56 → 71 mapping

**Expected Changes:**
- Documentation added or component deprecated

**Testing:**
- [ ] Compile succeeds ✅
- [ ] All tests pass ✅
- [ ] Run 100 episodes → Verify reward logs match baseline ± 5%

**Exit Criteria:** All deprecated code removed, tests green, behavior unchanged

---

### Phase 2: Reorganize Directory Structure (3-4 days)

**Goal:** Move files to responsibility-based directories

**Risk:** 🟡 Medium (file moves can break includes)

#### 2.1: Create New Directories

**Add subdirectories:**
```
Schola/Observers/
Schola/Rewards/
Schola/Actuators/
Schola/Subsystems/
Schola/Utils/
Tests/Integration/
Tests/Unit/
```

**Testing:**
- [ ] Build succeeds ✅

#### 2.2: Move Files to New Locations

**Schola reorganization:**
| Current Path | New Path |
|--------------|----------|
| `Schola/TacticalObserver.h` | `Schola/Observers/TacticalObserver.h` |
| `Schola/TacticalRewardProvider.h` | `Schola/Rewards/TacticalRewardProvider.h` |
| `Schola/TacticalParameterActuator.h` | `Schola/Actuators/TacticalParameterActuator.h` |
| `Schola/CombatChoiceActuator.h` | `Schola/Actuators/CombatChoiceActuator.h` |
| `Schola/CombinedTacticalActuator.h` | `Schola/Actuators/CombinedTacticalActuator.h` |
| `Schola/ThrottledScholaSubsystem.h` | `Schola/Subsystems/ThrottledScholaSubsystem.h` |
| `Schola/TimeBasedBrain.h` | `Schola/Utils/TimeBasedBrain.h` |
| `Schola/FollowerAgentTrainer.h` | `Schola/Utils/FollowerAgentTrainer.h` |

**Actor reorganization:**
| Current Path | New Path |
|--------------|----------|
| `Public/GameAIProjectCharacter.h` | `Public/Actor/CharacterBase.h` |

**Testing:**
- [ ] Update all `#include` statements
- [ ] Compile succeeds ✅
- [ ] Run integration tests ✅

#### 2.3: Rename Versioned Files

**Remove version suffixes:**
| Current Name | New Name |
|--------------|----------|
| `STTask_ExecuteTacticalMovement_v8.h` | `STTask_ExecuteTacticalMovement.h` |
| `STTask_ExecuteTacticalMovement_v8.cpp` | `STTask_ExecuteTacticalMovement.cpp` |

**Add header comment:**
```cpp
// STTask_ExecuteTacticalMovement.h
/**
 * Tactical movement execution task (EQS-based positioning).
 * Current implementation: v9.0 (RL-modulated weights, gradient-based rewards)
 * Previous versions: v8.0 (batch-level MCTS), v7.0 (agent-by-agent)
 */
```

**Update Build.cs references (if any):**
```csharp
// GameAI_Project.Build.cs
PublicDependencyModuleNames.AddRange(new string[] {
    // ... existing modules
});
```

**Testing:**
- [ ] Compile succeeds ✅
- [ ] State Tree tasks execute correctly ✅

**Exit Criteria:** All files moved, includes updated, tests green

---
### Phase 3: Architectural Decomposition (4-5 days)
**Goal:** Decompose the God Object and distribute responsibility (highest risk phase)

#### 3.1: Decompose TeamLeaderComponent

#### 3.1: Decompose TeamLeaderComponent
**SquadManagerComponent:**
- Move `Followers` and `PendingFollowerRegistration` variables.
- Move `RegisterFollower` and `GetAliveFollowers` functions.

**IntelManagerComponent:**
- Move `KnownEnemies`, `Friendly/HostileObjective`, and `TeamObservation`.
- Move `RegisterEnemy` and `DiscoverWorldObjectives`.

**StrategicPlannerComponent (Brain & Thread Safety):**
- Move `StrategicMCTS` pointer.
- **[NEW] Encapsulate Async Lifecycle:** Move `AsyncMCTSTask` and implement RAII pattern. The `TeamLeader` should NOT manage `FAsyncTask` pointers or call `delete` directly.
- Expose `OnPlanReady` delegate for the Leader to bind to.

**TeamLeader Refactoring:**
- Retain only the references to the 3 managers above.
- Act as the event broker between `TeamComms` (Input) and `StrategicPlanner` (Process).

#### 3.2: Decompose FollowerAgentComponent
**TeamCommsComponent:**
- Move references to `TeamLeader` and `TeamLeaderActor`.
- Move `RegisterWithTeamLeader` and `SignalEventToLeader`.

#### 3.3: Micro-Decomposition (Reducing Coupling)
**VisualLoggerComponent (Clean Code):**
- Extract all `DrawDebugLine`, `DrawDebugString`, and `DrawDebugSphere` calls from Leader/Follower.
- Components push data (e.g., "Draw this path") to VisualLogger; Logger handles rendering logic.
- Benefit: Can be completely disabled in Shipping builds without `#if UE_BUILD_SHIPPING` cluttering core logic.

**ContextBridgeComponent (Dependency Inversion):**
- Remove direct dependency on `UFollowerStateTreeComponent` from `FollowerAgent`.
- Create a shared data board (Target, AlertLevel, etc.) that `StateTree` reads from.
- `FollowerAgent` writes to `ContextBridge`; `StateTree` binds to changes in `ContextBridge`.

#### 3.4: Verify Integration
- Verify that the logic in `FollowerAgentComponent::Tick` works properly via the new components.
- Ensure MCTS Async tasks are cleaned up correctly upon `EndPlay` via the new `StrategicPlanner`.

### Original Component,Member/Function,New Component
Original Component,Member/Function,New Component
TeamLeader,Followers list -> SquadManager
TeamLeader,RegisterFollower() -> SquadManager
TeamLeader,KnownEnemies list -> IntelManager
TeamLeader,DiscoverWorldObjectives() -> IntelManager
TeamLeader,StrategicMCTS ptr -> StrategicPlanner
TeamLeader,RunStrategyAssignment() -> StrategicPlanner
FollowerAgent,TeamLeader ref -> TeamComms
FollowerAgent,SignalEventToLeader() -> TeamComms


### Phase 3: Add Documentation & Coordinators (2-3 days)

**Goal:** Clarify responsibilities and initialization flow

**Risk:** 🟢 Low (documentation only + optional coordinator)

#### 3.1: Add README.md per Subsystem

**Template:**
```markdown
# [Subsystem Name]

**Responsibility:** [One-sentence description]

## Key Components
| Component | Purpose | Key Methods |
|-----------|---------|-------------|
| ... | ... | ... |

## Dependencies
- **Requires:** [List required systems]
- **Used By:** [List dependent systems]

## Initialization Order
1. [Step 1]
2. [Step 2]
...

## Common Tasks
- **How to X:** [Instructions]
- **How to Y:** [Instructions]

## Related Documentation
- [Link to CLAUDE.md section]
- [Link to other README]
```

**Create READMEs for:**
- [ ] `Team/README.md` - Team coordination system
- [ ] `AI/MCTS/README.md` - MCTS v9.0 architecture
- [ ] `RL/README.md` - RL system overview
- [ ] `Observation/README.md` - Observation scaling guide (52→56→71)
- [ ] `StateTree/README.md` - State Tree execution flow
- [ ] `Schola/README.md` - Schola integration guide

#### 3.2: Document Observation Scaling

**File:** `Observation/README.md`

**Content:**
```markdown
# Observation System

## Feature Scaling (52 → 56 → 71)

### Base Observation (52 features)
**Generated by:** `ObservationBuilderComponent::BuildLocalObservation()`
**Used by:** C++ reward calculation, internal logic

**Breakdown:**
- Agent State (4): Position (3) + Health (1)
- Combat (1): DistanceToNearestEnemy
- Environment (16): Raycast distances
- Enemy Info (16): VisibleEnemyCount (1) + NearbyEnemies (3 × 5)
- Tactical Context (4): Cover info
- Ally Context (5): Ally health, distance, direction
- **Objective Context (6) [v9.0]:** FriendlyObjective (3) + HostileObjective (3)

### RL Policy Input (56 features)
**Generated by:** `ObservationElement::ToFeatureVector()` + strategy one-hot
**Used by:** RL policy network inference

**Composition:**
- 52 base features (from above)
- **+4 strategy one-hot:** [Assault, Defend, Support, Retreat]

### Schola Training Observation (71 features)
**Generated by:** `TacticalObserver::GetObservation()`
**Used by:** Python training environment (legacy?)

**Composition:**
- [DOCUMENT BREAKDOWN IF STILL USED]
- **Status:** Verify if deprecated in v9.0
```

#### 3.3: Create InitializationCoordinator (Optional)

**File:** `Public/Util/InitializationCoordinator.h`

**Purpose:** Centralize startup sequence documentation

**Implementation Option 1 (Documentation Only):**
```cpp
/**
 * Initialization Coordinator
 *
 * PURPOSE: Documents the startup sequence for CORTEX v9.0.
 * This is a documentation header - no actual code execution required.
 *
 * STARTUP SEQUENCE:
 *
 * 1. GameMode Initialization
 *    - SimulationManagerGameMode::BeginPlay()
 *    - ScholaGameInstance::Init()
 *
 * 2. Actor Spawning
 *    - Team Leader spawned
 *    - Follower agents spawned (4 per team)
 *    - ObjectiveActors spawned (Friendly/Hostile)
 *
 * 3. Component Initialization (BeginPlay order)
 *    a. TeamLeaderComponent::BeginPlay()
 *       - Initializes MCTS
 *       - Registers objectives
 *    b. FollowerAgentComponent::BeginPlay()
 *       - Initializes sub-components (TacticalState, ObservationBuilder, RLAgent, CombatExecutor)
 *       - Registers with TeamLeader
 *    c. ScholaAgentComponent::BeginPlay() [if training]
 *       - Connects to Python gRPC server
 *
 * 4. System Activation
 *    - TeamLeader starts MCTS async task (first run after 2s)
 *    - State Tree begins execution (Tick)
 *    - EQS queries start running (2-5 Hz)
 *
 * DEPENDENCIES:
 * - TeamLeader MUST exist before Followers register
 * - ObjectiveActors MUST be registered before MCTS runs
 * - RLAgentComponent MUST be initialized before rewards calculated
 */
namespace InitializationCoordinator {
    // No implementation - documentation only
}
```

**Implementation Option 2 (Active Coordinator):**
```cpp
// FUTURE ENHANCEMENT: Active orchestration
class UInitializationCoordinator : public UGameInstanceSubsystem {
public:
    UFUNCTION()
    void OrchestrateStartup();

    UFUNCTION()
    bool ValidateInitializationState();

private:
    enum class EInitPhase {
        GameMode,
        ActorSpawn,
        ComponentInit,
        SystemActivation
    };

    EInitPhase CurrentPhase = EInitPhase::GameMode;
};
```

**Recommendation:** Start with **Option 1 (documentation only)** to avoid over-engineering.

#### 3.4: Create ScholaCoordinator (Optional)

**File:** `Public/Schola/ScholaCoordinator.h`

**Purpose:** Centralize Schola initialization logic (currently scattered)

**Implementation:**
```cpp
/**
 * Schola Coordinator
 *
 * PURPOSE: Central manager for Schola training framework initialization.
 * Consolidates logic currently scattered across:
 * - ScholaGameInstance
 * - ScholaAgentComponent
 * - ThrottledScholaSubsystem
 *
 * RESPONSIBILITIES:
 * - Connect to Python gRPC server
 * - Initialize observers/actuators/rewards
 * - Manage training episodes
 * - Coordinate agent registration
 */
class UScholaCoordinator : public UObject {
public:
    UFUNCTION()
    void InitializeScholaEnvironment();

    UFUNCTION()
    void RegisterAgent(UScholaAgentComponent* Agent);

    UFUNCTION()
    void StartEpisode();

    UFUNCTION()
    void EndEpisode();

private:
    UPROPERTY()
    TArray<UScholaAgentComponent*> RegisteredAgents;

    UPROPERTY()
    UScholaCombatEnvironment* Environment;
};
```

**Migration Plan (if implemented):**
```
1. Extract initialization logic from ScholaGameInstance → ScholaCoordinator
2. Update ScholaAgentComponent to register with coordinator
3. Update ThrottledScholaSubsystem to use coordinator
4. Test episode flow (start → step → end)
```

**Recommendation:** **Defer to Phase 4** (optional enhancement). Document current flow first.

**Testing:**
- [ ] All README.md files added ✅
- [ ] Documentation reviewed for accuracy ✅
- [ ] (Optional) InitializationCoordinator validates startup ✅

**Exit Criteria:** Documentation complete, initialization flow clarified

---

### Phase 4: Update Tests for v9.0 (1-2 days)

**Goal:** Ensure tests validate v9.0 behavior (strategy-only MCTS)

**Risk:** 🟢 Low (test updates only)

#### 4.1: Update MCTS Assignment Test

**File:** `Tests/Unit/TestMCTSAssignment.cpp`

**Current Test (v8.20 - checks objectives):**
```cpp
TEST_F(MCTSAssignmentTest, AssignsBatchWithObjectives) {
    // OUTDATED: v8.20 checked TargetObjective != nullptr
    ASSERT_NE(assignment.TargetObjective, nullptr);
}
```

**Updated Test (v9.0 - strategy-only):**
```cpp
TEST_F(MCTSAssignmentTest, AssignsStrategyOnlyBatch) {
    // v9.0: MCTS assigns strategies only (no objectives)
    FBatchAssignment result = MCTS->SelectBatchByUCB1();

    ASSERT_EQ(result.Assignments.Num(), 4);  // 4 agents

    for (const FStrategyAssignment& assignment : result.Assignments) {
        ASSERT_NE(assignment.Agent, nullptr);
        ASSERT_NE(assignment.Strategy, EStrategyType::None);
        // v9.0: No TargetObjective field anymore
    }
}
```

#### 4.2: Update Integration Test

**File:** `Tests/Integration/TestIntegrationScenarios.cpp`

**Add v9.0-specific test:**
```cpp
TEST_F(IntegrationTest, AssaultAgentsApproachHostileObjective) {
    // v9.0: Verify reward-driven objective behavior

    // Setup: Spawn team with Assault strategy
    FBatchAssignment batch = CreateBatchAssignment({
        EStrategyType::Assault,
        EStrategyType::Assault,
        EStrategyType::Assault,
        EStrategyType::Support
    });

    TeamLeader->ApplyBatchAssignment(batch);

    // Run 100 steps
    for (int32 i = 0; i < 100; ++i) {
        Tick(0.1f);
    }

    // Verify: Assault agents moved closer to hostile objective
    for (AFollowerAgent* agent : GetAssaultAgents()) {
        float initialDistance = GetInitialHostileDistance(agent);
        float currentDistance = GetCurrentHostileDistance(agent);

        EXPECT_LT(currentDistance, initialDistance);  // Closer than start
    }
}
```

#### 4.3: Add Observation Feature Count Test

**File:** `Tests/Unit/TestObservationElement.cpp` (NEW)

```cpp
#include "Observation/ObservationElement.h"

TEST(ObservationElementTest, Produces52BaseFeatures) {
    FObservationElement obs;
    // ... populate observation ...

    TArray<float> features = obs.ToFeatureVector();

    EXPECT_EQ(features.Num(), 52);  // v9.0: 52 base features
}

TEST(ObservationElementTest, IncludesObjectiveContext) {
    FObservationElement obs;
    obs.FriendlyObjectiveDistance = 0.5f;
    obs.HostileObjectiveDistance = 0.8f;

    TArray<float> features = obs.ToFeatureVector();

    // Verify last 6 features are objective context
    EXPECT_FLOAT_EQ(features[46], 0.5f);  // FriendlyObjectiveDistance
    EXPECT_FLOAT_EQ(features[49], 0.8f);  // HostileObjectiveDistance
}
```

**Testing:**
- [ ] All tests updated for v9.0 ✅
- [ ] New tests added (observation feature count) ✅
- [ ] All tests pass ✅

**Exit Criteria:** Test suite validates v9.0 behavior

---

### Phase 5: Header Dependency Optimization (Optional - 1-2 days)

**Goal:** Reduce compile times by minimizing header includes

**Risk:** 🟡 Medium (can introduce compile errors if done incorrectly)

#### 5.1: Analyze Include Chains

**Use UE5's Include-What-You-Use (IWYU) or manual analysis:**
```bash
# Find files with most includes
grep -r "#include" Public/ | cut -d: -f1 | sort | uniq -c | sort -rn | head -20
```

**Common issues:**
- Including full headers when forward declarations suffice
- Circular include dependencies
- Including large headers (e.g., `RLTypes.h`) in many files

#### 5.2: Apply Forward Declarations

**Example (FollowerAgentComponent.h):**
```cpp
// BEFORE:
#include "Team/Components/TeamLeaderComponent.h"
#include "RL/Components/TacticalStateComponent.h"
#include "Observation/ObservationElement.h"

// AFTER:
class UTeamLeaderComponent;  // Forward declaration
class UTacticalStateComponent;
struct FObservationElement;

// In .cpp file:
#include "Team/Components/TeamLeaderComponent.h"
#include "RL/Components/TacticalStateComponent.h"
#include "Observation/ObservationElement.h"
```

**Testing:**
- [ ] Compile succeeds ✅
- [ ] Incremental build time reduced (measure before/after) ✅

**Exit Criteria:** Compile time reduced by 10-20%

---

## Implementation Checklist

### Phase 0: Preparation
- [ ] Create feature branch `refactor/v9.0-cleanup`
- [ ] Tag current state as `pre-refactor-v9.0`
- [ ] Record baseline behavior (10 sessions, 1000 episodes)
- [ ] Create integration tests (strategy assignment, observation features)
- [ ] Run baseline tests → All pass ✅

### Phase 1: Remove Deprecated Code
- [ ] Clean `FollowerAgentComponent` (~30 lines removed)
- [ ] Clean `TacticalStateComponent` (~5 lines removed)
- [ ] Clean `RLAgentComponent` (verify reward history usage)
- [ ] Clean Schola components (document 71-feature observer)
- [ ] Compile & test → All pass ✅
- [ ] Run 100 episodes → Rewards match baseline ± 5% ✅

### Phase 2: Reorganize Directory Structure
- [ ] Create new subdirectories (Schola/Observers, etc.)
- [ ] Move Schola files to new locations (8 files)
- [ ] Rename `GameAIProjectCharacter` → `CharacterBase`
- [ ] Rename `STTask_ExecuteTacticalMovement_v8` → remove suffix
- [ ] Update all `#include` statements
- [ ] Compile & test → All pass ✅

### Phase 3: Architectural Decomposition
- [ ] Implement `SquadManagerComponent` and Unit Test
- [ ] Implement `IntelManagerComponent` and Test Objective Discovery
- [ ] Implement `StrategicPlannerComponent` (ensure `FAsyncTask` is fully encapsulated)
- [ ] Implement `TeamCommsComponent` and Test Leader Registration
- [ ] Implement `VisualLoggerComponent` and migrate debug drawing
- [ ] Implement `ContextBridgeComponent` to remove StateTree dependency
- [ ] Convert `TeamLeaderComponent` to Coordinator
- [ ] Check for Compile and Runtime Errors

### Phase 3.1: Add Documentation
- [ ] Add `Team/README.md`
- [ ] Add `AI/MCTS/README.md`
- [ ] Add `RL/README.md`
- [ ] Add `Observation/README.md` (document 52→56→71 scaling)
- [ ] Add `StateTree/README.md`
- [ ] Add `Schola/README.md`
- [ ] Add `Util/InitializationCoordinator.h` (documentation)
- [ ] Review all READMEs for accuracy ✅

### Phase 4: Update Tests
- [ ] Update `TestMCTSAssignment.cpp` for strategy-only (v9.0)
- [ ] Add integration test: "Assault agents approach hostile objective"
- [ ] Add unit test: "ObservationElement produces 52 features"
- [ ] Run all tests → All pass ✅

### Phase 5: Header Optimization (Optional)
- [ ] Analyze include chains
- [ ] Apply forward declarations (10+ headers)
- [ ] Measure compile time improvement (target: -10-20%)
- [ ] Compile & test → All pass ✅

---

## Success Criteria

### Functional Requirements
- ✅ All deprecated code removed (40+ members)
- ✅ Directory structure reorganized by responsibility
- ✅ All files renamed (no version suffixes)
- ✅ Documentation added (6+ READMEs)
- ✅ Tests updated for v9.0

### Non-Functional Requirements
- ✅ No behavior changes (reward logs match baseline ± 5%)
- ✅ All tests pass
- ✅ Compile time unchanged or improved
- ✅ Code coverage maintained (if tracked)

### Quality Metrics
| Metric | Before | After | Target |
|--------|--------|-------|--------|
| **Deprecated Members** | 40+ | 0 | ✅ 0 |
| **Version Suffixes** | 2 files | 0 | ✅ 0 |
| **READMEs** | 0 | 6+ | ✅ 6+ |
| **Compile Warnings** | TBD | 0 | ✅ 0 |
| **Test Pass Rate** | 100% | 100% | ✅ 100% |

---

## Risk Mitigation

### Risk 1: Include Chain Breaks
**Likelihood:** Medium
**Impact:** High (compile errors)

**Mitigation:**
- Update one file at a time
- Compile after each move
- Keep `#include` updates in separate commits

### Risk 2: Blueprint References Break
**Likelihood:** Low
**Impact:** High (runtime errors)

**Mitigation:**
- Search for Blueprint references to renamed files
- Add deprecation redirects in `.uproject` if needed
- Test in-editor after renames

### Risk 3: Behavior Changes
**Likelihood:** Low
**Impact:** Critical (invalidates training)

**Mitigation:**
- Run 1000-episode validation after each phase
- Compare reward logs to baseline
- Revert if rewards differ by >5%

---

## Rollback Plan

If critical issues occur:

1. **Identify last working commit:**
   ```bash
   git log --oneline refactor/v9.0-cleanup
   ```

2. **Revert to safe state:**
   ```bash
   git reset --hard <commit-hash>
   ```

3. **Cherry-pick safe changes:**
   ```bash
   git cherry-pick <safe-commit-1> <safe-commit-2>
   ```

4. **Re-run validation:**
   - Compile ✅
   - Tests pass ✅
   - Rewards match baseline ✅

---

## Post-Refactoring Tasks

After all phases complete:

- [ ] **Merge to main:**
  ```bash
  git checkout main
  git merge refactor/v9.0-cleanup
  git tag v9.0-refactored
  ```

- [ ] **Update CLAUDE.md:**
  - [ ] Add link to REFACTORING_PLAN_v9.0.md
  - [ ] Update file locations (if changed)
  - [ ] Add "Code Organization" section

- [ ] **Run extended validation:**
  - [ ] 5000-episode training run
  - [ ] Performance profiling (ensure <35ms/step)
  - [ ] Memory usage check (ensure <4.5MB)

- [ ] **Announce completion:**
  - Update team documentation
  - Share key improvements (removed 40+ deprecated members, added 6 READMEs)

---

## Appendix A: File Move Mapping

### Schola Reorganization
```
OLD → NEW

Schola/TacticalObserver.h → Schola/Observers/TacticalObserver.h
Schola/TacticalRewardProvider.h → Schola/Rewards/TacticalRewardProvider.h
Schola/TacticalParameterActuator.h → Schola/Actuators/TacticalParameterActuator.h
Schola/CombatChoiceActuator.h → Schola/Actuators/CombatChoiceActuator.h
Schola/CombinedTacticalActuator.h → Schola/Actuators/CombinedTacticalActuator.h
Schola/ThrottledScholaSubsystem.h → Schola/Subsystems/ThrottledScholaSubsystem.h
Schola/TimeBasedBrain.h → Schola/Utils/TimeBasedBrain.h
Schola/FollowerAgentTrainer.h → Schola/Utils/FollowerAgentTrainer.h
```

### Actor Reorganization
```
Public/GameAIProjectCharacter.h → Public/Actor/CharacterBase.h
```

### StateTree Renaming
```
StateTree/Tasks/STTask_ExecuteTacticalMovement_v8.h → STTask_ExecuteTacticalMovement.h
```

---

## Appendix B: Deprecated Code Removal List

### FollowerAgentComponent.h (30+ lines)
```cpp
// Lines 320-348: Remove all v7 DEPRECATED cache variables
UPROPERTY(meta = (DeprecatedProperty))
EStrategyType CurrentStrategy;

UPROPERTY(meta = (DeprecatedProperty))
AObjectiveActor* CurrentObjective;

UPROPERTY(meta = (DeprecatedProperty))
FStrategyAssignment CachedAssignment;

UPROPERTY(meta = (DeprecatedProperty))
FObservationElement CachedObservation;

// ... (15+ more)

// Remove deprecated getters
UFUNCTION(meta = (DeprecatedFunction))
EStrategyType GetCurrentStrategy() const;

UFUNCTION(meta = (DeprecatedFunction))
AObjectiveActor* GetTargetObjective() const;
```

## Appendix C: Component Responsibility Matrix

| Original Component | Member/Function | New Component |
|--------------------|-----------------|---------------|
| `TeamLeader` | `Followers` list | `SquadManager` |
| `TeamLeader` | `KnownEnemies` list | `IntelManager` |
| `TeamLeader` | `StrategicMCTS` ptr | `StrategicPlanner` |
| `TeamLeader` | `AsyncMCTSTask` management | `StrategicPlanner` (Internal) |
| `TeamLeader` | `DrawDebugInfo()` | `VisualLogger` |
| `FollowerAgent` | `TeamLeader` ref | `TeamComms` |
| `FollowerAgent` | `DrawDebugInfo()` | `VisualLogger` |
| `FollowerAgent` | `StateTree->SharedContext` access | `ContextBridge` |

### TacticalStateComponent.h (5+ lines)
```cpp
// Remove deprecated methods
UFUNCTION(meta = (DeprecatedFunction))
AObjectiveActor* GetTargetObjective() const;

UFUNCTION(meta = (DeprecatedFunction))
FVector GetObjectiveLocation() const;
```

---

## Document Version History

- **v1.0 (2026-02-02):** Initial refactoring plan created
  - 5 phases defined (Preparation → Tests)
  - 40+ deprecated members identified
  - Directory reorganization planned
  - Documentation strategy outlined

---

**End of Refactoring Plan**
