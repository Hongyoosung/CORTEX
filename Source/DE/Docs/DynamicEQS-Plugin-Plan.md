# DynamicEQS Plugin — Implementation Plan

**Engine:** UE5.6 | **Language:** C++17 | **Status:** Implemented (Phase 1–4 complete)
**Date:** 2026-03-12 | **Author:** CORTEX Team

---

## 1. Overview

### 1.1 Purpose

**DynamicEQS** is a UE5 plugin that enables any project to use **RL-driven spatial reasoning via the Environment Query System (EQS)**. The core idea: instead of hand-tuned EQS query parameters, a trained RL policy outputs a vector of continuous weights at runtime that dynamically re-prioritize EQS scoring tests per agent, per step.

The plugin provides the framework. Projects provide the game logic.

### 1.2 Value Proposition

| Without DynamicEQS | With DynamicEQS |
|---|---|
| Hand-tuned static EQS weights | RL policy learns optimal weights per context |
| Agent behavior fixed at design time | Behavior adapts to match state at runtime |
| Reward/observation tightly coupled to game | Reward/observation defined by project via base classes |
| Significant Schola boilerplate per project | Attach a component, override base classes, done |

### 1.3 Target Users

- AI engineers adding learned spatial behavior to existing UE5 projects
- Researchers experimenting with RL-driven EQS for navigation and tactics
- Teams using the **Schola** RL framework who want a structured integration layer

---

## 2. Design Principles

1. **Framework, not implementation.** The plugin provides abstract base classes and infrastructure. All game-specific logic (observation layout, reward events, EQS weight semantics) lives in the project.
2. **Editor-first configuration.** Every configurable parameter must be accessible via the UE5 Details panel without touching code. Base classes use `UCLASS(Blueprintable)` and `UPROPERTY(EditAnywhere)`.
3. **Component-based workflow.** A developer should be able to integrate DynamicEQS by attaching components to actors in the editor and assigning Data Assets — no C++ required for basic use.
4. **Clean dependency boundary.** The plugin has zero knowledge of game-specific types (GAS, strategy enums, capture points, etc.). It depends only on UE5 core, AIModule, and Schola.
5. **Industry-quality API.** Consistent naming, proper `UFUNCTION(BlueprintCallable)` exposure, Blueprint-overridable events where appropriate, and XML doc comments on all public interfaces.

---

## 3. Plugin Architecture

### 3.1 Layer Diagram

```
┌──────────────────────────────────────────────────┐
│              PROJECT CONTENT                     │
│  DETacticalObserver    DETrainer                 │
│  DETacticalActuator    DERewardSubsystem         │
│  DECharacter (GAS)     DESquadManager            │
│  DEMatchManager        DECapturePoint, etc.      │
│                                                  │
│  (All derive from / use plugin base classes)     │
└──────────────┬───────────────────────────────────┘
               │ inherits / uses
┌──────────────▼───────────────────────────────────┐
│           DynamicEQS PLUGIN                      │
│                                                  │
│  ┌─────────────┐  ┌──────────────────────────┐  │
│  │  EQS Core   │  │  Schola Integration      │  │
│  │             │  │                          │  │
│  │  Executor   │  │  AgentComponent          │  │
│  │  Context    │  │  ObserverBase (Abstract) │  │
│  │  Weights    │  │  ActuatorBase (Abstract) │  │
│  │             │  │  TrainerBase  (Abstract) │  │
│  └─────────────┘  │  EnvironmentActor        │  │
│                   └──────────────────────────┘  │
│  ┌─────────────────┐  ┌───────────────────────┐ │
│  │  Reward System  │  │  Observation System   │ │
│  │                 │  │                       │ │
│  │  CalculatorBase │  │  ObsBuilderBase       │ │
│  │  RewardData     │  │  ObservationSpace     │ │
│  │  (DataAsset)    │  │  (descriptor struct)  │ │
│  └─────────────────┘  └───────────────────────┘ │
│                                                  │
│  ┌──────────────────────────────────────────┐   │
│  │  Logging / Training                      │   │
│  │  TransitionLogger  (State,Action,Reward) │   │
│  └──────────────────────────────────────────┘   │
└──────────────────────────────────────────────────┘
               │ depends on
┌──────────────▼───────────────────────────────────┐
│  UE5 Core | AIModule | Schola | ONNX (NNE)       │
└──────────────────────────────────────────────────┘
```

### 3.2 File Structure

