# MOC v10.1: Multi-Head Option-Conditioned Model-Based Planning

**Engine:** UE5.6 | **Language:** C++17 | **Platform:** Windows | **Date:** 2026-02-06

---

## 1. Executive Summary
**MOC v10.1** is a hierarchical multi-agent AI system. It combines MCTS-based tactical planning and EQS-based spatial reasoning, allowing flexible switching between five strategies depending on the situation.

* **Core Innovation:** Separation of tactical intent (Strategy) and detailed movement (Navigation) (RL → EQS Weights → Navigation)
* **Key Tech:** Multi-Head RL Policy, World Model, Multi-Objective Value Network

---

## 2. Reference Documents (Details)

For all detailed implementation specifications, formulas, and parameters, please refer to the linked documents below.

| Document Category | File Name | Main Content |
| :--- | :--- | :--- |
| **Game Environment** | `MocGameEnvSpecification.md` | Game Rules, Map (150x150m), Occupation Method, Reward Function, Agent Specification |
| **System Architecture** | `v10.0Architecture.md` | 3-Layer Structure, Neural Network Model (Policy, World Model), Training Pipeline |

---

## 3. Architecture Overview

The system consists of three layers to operate within a 15ms frame budget.

### 3.1 Three-Layer Hierarchy
1. **Layer 1: Event-Driven MCTS Planning** (High-Level)
* Replanning upon detection of status changes (e.g., decreased health, death of allies)
* Output: Selecting optimal options (strategy, objective, duration)
2. **Layer 2: Neural Network Inference** (Mid-Level)
* **World Model:** Predicting the next state and uncertainty (confidence)
* **Multi-Head Policy:** Generating **EQS weights (8-dim)** appropriate for the selected strategy
3. **Layer 3: EQS Spatial Reasoning** (Low-Level Execution)
* Selecting optimal tactical positions and movement based on the generated weights

### 3.2 Supported Strategies (5-Heads)
* **Assault:** Attacking and engaging enemy bases
* **Defend:** Defending strongpoints and capturing high ground
* **Support:** Supporting friendly forces and maintaining formation
* **Scout:** Securing visibility and gathering information
* **Retreat:** Prioritizing survival and securing recovery items

---

## 4. Implementation Roadmap (Summary)
* **Phase 1:** Environment construction and policy learning (Optional-Conditioned)
* **Phase 2:** World Model and Value Network learning
* **Phase 3:** MCTS integration and EQS pipeline connection
* **Phase 4:** Verification and ablation study