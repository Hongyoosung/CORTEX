# CORTEX: A Hybrid Game AI System Combining World Model-Based MCTS, Distributed Reinforcement Learning, and Spatial Reasoning

> **A real-time tactical AI framework for 5v5 team combat in Unreal Engine 5, featuring hierarchical decision-making across three abstraction layers: centralized strategic planning via Monte Carlo Tree Search with a learned World Model, decentralized behavior execution via PPO-trained policy networks, and spatial reasoning via UE5's Environment Query System.**

[Image: Hero banner — 5v5 tactical combat scene rendered in UE5 with AI agent decision overlays showing MCTS tree expansion, RL policy activation, and EQS spatial heatmaps. Style: dark technical blueprint aesthetic with neon highlights for each decision layer (blue = MCTS, green = RL, orange = EQS).]

---

## Project Description

In competitive team-based games, AI agents must solve a layered decision problem: *what should the team do* (strategy), *how should each individual behave* (tactics), and *where should each agent move* (positioning) — all within a hard real-time budget of 16.6ms per frame.

**CORTEX** addresses this challenge by decomposing the decision space into three coupled layers, each using the algorithm best suited to its abstraction level. A centralized **Squad Commander** uses MCTS guided by a learned World Model to select team-wide tactical plays every 500ms. Five **Executor Agents**, each running a PPO-trained policy network conditioned on their assigned role (Assault / Defend / Support), translate strategy into continuous spatial parameters. These parameters drive UE5's **Environment Query System** to score 48 candidate positions across 8 weighted tactical tests, producing the final movement target.

This architecture enables emergent squad-level behaviors — coordinated flanking maneuvers, sacrificial bait-and-ambush plays, adaptive formation shifts in response to casualties — while maintaining real-time performance within a 15ms planning budget.

---

## System Architecture Overview

[Image: Three-layer architecture diagram. Top layer (Layer 1): Squad Commander box containing "MCTS + World Model" with input arrow labeled "FTeamWorldState (70-dim)" and output arrow labeled "ETacticalPlay (1 of 10)". Middle layer (Layer 2): Five parallel Agent boxes, each labeled "PPO Policy (52→6)" with input arrows from Layer 1 labeled "Role Assignment (Assault/Defend/Support)" and output arrows labeled "EQS Weights (6-dim)". Bottom layer (Layer 3): Five parallel EQS boxes, each labeled "48 samples × 8 tests" with output arrows to "NavMesh Target". Vertical timing annotations: Layer 1 = "500ms cycle / 15ms budget", Layer 2 = "Per-step inference (<1ms)", Layer 3 = "Per-query (~2ms)". Side panel: data flow arrows showing the feedback loop from game state back to Layer 1.]

### Decision Flow

```
Game State (70-dim)
    │
    ▼
┌─────────────────────────────────────────────┐
│  Layer 1: Squad Commander (ASquadManager)   │
│  ┌───────────────────────────────────────┐  │
│  │  UTeamMCTS: Time-budgeted tree search │  │
│  │  ┌─────────────────────────────────┐  │  │
│  │  │  UTeamWorldModel (ONNX/NNE)    │  │  │
│  │  │  Input: State(60) + Play(10)   │  │  │
│  │  │  Output: NextState + Reward    │  │  │
│  │  │         + Confidence           │  │  │
│  │  └─────────────────────────────────┘  │  │
│  │  Selection: ConfidenceUCB             │  │
│  │  Action Space: 10 Tactical Plays      │  │
│  └───────────────────────────────────────┘  │
│  Output: Role[5] = {Assault, Defend, Support} │
└─────────────────────────────────────────────┘
    │ SetCommandedStrategy()
    ▼
┌─────────────────────────────────────────────┐
│  Layer 2: Executor Agents (×5)              │
│  ┌─────────────────────────────────────┐    │
│  │ SingleHeadPolicy (PPO-trained)      │    │
│  │ Input:  52-dim (49 obs + 3 role)    │    │
│  │ Network: [256,256] + LayerNorm      │    │
│  │ Output: 6-dim EQS weights [-1,1]   │    │
│  └─────────────────────────────────────┘    │
└─────────────────────────────────────────────┘
    │ EQS Weight Parameters
    ▼
┌─────────────────────────────────────────────┐
│  Layer 3: EQS Spatial Reasoning             │
│  48 candidate positions × 8 weighted tests  │
│  Output: Best tactical location → NavMesh   │
└─────────────────────────────────────────────┘
```