```
Plugins/DynamicEQS/
├── DynamicEQS.uplugin
└── Source/
    └── DynamicEQS/
        ├── DynamicEQS.Build.cs
        ├── Public/
        │   ├── DynamicEQSModule.h
        │   │
        │   ├── EQS/
        │   │   ├── DynamicEQSExecutor.h          ← UActorComponent, executes weighted queries
        │   │   ├── DynamicEQSContext.h            ← UEnvQueryContext subclass
        │   │   └── DynamicEQSWeightParameters.h  ← FDynamicEQSWeightParams (EditAnywhere)
        │   │
        │   ├── Schola/
        │   │   ├── DynamicEQSAgentComponent.h    ← UActorComponent (attach to any agent)
        │   │   ├── DynamicEQSEnvironmentActor.h  ← AActor (place in level)
        │   │   └── Base/
        │   │       ├── DynamicEQSObserverBase.h  ← Abstract, Blueprintable
        │   │       ├── DynamicEQSActuatorBase.h  ← Abstract, Blueprintable
        │   │       └── DynamicEQSTrainerBase.h   ← Abstract, Blueprintable
        │   │
        │   ├── Reward/
        │   │   ├── DynamicEQSRewardCalculatorBase.h  ← Abstract, Blueprintable
        │   │   └── DynamicEQSRewardData.h             ← UDataAsset, editor-configurable
        │   │
        │   └── Observation/
        │       ├── DynamicEQSObservationBuilderBase.h ← Abstract, Blueprintable
        │       └── DynamicEQSObservationSpace.h       ← Space descriptor struct/class
        │
        └── Private/
            ├── DynamicEQSModule.cpp
            ├── EQS/
            │   ├── DynamicEQSExecutor.cpp
            │   └── DynamicEQSContext.cpp
            ├── Schola/
            │   ├── DynamicEQSAgentComponent.cpp
            │   ├── DynamicEQSEnvironmentActor.cpp
            │   └── Base/
            │       ├── DynamicEQSObserverBase.cpp
            │       ├── DynamicEQSActuatorBase.cpp
            │       └── DynamicEQSTrainerBase.cpp
            ├── Reward/
            │   ├── DynamicEQSRewardCalculatorBase.cpp
            │   └── DynamicEQSRewardData.cpp
            └── Observation/
                ├── DynamicEQSObservationBuilderBase.cpp
                └── DynamicEQSObservationSpace.cpp
```

---

## 4. Module Breakdown

### 4.1 EQS Core

The lowest-level layer. No Schola dependency. Could be used standalone.

#### `FDynamicEQSWeightParameters`
```
File: Public/EQS/DynamicEQSWeightParameters.h
```

- Generic N-dimensional weight vector for EQS scoring tests
- `USTRUCT` with `TArray<float> Weights` and named accessors
- `EditAnywhere` so designers can set defaults per-agent in the Details panel
- `ToArray()` / `FromArray()` / `Clamp()` methods
- The number of dimensions and their semantic meaning is defined by the project, not the plugin

#### `UDynamicEQSExecutor`
```
File: Public/EQS/DynamicEQSExecutor.h
```

- `UActorComponent` — attach to any actor
- Accepts `FDynamicEQSWeightParameters`, applies them to a named EQS query template, returns best location
- Exposes: `ExecuteQuery()` (async), `ExecuteQuerySynchronous()` (training mode)
- `UPROPERTY(EditAnywhere)`: EQS query template asset, search radius, weight scale factor
- `UFUNCTION(BlueprintCallable)`: `SetWeights()`, `GetLastResult()`

#### `UDynamicEQSContext`
```
File: Public/EQS/DynamicEQSContext.h
```

- `UEnvQueryContext` subclass providing the querying actor as context
- Required for EQS queries initiated by the executor component

---

### 4.2 Schola Integration Layer

#### `UDynamicEQSAgentComponent`
```
File: Public/Schola/DynamicEQSAgentComponent.h
```

The central hub component. Attach this to any agent actor.

- Holds references to: one `ObserverBase`, one `ActuatorBase`, one `TrainerBase`
- These are assigned in the Details panel (no code needed)
- Exposes: `GetCurrentWeights()`, `SetExternalParameters(FInstancedStruct)` — generic channel for project-specific data (e.g. commanded strategy) with no plugin-level type coupling
- Mode switching: `EDynamicEQSAgentMode` (Training / Inference)
- `UPROPERTY(EditAnywhere, Instanced)` for sub-objects so they're configured inline in the Details panel

