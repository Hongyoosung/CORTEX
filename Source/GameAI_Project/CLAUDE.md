# SBDAPM: Real-Time Multi-Agent Combat AI with MCTS + PPO

**Engine:** UE5.6 | **Language:** C++17 | **Platform:** Windows | **Version:** v4.0 (Macro Actions) ✅

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
| MCTS | 30-50ms | 1MB | PPO critic value estimation |
| RL Inference | 1-3ms | 500KB | ONNX/NNE optimized (actor + critic) |
| StateTree | <0.5ms/agent | 100KB | Per-tick budget |
| **Total (4 agents)** | **5-10ms** | **4MB** | Real-time requirement |

### File Locations (Quick Jump)
| Feature | Path | Key Methods |
|---------|------|-------------|
| MCTS | `AI/MCTS/MCTS.cpp` | `RunMCTS():71`, `SimulateNode():153` |
| RL Policy (Actor + Critic) | `RL/RLPolicyNetwork.cpp` | `GetAction():562`, `GetStateValue():601`, `GetObjectivePriors():649` |
| Team Leader | `Team/TeamLeaderComponent.cpp` | `TickComponent():182`, `ProcessMCTSResults():245` |
| Follower | `Team/FollowerAgentComponent.cpp` | `ExecuteCommand():94` |

### Search Terms (When Unfamiliar)
- **MCTS:** "UCB1 algorithm 2002", "PUCT AlphaGo", "UE5 async task graph"
- **PPO:** "Proximal Policy Optimization Schulman 2017", "PPO critic value function", "actor-critic methods"
- **RLlib:** "Ray RLlib real-time training", "RLlib PPO algorithm", "RLlib custom environments"
- **UE5 APIs:** "UE5.6 NNE ONNX runtime", "UStaticMesh performance", "FTimerManager"


---

## Architecture (v4.0 Macro Actions)

### System Flow
```
Team Leader (1 per team, continuous 1-2s planning)
  ├─ MCTS (Strategic heuristic evaluation, UCB1 selection)
  │   └─ Leaf value: Objective progress + Team strength + Positioning
  ├─ Confidence Estimates (visit count, value variance, policy entropy)
  └─ Strategic Commands → Followers
                           │
Followers (N agents, tactical execution)
  ├─ PPO Policy Network (actor + critic, real-time RLlib training)
  │   └─ Outputs: [Position Index, Target Index, Fire Mode]
  ├─ Engine Integration (NavMesh, SetFocus, CharacterMovement)
  ├─ StateTree Execution (macro action → engine commands)
  └─ Schola Integration → RLlib Environment → Real-Time PPO Updates
```

### Key Features
1. **Macro Action Space** - High-level tactical decisions (position, target, fire mode) - Stance removed v4.0
2. **Engine-Driven Execution** - NavMesh pathfinding, auto-aim, physics handling
3. **Strategic Heuristic Evaluation** - MCTS uses handcrafted game state analysis (objectives, strength, positioning)
4. **Unified Rewards** - Hierarchical alignment (individual + coordination + strategic)
5. **Faster Convergence** - Focus on tactics, not motor skills

---

## Core Components

### 1. Team Leader (`Team/TeamLeaderComponent.cpp`)
**Role:** Strategic planning via continuous MCTS

**v4.0 Features:**
- Continuous MCTS (1-2s intervals, not event-driven)
- Strategic heuristic leaf evaluation (objective progress + team strength + positioning)
- UCB1 action selection with RL policy priors
- Exports uncertainty metrics (visit counts, value variance, policy entropy)

**Commands:** Assault, Defend, Support, Retreat (with confidence estimates, v4.0 simplified from 7 types)

**Performance:** 30-50ms/tick (500-1000 simulations), ~50x faster leaf evaluation vs neural network

**Files:** `Team/TeamLeaderComponent.h/cpp`, `AI/MCTS/MCTS.h/cpp`, `AI/MCTS/TeamMCTSNode.h`

---

