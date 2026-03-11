# DynamicEQS Plugin

**Engine:** UE5.6 | **Language:** C++17 | **Version:** 1.0 (Beta)
**Dependencies:** Schola, AIModule, NavigationSystem, StructUtils

---

## Overview

DynamicEQS enables **RL-driven spatial reasoning** via the UE5 Environment Query System. Instead of hand-tuned static EQS scoring weights, a trained RL policy outputs a continuous weight vector at runtime that re-prioritizes EQS tests per agent, per step.

The plugin provides the framework. Projects provide the game logic.

| Without DynamicEQS | With DynamicEQS |
|---|---|
| Hand-tuned static EQS weights | RL policy learns optimal weights per context |
| Behavior fixed at design time | Behavior adapts to game state at runtime |
| Reward/observation tightly coupled to game | Defined by project via abstract base classes |
| Schola boilerplate per project | Attach a component, override base classes |

---

## Architecture

```
┌─────────────────────────────────┐
│         PROJECT                 │
│  ConcreteObserver               │
│  ConcreteActuator               │
│  ConcreteTrainer                │
│  (derive from plugin bases)     │
└──────────────┬──────────────────┘
               │ inherits
┌──────────────▼──────────────────┐
│         DynamicEQS PLUGIN       │
│                                 │
│  UDynamicEQSExecutor     ◄──── core: weighted EQS query
│  UDynamicEQSAgentComponent      │
│  UDynamicEQSObserverBase        │
│  UDynamicEQSActuatorBase        │
│  UDynamicEQSTrainerBase         │
│  UDynamicEQSRewardData          │
│  UDynamicEQSTransitionLogger    │
└──────────────┬──────────────────┘
               │ depends on
┌──────────────▼──────────────────┐
│  UE5 Core | AIModule | Schola   │
└─────────────────────────────────┘
```

---

## Integration Steps

### Step 1 — Add the plugin dependency

In your project's `Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "DynamicEQS",
    // ...
});
```

### Step 2 — Place the Environment Actor

Drag `ADynamicEQSEnvironmentActor` into the level. Set `EnvironmentId` in the Details panel.

### Step 3 — Configure the Agent

Add `UDynamicEQSAgentComponent` to your agent actor. In the Details panel assign:

| Property | What to set |
|---|---|
| **Observer** | Your concrete `UDynamicEQSObserverBase` subclass |
| **Actuator** | Your concrete `UDynamicEQSActuatorBase` subclass |
| **Trainer** | Your concrete `UDynamicEQSTrainerBase` subclass |
| **Agent Mode** | `Training` or `Inference` |

### Step 4 — Add the EQS Executor

Add `UDynamicEQSExecutor` to the same actor. In the Details panel set:

| Property | What to set |
|---|---|
| **Query Template** | Your EQS query asset |
| **Search Radius** | Spatial search range (cm) |
| **Weight Scale Factor** | Global multiplier on all weights |

EQS scoring tests reference weights by name (`Weight0`, `Weight1`, …). The number of names must match the dimension count returned by your `ActuatorBase::GetActionSpace()`.

### Step 5 — Implement the three abstract classes

This is the only C++ code a project must write.

```cpp
// Observer — fills the observation vector each step
UCLASS()
class UMyObserver : public UDynamicEQSObserverBase
{
    GENERATED_BODY()
    virtual FBoxSpace GetObservationSpace() const override;
    virtual void CollectObservations(FBoxPoint& Out) override;
};

// Actuator — maps policy output (floats) to EQS weights
UCLASS()
class UMyActuator : public UDynamicEQSActuatorBase
{
    GENERATED_BODY()
    virtual FBoxSpace GetActionSpace() override;
    virtual void TakeAction(const FBoxPoint& Action) override;
};