#### `ADynamicEQSEnvironmentActor`
```
File: Public/Schola/DynamicEQSEnvironmentActor.h
```

- Thin wrapper around Schola's environment actor
- Already environment-agnostic; moved to plugin with project-specific logic removed
- `UPROPERTY(EditAnywhere)`: environment ID, agent registration list

#### `UDynamicEQSObserverBase` *(Abstract)*
```
File: Public/Schola/Base/DynamicEQSObserverBase.h
```

- `UCLASS(Abstract, Blueprintable)`
- Pure virtual: `GetObservationSpace()` → returns space descriptor
- Pure virtual: `CollectObservations(FDynamicEQSObservationWriter&)` → fills observation buffer
- Blueprint event: `OnObservationCollected` (for debug/visualization hooks)
- Project creates a concrete subclass (e.g. `DETacticalObserver`) that fills in game state

#### `UDynamicEQSActuatorBase` *(Abstract)*
```
File: Public/Schola/Base/DynamicEQSActuatorBase.h
```

- `UCLASS(Abstract, Blueprintable)`
- Pure virtual: `GetActionSpace()` → returns action space descriptor
- Pure virtual: `ApplyAction(const FDynamicEQSActionData&, UDynamicEQSExecutor*)` → converts RL action to EQS weights and fires the executor
- Project creates a concrete subclass (e.g. `DETacticalParameterActuator`) that maps 7-dim Box action to `FDEEQSWeightParameters`

#### `UDynamicEQSTrainerBase` *(Abstract)*
```
File: Public/Schola/Base/DynamicEQSTrainerBase.h
```

- `UCLASS(Abstract, Blueprintable)`
- Pure virtual: `ComputeReward()` → float
- Pure virtual: `ComputeTermination()` → `EDynamicEQSTerminationReason`
- Virtual: `ResetEpisode()`, `GetDebugInfo()` → `TMap<FString, float>`
- `UPROPERTY(EditAnywhere)`: `MaxEpisodeSteps`, `bLogTransitions`, `TransitionLogPath`
- Project creates a concrete subclass (e.g. `DETrainer`) that implements game-specific reward logic

---

### 4.3 Reward System

#### `UDynamicEQSRewardData` *(DataAsset)*
```
File: Public/Reward/DynamicEQSRewardData.h
```

- `UDataAsset` subclass — users create an asset in the Content Browser, configure in editor
- Base fields: `RewardScale`, `SurvivalBonus`, `StepPenalty`, `TerminalWinReward`, `TerminalLossReward`
- Projects can subclass to add domain-specific fields (e.g. `DERewardData` adds `KillReward`, `CaptureReward`, etc.)
- Referenced by `UDynamicEQSTrainerBase` via `UPROPERTY(EditAnywhere)`

#### `UDynamicEQSRewardCalculatorBase` *(Abstract)*
```
File: Public/Reward/DynamicEQSRewardCalculatorBase.h
```

- `UCLASS(Abstract, Blueprintable)`
- Pure virtual: `CalculateStepReward(const FDynamicEQSStepContext&)` → float
- Pure virtual: `CalculateTerminalReward(bool bWon)` → float
- Virtual: `Reset()`
- Blueprint-native event versions for BP subclassing
- Holds a `TObjectPtr<UDynamicEQSRewardData>` configured in editor

---

### 4.4 Observation System

#### `UDynamicEQSObservationBuilderBase` *(Abstract)*
```
File: Public/Observation/DynamicEQSObservationBuilderBase.h
```

- `UCLASS(Abstract, Blueprintable)`
- Pure virtual: `BuildObservation(TArray<float>& OutObservation)`
- Pure virtual: `GetObservationDimension()` → int32
- Pure virtual: `ValidateObservation(const TArray<float>&)` → bool
- Projects subclass and implement the entity-centric or any other observation layout

#### `FDynamicEQSObservationSpace`
```
File: Public/Observation/DynamicEQSObservationSpace.h
```

- `USTRUCT(BlueprintType)`
- Descriptor: `Dimensions`, `Low`, `High`, `SpaceType` (Box / Discrete)
- Used by `ObserverBase::GetObservationSpace()` to register with Schola

---

### 4.5 Logging / Training Helpers

#### `UDynamicEQSTransitionLogger`
```
Moved from: DEScholaTransitionLogger
```