The key architectural insight is that each layer operates on a different time scale and abstraction level. The Squad Commander reasons about team-level state transitions over multi-second horizons. The RL policies map moment-to-moment observations to spatial preferences. The EQS converts abstract preferences into concrete, physically valid positions on the navigation mesh. This separation allows each component to be trained, tuned, and debugged independently.

---

## Tech Stack

| Category | Technologies |
|---|---|
| **Game Engine** | Unreal Engine 5.6 (C++17) |
| **RL Framework** | Ray RLlib 2.x, PyTorch |
| **UE5-Python Bridge** | Schola Plugin (gRPC-based) |
| **Neural Network Inference** | ONNX Runtime via UE5 NNE (Neural Network Engine) |
| **Containerization** | Docker (Linux training containers) |
| **Communication** | gRPC (Schola protocol) |
| **Monitoring** | TensorBoard, custom per-strategy metric callbacks |
| **Build & CI** | UE5 Build System, Python packaging |
| **Version Control** | Git |

---

## Key Features

### 1. Centralized MCTS with Learned World Model

The Squad Commander (`ASquadManager`) runs a time-budgeted MCTS to select one of 10 predefined Tactical Plays — team-wide role distributions such as *PincerManeuver* (3 Assault split-push + 2 Support) or *BaitStrategy* (1 Assault bait + 4 Defend ambush).

Rather than simulating the actual game environment for each tree rollout (which would be prohibitively expensive at runtime), the MCTS uses a **learned World Model** (`UTeamWorldModel`) to predict state transitions. The model takes a 70-dimensional input (60-dim team state + 10-dim tactical play one-hot) and outputs a predicted next state (60D), a composite reward vector (WinProb, HealthDelta, ObjectiveScore), and a confidence score.

**Confidence-Aware UCB Selection:**
The tree search uses a modified UCB formula that incorporates the World Model's confidence output:

```
ConfidenceUCB = Q(s,a) + C_PUCT × √(ln N_parent / N_child) − K_RISK × (1 − confidence)
```

Predictions with confidence below 0.3 are discarded entirely, preventing low-quality model predictions from corrupting the search tree. This is critical because the World Model is trained on limited data and may produce unreliable predictions for rarely-visited states.

**Feasibility Gating:**
Before tree expansion, each Tactical Play is filtered against the current game state. *Defend*-heavy plays require at least one friendly-owned capture point; *Support*-heavy plays require at least two alive allies. If no plays pass feasibility, *AllOutRush* (pure assault) serves as the unconditional fallback.

[Image: MCTS tree visualization showing 3 levels of expansion. Root node labeled "Current State" with 10 child branches (one per Tactical Play). Each child node shows: visit count (N), Q-value, confidence score, and the predicted reward vector. Highlight the selected path (highest visit count) with a bold line. Gray out pruned branches (low confidence or infeasible). Annotate with timing: "50 iterations, 15ms budget".]

### 2. Distributed Multi-Agent Reinforcement Learning

Each executor agent runs an independent PPO-trained policy conditioned on its assigned strategy. The architecture uses **three separate policy networks** — one per role (Assault, Defend, Support) — rather than a shared multi-head model, to eliminate gradient interference between roles with fundamentally different objectives.

**Policy Architecture:**
```
Input:  52-dim (49 local observation + 3 strategy one-hot)
        ↓
Encoder: Linear(52→256) → ReLU → LayerNorm → Linear(256→256) → ReLU → LayerNorm
        ↓
Action Head: Linear(256→64) → ReLU → Linear(64→6) → Tanh  →  EQS weights ∈ [-1, 1]
Value Head:  Linear(256→64) → ReLU → Linear(64→1)          →  V(s)
```

**49-Dimensional Observation Space:**
- Self state (8D): position/7500, health, velocity/600, cooldown
- Ally state (16D): 4 × [relative position/8000, health]
- Enemy state (20D): 5 × [relative position if visible, visibility flag]
- Map state (5D): 5 capture point statuses

