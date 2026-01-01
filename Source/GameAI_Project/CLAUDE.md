# SBDAPM: Real-Time Multi-Agent Combat AI with MCTS + Multi-Head PPO

**Engine:** UE5.6 | **Language:** C++17 | **Platform:** Windows | **Version:** v5.0 (Multi-Head + Individual Assignment)

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
| MCTS | 30-50ms | 1MB | Individual strategy assignment |
| RL Inference | 1-3ms | 550KB | Multi-head ONNX (4 heads + shared trunk, 64 features) |
| StateTree | <0.5ms/agent | 100KB | Per-tick budget |
| **Total (4 agents)** | **5-10ms** | **4MB** | Real-time requirement |

### File Locations (Quick Jump)
| Feature | Path | Key Methods |
|---------|------|-------------|
| MCTS | `AI/MCTS/MCTS.cpp` | `RunMCTS():71`, `CalculateObjectiveScore():180` |
| RL Policy (Multi-Head) | `RL/RLPolicyNetwork.cpp` | `GetAction():562`, `SelectHead():580` |
| Team Leader | `Team/TeamLeaderComponent.cpp` | `TickComponent():182`, `AssignIndividualStrategies():260` |
| Follower | `Team/FollowerAgentComponent.cpp` | `ExecuteCommand():94` |
| Types & Config | `RL/RLTypes.h` | `EStrategyType`, `FMultiHeadPolicyConfig` |

### Search Terms (When Unfamiliar)
- **Multi-Head RL:** "multi-task learning shared trunk", "gated policy networks"
- **MCTS:** "UCB1 algorithm 2002", "PUCT AlphaGo", "UE5 async task graph"
- **PPO:** "Proximal Policy Optimization Schulman 2017", "actor-critic methods"
- **RLlib:** "Ray RLlib real-time training", "RLlib custom models"
- **UE5 APIs:** "UE5.6 NNE ONNX runtime", "FTimerManager"

---

## Architecture (v5.0 Multi-Head + Individual Assignment)

### System Flow
```
Team Leader (1 per team, continuous 1-2s planning)
  ├─ MCTS (Strategic heuristic evaluation, UCB1 selection)
  │   └─ Individual Assignment: Agent1→Assault, Agent2→Defend, Agent3→Support...
  ├─ Scoring considers: agent health, ammo, position, ally needs
  └─ Strategic Commands → Followers (each with unique strategy)
                           │
Followers (N agents, strategy-specific tactical execution)
  ├─ Multi-Head PPO Network:
  │   ├─ Shared Trunk (128→128→64) - common features
  │   ├─ Strategy Head Selection based on assigned EStrategyType
  │   └─ Outputs: [Position, Target, FireMode] per head
  ├─ Engine Integration (NavMesh, SetFocus, CharacterMovement)
  ├─ StateTree Execution (macro action → engine commands)
  └─ Schola Integration → RLlib Environment → Real-Time PPO Updates
```

### Multi-Head Network Architecture
```
Input: 64 features (streamlined observation)
  ├─ Agent State (7): pos(3), vel(3), health(1)
  ├─ Combat (1): enemy_dist(1)
  ├─ Perception (32): raycasts(16), hit_types(16)
  ├─ Enemy Info (16): count(1), nearby(15)
  ├─ Tactical (4): has_cover(1), cover_dist(1), cover_dir(2)
  └─ Support Context (4): ally_needs(1), ally_health(1), ally_dist(1), ally_dir(1)

                    ↓
┌──────────────────────────────────────────────────────────┐
│  Shared Trunk: [128 → 128 → 64] ReLU                     │
│  (Common features: aiming, cover, movement)              │
└──────────────────────────────────────────────────────────┘
        ↓           ↓           ↓           ↓
   ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐
   │Assault │  │ Defend │  │Support │  │Retreat │
   │  Head  │  │  Head  │  │  Head  │  │  Head  │
   └────────┘  └────────┘  └────────┘  └────────┘
        ↓           ↓           ↓           ↓
   [pos,tgt,fire] per head - strategy gates active head
                    ↓
┌──────────────────────────────────────────────────────────┐
│  Shared Critic: Value estimate (1) - for PPO training    │
└──────────────────────────────────────────────────────────┘
```

### Key Features (v5.0)
1. **Individual Strategy Assignment** - MCTS assigns different strategies to different agents
2. **Multi-Head Architecture** - Shared trunk + 4 strategy-specific heads
3. **Support Strategy** - Retained for ally protection (not removed)
4. **Streamlined Observations** - 64 features (down from 74)
5. **Strategy-Specific Rewards** - Each strategy has tuned reward functions

