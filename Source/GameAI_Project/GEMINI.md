# SBDAPM: Real-Time Multi-Agent Combat AI with MCTS + PPO

**Engine:** UE5.6 | **Language:** C++17 | **Platform:** Windows | **Version:** v3.1 (Real-Time Only) ✅

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

## Architecture (v3.1 Real-Time Training)

### System Flow
```
Team Leader (1 per team, continuous 1-2s planning)
  ├─ MCTS (PPO critic-guided, UCB1 selection)
  │   └─ Value estimation via aggregated PPO critic queries
  ├─ Confidence Estimates (visit count, value variance, policy entropy)
  └─ Strategic Commands → Followers
                           │
Followers (N agents, tactical execution)
  ├─ PPO Policy Network (actor + critic, real-time RLlib training)
  ├─ StateTree Execution (command-driven states)
  └─ Schola Integration → RLlib Environment → Real-Time PPO Updates
```

### Key Features
1. **Real-Time PPO Training** - Continuous learning via RLlib (no offline batches)
2. **Integrated Actor-Critic** - Single network for actions + value estimation
3. **MCTS Guidance** - Strategic planning with PPO critic leaf evaluation
4. **Unified Rewards** - Hierarchical alignment (individual + coordination + strategic)
5. **Continuous Planning** - Proactive 1-2s intervals with uncertainty quantification

---

## Core Components

### 1. Team Leader (`Team/TeamLeaderComponent.cpp`)
**Role:** Strategic planning via continuous MCTS

**v3.1 Features:**
- Continuous MCTS (1-2s intervals, not event-driven)
- PPO critic leaf evaluation (aggregated individual values → team value)
- UCB1 action selection with policy priors
- Exports uncertainty metrics (visit counts, value variance, policy entropy)

**Commands:** Assault, Defend, Move, Support, Retreat (with confidence estimates)

**Performance:** 30-50ms/tick (500-1000 simulations)

**Files:** `Team/TeamLeaderComponent.h/cpp`, `AI/MCTS/MCTS.h/cpp`, `AI/MCTS/TeamMCTSNode.h`

---

### 2. PPO Policy Network (`RL/RLPolicyNetwork.cpp`) ✅
**Purpose:** Tactical action selection + value estimation + MCTS priors

**Dual-Head Architecture (Actor-Critic):**
```
Input: Individual observation (71 features + 7 objective embedding = 78 total)
  → Shared Trunk (128→128→64, ReLU)
  ├─ Actor Head (8 dims) → Atomic actions (move, aim, fire, crouch, ability)
  └─ Critic Head (1 dim) → State value estimate ∈ [-1, 1]
```

**Actions:** 8-dimensional atomic action space (continuous + discrete)

**Training:** Real-time PPO via RLlib (no offline batches)

**Unified Rewards (v3.0):**
```cpp
// Individual
+10  Kill, +5 Damage, -5 Take Damage, -10 Death

// Coordination (NEW)
+15  Kill during strategic command
+10  Combined action (crossfire, covering)
+5   Formation maintenance
-15  Disobey strategic command

// Strategic (team-level, MCTS only)
+50  Objective captured, +30 Enemy squad wiped, -30 Own squad wiped
```

**MCTS Integration:** `GetObjectivePriors()` provides strategic guidance for MCTS tree initialization

**Value Estimation:** `GetStateValue()` queries PPO critic for MCTS leaf evaluation (aggregated across team)

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

### 4. StateTree Execution (`StateTree/FollowerStateTreeComponent.cpp`) ✅
**Purpose:** Unified command execution (replaces separate assault/defend/support/move/retreat states)

**Structure:**
```
Root
├─ ExecuteObjective (unified task, command-driven)
│   ├─ Evaluators: SyncCommand, UpdateObservation
│   └─ Conditions: CheckCommandType, CheckTacticalAction, IsAlive
```

**Files:**
- `StateTree/Tasks/STTask_ExecuteObjective.h/cpp`
- `StateTree/Evaluators/STEvaluator_SyncCommand.h/cpp`
- `StateTree/Conditions/STCondition_CheckCommandType.h/cpp`
- `StateTree/Conditions/STCondition_CheckTacticalAction.h/cpp`

---

### 5. Observations (`Observation/TeamObservation.cpp`) ✅
**Individual:** 71 features (health, position, velocity, ammo, enemy distances, combat state)

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
3. **PPO critic provides values** (MCTS queries `RLPolicyNetwork::GetStateValue()`, aggregates individual values)
4. **No offline training** (all training via real-time RLlib, no JSON export/self-play)
5. **RL provides MCTS priors** (`RLPolicyNetwork::GetObjectivePriors()`)
6. **Rewards are hierarchical** (individual + coordination + strategic, aligned across levels)
7. **Commands include confidence** (visit count, value variance, policy entropy fields)
8. **Single PPO model** (actor + critic in one network, no separate value/world models)

---

## File Structure (v3.1 Real-Time)