- Logs `(State, Action, Reward, NextState, bTerminal)` tuples
- `UPROPERTY(EditAnywhere)`: output path, flush interval, format (JSON / Binary)
- Generic — no project-specific types

---

## 5. Plugin ↔ Project Boundary

### 5.1 What Moves to the Plugin

| Current File | Plugin Equivalent | Notes |
|---|---|---|
| `DEEQSExecutor` | `UDynamicEQSExecutor` | Generalized, no strategy types |
| `DEEQSContext` | `UDynamicEQSContext` | Direct move |
| `DEEQSTypes` | `FDynamicEQSWeightParameters` | Generalized dimensions |
| `DEScholaAgent` | `UDynamicEQSAgentComponent` | Strategy coupling removed |
| `DEScholaEnvironment` | `ADynamicEQSEnvironmentActor` | Already generic |
| `DEScholaTransitionLogger` | `UDynamicEQSTransitionLogger` | Direct move |
| Abstract reward interface | `UDynamicEQSRewardCalculatorBase` | New |
| Abstract observation interface | `UDynamicEQSObservationBuilderBase` | New |
| Abstract trainer interface | `UDynamicEQSTrainerBase` | New (extracted from `DETrainer`) |
| Abstract observer interface | `UDynamicEQSObserverBase` | New (extracted from `DETacticalObserver`) |
| Abstract actuator interface | `UDynamicEQSActuatorBase` | New (extracted from `DETacticalParameterActuator`) |

### 5.2 What Stays in the Project

| File | Reason |
|---|---|
| `DETacticalObserver` | 170-dim entity-centric layout is project-specific |
| `DETacticalParameterActuator` | 7-dim EQS weight mapping is project-specific |
| `DETrainer` | Kill/capture/assist reward logic is project-specific |
| `DERewardSubsystem` | Concrete reward events tied to game rules |
| `DERewardData` | Subclasses plugin DataAsset with extra fields |
| `DECharacter` + GAS | Game content |
| `DESquadManager` / `DEMatchManager` | Tactical commander is project-specific |
| `DETeamWorldState`, `DEStrategyTypes` | Project-specific team state model |
| `DEObservationTypes` | Project-specific observation layout |
| `DECapturePoint`, `DESpawnArea`, etc. | Game content actors |
| All GAS classes | Game content |

### 5.3 Coupling Strategy for `SetExternalParameters`

The current `DEScholaAgent` knows about `EDEStrategyType` (Assault/Defend/Support). The plugin cannot know this. The solution:

```
UDynamicEQSAgentComponent::SetExternalParameters(FInstancedStruct Params)
```

- `FInstancedStruct` is UE5.3+ and fully editor-accessible
- The project defines `FDEAgentCommandParams { EDEStrategyType Strategy; }` and passes it
- The agent component stores it and forwards to the Actuator/Observer via the same mechanism
- Zero plugin-level coupling to game types

---

## 6. Editor UX Workflow

The target workflow for a developer integrating DynamicEQS into a new project:

### Step 1: Place Environment Actor
- Drag `ADynamicEQSEnvironmentActor` into the level
- Configure environment ID and agent slots in Details panel

### Step 2: Configure the Agent
- Add `UDynamicEQSAgentComponent` to the agent actor
- In Details panel, set:
  - **Observer** → assign a concrete `ObserverBase` subclass (Instanced, inline-editable)
  - **Actuator** → assign a concrete `ActuatorBase` subclass (Instanced, inline-editable)
  - **Trainer** → assign a concrete `TrainerBase` subclass (Instanced, inline-editable)
  - **RewardData** → assign a `DynamicEQSRewardData` asset from Content Browser
  - **Agent Mode** → Training / Inference dropdown

### Step 3: Configure the EQS Executor
- Add `UDynamicEQSExecutor` to the agent actor (or it can be auto-created by the agent component)
- In Details panel, set:
  - EQS Query Template asset
  - Search radius
  - Weight count (must match Actuator output dimensions)

### Step 4: Create Concrete Subclasses
The only code a project developer writes:
- Subclass `UDynamicEQSObserverBase` → implement `CollectObservations()`
- Subclass `UDynamicEQSActuatorBase` → implement `ApplyAction()` (map floats → EQS weights)
- Subclass `UDynamicEQSTrainerBase` → implement `ComputeReward()`
- Optionally subclass `UDynamicEQSRewardData` → add domain-specific reward parameters

---

## 7. Dependencies

