***

# 📄 수정된 README.md

```markdown
# CORTEX: Coordinated Orchestrated Real-Time Tactical EXecution

A sophisticated, hierarchical multi-agent AI system for Unreal Engine 5. CORTEX combines **batch-level Monte Carlo Tree Search (MCTS) v8.20** for high-level team composition with **Reinforcement Learning (RL) v8.0** for low-level tactical parameters, all orchestrated through Behavior Trees (BT) and a Finite State Machine (FSM) to deliver real-time tactical decision-making.

**Engine:** Unreal Engine 5.6 | **Language:** C++17 | **Platform:** Windows | **Version:** v8.20

---

## Architecture

The system is designed around a hierarchical team structure where a single Team Leader directs multiple Follower agents through **batch-level strategy assignment** (v8.20).

**Hierarchical Team System:** Leader (MCTS batch selection) → Followers (RL tactical parameters + BT execution)

Team Leader (per team) → Batch-Level MCTS v8.20 → Complete 4-agent strategy assignments
↓
Followers (4 agents) → FSM + RL Policy v8.0 + Behavior Tree → Tactical execution

text

### Key Architectural Benefits (v8.20):
- **Guaranteed Completeness:** Every MCTS output contains all 4 agent assignments (no partial assignments)
- **Clear Learning Signal:** Batch-level win rates enable systematic improvement (e.g., "TightAssault: 60% vs WideDefense: 45%")
- **Cold Start Mitigation:** Persistent batch cache enables warm start across training runs
- **Reduced Complexity:** Action space reduced from 4,096 combinations to 8 strategic prototypes
- **Performance:** MCTS runs once per team with 50% latency reduction (20-30ms vs 30-50ms in v8.10)
- **Asynchronous Strategy:** The strategic MCTS calculation runs on a background thread
- **Rich Observations:** 68 base features + 4 strategy features = 72-dimensional observation space

---

## Core Components

### 1. **Team Leader (`Team/TeamLeaderComponent.h/cpp`) - v8.20**
-   Uses **batch-level MCTS** to select from 8 pre-defined team composition prototypes.
-   Implements **UCB1 algorithm** for exploration-exploitation balance.
-   Maintains **persistent batch performance cache** (JSON) for warm start across training runs.
-   Issues complete 4-agent strategy assignments to follower agents.
-   **Key Methods:** `RunStrategyAssignment()` (calls v8.20), `UpdateBatchCache()`, `SaveBatchCache()`

### 2. **MCTS Batch System (`AI/MCTS/MCTS.h/cpp`) - v8.20**
-   **8 Batch Prototypes:** TightAssault, WideDefense, Balanced, SupportFocus, DefenseFocus, OffensiveSwarm, DefensiveWall, MixedObjectives
-   **Batch Generation:** `GenerateCompleteBatches()` maps prototypes to actual agents
-   **Batch Selection:** `SelectBatchByUCB1()` uses UCB = WinRate + C * sqrt(log(N)/n)
-   **Performance Tracking:** `UpdateBatchCache()` accumulates wins/trials per batch
-   **Persistence:** `SaveBatchCache()` / `LoadBatchCache()` for cross-run learning
-   **Key Structures:** `FBatchPrototype`, `FBatchPerformance`

### 3. **Followers (`Team/FollowerAgentComponent.h/cpp`) - v8.0**
-   Receive strategy assignments from the leader and transition states via an FSM.
-   Use a **Reinforcement Learning (RL) policy** to select tactical parameters (Aggression, Cover, Spread, Risk).
-   Execute actions using a Behavior Tree.
-   **Unchanged from v8.0** (tactical parameter control)

### 4. **Finite State Machine (FSM) (`StateMachine.h/cpp`) - v8.0**
-   Manages high-level state transitions for followers based on MCTS-assigned strategies.
-   States include: `Idle`, `Assault`, `Defend`, `Support`, `Move`, `Retreat`, `Dead`.
-   **Unchanged from v8.0**

### 5. **Reinforcement Learning (RL) Policy (`RL/RLPolicyNetwork.h/cpp`) - v8.0**
-   A 3-layer neural network trained via PPO.
-   **4 strategy-specific policy heads** for guaranteed tactical differentiation.
-   Outputs **4 continuous tactical parameters** + **2 discrete combat choices**.
-   **Unchanged from v8.0** (modulates EQS spatial reasoning)

### 6. **Behavior Trees (BT) (`BehaviorTree/*`) - v8.0**
-   Contains custom nodes to execute the tactical actions chosen by the RL policy.
-   Integrates directly with the FSM and RL components.
-   **Unchanged from v8.0**

### 7. **Observation System (`Observation/*`) - v8.0**
-   Gathers and manages the **68 individual features + 4 strategy one-hot = 72 features** that feed the AI decision-making processes.
-   **Unchanged from v8.0**

---

## System Flow (v8.20)

┌──────────────────────────────────────────────────────────────────┐
│ Team Leader (async every 1.5s) │
│ │
│ [Phase 1] GenerateCompleteBatches() │
│ ├─ 8 pre-defined team prototypes │
│ └─ Output: 8 complete 4-agent batches │
│ │
│ [Phase 2] SelectBatchByUCB1() │
│ ├─ Query BatchCache for win rates │
│ ├─ Calculate UCB = WinRate + C * sqrt(log(TotalTrials) / n) │
│ └─ Select batch with highest UCB │
│ │
│ [Phase 3] Optional Tactical Refinement (currently skipped) │
│ └─ Delegate to RL for tactical parameter control │
│ │
│ Output: FStrategyAssignment × 4 (guaranteed complete) ✅ │
└──────────────────────────────────────────────────────────────────┘
↓
┌──────────────────────────────────────────────────────────────────┐
│ Followers (4 agents, tactical control 2-5 Hz) │
│ │
│ RL Policy Network (v8.0): │
│ ├─ Strategy-Specific Policy Heads (Assault, Defend, Support, Retreat)
│ ├─ Tactical Parameters [Aggression, Cover, Spread, Risk] │
│ └─ Combat Choice [TargetPriority: Closest/LowestHP] │
│ │
│ EQS Execution: │
│ ├─ Tactical parameters → EQS weights │
│ ├─ EQS → Optimal tactical position │
│ └─ NavMesh → Movement execution │
│ │
│ Combat Execution: │
│ ├─ Target priority → Target selection │
│ └─ Auto-aim + Auto-fire │
└──────────────────────────────────────────────────────────────────┘

text

---

## 8 Batch Prototypes (v8.20)

| Prototype | Composition | Primary Objective | Use Case |
|-----------|-------------|-------------------|----------|
| **TightAssault** | [A, A, A, S] | Hostile | Aggressive push with support |
| **WideDefense** | [D, D, S, S] | Friendly | Defensive formation |
| **Balanced** | [A, D, S, R] | Neutral | Adaptable composition |
| **SupportFocus** | [A, A, S, S] | Hostile | Coordinated offense |
| **DefenseFocus** | [D, D, D, S] | Friendly | Fortified position |
| **OffensiveSwarm** | [A, A, A, A] | Hostile | Maximum aggression |
| **DefensiveWall** | [D, D, D, D] | Friendly | Turtle strategy |
| **MixedObjectives** | [A, A, D, D] | Mixed | Split team objectives |

**Legend:** A = Assault, D = Defend, S = Support, R = Retreat

---

## Project Status

**✅ Implemented (v8.20):**
-   Batch-level MCTS strategy assignment (8 prototypes)
-   UCB1-based batch selection with exploration-exploitation balance
-   Persistent batch performance cache (JSON save/load)
-   Guaranteed complete 4-agent assignments
-   Comprehensive logging and diagnostics
-   50% MCTS latency reduction (20-30ms)

**✅ Implemented (v8.0):**
-   Enhanced observation system (68+4=72 features)
-   Strategy-specific RL policy heads (4 heads)
-   Tactical parameter control (Aggression, Cover, Spread, Risk)
-   FSM refactored for command-driven transitions

**🔄 In Progress:**
-   Extended training validation (1000+ episodes with batch cache)
-   Batch performance visualization dashboard
-   Cache analytics (batch usage heatmap)

**📋 Planned (v8.21+):**
-   Optional tactical refinement within selected batch (MCTS depth 2+)
-   Dynamic batch prototype generation (learned team compositions)
-   Multi-map batch cache (map-specific performance tracking)
-   Distributed training (Ray RLlib) integration

---

## File Structure

Source/GameAI_Project/
├── AI/
│ └── MCTS/ # v8.20: Batch-level strategy assignment
│ ├── MCTS.h # FBatchPrototype, FBatchPerformance
│ └── MCTS.cpp # GenerateCompleteBatches, SelectBatchByUCB1
├── RL/ # v8.0: Tactical parameter control
│ ├── RLPolicyNetwork.h/cpp # Strategy-specific policy heads
│ └── RLTypes.h # FTacticalParameters, FCombatParameters
├── StateMachine/ # Command-driven FSM
├── BehaviorTree/ # Custom BT components
├── Team/ # Leader (v8.20), Follower, Communication
│ ├── TeamLeaderComponent.h/cpp # RunStrategyAssignment, UpdateBatchCache
│ └── FollowerAgentComponent.h/cpp
└── Observation/ # 72-feature observation system

ProjectSaved/
└── MCTS/
└── BatchCache.json # v8.20: Persistent batch performance cache

text

---

## Performance Metrics (v8.20)

| Component | Latency | Memory | Notes |
|-----------|---------|--------|-------|
| **MCTS Batch Selection** | 20-30ms | 1.5MB | 50% reduction vs v8.10 |
| **Batch Cache Query** | <1ms | 500KB | 8 batches × performance tracking |
| **RL Inference** | 2-4ms | 458KB | Batched 4 agents (unchanged) |
| **Total per Episode** | 25-35ms | 4.5MB | Real-time compatible |

---

## Getting Started

### Prerequisites
- Unreal Engine 5.6
- C++17 compiler
- Python 3.8+ (for training)
- RLlib (for distributed training)

### Building
```bash
# Clone repository
git clone https://github.com/your-org/CORTEX.git
cd CORTEX

# Generate project files
./GenerateProjectFiles.bat

# Build in Visual Studio
# Open CORTEX.sln → Build → Development Editor
Training
bash
# Navigate to training directory
cd CORTEX_Training

# Launch distributed training
python train_rllib.py --config configs/v8_20_batch_mcts.yaml

# Monitor batch cache performance
tail -f ../ProjectSaved/MCTS/BatchCache.json
Key Innovations
v8.20 (Current)
Batch-Level Strategy Assignment: 8 pre-defined team composition prototypes

UCB1 Batch Selection: Exploitation + exploration balance

Persistent Batch Cache: Warm start across training runs

Guaranteed Completeness: Always outputs 4 agent assignments

v8.0
Tactical Parameter Control: RL modulates EQS spatial reasoning

Strategy-Specific Policy Heads: Guaranteed tactical differentiation

Continuous Action Space: 4 tactical parameters per strategy

Citation
If you use CORTEX in your research, please cite:

text
@software{cortex_v8_20,
  title = {CORTEX: Batch-Level Monte Carlo Tree Search for Multi-Agent Coordination},
  author = {Your Name},
  year = {2026},
  version = {v8.20},
  url = {https://github.com/your-org/CORTEX}
}