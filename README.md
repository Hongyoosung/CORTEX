
# CORTEX: Coordinated Orchestrated Real-Time Tactical EXecution

A sophisticated, hierarchical multi-agent AI system for Unreal Engine 5. CORTEX combines Monte Carlo Tree Search (MCTS) for high-level strategy with Reinforcement Learning (RL) for low-level tactics, all orchestrated through Behavior Trees (BT) and a Finite State Machine (FSM) to deliver real-time tactical decision-making.
**Engine:** Unreal Engine 5.6 | **Language:** C++17 | **Platform:** Windows

## Architecture

The system is designed around a hierarchical team structure where a single Team Leader directs multiple Follower agents.

**Hierarchical Team System:** Leader (MCTS strategic) → Followers (RL tactical + BT execution)

```
Team Leader (per team) → Event-driven MCTS → Strategic commands
    ↓
Followers (N agents) → FSM + RL Policy + Behavior Tree → Tactical execution
```

### Key Architectural Benefits:
- **Performance:** MCTS runs once per team, not per agent (O(1) complexity), and is triggered by in-game events to minimize performance impact.
- **Asynchronous Strategy:** The strategic MCTS calculation runs on a background thread, ensuring the game's main thread is not blocked.
- **Rich Observations:** The AI makes decisions based on a comprehensive set of 71 features for each follower and 40 features for the team leader.

## Core Components

1.  **Team Leader (`Team/TeamLeaderComponent.h/cpp`)**
    -   Uses an event-driven, asynchronous MCTS to formulate high-level strategy.
    -   Issues strategic commands (Assault, Defend, Move, etc.) to follower agents.

2.  **Followers (`Team/FollowerAgentComponent.h/cpp`)**
    -   Receive commands from the leader and transition states via an FSM.
    -   Use a Reinforcement Learning (RL) policy to select the best tactical action.
    -   Execute actions using a Behavior Tree.

3.  **Finite State Machine (FSM) (`StateMachine.h/cpp`)**
    -   Manages high-level state transitions for followers based on commands from the leader.
    -   States include: `Idle`, `Assault`, `Defend`, `Support`, `Move`, `Retreat`, `Dead`.

4.  **Reinforcement Learning (RL) Policy (`RL/RLPolicyNetwork.h/cpp`)**
    -   A 3-layer neural network trained via PPO.
    -   Selects from 16 different tactical actions to execute the leader's strategy.

5.  **Behavior Trees (BT) (`BehaviorTree/*`)**
    -   Contains custom nodes to execute the tactical actions chosen by the RL policy.
    -   Integrates directly with the FSM and RL components.

6.  **Observation System (`Observation/*`)**
    -   Gathers and manages the 71 individual and 40 team features that feed the AI decision-making processes.

7.  **Communication (`Team/TeamCommunicationManager.h/cpp`)**
    -   Manages message passing and events between the Team Leader and Followers.

## Project Status

**✅ Implemented:**
-   Enhanced observation system (71+40 features)
-   Team architecture foundations
-   RL policy network structure
-   FSM refactored for command-driven transitions

**🔄 In Progress:**
-   Behavior Tree custom components
-   RL training infrastructure
-   Complete state implementations

**📋 Planned:**
-   Distributed training (e.g., Ray RLlib)
-   Model persistence (saving/loading trained models)
-   Multi-team support (Red vs. Blue)

## File Structure

```
Source/GameAI_Project/
├── MCTS/              # Team leader strategic planning
├── RL/                # Follower tactical policies
├── StateMachine/      # Command-driven FSM
├── BehaviorTree/      # Custom BT components
├── Team/              # Leader, Follower, Communication
└── Observation/       # 71+40 feature observation system
```
