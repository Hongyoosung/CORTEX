# MOC v10.2: Centralized Commander-Executor Architecture

**Engine:** UE5.6 | **Language:** C++17 | **Platform:** Windows | **Date:** 2026-02-19

---

## 1. Executive Summary
**MOC v10.2** implements a **Hierarchical Centralized Planning (Commander-Executor) Architecture**. Unlike v10.1's decentralized approach where each agent ran individual MCTS, v10.2 introduces a centralized Squad Commander that performs tactical planning and distributes role assignments.

* **Core Innovation:** Centralized tactical intelligence with explicit squad-level coordination
* **Key Tech:** Squad Commander (ASquadManager), Tactical Plays (pruned action space), Team-Level World Model (UTeamWorldModel), Sacrificial Decision-Making
* **Benefits:** 5× computational reduction, squad-level tactical synergy, coordinated plays

---

## 2. Reference Documents (Details)

For all detailed implementation specifications, formulas, and parameters, please refer to the linked documents below.

| Document Category | File Name | Main Content |
| :--- | :--- | :--- |
| **Game Environment** | `MocGameEnvSpecification.md` | Game Rules, Map (150x150m), Occupation Method, Reward Function, Agent Specification |
| **v10.1 Architecture** | `v10.0Architecture.md` | Decentralized MCTS, Multi-Head Policy, Individual Agent Planning |
| **v10.2 Architecture** | `v10.2Architecture.md` | Centralized Commander-Executor, Tactical Plays, Team-Level Planning |

---

## 3. Architecture Overview (v10.2)

The system consists of three layers: centralized planning, decentralized execution, and spatial reasoning.

### 3.1 Three-Layer Hierarchy
1. **Layer 1: Squad Commander (Centralized Planning)** (High-Level)
   * Actor: `ASquadManager` performs MCTS on global team state
   * Input: `FTeamWorldState` (~60-70 dim: 5 friendly + 5 enemy + map state)
   * Output: `ETacticalPlay` → Role Distribution [5 × `EStrategyType`]
   * Frequency: Every 0.5s or on critical events (Kill/Death/Capture)
   * Time Budget: 15ms (single MCTS run via `UTeamMCTS`)

2. **Layer 2: Executor Agents (Decentralized Execution)** (Mid-Level)
   * Actor: `AMocCharacter` (×5) receive role assignments via `SetCommandedStrategy()`
   * Input: Commanded Strategy (Assault/Defend/Support)
   * Process: RL Policy → EQS Weights (7-dim)
   * Output: Spatial reasoning via EQS → Navigation
   * No Local MCTS: Removed to reduce computational load

3. **Layer 3: EQS Spatial Reasoning** (Low-Level, unchanged from v10.1)
   * Input: 7-dim EQS Weights
   * Process: `UMocEQSExecutor` queries 48 samples, 8 weighted tests
   * Output: Best tactical location → UE5 Navigation

### 3.2 Supported Tactical Plays (10 total)
* **AllOutRush:** 5 Assault (aggressive all-in)
* **AggressivePush:** 4 Assault, 1 Support
* **Phalanx:** 2 Defend, 3 Support (balanced defense)
* **StandardComp:** 2 Assault, 2 Defend, 1 Support (balanced)
* **FortressDefense:** 1 Assault, 4 Defend (heavy defense)
* **TurtleFormation:** 5 Defend (full turtle)
* **BaitStrategy:** 1 Assault (bait), 4 Defend (ambush)
* **PincerManeuver:** 3 Assault (split push), 2 Support
* **HealerComp:** 2 Assault, 1 Defend, 2 Support
* **ResourceDeny:** 3 Support, 2 Assault

### 3.3 World State Dimensions (~60-70 dim)
* **Friendly (35D):** 5 positions (15D) + 5 health (5D) + 5 strategies (5D) + 5 cooldowns (5D) + 5 alive flags (5D)
* **Enemy (30D):** 5 positions (15D) + 5 confidences (5D) + 5 health (5D) + 5 alive flags (5D)
* **Map (7D):** 5 capture point states (5D) + pickup availability (1D) + time remaining (1D)

### 3.4 Team World Model Architecture
* **Input (70-dim):** 60-dim team state + 10-dim tactical play one-hot
* **Network:** Encoder (70→256→512) → Residual Blocks (512→512) → Decoder (512→256)
* **Output Heads:** Next State (60D) + Reward (3D: WinProb, HealthDelta, ObjectiveScore) + Confidence (1D)
* **Runtime:** ONNX via UE5 NNE — target latency < 1.8ms for batch=16
* **Training:** Python script at `Training/train_team_world_model.py`

---

