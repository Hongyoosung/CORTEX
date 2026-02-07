# MOC v10.2: Centralized Commander-Executor Architecture

**Engine:** UE5.6 | **Language:** C++17 | **Platform:** Windows | **Date:** 2026-02-08

---

## 1. Executive Summary
**MOC v10.2** implements a **Hierarchical Centralized Planning (Commander-Executor) Architecture**. Unlike v10.1's decentralized approach where each agent ran individual MCTS, v10.2 introduces a centralized Squad Commander that performs tactical planning and distributes role assignments.

* **Core Innovation:** Centralized tactical intelligence with explicit squad-level coordination
* **Key Tech:** Squad Commander (ASquadManager), Tactical Plays (pruned action space), Team-Level World Model, Sacrificial Decision-Making
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

The system consists of two layers: centralized planning and decentralized execution.

### 3.1 Two-Layer Hierarchy
1. **Layer 1: Squad Commander (Centralized Planning)** (High-Level)
* Actor: ASquadManager performs MCTS on global team state
* Input: FTeamState (60-dim: 5 friendly + 5 enemy + map state)
* Output: ETacticalPlay → Role Distribution [5 × EStrategyType]
* Frequency: Every 0.5s or on critical events (Kill/Death)
* Time Budget: 15ms (single MCTS run)

2. **Layer 2: Executor Agents (Decentralized Execution)** (Mid-Level)
* Actor: AMocCharacter (×5) receive role assignments
* Input: Commanded Strategy (Assault/Defend/Support)
* Process: RL Policy → EQS Weights (8-dim)
* Output: Spatial reasoning via EQS → Navigation
* No Local MCTS: Removed to reduce computational load

3. **Layer 3: EQS Spatial Reasoning** (Low-Level, unchanged from v10.1)
* Input: 8-dim EQS Weights
* Process: Query 48 samples, 8 weighted tests
* Output: Best tactical location → UE5 Navigation

### 3.2 Supported Tactical Plays
* **AllOutRush:** 5 Assault (aggressive)
* **Phalanx:** 2 Defend, 3 Support (balanced defense)
* **BaitStrategy:** 1 Assault (bait), 4 Defend (ambush)
* **StandardComp:** 2 Assault, 2 Defend, 1 Support (balanced)
* **FortressDefense:** 1 Assault, 4 Defend (heavy defense)
* ...and 5 more predefined compositions

---

## 4. Key Architectural Changes (v10.1 → v10.2)
* **Planning:** Decentralized (5 MCTS) → Centralized (1 MCTS)
* **Compute Cost:** 5 × 15ms = 75ms → 1 × 15ms = 15ms (5× reduction)
* **Action Space:** 3^5 = 243 combinations → ~10 Tactical Plays (16× pruning)
* **Coordination:** Implicit (team state observation) → Explicit (command-driven)
* **Objective:** Individual survival + diversity → Team win rate + tactical synergy
* **Sacrificial Plays:** Difficult → Natural (team-level optimization)

---

## 5. Implementation Status
**Implemented:**
* ✅ FTeamState (Global team state representation)
* ✅ ETacticalPlay enum (Tactical play action space)
* ✅ ECriticalEventType enum (Event-driven triggers)
* ✅ ASquadManager (Centralized commander actor)
* ✅ AMocCharacter::SetCommandedStrategy (Command interface)
* ✅ ATeamManager::GetSquadCommander (Integration)

**TODO (Next Steps):**
* ⏳ Implement centralized MCTS in UModelBasedMCTS
* ⏳ Create UTeamWorldModel (Batch aggregator)
* ⏳ Train Tactical Play Value Network
* ⏳ Integrate with existing reward system
* ⏳ End-to-end testing and validation

---

## 6. Implementation Roadmap (Summary)
* **Week 1:** Core infrastructure (FTeamState, ASquadManager) ✅ **COMPLETE**
* **Week 2:** World Model aggregation (UTeamWorldModel)
* **Week 3:** MCTS integration with Tactical Plays
* **Week 4:** Event system and end-to-end integration