---

## Core Components

### 1. Team Leader (`Team/TeamLeaderComponent.cpp`)
**Role:** Strategic planning with individual agent assignment

**v5.0 Features:**
- Individual strategy assignment per agent based on state
- Enhanced scoring: health, ammo, position, ally needs
- Support trigger detection (ally critical → assign Support)
- Continuous MCTS (1-2s intervals)

**Individual Assignment Logic:**
```cpp
// Agent1 (healthy, has ammo) → Assault
// Agent2 (good position, flank covered) → Defend
// Agent3 (ally Agent4 critical) → Support
// Agent4 (low health) → Retreat
```

**Files:** `Team/TeamLeaderComponent.h/cpp`, `AI/MCTS/MCTS.h/cpp`

---

### 2. Multi-Head PPO Network (`RL/RLPolicyNetwork.cpp`) ✅ v5.0
**Purpose:** Strategy-conditioned tactical decision-making

**Architecture:**
```
Shared Trunk: [128 → 128 → 64] (learns common features)
  ├─ Assault Head: Aggressive positioning, priority targeting
  ├─ Defend Head: Hold position, suppress threats
  ├─ Support Head: Protect ally, draw aggro
  └─ Retreat Head: Disengage, maintain distance
Shared Critic: Value estimate (for PPO training only)
```

**Head Selection:**
```cpp
FMacroAction GetAction(Observation, EStrategyType Strategy) {
    Features = RunSharedTrunk(Observation);
    Logits = RunHead(Features, Strategy);  // Strategy gates head
    return DecodeAction(Logits);
}
```

**Files:** `RL/RLPolicyNetwork.h/cpp`, `RL/RLTypes.h`

---

### 3. Strategy-Specific Rewards (`RL/RewardCalculator.cpp`)

**Assault Rewards:**
| Event | Reward | Rationale |
|-------|--------|-----------|
| Kill enemy | +15.0 | Primary objective |
| Advance toward objective | +0.5/sec | Progress |
| Death | -8.0 | Lower penalty (expected risk) |

**Defend Rewards:**
| Event | Reward | Rationale |
|-------|--------|-----------|
| Hold position | +0.3/sec | Primary objective |
| Suppress enemy | +3.0 | Denying movement |
| Death | -12.0 | Higher penalty (anchor loss) |

**Support Rewards:**
| Event | Reward | Rationale |
|-------|--------|-----------|
| Protected ally survives | +15.0 | Primary objective |
| Kill threat to ally | +12.0 | Direct protection |
| Ally dies | -20.0 | Mission failed |

**Retreat Rewards:**
| Event | Reward | Rationale |
|-------|--------|-----------|
| Increase distance | +0.3/sec | Primary objective |
| Reach safe zone | +10.0 | Mission success |
| Death | -15.0 | Failed retreat |

**Files:** `RL/RewardCalculator.h/cpp`, `RL/RLTypes.h:FStrategyRewardWeights`

---

### 4. Followers (`Team/FollowerAgentComponent.cpp`)
**Role:** Strategy-specific tactical execution

**v5.0 Features:**
- Receives individual strategy from leader
- Strategy determines which head is active
- Support context observation (ally tracking)
- Real-time experience collection via Schola

---

### 5. Observations (`Observation/TacticalObserver.cpp`)

**v5.0 Streamlined Observation (64 features):**
```
Agent State (7): pos(3), vel(3), health(1)
Combat (1): enemy_dist(1)
Perception (32): raycasts(16), hit_types(16)
Enemy Info (16): count(1), nearby(15)
Tactical (4): has_cover(1), cover_dist(1), cover_dir(2)
Support Context (4): ally_needs(1), ally_health(1), ally_dist(1), ally_dir(1)
```

**Removed from v4.0:**
- Rotation (3) - engine auto-aim handles this
- Shield (1) - not implemented
- Ammo (1) - deprecated, infinite ammo assumed
- Cooldown (1) - deprecated, handled by engine
- Weapon type (1) - deprecated, single weapon type
- Terrain type (1) - covered by raycasts
- Objective embedding (4) - replaced by head selection

---

### 6. Types & Configuration (`RL/RLTypes.h`)

**Key Structs (v5.0):**
- `EStrategyType` - Assault, Defend, Support, Retreat
- `FAllyContext` - Support observation (ally health, distance, direction)
- `FMultiHeadPolicyConfig` - Network architecture config
- `FStrategyRewardWeights` - Per-strategy reward constants
- `FAssignmentScoreConfig` - MCTS individual assignment thresholds