### 2. PPO Policy Network (`RL/RLPolicyNetwork.cpp`) ✅ v4.0
**Purpose:** Tactical decision-making + value estimation + MCTS priors

**Dual-Head Architecture (Actor-Critic):**
```
Input: Individual observation (70 features + 4 objective embedding = 74 total)
  → Shared Trunk (128→128→64, ReLU)
  ├─ Actor Head (3 discrete logits) → Macro actions:
  │   ├─ Position: [Hold, ForwardCover, Retreat, Advance] (4)
  │   ├─ Target: [None, Enemy_0, ..., Enemy_N] (N+1, dynamic)
  │   └─ Fire Mode: [HoldFire, Fire, Suppress] (3)
  └─ Critic Head (1 dim) → Tactical value estimate ∈ [-1, 1] (for PPO training only)
```

**Actions:** MultiDiscrete([4, N+1, 3]) - High-level tactical decisions (v4.0: flanking + stance removed)

**Execution Pipeline (v4.0):**
- Python RLlib → Schola gRPC → TacticalActuator → FMacroAction → STTask_ExecuteObjective
- Movement: EQS position query → NavMesh pathfinding
- Aiming: SetFocus on enemy actor (engine auto-aim)
- Firing: FireMode enum (HoldFire/Fire/Suppress)

**Training:** Real-time PPO via RLlib (no offline batches)


**MCTS Integration:** `GetObjectivePriors()` provides strategic guidance for MCTS action priors

**Value Estimation:** PPO critic used ONLY for advantage estimation during training (NOT for MCTS)

**Files:** `RL/RLPolicyNetwork.h/cpp`, `Schola/ScholaAgentComponent.h/cpp`, `Scripts/train_rllib.py`

**Search Terms:** "PPO actor-critic architecture", "RLlib custom environments", "real-time RL training"

---

### 3. Followers (`Team/FollowerAgentComponent.cpp`) ✅
**Role:** Tactical execution with coordination tracking

**v3.1 Features:**
- Confidence-weighted command execution (low confidence → RL override allowed)
- Coordination detection (combined actions with allies → reward bonuses)
- Real-time experience collection via Schola → RLlib environment
- Auto-signals events to leader (enemy spotted, ally killed, low health)

**Integration:** PPO policy (via Schola) → StateTree execution → Perception/Combat systems

---

### 4. StateTree Execution (`StateTree/FollowerStateTreeComponent.cpp`) ✅ v4.0
**Purpose:** Unified command execution with macro action execution

**Structure:**
```
Root
├─ ExecuteObjective (unified task, macro action driven)
│   ├─ Evaluators: SyncCommand, UpdateObservation
│   └─ Conditions: CheckCommandType, CheckTacticalAction, IsAlive
```

**v4.0 Macro Action Execution (`STTask_ExecuteObjective.cpp`):**
- `ExecuteMovement()`: EQS query → NavMesh pathfinding (NO manual velocity input)
- `ExecuteAiming()`: SetFocus on enemy (engine auto-aim, NO manual rotation)
- `ExecuteFire()`: FireMode switch (HoldFire/Fire/Suppress)

**EQS Integration (`STTask_ExecuteObjective.cpp:514-595`):**
- `RunEQSQuery()`: Loads `/Game/AI/EQS/{QueryName}`, executes instant query
- Returns sorted positions (best score first) or empty array on failure
- **NO FALLBACKS** - Logs detailed errors, agents stop movement if query fails

**Required EQS Assets:**
- `/Game/AI/EQS/EQS_ForwardCover.uasset` - Cover toward objective
- `/Game/AI/EQS/EQS_RetreatCover.uasset` - Cover away from enemies
- `/Game/AI/EQS/EQS_Advance.uasset` - Forward positions (no cover)

**Note:** FlankLeft/FlankRight removed to reduce action space (6→4 positions) for faster learning