**Strategy-Conditioned Reward Shaping:**
Each role receives a distinct reward function calibrated to its tactical purpose:
- **Assault:** Rewarded for approaching enemy-held objectives, zone capture progress, and post-capture momentum. Penalized for idling outside combat zones.
- **Defend:** Proximity-proportional bonuses within friendly zones, zone durability rewards for absorbing damage, and zone guard kill bonuses. Soft distance penalties when outside zones.
- **Support:** Injured-ally target tracking with a 5-step staleness cache to prevent gradient noise from rapid target switching. Heal tick rewards, rear-guard positioning bonuses, and role-break penalties for engaging enemies while allies need healing.

**Training Infrastructure:**
- Ray RLlib multi-agent setup with per-strategy policy routing via `policy_mapping_fn`
- Strategy-balanced replay buffer ensuring 33/33/33% data distribution across roles
- PPO with GAE (γ=0.99, λ=0.90), adaptive learning rate and entropy schedules
- Custom RLlib callbacks for per-strategy TensorBoard metrics
- ONNX export pipeline for deploying trained policies to UE5 NNE

[Image: Training dashboard mockup showing 4 panels. Panel 1: Episode reward curves for all three strategies (Assault in red, Defend in blue, Support in green) over 100K timesteps. Panel 2: Per-strategy entropy decay showing exploration reduction. Panel 3: Policy loss convergence per strategy. Panel 4: Strategy distribution pie chart confirming 33/33/33% balance.]

### 3. EQS-Based Spatial Reasoning

The Environment Query System provides the final translation from abstract RL outputs to physically valid positions. The EQS executor (`UMocEQSExecutor`) takes the 6-dimensional weight vector from the RL policy and uses it to parameterize 8 weighted scoring tests across 48 candidate positions sampled from the navigation mesh.

**The 6 EQS Weight Dimensions:**
| Index | Parameter | Tactical Meaning |
|---|---|---|
| 0 | EnemyObjectiveProximity | Preference for positions near enemy-held objectives |
| 1 | AllyObjectiveProximity | Preference for positions near friendly objectives |
| 2 | CoverDensity | Preference for positions with nearby cover geometry |
| 3 | EnemyVisibility | Preference for positions with/without enemy line-of-sight |
| 4 | AllyProximity | Preference for positions near/far from teammates |
| 5 | CombatRange | Preferred engagement distance |

This decoupling is powerful: the RL policy learns *what spatial properties matter* for its role, while UE5's navigation system handles *how to get there* — pathfinding, obstacle avoidance, and physics constraints are handled entirely by the engine, allowing the RL training to focus purely on tactical decision-making.

### 4. Event-Driven Replanning

The Squad Commander doesn't only replan on a fixed 500ms timer. Critical game events — agent kills, agent deaths, and capture point changes — trigger immediate replanning via an Observer-pattern event system. Kill events are wired through `ATeamManager::OnAgentKilled`, and capture events through `ACapturePoint::OnPointCaptured`. Both route to `ASquadManager::ReplanMCTSOnCriticalEvent()`.

This enables reactive adaptation: when a teammate dies, the Squad Commander can immediately shift from *AggressivePush* to *FortressDefense*; when an enemy is eliminated, it can escalate to *AllOutRush*.

### 5. Dual-Mode Architecture

Every major component supports both **training mode** and **inference mode** within the same UE5 binary:

| Component | Training Mode | Inference Mode |
|---|---|---|
| Squad Commander | ε-greedy / random play sampling | Full MCTS with World Model |
| Agent Execution | Synchronous EQS → direct MoveTo | Blackboard → Behavior Tree |
| World Model | Stub mode (neutral predictions) | ONNX NNE inference |
| EQS Executor | Blocking query | Async query with callback |

This dual-mode design means the same compiled binary serves both data collection and production gameplay, eliminating build-configuration drift.

---

## Technical Challenges & Solutions

### Challenge 1: MCTS Computational Bottleneck in Real-Time Environment

**Situation:** The initial architecture (v10.1) ran independent MCTS instances on each of the 5 agents. Each MCTS required a 15ms time budget, creating a cumulative 75ms computational overhead per planning cycle — far exceeding the 16.6ms frame budget and causing visible frame drops during intense combat.

**Task:** Reduce MCTS computational cost to fit within real-time constraints while maintaining or improving tactical coordination quality.