```ini
# DynamicEQS.uplugin — module dependencies
PublicDependencyModuleNames:
  - Core
  - CoreUObject
  - Engine
  - AIModule          ← EQS
  - NavigationSystem  ← NavMesh
  - Schola            ← RL training framework
  - StructUtils       ← FInstancedStruct (UE5.3+)

# Optional / editor-only
PrivateDependencyModuleNames:
  - NNE               ← ONNX inference (if inference mode included in plugin)
```

---

## 8. Migration Plan

### Phase 1: Plugin Scaffold ✅
- [x] Create `Plugins/DynamicEQS/` directory structure
- [x] Write `DynamicEQS.uplugin` descriptor
- [x] Write `DynamicEQS.Build.cs` with correct dependencies
- [x] Create stub `DynamicEQSModule.h/.cpp`

### Phase 2: EQS Core (Lowest Risk) ✅
- [x] Move `DEEQSContext` → `UDynamicEQSContext` (near-direct move)
- [x] Generalize `DEEQSTypes` → `FDynamicEQSWeightParameters` (remove game-specific dimension names, keep generic array)
- [x] Move `DEEQSExecutor` → `UDynamicEQSExecutor` (strip `DEStrategyType` references)
- [ ] Update project `DEEQSExecutor` usages to new class name

### Phase 3: Schola Base Classes (New abstractions) ✅
- [x] Extract `UDynamicEQSObserverBase` from `DETacticalObserver`
- [x] Extract `UDynamicEQSActuatorBase` from `DETacticalParameterActuator`
- [x] Extract `UDynamicEQSTrainerBase` from `DETrainer`
- [x] Implement `FInstancedStruct` external params channel in `UDynamicEQSAgentComponent`
- [x] Move `DEScholaEnvironment` → `ADynamicEQSEnvironmentActor`

### Phase 4: Reward & Observation Base Classes (New abstractions) ✅
- [x] Create `UDynamicEQSRewardData` DataAsset base
- [x] Create `UDynamicEQSRewardCalculatorBase`
- [x] Create `UDynamicEQSObservationBuilderBase`
- [x] Subclass `UDynamicEQSRewardData` in project as `UDERewardData` (add game fields)

### Phase 5: Logging
- [x] Move `DEScholaTransitionLogger` → `UDynamicEQSTransitionLogger` (strip game types)

### Phase 6: Project Cleanup
- [x] Update all project classes to derive from plugin base classes
- [x] Verify no circular dependency (project → plugin only, never plugin → project)
- [x] Compile and fix all include paths
- [x] Run existing training pipeline to verify no behavioral regression

---

## 9. Naming Conventions

| Category | Convention | Example |
|---|---|---|
| Plugin classes | `UDynamicEQS` prefix | `UDynamicEQSExecutor` |
| Plugin structs | `FDynamicEQS` prefix | `FDynamicEQSWeightParameters` |
| Plugin enums | `EDynamicEQS` prefix | `EDynamicEQSAgentMode` |
| Project classes | `UDE` / `ADE` prefix | `UDETacticalObserver` |
| Project concrete classes | Derive from plugin abstract | `UDETacticalObserver : public UDynamicEQSObserverBase` |

---

## 10. Resolved Decisions

1. **ONNX inference location:** Kept in the project (NNE/ONNX not a plugin dependency). Plugin does not include `NNE` module.
2. **Weight parameter dimensionality:** `TArray<float>` chosen for runtime flexibility. `FromArray` / `ToArray` / `Clamp` methods provided. Dimension semantics defined by project.
3. **Blueprint-only workflow:** C++ subclassing required for concrete implementations. All base classes are `Blueprintable` for BP event hooks.
4. **Plugin name:** `DynamicEQS` confirmed.
5. **Weight parameter channel:** EQS named float params (`Weight0`, `Weight1`, …) used to pass weights into query at runtime. EQS test assets reference these by name.
6. **External params channel:** `FInstancedStruct SetExternalParameters()` on `UDynamicEQSAgentComponent` — zero plugin-level coupling to game enums/types.

## 11. Remaining Work

- Phase 5: `UDynamicEQSTransitionLogger` (move from `DEScholaTransitionLogger`)
- Phase 6: Update project concrete classes (`DETacticalObserver`, `DETacticalParameterActuator`, `DETrainer`, `DEScholaAgent`, `DEScholaEnvironment`) to inherit from plugin base classes
- First compile and fix any include/UHT errors
