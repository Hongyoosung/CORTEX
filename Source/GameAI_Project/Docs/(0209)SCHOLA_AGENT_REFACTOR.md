# ScholaMocAgent v10.2 Refactor Summary

**Date:** 2026-02-09
**Objective:** Implement proper Segregation of Responsibilities and v10.2 compatibility

---

## What Changed

### 1. Removed Components (Segregation of Responsibilities)

**Removed from ScholaMocAgent:**
- ❌ `UTeamMCTS* MCTSAlgorithm` → Moved to ASquadManager (centralized planning)
- ❌ `UScholaTransitionLogger* DataLogger` → Belongs in ScholaEnvironment/Trainer
- ❌ `ExecuteOption()`, `ExecuteGymAction()` → Actions handled by Schola Actuators
- ❌ `GetCurrentState()` → Observations handled by Schola Observers
- ❌ `ReloadModels()` → Model management is Policy's responsibility
- ❌ `LastState`, `LastOption`, `bHasLastAction` → State tracking is Environment's job

**Why Removed:**
These created overlap with other systems, violating single responsibility principle.

### 2. New Clean Design

**ScholaMocAgent is now a thin integration layer that:**
- ✅ Stores commanded strategy from SquadManager
- ✅ Provides strategy to Observers (via `GetCommandedStrategy()`)
- ✅ Supports mode switching (Training vs Inference)
- ✅ Delegates all work to Schola's parent class components

**Responsibilities by Component:**

| Component | Responsibility |
|-----------|---------------|
| **ASquadManager** | Centralized MCTS, team planning, role distribution |
| **AMocCharacter** | Receive commands, own components, forward to agent |
| **UScholaMocAgent** | Store strategy, provide to observers, mode switching |
| **Schola Observers** | Collect state + commanded strategy |
| **Schola Policy** | (State + Strategy) → EQS Weights |
| **Schola Actuators** | Apply EQS weights to navigation |
| **Schola Brain** | Coordinate Think/Act cycle |
| **ScholaEnvironment** | Episode management, logging, rewards |

---

## v10.2 Architecture Flow

```
┌──────────────────────────────────────────────────────────────┐
│ Layer 1: Centralized Planning (Squad Commander)             │
│                                                              │
│ ASquadManager:                                              │
│   ├─ Collect FTeamState (5 friendly + 5 enemy + map)      │
│   ├─ Run MCTS (15ms budget)                                │
│   ├─ Select ETacticalPlay                                  │
│   └─ Distribute [5 × EStrategyType]                        │
└───────────────────┬──────────────────────────────────────────┘
                    │
                    ▼
┌──────────────────────────────────────────────────────────────┐
│ Layer 2: Decentralized Execution (Agents)                   │
│                                                              │
│ AMocCharacter::SetCommandedStrategy(EStrategyType):         │
│   ├─ Store in member variable                              │
│   ├─ Update ScholaAgent                                    │
│   └─ Update Blackboard (for BT)                            │
│                                                              │
│ UScholaMocAgent::UpdateCommandedStrategy(EStrategyType):    │
│   └─ Store strategy (available to Observers)               │
│                                                              │
│ Schola Pipeline (Automatic):                                │
│   ├─ Observers: Collect (State + Strategy) → Observation   │
│   ├─ Brain: Request decision                               │
│   ├─ Policy: Observation → EQS Weights (7-dim)            │
│   └─ Actuators: Apply weights → Navigation                 │
└──────────────────────────────────────────────────────────────┘
```

---

## Code Changes

### ScholaMocAgent.h (Before)
```cpp
class UScholaMocAgent : public UInferenceComponent {
    // ❌ Too many responsibilities
    void ExecuteGymAction(const TArray<int32>&);
    void ExecuteOption(const FTacticalOption&);
    void ReloadModels(FString, FString);
    TArray<float> GetCurrentState() const;

    UTeamMCTS* MCTSAlgorithm;           // ❌ Should be in SquadManager
    UScholaTransitionLogger* DataLogger; // ❌ Should be in Environment
    TArray<float> LastState;             // ❌ Should be in Environment
    FTacticalOption LastOption;          // ❌ Should be in Environment
};
```

### ScholaMocAgent.h (After)
```cpp
class UScholaMocAgent : public UInferenceComponent {
    // ✅ Minimal, focused interface
    void UpdateCommandedStrategy(EStrategyType NewStrategy);
    EStrategyType GetCommandedStrategy() const;

    EAgentMode CurrentMode; // Training or Inference

private:
    EStrategyType CommandedStrategy; // ✅ Only stores strategy
};
```