**Action:**
1. **Centralized Commander-Executor architecture:** Replaced 5 independent MCTS planners with a single centralized `ASquadManager` that plans at the team level. This required redesigning the action space from per-agent strategies (3^5 = 243 combinations) to 10 curated Tactical Plays, achieving 16× action space pruning.
2. **Learned World Model:** Introduced `UTeamWorldModel` — an ONNX neural network (70→256→512→256 with residual blocks) that predicts state transitions in <1.8ms per batch of 16, replacing expensive game-state simulation during MCTS rollouts.
3. **Confidence-gated search:** Added a confidence output head to the World Model. Predictions below 0.3 confidence are discarded, preventing unreliable predictions from corrupting the search tree. The stub mode (used before the model is trained) outputs confidence=0.1 to gracefully degrade to heuristic planning.
4. **Batch leaf expansion:** Each MCTS iteration generates all 10 tactical play children for the selected leaf, batched into a single World Model inference call, reducing per-iteration latency.

**Result:** Planning compute reduced from 75ms to 15ms (5× improvement). Action space reduced from 243 to 10 options (16× pruning). The centralized planner naturally enables sacrificial plays (e.g., *BaitStrategy*: one agent draws fire while four set up an ambush) — behaviors that were extremely difficult to learn with individual agent optimization.

**Design Patterns Applied:**
- **Strategy Pattern:** `ETacticalPlay` enum encapsulates tactical compositions; `DecodeTacticalPlay()` maps each to a concrete role distribution without conditional logic.
- **Observer Pattern:** Event-driven replanning via delegate binding (`OnAgentKilled`, `OnPointCaptured`) decouples event sources from the planning response.
- **Null Object Pattern (Graceful Degradation):** `UTeamWorldModel` operates in stub mode when no trained model is available, returning neutral predictions with low confidence rather than failing.

[Image: Before/after comparison diagram. Left side: "v10.1 — Decentralized" showing 5 separate MCTS trees, each with "15ms" annotation, total "75ms". Right side: "v10.2 — Centralized" showing 1 MCTS tree with "15ms" annotation, feeding into 5 lightweight executor agents. Highlight the 5× speedup with a prominent callout.]

### Challenge 2: Environment Isolation and Multi-Worker Training on Windows

**Situation:** The RL training pipeline required connecting Python (Ray RLlib) to a running Unreal Engine editor via the Schola gRPC bridge. On Windows, Ray's multi-worker architecture hit two critical issues: (1) Ray's Learner actor hung during inter-process weight synchronization, and (2) the single-worker limitation capped training throughput since only one UE5 environment could be utilized at a time.

**Task:** Establish a multi-worker, OS-independent training pipeline that maximizes data collection throughput while maintaining stable communication with UE5.

**Action:**
1. **Docker containerization:** Packaged the Python training scripts, Ray RLlib, and all dependencies into Linux Docker containers. This bypassed Windows-specific Ray multiprocessing issues (spawn method incompatibilities, Learner actor hangs) and provided reproducible training environments.
2. **gRPC bridge optimization:** Configured Schola's gRPC connection to support port-based multi-worker routing: each RLlib env-runner resolves its port as `base_port + worker_index`, allowing multiple workers to connect to separate UE5 environment instances simultaneously.
3. **Environment-variable-driven configuration:** Training parameters (`NUM_SCHOLA_ENVS`, `NUM_WORKERS`, `NUM_ITERATIONS`) are read from environment variables, allowing Docker Compose to override them without editing source code.
4. **Platform-adaptive code paths:** The training script detects the OS at runtime and configures Ray accordingly — `num_learners=0` on Windows (forces local learner), full distributed mode on Linux.

**Result:** Training throughput scaled with the number of UE5 environment instances. The Docker-based pipeline eliminated "works on my machine" issues and enabled consistent training runs. Environment variable configuration allowed rapid hyperparameter sweeps via Docker Compose overrides.

**Design Patterns Applied:**
- **Adapter Pattern:** `MOCv10_2MultiAgentEnv` adapts Schola's nested observation format (dict-of-dict keyed by `(env_idx, agent_idx)`) to RLlib's flat agent-ID multi-agent interface.
- **Factory Pattern:** `create_ppo_config()` and `create_env_config()` encapsulate all configuration decisions, providing a single point of change for training parameters.

### Challenge 3: Training Freeze in Multi-Agent Episode Boundaries

**Situation:** During training, the system consistently froze at approximately Python step ~1000 (UE5 step ~501). Investigation revealed a 25× step count discrepancy between Python and UE5 — Python registered only 21 real environment steps while UE5 had processed 501. Alive agents stopped receiving weight updates while dead agents cycled indefinitely.