---

## Design Patterns & Principles

### SOLID Adherence
| Principle | Implementation |
|-----------|----------------|
| **Single Responsibility** | Leader (strategy), Follower (tactics), Head (specialized behavior) |
| **Open/Closed** | `EStrategyType` extensible, heads addable |
| **Liskov Substitution** | All heads share same interface |
| **Interface Segregation** | `IObservable`, `ICommandReceiver` interfaces |
| **Dependency Inversion** | MCTS depends on `URLPolicyNetwork` abstraction |

### Key Patterns
- **Strategy Pattern:** Multi-head selection based on EStrategyType
- **Observer Pattern:** Team communication (leader → followers)
- **Gated Network:** Strategy embedding gates active head
- **Facade Pattern:** `TeamLeaderComponent` (MCTS + assignment facade)

---

## Architecture Rules (Invariants)

1. **ONLY Leaders run MCTS** (followers NEVER touch MCTS)
2. **MCTS assigns individual strategies** (different agents can have different strategies)
3. **Strategic heuristic evaluation** (objective + agent state → strategy score)
4. **Multi-head network** (shared trunk + 4 strategy-specific heads)
5. **Strategy gates head selection** (EStrategyType determines active head)
6. **Rewards are strategy-specific** (each strategy has tuned reward functions)
7. **Support is a first-class strategy** (not removed, triggers on ally critical)
8. **Observations streamlined to 64** (removed redundant features)
9. **Engine handles physics** (NavMesh, SetFocus, CharacterMovement)
10. **Macro actions only** (RL outputs discrete indices)

---

## MCTS Individual Assignment (v5.0 Core Innovation)

### Problem Solved
v4.0 assigned same strategy to all agents. v5.0 enables tactical diversity.

### Assignment Scoring
```cpp
float CalculateObjectiveScore(Agent, Strategy) {
    float base = ObjectiveProgress(Strategy);

    // Individual modifiers
    if (Strategy == Assault && Agent.Health > 0.7f && Agent.Ammo > 0.5f)
        base += 0.3f;

    if (Strategy == Retreat && Agent.Health < 0.3f)
        base += 0.4f;

    if (Strategy == Support && FindAllyInNeed())
        base += 0.3f * (1.0f - NeedyAlly.Health);

    if (Strategy == Defend && Agent.HasCover)
        base += 0.2f;

    return base;
}
```

### Support Triggers
| Condition | Action |
|-----------|--------|
| Ally health < 30% | Nearest healthy → Support |
| Ally surrounded (3+ enemies) | Best positioned → Support |
| Ally retreating | One agent → Support cover |

### Emergent Tactics
- **2 Assault + 1 Defend + 1 Support** - Balanced push
- **1 Assault + 2 Defend + 1 Retreat** - Defensive posture
- **Dynamic switching** - Wounded agents transition Assault → Retreat

---

## v5.0 Implementation Status (2026-01-01)

### 🎯 CURRENT STATUS: Planning Complete, Ready for Implementation

**Completed:**
- ✅ Architecture design (Multi-Head + Individual Assignment)
- ✅ RLTypes.h refactored (EStrategyType, FAllyContext, FMultiHeadPolicyConfig)
- ✅ Refactoring plan document created (REFACTORING_PLAN_v5.0.md)
- ✅ CLAUDE.md updated to v5.0

**Implementation Phases:**
| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Data Structures (RLTypes.h) | ✅ Complete |
| 2 | MCTS Individual Assignment | ⏳ Pending |
| 3 | Python Multi-Head Network | ⏳ Pending |
| 4 | Strategy-Specific Rewards | ⏳ Pending |
| 5 | C++ Inference Updates | ⏳ Pending |
| 6 | Support Context Observation | ⏳ Pending |

**Reference:** See `REFACTORING_PLAN_v5.0.md` for detailed implementation steps.

---

## References

### Papers
- **PPO:** "Proximal Policy Optimization Algorithms" (Schulman et al., 2017)
- **Multi-Task RL:** "Multi-Task Learning in Deep Networks" (Ruder, 2017)
- **UCB1:** "Finite-time Analysis of the Multiarmed Bandit Problem" (Auer et al., 2002)

### UE5 Documentation
- **NNE ONNX Runtime:** Search "UE5.6 Neural Network Engine ONNX"
- **StateTree:** Search "UE5 StateTree plugin documentation"
- **EQS:** Search "UE5 Environment Query System cover generation"

---

**Last Updated:** v5.0 Architecture Design Complete (2026-01-01)