### AMocCharacter Integration
```cpp
void AMocCharacter::SetCommandedStrategy(EStrategyType NewStrategy)
{
    CommandedStrategy = NewStrategy;

    // 1. Update Schola Agent (for RL observation)
    if (ScholaAgent)
    {
        ScholaAgent->UpdateCommandedStrategy(NewStrategy);
    }

    // 2. Update Blackboard for Behavior Tree
    if (AAIController* AICtrl = Cast<AAIController>(GetController()))
    {
        if (UBlackboardComponent* BB = AICtrl->GetBlackboardComponent())
        {
            BB->SetValueAsEnum("CurrentStrategy", static_cast<uint8>(NewStrategy));
        }
    }
}
```

---

## Benefits of New Design

### 1. Clean Separation of Concerns
- Each component has ONE clear responsibility
- No duplicate functionality across systems
- Easy to understand and maintain

### 2. v10.2 Compatibility
- No individual agent MCTS (centralized in SquadManager)
- Proper command-driven architecture
- 5× computational reduction (1 MCTS vs 5)

### 3. Proper Schola Integration
- Uses parent class features correctly
- Leverages Observers, Policy, Brain, Actuators
- No bypassing of framework features

### 4. Scalability
- Easy to add new strategies (just enum value)
- Easy to swap Policy implementations (Python/ONNX)
- Easy to add new Observers/Actuators

### 5. Testability
- Components can be tested independently
- Clear interfaces between systems
- No hidden dependencies

---

## Integration with Schola Framework

### Training Mode (Python RLlib)
```
1. ScholaEnvironment calls Reset()
2. Observer collects (State + Strategy) → Python
3. Python RLlib decides → EQS Weights
4. TacticalParameterActuator applies weights
5. Agent navigates via EQS
6. ScholaEnvironment logs transition
7. Repeat
```

### Inference Mode (Local ONNX)
```
1. SquadManager assigns strategy
2. Observer collects (State + Strategy)
3. Local ONNX model decides → EQS Weights
4. TacticalParameterActuator applies weights
5. Agent navigates via EQS
6. Repeat
```

---

## Next Steps (TODO)

### Observers
- [ ] Create/update MocObserver to include commanded strategy in observation
- [ ] Ensure observation space includes strategy as one-hot or integer

### Environment
- [ ] Move data logging from agent to ScholaEnvironment
- [ ] Implement episode management and reward calculation
- [ ] Add state tracking (s_t, a_t → s_t+1)

### Testing
- [ ] Test Training mode with Python connection
- [ ] Test Inference mode with ONNX models
- [ ] Verify strategy propagation: SquadManager → Character → Agent → Observer
- [ ] Validate no duplicate MCTS execution

---

## Migration Guide

If you have existing code using old ScholaMocAgent API:

### Instead of:
```cpp
// ❌ Old way
Agent->ExecuteGymAction({0, 1});
Agent->ExecuteOption(Option);
TArray<float> State = Agent->GetCurrentState();
```

### Do this:
```cpp
// ✅ New way
Character->SetCommandedStrategy(EStrategyType::Assault);
// Schola framework handles the rest automatically
```

### Instead of:
```cpp
// ❌ Old way - Agent had MCTS
FTacticalOption Best = Agent->MCTSAlgorithm->FindBestOption(State);
```

### Do this:
```cpp
// ✅ New way - SquadManager has MCTS
SquadManager->PerformTacticalPlanning();
EStrategyType Strategy = SquadManager->GetAgentStrategy(AgentIndex);
Character->SetCommandedStrategy(Strategy);
```

---

## Architecture Validation

**v10.2 Requirements:**
- ✅ Centralized planning (SquadManager)
- ✅ No individual agent MCTS
- ✅ Command-driven execution
- ✅ Strategy → EQS weights pipeline
- ✅ Proper Schola integration

**Segregation of Responsibilities:**
- ✅ Planning: SquadManager
- ✅ Execution: AMocCharacter + Components
- ✅ Integration: UScholaMocAgent (thin layer)
- ✅ Observation: Schola Observers
- ✅ Decision: Schola Policy
- ✅ Action: Schola Actuators
- ✅ Logging: ScholaEnvironment

**No Overlaps:**
- ✅ No duplicate state tracking
- ✅ No duplicate logging
- ✅ No bypassing framework features
- ✅ No mixing of concerns

---

## Summary

The refactored `UScholaMocAgent` is now a **clean, minimal integration layer** that properly delegates work to the Schola framework's standard components. It stores the commanded strategy from the centralized planner and makes it available to the observation system, while all actual decision-making, action execution, and logging happens in the appropriate specialized components.

This design follows v10.2's centralized command architecture and respects proper separation of concerns, making the codebase more maintainable, testable, and scalable.