**Files:**
- `StateTree/Tasks/STTask_ExecuteObjective.h/cpp` (v4.0 complete)
- `StateTree/Evaluators/STEvaluator_SyncCommand.h/cpp`
- `StateTree/Conditions/STCondition_CheckCommandType.h/cpp`
- `StateTree/Conditions/STCondition_CheckTacticalAction.h/cpp`

---

### 5. Observations (`Observation/TeamObservation.cpp`) ✅
**Individual:** 70 features (health, position, velocity, ammo, enemy distances, combat state, shield)

**Team:** 40 features (avg health, formation metrics, command type, confidence)

**Methods:**
- `ToFeatureVector()` - Convert to neural network input
- `Clone()` - Deep copy for MCTS simulation

---

### 6. Combat System (`Combat/HealthComponent.cpp`, `Combat/WeaponComponent.cpp`) ✅
**No changes from v2.0** - Damage calculation, health management, death events

---

### 7. EQS Cover System (`EQS/*`) ✅
**No changes from v2.0** - Grid/tag-based cover generation, multi-factor scoring

---

### 8. Simulation Manager (`Core/SimulationManagerGameMode.cpp`) ✅
**No changes from v2.0** - Team registration, enemy relationship tracking, actor-team mapping

---

## Design Patterns & Principles

### SOLID Adherence
| Principle | Implementation |
|-----------|----------------|
| **Single Responsibility** | Leader (strategy), Follower (tactics), StateTree (execution) separate |
| **Open/Closed** | `ETacticalAction` enum extensible, `FStrategicCommand` struct modifiable |
| **Liskov Substitution** | `UActorComponent` base for Leader/Follower |
| **Interface Segregation** | `IObservable`, `ICommandReceiver` interfaces |
| **Dependency Inversion** | MCTS depends on `URLPolicyNetwork` abstraction (actor + critic) |

### Key Patterns
- **Strategy Pattern:** `RLPolicyNetwork` (action selection strategy)
- **Observer Pattern:** Team communication (leader → followers, event signals)
- **State Pattern:** StateTree states (assault, defend, move, support, retreat)
- **Facade Pattern:** `TeamLeaderComponent` (MCTS + RLPolicy facade)
- **Template Method:** `MCTS::SimulateNode()` (expansion, simulation, backprop steps)

---

## Work Instructions (Token Efficiency CRITICAL)

### DO
1. **Read first** - Always read affected files before modifying
2. **Use file:line** - Reference code locations precisely (e.g., `MCTS.cpp:143`)
3. **Implement directly** - Code > planning documents
4. **Batch operations** - Read multiple files in parallel, edit sequentially
5. **Search when uncertain** - Use terms from "Search Terms" section above
6. **Profile before optimizing** - Measure, don't guess

### DON'T
1. **NO verbose reports** - Code-focused only, minimal prose
2. **NO redundant updates** - Don't repeat CLAUDE.md content back
3. **NO long explanations** - Brief comments for complex logic only
4. **NO premature abstraction** - YAGNI principle
5. **NO guessing APIs** - Search "UE5.6 [API name]" if unfamiliar

### Code Style
```cpp
// Good (concise, self-documenting)
float Value = ValueNetwork->Evaluate(State);
if (Value > BestValue) { BestAction = Action; BestValue = Value; }

// Bad (over-commented, verbose)
// Evaluate the current state using the value network to get a score
float StateEvaluationScore = ValueNetworkComponent->EvaluateCurrentState(CurrentState);
// Check if this is better than our best so far
if (StateEvaluationScore > BestValueSoFar) {
    // Update best action and value
    BestActionFound = CurrentAction;
    BestValueSoFar = StateEvaluationScore;
}
```

---

## Architecture Rules (Invariants)

