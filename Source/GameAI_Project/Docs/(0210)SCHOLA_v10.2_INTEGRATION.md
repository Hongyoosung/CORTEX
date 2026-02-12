# Schola v1.3.0 Integration with MOC v10.2

**Status:** ✅ Implemented | **Date:** 2026-02-10 | **Engine:** UE5.6

---

## Executive Summary

MOC v10.2 uses AMD's **Schola plugin (v1.3.0)** for RL training infrastructure. This document explains the integration architecture, replacing the deprecated `MocScholaBridge` with proper Schola components.

**Key Changes:**
- ❌ **Removed:** Custom `MocScholaBridge` (TCP socket server, manual serialization)
- ✅ **Using:** Schola's built-in gRPC Gym Connector, Observer/Actuator pattern
- 🔧 **Refactored:** `TacticalObserver` → `MocTacticalObserver` extending `UBoxObserver`

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│ Python Training Script (RLlib / Stable Baselines)               │
│ • train_rllib.py: Multi-agent PPO training                      │
│ • train_sb3.py: Single-agent SAC training                       │
└─────────────────────────────────────────────────────────────────┘
                          ↕ gRPC / Protobuf
┌─────────────────────────────────────────────────────────────────┐
│ Schola Plugin (v1.3.0)                                           │
│ • UAbstractGymConnector: Python↔UE5 communication               │
│ • FTrainingState: Shared state management                       │
│ • Episode orchestration, auto-reset, logging                    │
└─────────────────────────────────────────────────────────────────┘
                               ↓