**Task:** Diagnose and resolve the freeze without losing training data or destabilizing the episode boundary logic.

**Action:**
1. **Root cause analysis:** Identified two conflicting episode termination systems. Schola's `AutoResetType::SAME_STEP` triggered auto-resets on individual agent deaths, but Python suppressed all termination signals to prevent mixed-trajectory batches in RLlib. Dead agents entered an infinite respawn-die loop, consuming all Schola step budget.
2. **Fix 1 — Consistent termination semantics:** Modified `IsEpisodeDone()` to only return `true` at `MaxEpisodeSteps`, making it consistent with `ComputeStatus()` which already returned `Running` for dead agents. This stopped the auto-reset cycling.
3. **Fix 2 — Uniform step counting:** After Fix 1, a second freeze emerged: alive agents reached `MaxEpisodeSteps` and entered `Truncated` state, but dead agents (whose step counter wasn't incrementing in the action-drain path) remained `Running`, blocking `AllDone=true`. Added `CurrentEpisodeSteps++` to the dead-agent drain path so all agents reach the timeout simultaneously.
4. **Freeze diagnostics:** Added instrumentation to detect NaN/Inf in rewards and observations before returning to RLlib, and inter-step gap monitoring to distinguish environment hangs from RLlib training hangs.

**Result:** Training runs complete without freezing. All agents terminate simultaneously at episode boundaries, producing clean single-trajectory batches for RLlib's postprocessing. The diagnostic instrumentation remains active for future debugging.

[Image: Timeline diagram showing the freeze scenario. Two horizontal lanes: "Alive Agents" and "Dead Agents". The dead agent lane shows a rapid cycle of "Die → Auto-Reset → Die → Auto-Reset → ..." consuming all step budget, while the alive agent lane shows "Waiting..." with no progress. An arrow labeled "Fix: Remove death as terminal condition" breaks the cycle. After the fix, both lanes progress uniformly to "MaxEpisodeSteps → Episode End".]

### Challenge 4: Gradient Interference in Multi-Strategy Policy Training

**Situation:** The initial v10.2.0 architecture used a single multi-head model with a shared encoder feeding three strategy-specific heads. During training, the Support head collapsed — its outputs converged to near-zero regardless of input — while Assault and Defend heads learned normally. Analysis showed gradient interference: the shared encoder received contradictory gradient signals from roles with fundamentally opposed objectives (Assault: approach enemies; Support: stay behind allies).

**Task:** Eliminate gradient interference while maintaining the ability to share training infrastructure across strategies.

**Action:**
1. **Independent single-head policies (v10.2.1):** Replaced the shared-encoder multi-head model with three completely independent `SingleHeadPolicy_v10_2` instances. Each policy has its own encoder, action head, value head, and learnable log standard deviation.
2. **RLlib multi-agent routing:** Used `policy_mapping_fn` to route each agent to the correct strategy-specific policy based on the strategy one-hot encoded in the observation.
3. **Strategy-balanced replay buffer:** Implemented `StrategyBalancedReplayBuffer` with three independent sub-buffers (one per strategy), ensuring each policy sees exactly 33% of training data regardless of the natural distribution of role assignments.
4. **Uniform strategy distribution:** Added `force_uniform_strategy` mode in the environment, which overrides the UE5 Squad Commander's role assignments with round-robin distribution during training, ensuring all three policies receive equal data volume.

**Result:** All three strategy heads now converge independently without interference. The Support policy learns appropriate healing-priority and rear-guard positioning behaviors. Per-strategy TensorBoard metrics (reward mean, entropy, policy loss) can be monitored independently.

---

## Results

### Performance Improvements

| Metric | v10.1 (Decentralized) | v10.2 (Centralized) | Improvement |
|---|---|---|---|
| MCTS Planning Latency | 75ms (5 × 15ms) | 15ms (1 × 15ms) | **5× reduction** |
| Action Space | 243 combinations | 10 Tactical Plays | **24× pruning** |
| World Model Inference | N/A (simulation) | <1.8ms (batch=16) | **Real-time capable** |
| Training Data Balance | Uncontrolled | 33/33/33% per strategy | **Uniform coverage** |
| Episode Stability | Freeze at ~1000 steps | Stable indefinitely | **Resolved** |

### Emergent Tactical Behaviors

Through the combination of centralized planning and role-conditioned execution, the system produces squad-level behaviors that were not explicitly programmed:

- **Coordinated Flanking:** *PincerManeuver* assigns 3 Assault agents to approach from different angles while 2 Support agents maintain rear coverage, creating crossfire situations.
- **Sacrificial Bait:** *BaitStrategy* sends a single Assault agent forward to draw enemy fire while 4 Defend agents set up an ambush — a behavior impossible to learn with individual agent optimization.
- **Adaptive Formation Shifts:** When the Squad Commander detects a casualty via the event system, it immediately transitions from offensive plays to defensive formations, then re-escalates when the advantage shifts.
- **Role-Appropriate Spatial Behavior:** Assault agents learn to prefer positions with enemy visibility and close combat range. Defend agents cluster around friendly capture points. Support agents maintain rear-guard positioning near injured allies.

[Image: Four-panel visualization of emergent behaviors. Panel 1 "PincerManeuver": Top-down map view showing 3 red arrows (Assault) converging on an objective from different angles, 2 green arrows (Support) behind. Panel 2 "BaitStrategy": One red arrow forward (bait), four blue shields (Defend) in ambush positions. Panel 3 "Adaptive Shift": Timeline showing play transitions in response to events (Kill → AggressivePush, Death → FortressDefense). Panel 4 "Spatial Heatmaps": Three overlaid heatmaps showing where each role type prefers to position (Assault = near enemies, Defend = near objectives, Support = near allies).]

### System Architecture Quality

- **Graceful Degradation:** Three-layer fallback chain — World Model stub mode → MCTS heuristic fallback → EQS random fallback — ensures the system never hard-fails, producing valid behavior even with all learned components absent.
- **Dual-Mode Execution:** Same UE5 binary serves both training data collection and production inference, eliminating configuration drift between development and deployment.
- **Modular Training Pipeline:** Each component (World Model, per-strategy RL policies) can be trained, exported to ONNX, and deployed independently without rebuilding the UE5 project.

---

## Repository Structure

```
CORTEX/
├── Source/GameAI_Project/
│   ├── Public/
│   │   ├── AI/
│   │   │   ├── MCTS/          # UTeamMCTS, FTeamTreeNode
│   │   │   ├── Models/        # UTeamWorldModel, TeamWorldModelTypes
│   │   │   ├── EQS/           # UMocEQSExecutor
│   │   │   └── Training/      # UTeamDataCollector
│   │   ├── Characters/        # AMocCharacter
│   │   ├── Team/              # ATeamManager, ASquadManager, FTeamWorldState
│   │   ├── RL/Rewards/        # MocRewardCalculator
│   │   └── Types/             # RewardTypes, enums
│   └── Private/               # Implementation files (.cpp)
│
├── MOC_Training/
│   └── training/
│       ├── phase1_policy_training_v10_2.py   # RLlib PPO training + ONNX export
│       └── moc_v10_2_env.py                  # Multi-agent Schola environment wrapper
│
└── Content/Game/              # UE5 assets, trained ONNX models
```

---

## Technical Depth: Design Pattern Application

| Pattern | Where Applied | Justification |
|---|---|---|
| **Strategy** | `ETacticalPlay` → `DecodeTacticalPlay()` | Encapsulates 10 tactical compositions as interchangeable strategies without conditional branching |
| **Observer** | `OnAgentKilled`, `OnPointCaptured` delegates | Decouples game events from planning response; enables event-driven replanning |
| **Command** | `SetCommandedStrategy()` | Fire-and-forget push from commander to executor; no acknowledgment required |
| **Null Object** | `UTeamWorldModel` stub mode | Returns neutral predictions when no trained model exists, enabling graceful degradation |
| **Adapter** | `MOCv10_2MultiAgentEnv` | Translates Schola's nested dict format to RLlib's flat multi-agent interface |
| **Factory** | `create_ppo_config()`, `create_env_config()` | Centralizes configuration decisions; single point of change for training parameters |
| **State** | Strategy-conditioned reward (`GetStrategyScale`) | Reward function behavior changes based on the active strategy state without branching |
| **Template Method** | `PerformTacticalAction()` | Defines the skeleton (stop → query → move) with training/inference mode hooks |

---

*Built with Unreal Engine 5.6, C++17, Python, Ray RLlib, PyTorch, Docker, and gRPC.*