// Trainer — computes reward and termination condition
UCLASS()
class UMyTrainer : public UDynamicEQSTrainerBase
{
    GENERATED_BODY()
    virtual float ComputeReward() override;
    virtual EDynamicEQSTerminationReason ComputeTermination() override;
};
```

---

## Plugin Classes

### EQS Core

#### `UDynamicEQSExecutor` — `Public/EQS/DynamicEQSExecutor.h`

The central concrete class. Attach to any actor. Receives a weight vector and drives a named EQS query.

| Method | Description |
|---|---|
| `SetWeights(FDynamicEQSWeightParameters)` | Replace active weights |
| `ExecuteQuery(OnComplete)` | Async query; callback on game thread |
| `ExecuteQuerySynchronous()` | Blocking query; returns `TOptional<FVector>` |
| `GetLastResult()` | Best location from last completed query |

**Config properties:** `QueryTemplate`, `SearchRadius`, `WeightScaleFactor`

#### `FDynamicEQSWeightParameters` — `Public/EQS/DynamicEQSWeightParameters.h`

Generic N-dimensional weight vector. `TArray<float> Weights`. Utility methods: `ToArray()`, `FromArray()`, `Clamp(Min, Max)`, `Num()`.

---

### Schola Integration

#### `UDynamicEQSAgentComponent` — `Public/Schola/DynamicEQSAgentComponent.h`

Central hub component. Extends `UInferenceComponent` (Schola). Holds instanced Observer, Actuator, Trainer sub-objects.

| Method | Description |
|---|---|
| `GetCurrentWeights()` | Last weight vector applied |
| `SetExternalParameters(FInstancedStruct)` | Pass project-specific data (e.g. commanded strategy) to sub-objects without plugin-level type coupling |
| `GetExternalParameters()` | Read the stored params from Observer/Actuator/Trainer |

#### `ADynamicEQSEnvironmentActor` — `Public/Schola/DynamicEQSEnvironmentActor.h`

Thin actor that groups agents into a named RL environment. Place one per arena. `RegisterAgent()` / `UnregisterAgent()` available at runtime.

#### `UDynamicEQSObserverBase` — `Public/Schola/Base/DynamicEQSObserverBase.h`

Abstract. Extends `UBoxObserver` (Schola). Implement:
- `GetObservationSpace() const` → `FBoxSpace`
- `CollectObservations(FBoxPoint& Out)`

Blueprint event: `OnObservationCollected` (debug / visualization hook).

#### `UDynamicEQSActuatorBase` — `Public/Schola/Base/DynamicEQSActuatorBase.h`

Abstract. Extends `UBoxActuator` (Schola). Implement:
- `GetActionSpace()` → `FBoxSpace`
- `TakeAction(const FBoxPoint& Action)`

#### `UDynamicEQSTrainerBase` — `Public/Schola/Base/DynamicEQSTrainerBase.h`

Abstract `UObject`. Owns per-episode logic. Implement:
- `ComputeReward()` → `float`
- `ComputeTermination()` → `EDynamicEQSTerminationReason`

Overridable: `ResetEpisode()`, `GetDebugInfo()`.
Config properties: `MaxEpisodeSteps`, `bLogTransitions`, `TransitionLogPath`, `RewardData`.

---

### Reward System

#### `UDynamicEQSRewardData` — `Public/Reward/DynamicEQSRewardData.h`

`UDataAsset`. Create in the Content Browser, assign to `UDynamicEQSTrainerBase::RewardData`.

| Property | Default | Description |
|---|---|---|
| `RewardScale` | 1.0 | Global multiplier |
| `SurvivalBonus` | 0.01 | Per-step survival reward |
| `StepPenalty` | -0.001 | Per-step efficiency penalty |
| `TerminalWinReward` | 1.0 | Episode win reward |
| `TerminalLossReward` | -1.0 | Episode loss reward |

Subclass to add domain-specific fields (e.g. `KillReward`, `CaptureReward`).

#### `UDynamicEQSRewardCalculatorBase` — `Public/Reward/DynamicEQSRewardCalculatorBase.h`

Abstract. Implement `CalculateStepReward(FDynamicEQSStepContext&)` and `CalculateTerminalReward(bool bWon)`. Holds a `TObjectPtr<UDynamicEQSRewardData>`.

---

### Observation System

#### `UDynamicEQSObservationBuilderBase` — `Public/Observation/DynamicEQSObservationBuilderBase.h`

Abstract. Implement `BuildObservation(TArray<float>& Out)`, `GetObservationDimension()`, `ValidateObservation()`. Use when building the observation array outside of the Schola observer pipeline.

#### `FDynamicEQSObservationSpace` — `Public/Observation/DynamicEQSObservationSpace.h`

Descriptor struct: `Dimensions`, `Low`, `High`, `SpaceType` (Box / Discrete).

---

### Logging

#### `UDynamicEQSTransitionLogger` — `Public/Logging/DynamicEQSTransitionLogger.h`

Generic `(State, Action, Reward, NextState, bTerminal)` recorder. No project-specific types.

```cpp
Logger->Initialize();
Logger->RecordTransition(State, Action, Reward, NextState, bTerminal);
// auto-flushes at FlushInterval; or call explicitly:
Logger->FlushToDisk();
```

| Property | Default | Description |
|---|---|---|
| `OutputPath` | `Saved/DynamicEQS/Transitions` | Output directory (project-relative or absolute) |
| `FlushInterval` | 1000 | Transitions before auto-flush (0 = manual) |
| `LogFormat` | JSON | `JSON` or `Binary` |

---

## Passing Project-Specific Context

The plugin has zero knowledge of game types. Use `FInstancedStruct` to pass project data to sub-objects:

```cpp
// Project side — define your command struct
USTRUCT()
struct FMyAgentCommand
{
    GENERATED_BODY()
    EMyStrategyType Strategy = EMyStrategyType::Assault;
};

// At runtime — push data into the agent component
FInstancedStruct Params;
Params.InitializeAs<FMyAgentCommand>();
Params.GetMutable<FMyAgentCommand>().Strategy = EMyStrategyType::Defend;
AgentComponent->SetExternalParameters(Params);

// Inside your Observer/Actuator/Trainer subclass — read it back
const FMyAgentCommand* Cmd = AgentComponent->GetExternalParameters().GetPtr<FMyAgentCommand>();
if (Cmd) { /* use Cmd->Strategy */ }
```

---

## Naming Conventions

| Scope | Prefix | Example |
|---|---|---|
| Plugin classes | `UDynamicEQS` / `ADynamicEQS` / `FDynamicEQS` / `EDynamicEQS` | `UDynamicEQSExecutor` |
| Project concrete classes | Project prefix, derive from plugin abstract | `UMyObserver : public UDynamicEQSObserverBase` |

---

## Dependencies

```ini
PublicDependencyModuleNames:
  Core, CoreUObject, Engine
  AIModule          # EQS
  NavigationSystem  # NavMesh
  Schola            # RL training framework
  StructUtils       # FInstancedStruct (UE5.3+)
```

ONNX inference (`NNE`) is intentionally excluded. Runtime model inference belongs in the project.

---

## What the Plugin Does Not Provide

- Observation collection logic (entity-centric layout, sensor reads) — project-specific
- Reward calculation math (kill/capture/zone rewards) — project-specific
- Concrete EQS weight semantics — defined by each project's actuator
- ONNX model loading / inference — project-specific (use UE5 NNE directly)
- Match state, team management, respawn — project-specific