1. **ONLY Leaders run MCTS** (followers NEVER touch MCTS)
2. **MCTS runs continuously** (1-2s timer, not event-driven)
3. **Strategic heuristic evaluation** (MCTS uses handcrafted game state analysis: objective progress + team strength + positioning)
4. **No offline training** (all training via real-time RLlib, no JSON export/self-play)
5. **RL provides MCTS priors** (`RLPolicyNetwork::GetObjectivePriors()`)
6. **Rewards are hierarchical** (individual + coordination + strategic, aligned across levels)
7. **Commands include confidence** (visit count, value variance, policy entropy fields)
8. **Single PPO model** (actor + critic in one network, critic used ONLY for PPO training advantage estimation)
9. **Engine handles physics** (v4.0: NavMesh for movement, SetFocus for aiming)
10. **Macro actions only** (v4.0: RL outputs discrete indices, not continuous velocities)
11. **EQS required for movement** (v4.0: No fallback code, agents stop and log errors if EQS queries fail)

---

## Strategic Heuristic Evaluation (v4.0 Core Innovation)

### The Architecture Decision

**Problem Solved:** MCTS (strategic layer) needed team-level evaluation, but PPO critic (tactical layer) only understood individual agent survival.

**Solution:** Handcrafted heuristic evaluation that directly analyzes game state for strategic value.

### Evaluation Formula

```cpp
Strategic Value = ObjectiveProgress + TeamStrength + PositionalAdvantage
Range: [-1.0, +1.0] = (±0.6 + ±0.3 + ±0.1)
```

### Components

**1. Objective Progress (±0.6) - PRIMARY metric**
- **Assault:** Rewards proximity to objective (+0.2 per agent near target)
- **Defend:** Rewards being AT objective location (+0.15 if within 5m)
- **Support:** Rewards moderate distance (balanced positioning)
- **Retreat:** Rewards distance FROM enemies when threatened

**2. Team Strength (±0.3) - Force composition**
- Survival rate (alive count / total agents) × 0.15
- Average health × 0.1
- Average ammo × 0.05
- Weighted: Survival > Health > Ammo

**3. Positional Advantage (±0.1) - Tactical quality**
- Cover usage: +0.05 if >50% team in cover
- Formation spread: +0.05 if well-distributed (5-20m apart)
- Clumping penalty: -0.05 if too close (<2m apart)

### Why This Is Better

| Aspect | Neural Network (Old) | Heuristic (New) |
|--------|---------------------|-----------------|
| **Speed** | ~5ms (N agents × network) | ~0.1ms (direct math) |
| **Alignment** | Individual survival focus | Objective-centric |
| **Debuggability** | Black box | "MCTS chose Assault because objective 500m away (+0.4), team healthy (+0.3)" |
| **Training Overhead** | Requires aligned rewards | None - works immediately |
| **Separation of Concerns** | Critic used for 2 purposes | MCTS=strategy, RL=tactics |

### Implementation

**Files:**
- `AI/MCTS/MCTS.h` - Method declarations (lines 118-140)
- `AI/MCTS/MCTS.cpp` - Implementations (lines 1046-1234)
- `AI/MCTS/MCTS.cpp` - SimulateNode() integration (lines 156-184)

**Key Methods:**
- `EvaluateObjectiveProgress()` - Analyzes objective type and agent proximity
- `EvaluateTeamStrength()` - Aggregates health/ammo/survival metrics
- `EvaluatePositionalAdvantage()` - Evaluates cover and formation quality

---


## References (Search These First When Uncertain)

### Papers
- **PPO:** "Proximal Policy Optimization Algorithms" (Schulman et al., 2017)
- **UCB1:** "Finite-time Analysis of the Multiarmed Bandit Problem" (Auer et al., 2002)
- **OpenAI Five:** "Dota 2 with Large Scale Deep Reinforcement Learning" (Berner et al., 2019)
- **Actor-Critic Methods:** "Policy Gradient Methods for Reinforcement Learning with Function Approximation" (Sutton et al., 2000)

### UE5 Documentation
- **NNE ONNX Runtime:** Search "UE5.6 Neural Network Engine ONNX"
- **StateTree:** Search "UE5 StateTree plugin documentation"
- **EQS:** Search "UE5 Environment Query System cover generation"
- **Async Tasks:** Search "UE5 FAsyncTask TAsyncTask performance"