┌─────────────────────────────────────────────────────────────────┐
│ MOC Game Project                                                 │
│                                                                  │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │ AScholaEnvironment (extends AStaticScholaEnvironment)       │ │
│ │ • Auto-discovers agents (finds UScholaMocAgent components)  │ │
│ │ • Manages episode lifecycle                                 │ │
│ │ • Integrates with SquadManager (v10.2 centralized planning) │ │
│ └─────────────────────────────────────────────────────────────┘ │
│                               ↓                                  │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │ AMocTrainer (extends AAbstractTrainer) × 5 agents           │ │
│ │                                                              │ │
│ │ Observers (collect state):                                  │ │
│ │ • UMocTacticalObserver → 55-dim observation                 │ │
│ │   - Base state (52): Position, health, allies, enemies      │ │
│ │   - Commanded strategy (3): One-hot encoding                │ │
│ │                                                              │ │
│ │ Actuators (apply actions):                                  │ │
│ │ • UTacticalParameterActuator → Apply EQS weights            │ │
│ │   - 7-dim continuous action: [EnemyObjProx, AllyObjProx,... │ │
│ │                                                              │ │
│ │ Reward Calculation:                                         │ │
│ │ • ComputeReward() → Strategy-specific reward shaping        │ │
│ │ • ComputeStatus() → Episode termination logic               │ │
│ └─────────────────────────────────────────────────────────────┘ │
│                               ↓                                  │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │ AMocCharacter with UScholaMocAgent component                │ │
│ │ • Receives commanded strategy from SquadManager             │ │
│ │ • Executes EQS-based spatial reasoning                      │ │
│ │ • Interacts with game environment                           │ │
│ └─────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

---

## Core Components

### 1. AScholaEnvironment

**File:** `Public/Schola/ScholaEnvironment.h`
**Base Class:** `AStaticScholaEnvironment` (Schola plugin)

**Responsibilities:**
- Environment setup and reset
- Agent registration and lifecycle management
- Integration with SquadManager for v10.2 centralized planning
- Episode termination detection

**Key Methods:**
```cpp
// Schola interface implementations
virtual void InitializeEnvironment() override;
virtual void ResetEnvironment() override;
virtual void InternalRegisterAgents(TArray<FTrainerAgentPair>& OutAgentTrainerPairs) override;

// v10.2 specific
const ASquadManager* GetSquadCommander(int32 TeamID) const;
```

**Configuration:**
```cpp
UPROPERTY(EditAnywhere)
bool bAutoDiscoverAgents = true;  // Finds all UScholaMocAgent components

UPROPERTY(EditAnywhere)
bool bEnableCentralizedPlanning = true;  // v10.2 SquadManager integration
```

---

### 2. AMocTrainer

**File:** `Public/Schola/Trainers/MocTrainer.h`
**Base Class:** `AAbstractTrainer` (Schola plugin, extends `AAIController`)

**Responsibilities:**
- Controls a single AMocCharacter (possesses pawn)
- Collects observations via Observers array
- Applies actions via Actuators array
- Computes rewards based on commanded strategy
- Determines episode termination

**Key Methods:**
```cpp
// Schola interface implementations
virtual float ComputeReward() override;
virtual EAgentTrainingStatus ComputeStatus() override;
virtual void GetInfo(TMap<FString, FString>& Info) override;
virtual void ResetTrainer() override;

// MOC-specific
void InitializeMocTrainer(UScholaMocAgent* InAgent);
FObservation GatherStateObservation();
```

**Observers/Actuators Configuration:**
```cpp
// Set in Blueprint or C++
UPROPERTY(EditAnywhere, Instanced)
TArray<UAbstractObserver*> Observers;  // From Schola

UPROPERTY(EditAnywhere, Instanced)
TArray<UActuator*> Actuators;  // From Schola
```

---

### 3. UMocTacticalObserver

**File:** `Public/Schola/Observers/MocTacticalObserver.h`
**Base Class:** `UBoxObserver` (Schola plugin)

**Responsibilities:**
- Collect 55-dim observation for RL policy
- Normalize values for neural network input
- Validate observation integrity

**Observation Space (55-dim):**

| Component | Dimensions | Range | Description |
|-----------|------------|-------|-------------|
| **Self State** | 10 | [-1, 1] | Position(3), Health(1), Velocity(3), Cooldown(1), Strategy(1), Alive(1) |
| **Allies** | 20 | [-1, 1] | 4 agents × [Position(3), Health(1), Strategy(1)] |
| **Enemies** | 20 | [-1, 1] | 5 agents × [Position(3), Visible(1)] |
| **Map** | 2 | [-1, 1] | CapturePointBalance(1), TimeRemaining(1) |
| **Commanded Strategy** | 3 | [0, 1] | One-hot: [Assault, Defend, Support] |

**Key Methods:**
```cpp
// Schola interface
virtual FBoxSpace GetObservationSpace() const override;
virtual void CollectObservations(FBoxPoint& OutObservations) override;

// Helper functions
FObservation GatherBaseObservation() const;
TArray<float> EncodeStrategyOneHot(EStrategyType Strategy) const;
bool ValidateObservation(const TArray<float>& Observation) const;
```

**Integration Example:**
```cpp
// In AMocTrainer Blueprint or BeginPlay():
UMocTacticalObserver* Observer = NewObject<UMocTacticalObserver>(this);
Observers.Add(Observer);
```

---

### 4. UScholaMocAgent

**File:** `Public/Schola/Components/ScholaMocAgent.h`
**Base Class:** `UInferenceComponent` (Schola plugin)

**Responsibilities:**
- Store commanded strategy from SquadManager
- Provide strategy to observers
- Support mode switching (Training vs Inference)

**Key Methods:**
```cpp
// v10.2 command interface
void UpdateCommandedStrategy(EStrategyType NewStrategy);
EStrategyType GetCommandedStrategy() const;
```

**Usage:**
```cpp
// Add component to AMocCharacter in Blueprint
UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
UScholaMocAgent* ScholaAgent;

// SquadManager calls this to assign strategy
void AMocCharacter::SetCommandedStrategy(EStrategyType NewStrategy)
{
    if (ScholaAgent)
    {
        ScholaAgent->UpdateCommandedStrategy(NewStrategy);
    }
}
```

---

## v10.2 Hierarchical Training Flow

### Layer 1: Squad Commander (Centralized Planning)

**Not trained via Schola** - Uses separate MCTS planning:
```
ASquadManager:
  Input: FTeamState (60-dim team state)
  Process: Centralized MCTS (15ms)
  Output: ETacticalPlay → [5 × EStrategyType]
  Frequency: 0.5s or critical events
```

### Layer 2: Executor Agents (RL Policy Training)

**Trained via Schola** - 5 parallel agent trainers:

#### Training Loop (per agent):
```
1. Observe:
   UMocTacticalObserver.CollectObservations()
   → Returns 55-dim observation
   → Sent to Python via gRPC

2. Think:
   Python RL Policy (PPO/SAC)
   → Receives observation
   → Returns 7-dim action (EQS weights)

3. Act:
   UTacticalParameterActuator.ApplyAction()
   → Sets EQS weights on AMocCharacter
   → Character executes EQS query → Navigation

4. Reward:
   AMocTrainer.ComputeReward()
   → Strategy-specific reward shaping
   → Assault: +damage dealt, -damage taken
   → Defend: +position holding, +health conservation
   → Support: +ally proximity, +resource control

5. Reset:
   AMocTrainer.ComputeStatus() checks termination
   → Episode ends on: Death, time limit, game over
   AScholaEnvironment.ResetEnvironment()
   → Respawns agents, resets game state
```

#### Multi-Agent Training:
```python
# Python: train_rllib.py
config = PPOConfig().multi_agent(
    policies={
        "executor_policy": PolicySpec(
            observation_space=spaces.Box(-1, 1, (55,)),
            action_space=spaces.Box(-1, 1, (8,))
        )
    },
    policy_mapping_fn=lambda agent_id, *args, **kwargs: "executor_policy"
)

# Connect to Schola via UnrealEnv
env = UnrealEnv(
    env_config={
        "ip": "127.0.0.1",
        "port": 9876,  # Schola gRPC port
        "num_envs": 4  # 4 parallel AScholaEnvironment actors
    }
)
```

---

## Setup Instructions

### 1. Environment Setup (Blueprint)

Create or edit `BP_ScholaEnvironment`:

```
1. Add AScholaEnvironment actor to level
2. Set properties:
   • bAutoDiscoverAgents = true
   • bEnableCentralizedPlanning = true
   • ScholaEnvID = 0 (incremental for multiple environments)

3. Add AMocGameMode reference
4. Ensure SquadManagers exist for both teams
```

### 2. Character Setup (Blueprint)

Configure `BP_MocCharacter`:

```
1. Add UScholaMocAgent component:
   • CurrentMode = Training (or Inference)

2. Ensure components exist:
   • UHealthComponent
   • UWeaponComponent
   • AIPerceptionStimuliSource

3. Set AIController Class = AMocTrainer_BP
```

### 3. Trainer Setup (Blueprint)

Create `BP_MocTrainer` (extends AMocTrainer):

```
1. Configure Observers array:
   • Add UMocTacticalObserver instance
   • Configure debug logging if needed

2. Configure Actuators array:
   • Add UTacticalParameterActuator instance
   • Set EQS query template reference

3. Set reward parameters:
   • AssaultMovementReward = 0.01
   • DefendPositionReward = 2.0
   • SupportPositionReward = 1.0
   • DeathPenalty = 100.0

4. Configure TrainerConfiguration:
   • DecisionPeriod = 10 (frames between actions)
   • MaxSteps = 3000 (episode length)
```

### 4. Python Training Script

```python
# train_rllib_v10.2.py
from ray.rllib.algorithms.ppo import PPOConfig
from schola_env import UnrealEnv

# Configure multi-agent PPO
config = PPOConfig()
config.environment(env=UnrealEnv, env_config={
    "ip": "127.0.0.1",
    "port": 9876,
    "num_envs": 4,
    "timeout": 60.0
})

config.training(
    lr=3e-4,
    gamma=0.99,
    lambda_=0.95,
    clip_param=0.2,
    vf_clip_param=10.0,
    train_batch_size=4096,
    sgd_minibatch_size=512,
    num_sgd_iter=10
)

config.rollouts(
    num_rollout_workers=4,
    num_envs_per_worker=1
)

config.multi_agent(
    policies={"executor_policy": PolicySpec(...)},
    policy_mapping_fn=lambda aid, *args, **kwargs: "executor_policy"
)

# Build and train
algo = config.build()
for i in range(1000):
    result = algo.train()
    print(f"Iteration {i}: reward={result['episode_reward_mean']}")

    if i % 50 == 0:
        algo.save(f"checkpoints/executor_policy_iter_{i}")
```

### 5. Launch Training

```bash
# Terminal 1: Start UE5
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX
# Open project in UE5 Editor
# Play in Editor (PIE)

# Terminal 2: Start Python training
cd training/
python train_rllib_v10.2.py
```

---

## Troubleshooting

### Issue: "Observer owner is not AMocTrainer"

**Cause:** Observer added to wrong component
**Fix:** Ensure observer is added to `AMocTrainer.Observers` array, not directly to character

### Issue: "Character invalid at observation"

**Cause:** Trainer not possessing character yet
**Fix:** Check that `AMocTrainer::OnPossess()` is called and `GetPawn()` returns valid character

### Issue: "Observation size mismatch"

**Cause:** FObservation.ToArray() returning wrong size
**Fix:** Verify `FObservation::ToArray()` returns exactly 52 dimensions (check MocTypes.h)

### Issue: "gRPC connection refused"

**Cause:** Schola server not started or wrong port
**Fix:** Check `ScholaManagerSubsystem` settings in Project Settings → Schola → GymConnector

### Issue: "Agent not discovered by environment"

**Cause:** Missing `UScholaMocAgent` component
**Fix:** Add component to character blueprint, ensure `bAutoDiscoverAgents = true`

---

## Performance Monitoring

### Enable Schola Stats Logging

In `AScholaEnvironment` blueprint:
```cpp
UPROPERTY(EditAnywhere)
bool bLogEpisodeStats = true;
```

Logs printed every episode:
```
[Schola] Episode 42 complete:
  - Duration: 45.2s
  - Total Reward: 127.5
  - Steps: 2714
  - Terminal Reason: EnemyEliminated
```

### Enable Observer Validation

In `UMocTacticalObserver` blueprint:
```cpp
UPROPERTY(EditAnywhere)
bool bEnableDebugLogging = true;

UPROPERTY(EditAnywhere)
int32 DebugLogFrequency = 100;  // Log every 100 observations
```

### TensorBoard Integration

Schola automatically logs to TensorBoard if configured:
```cpp
// Project Settings → Schola → Logging
bEnableTensorBoardLogging = true
TensorBoardLogDirectory = "Saved/TensorBoard/"
```

View in browser:
```bash
tensorboard --logdir=Saved/TensorBoard/
# Open http://localhost:6006
```

---

## Migration Notes (v10.1 → v10.2)

### Removed Components

| Component | Reason | Replacement |
|-----------|--------|-------------|
| `MocScholaBridge` | Duplicate of Schola's GymConnector | Use Schola's built-in gRPC |
| `TacticalObserver` | Wrong base class, broken dependencies | `UMocTacticalObserver` extending `UBoxObserver` |
| `AFollowerCharacter` | Renamed architecture | `AMocCharacter` |

### Observation Space Changes

| Version | Dimensions | Change |
|---------|------------|--------|
| v10.1 | 52 + 5 = 57 | 5-dim strategy one-hot (including Flank/Retreat) |
| v10.2 | 52 + 3 = 55 | 3-dim strategy one-hot (Assault/Defend/Support only) |

**Action Required:** Retrain RL policies with new observation space

### Training Script Changes

**Old (v10.1):**
```python
# Manual TCP connection to MocScholaBridge
import socket
sock = socket.socket()
sock.connect(("127.0.0.1", 8888))
```

**New (v10.2):**
```python
# Use Schola's UnrealEnv wrapper
from schola_env import UnrealEnv
env = UnrealEnv(env_config={"ip": "127.0.0.1", "port": 9876})
```

---

## References

- **Schola Plugin Documentation:** `Plugins/Schola-1.3.0/Docs/`
- **MOC v10.2 Architecture:** `v10.2Architecture.md`
- **Game Environment Specification:** `MocGameEnvSpecification.md`
- **Schola Agent Architecture:** `SCHOLA_AGENT_ARCHITECTURE.md`

---

## Changelog

| Date | Version | Changes |
|------|---------|---------|
| 2026-02-10 | v1.0 | Initial documentation for v10.2 Schola integration |
| 2026-02-10 | v1.0 | Removed MocScholaBridge, refactored TacticalObserver → MocTacticalObserver |

---

## FAQ

**Q: Can I still use local ONNX inference instead of Python training?**
A: Yes, set `UScholaMocAgent.CurrentMode = EAgentMode::Inference` and provide ONNX model path.

**Q: How do I train multiple teams simultaneously?**
A: Spawn multiple `AScholaEnvironment` actors with different `ScholaEnvID` values. Each environment is independent.

**Q: Can I customize the reward function?**
A: Yes, override `AMocTrainer::ComputeReward()` in C++ or Blueprint. See `MocTrainer.cpp` for examples.

**Q: How do I visualize agent observations during training?**
A: Enable `bEnableDebugVisualization = true` in `AMocTrainer` and use `DrawDebugHelpers` in `CollectObservations()`.

**Q: What Python libraries are required?**
A: Ray RLlib (2.x), PyTorch (2.x), gRPC (1.x). See `training/requirements.txt`.

---

**For further assistance, refer to Schola plugin documentation or contact the MOC development team.**