## 4. Key Architectural Changes (v10.1 → v10.2)
* **Planning:** Decentralized (5 MCTS) → Centralized (1 MCTS via `UTeamMCTS`)
* **Compute Cost:** 5 × 15ms = 75ms → 1 × 15ms = 15ms (5× reduction)
* **Action Space:** 3^5 = 243 combinations → 10 Tactical Plays (16× pruning)
* **Coordination:** Implicit (team state observation) → Explicit (command-driven)
* **Objective:** Individual survival + diversity → Team win rate + tactical synergy
* **Sacrificial Plays:** Difficult → Natural (team-level optimization)
* **UCB:** Standard UCB → Confidence-aware UCB (`ConfidenceUCB`, C_PUCT=1.41, K_RISK=2.5)

---

## 5. Key Classes

| Class | File | Purpose |
| :--- | :--- | :--- |
| `ASquadManager` | `Team/SquadManager` | Centralized commander: MCTS planning, role distribution, event handling |
| `UTeamMCTS` | `AI/MCTS/TeamMCTS` | Centralized MCTS — UCB selection, batch leaf expansion |
| `FTeamTreeNode` | `AI/MCTS/TeamTreeNode` | MCTS tree node with confidence-aware UCB and virtual loss |
| `UTeamWorldModel` | `AI/Models/TeamWorldModel` | ONNX model wrapper: batch inference, NNE integration |
| `FTeamWorldState` | `Team/TeamWorldState` | 60-70 dim global team state, `ToTensor()` method |
| `AMocCharacter` | `Characters/MocCharacter` | Agent: receives strategy commands, executes via EQS |
| `ATeamManager` | `Team/TeamManager` | 5v5 spawning, respawn, squad commander integration |
| `UMocEQSExecutor` | `AI/EQS/MocEQSExecutor` | 7-dim weight EQS query → best tactical location |
| `UTeamDataCollector` | `AI/Training/TeamDataCollector` | (State, Action, NextState, Reward) → CSV for training |
| `MocRewardCalculator` | `RL/Rewards/` | Composite reward: WinProb + HealthDelta + ObjectiveScore |

---

## 6. Implementation Status

**✅ Complete (Weeks 1–3):**
* `FTeamWorldState` — global team state representation
* `ETacticalPlay` enum — 10 predefined team compositions
* `ECriticalEventType` enum — event-driven replanning triggers
* `ASquadManager` — centralized commander with full MCTS and event wiring
* `UTeamMCTS` — centralized MCTS with batch expansion
* `FTeamTreeNode` — MCTS tree node structure
* `ConfidenceUCB` — confidence-aware node selection
* `UTeamWorldModel` — ONNX model loading and batch inference (NNE)
* `UTeamDataCollector` — training data collection and CSV export
* `AMocCharacter::SetCommandedStrategy()` — command interface
* `AMocCharacter::PerformTacticalAction()` — EQS execution with dynamic weights
* `ATeamManager::GetSquadCommander()` — integration
* Event system — Kill/Death/Capture events wired to `ASquadManager::ReplanMCTSOnCriticalEvent()`

**⏳ In Progress (Week 4):**
* Collect 5,000+ team state transitions (data collection mode active, `bDataCollectionMode=true`)
* Train `UTeamWorldModel` using `Training/train_team_world_model.py`
* Export trained ONNX model and deploy to UE5 Content directory
* End-to-end integration testing and latency benchmarking

**📋 Planned (Optional):**
* Tactical Play Value Network (direct value prediction, bypasses rollouts)
* Dynamic action space pruning based on team state context

---

## 7. Implementation Roadmap

* **Week 1:** Core infrastructure (FTeamState, ASquadManager, enums) ✅ **COMPLETE**
* **Week 2:** World Model and MCTS (UTeamWorldModel, UTeamMCTS, FTeamTreeNode) ✅ **COMPLETE**
* **Week 3:** Event system and EQS integration (data collector, event wiring, EQS executor) ✅ **COMPLETE**
* **Week 4:** Training pipeline and end-to-end validation ⏳ **IN PROGRESS**

---

## 8. Key Configuration Parameters

```cpp
// ASquadManager
PlanningInterval  = 0.5f;    // Replan every 500ms
MCTSTimeBudget    = 0.015f;  // 15ms per MCTS run
MCTSBatchSize     = 8;       // Leaf expansion batch size
bDataCollectionMode = false; // true = ε-greedy for training data collection
ExplorationRate   = 0.7f;   // ε-greedy exploration rate

// UTeamMCTS
TimeBudgetSeconds = 0.015f;  // 15ms budget
MaxIterations     = 50;      // Safety cap

// ConfidenceUCB
C_PUCT = 1.41f;
K_RISK = 2.5f;
```