### Libraries & Tools
- **RLlib:** Search "Ray RLlib PPO tutorial", "RLlib custom environments", "RLlib real-time training"
- **MCTS:** Search "UCB1 tree search", "Monte Carlo Tree Search tutorial"
- **PPO Implementation:** Search "PPO actor-critic architecture", "advantage estimation GAE", "PPO clipping ratio"

---

## v4.0 Implementation Status (2025-12-24)

### 🎯 CURRENT STATUS: Ready for Integration Testing

**Verification Complete (2025-12-24):**
- ✅ **MCTS Integration Confirmed:** MCTS assigns objectives → FollowerAgent → TacticalObserver encodes features 71-77 → RLlib receives strategic context
- ✅ **Hybrid Architecture Working:** TeamLeaderComponent runs MCTS (continuous planning) → assigns objectives → followers receive MCTS guidance in observations
- ✅ **Training Pipeline Intact:** UE5 → Schola gRPC → sbdapm_env.py (74 features) → RLlib PPO
- ✅ **All Code Complete:** v4.0 macro actions, EQS integration, UPROPERTY-based queries
- ✅ **All Assets Created:** EQS_ForwardCover, EQS_Retreat, EQS_Advance configured with C++ contexts

**Next Step:** Phase 4D Integration Testing (1-2 hours) → Phase 4E Training

---

### ✅ Phase 1: Data Structures & Python Interface (COMPLETE)
- MultiDiscrete action space: [4, N+1, 3] (FlankLeft/Right and Stance removed for simpler learning)
- FMacroAction struct with enums (ETacticalPosition, EFireMode)
- Python environment updated (sbdapm_env.py)
- Schola actuator updated (TacticalActuator)

### ✅ Phase 2: C++ Execution Logic (COMPLETE)
- ExecuteMovement/Aiming/Fire methods implemented
- NavMesh pathfinding integration
- SetFocus auto-aim integration
- FireMode execution

### ✅ Phase 3: EQS Integration (COMPLETE)
- RunEQSQuery() implemented with UEnvQueryManager
- Loads EQS assets from /Game/AI/EQS/{QueryName}
- Executes instant queries, returns sorted positions
- **NO FALLBACK CODE** - Returns empty array and logs errors on failure
- Header documentation updated with required EQS assets
- Legacy v3.x code removed

### ✅ Phase 4A: EQS Assets Created (COMPLETE)
- ✅ EQS_ForwardCover.uasset - Cover points toward objective
- ✅ EQS_RetreatCover.uasset - Cover points away from enemies
- ✅ EQS_Advance.uasset - Forward positions (no cover required)


---

### ⏳ Phase 4E: Python RLlib Training Integration (NEXT - 30 mins)
**Prerequisites:** Phase 4D integration testing passed

**Steps:**
1. Start UE5 in PIE (Schola plugin enabled)
2. Run: `cd CORTEX_Training && python train_rllib.py`
3. Monitor logs:
   - ✅ `[Schola] Connected to UE5 at localhost:50051`
   - ✅ `[RLlib] Action space: MultiDiscrete([4, 11, 3, 3])`
   - ✅ `[EQS v4.0] Query returned N positions`
4. Watch agent behavior (should move to cover, not random wander)
5. Let training run 10-15 minutes, monitor TensorBoard

---

**Code Implementation:** 100% Complete ✅
**EQS Assets:** 100% Complete ✅
**Code Refactor (UPROPERTY):** 100% Complete ✅
**Editor Configuration:** 100% Complete ✅
**MCTS Integration Verification:** 100% Complete ✅ (2025-12-24)
**Integration Testing:** 0% Complete 🎯 **CURRENT BLOCKER**

**Time to Training-Ready:** 1-2 hours (integration testing + validation)

---

**Last Updated:** v4.0 MCTS Verification Complete, Ready for Phase 4D (2025-12-24)