```
Source/GameAI_Project/
├── AI/MCTS/
│   ├── MCTS.h/cpp                        # ✅ PPO critic integration
│   ├── TeamMCTSNode.h                    # ✅ ActionPriors added
│   └── CommandSynergy.h/cpp              # ✅ Synergy scoring
├── RL/
│   ├── RLPolicyNetwork.h/cpp             # ✅ Actor + Critic + Priors
│   └── RewardCalculator.h/cpp            # ✅ Unified rewards
├── Schola/
│   ├── ScholaAgentComponent.h/cpp        # ✅ RLlib environment bridge
│   └── TacticalActuator.h/cpp            # ✅ Action execution
├── StateTree/
│   ├── Tasks/STTask_ExecuteObjective.*   # ✅ Unified execution
│   ├── Evaluators/STEvaluator_SyncCommand.*
│   ├── Evaluators/STEvaluator_UpdateObservation.*
│   ├── Conditions/STCondition_CheckCommandType.*
│   ├── Conditions/STCondition_CheckTacticalAction.*
│   ├── Conditions/STCondition_IsAlive.*
│   └── FollowerStateTreeComponent.h/cpp  # ✅ v3.1 updated
├── Team/
│   ├── TeamLeaderComponent.h/cpp         # ✅ Continuous planning
│   ├── FollowerAgentComponent.h/cpp      # ✅ Confidence-weighted execution
│   ├── StrategicCommand.h                # ✅ Confidence fields added
│   └── TeamCommunicationManager.h/cpp    # ✅ MCTS stats messaging
├── Observation/
│   ├── ObservationElement.h/cpp          # ✅ ToFeatureVector()
│   └── TeamObservation.h/cpp             # ✅ Team aggregation
├── Combat/                                # ✅ No changes (v2.0)
├── Perception/                            # ✅ No changes (v2.0)
├── EQS/                                   # ✅ No changes (v2.0)
├── Core/                                  # ✅ No changes (v2.0)
├── Scripts/
│   ├── train_rllib.py                    # ✅ Real-time PPO training
│   ├── sbdapm_env.py                     # ✅ RLlib environment
│   ├── evaluate_agents.py                # ✅ Evaluation metrics
│   └── requirements.txt                  # ✅ RLlib dependencies
└── Tests/
    ├── TestPPOIntegration.cpp            # ✅ Real-time training tests
    └── TestMCTSIntegration.cpp           # ✅ MCTS + PPO critic tests
```

---

## Success Metrics (v3.1 Real-Time)

### Quantitative
| Metric | Target | Baseline (v2.0) |
|--------|--------|-----------------|
| Win Rate (4v4) | ≥70% vs v2.0 | - |
| MCTS Efficiency | Faster with PPO critic | 500-1000 sims |
| Coordination Rate | ≥30% kills coordinated | ~5% |
| Training Convergence | Continuous improvement | N/A (real-time) |

### Qualitative
1. **Emergent Tactics:** Flanking, suppression, crossfire without explicit programming
2. **Adaptability:** Handle 3v5, varied unit types gracefully
3. **Continuous Learning:** Adapts in real-time to opponent strategies

---

## Training Pipeline (v3.1 Real-Time)

### Real-Time PPO Loop
```
1. Gameplay Loop (continuous)
   ├─ Agents interact via Schola components
   ├─ RLlib environment collects experiences automatically
   └─ PPO updates policy every N timesteps (default: 4000)

2. Model Inference
   ├─ Load latest ONNX model: rl_policy_network.onnx
   ├─ Actor head: 8-dim atomic actions
   └─ Critic head: state value estimates for MCTS

3. Evaluation (periodic)
   ├─ Monitor win rate, reward trends via TensorBoard
   ├─ Export updated ONNX models
   └─ Reload models in UE5 for continuous improvement
```

### Dependencies (requirements.txt)
```
torch==2.0.1
onnx==1.14.0
ray[rllib]==2.6.0
numpy==1.24.3
tensorboard==2.13.0
gymnasium
```

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

## Common Debugging (Quick Fixes)

| Issue | Likely Cause | Fix Location |
|-------|--------------|--------------|
| MCTS timeout | PPO critic too slow | `RLPolicyNetwork.cpp:601` - Check ONNX model size |
| RL always same action | Exploration too low | RLlib config - Increase epsilon/entropy |
| Followers ignore commands | Confidence too low | `FollowerAgentComponent.cpp:94` - Lower threshold |
| Memory spike | MCTS tree not pruned | `MCTS.cpp:71` - Enable early pruning |
| Training not converging | Learning rate issues | `train_rllib.py` - Adjust PPO hyperparameters |

---

## Next Steps (Post v3.1)

### Future Enhancements (v4.0 Candidates)
1. **Distributed Training:** Ray RLlib multi-GPU/multi-node scaling
2. **Opponent Modeling:** Predict enemy strategy (Theory of Mind)
3. **Hierarchical MCTS:** Multi-level planning (squad → team → faction)
4. **Curriculum Learning:** Progressive difficulty scaling
5. **Explainability:** Visualize MCTS trees, value heatmaps in-editor

### Performance Optimizations
- **MCTS Parallelization:** Multi-threaded tree search (currently single-thread)
- **Model Quantization:** INT8 inference (reduce latency 2-3x)
- **Sparse Observations:** Only update changed features (reduce network input size)
- **Compiled Models:** UE5 NNE compiled runtime (vs interpreted ONNX)

---

**Last Updated:** v3.1 Real-Time Training Refactoring (2025-11-28)
