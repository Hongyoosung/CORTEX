# ScholaMocAgent Architecture Diagram (v10.2)

## Component Hierarchy and Data Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                         ASquadManager                                │
│                     (Centralized Commander)                          │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ 1. Collect FTeamState                                        │   │
│  │    - 5 Friendly Agents (pos, health, strategy)              │   │
│  │    - 5 Enemy Agents (pos, health, strategy)                 │   │
│  │    - Map State (objective control, fog of war)              │   │
│  │                                                              │   │
│  │ 2. Run MCTS (15ms budget)                                   │   │
│  │    - Simulate Tactical Plays                                │   │
│  │    - Use Team World Model                                   │   │
│  │    - Evaluate with Value Network                            │   │
│  │                                                              │   │
│  │ 3. Select Best ETacticalPlay                                │   │
│  │    - AllOutRush, Phalanx, BaitStrategy, etc.               │   │
│  │                                                              │   │
│  │ 4. Decompose to Role Assignments                            │   │
│  │    [EStrategyType × 5] = {Assault, Defend, Support, ...}   │   │
│  └─────────────────────────────────────────────────────────────┘   │
└───────────────┬────────────┬────────────┬────────────┬─────────────┘
                │            │            │            │
                ▼            ▼            ▼            ▼
        [Agent 0]    [Agent 1]    [Agent 2]    [Agent 3]    [Agent 4]
                │            │            │            │            │
                └────────────┴────────────┴────────────┴────────────┘
                                      │
                                      ▼
    ┌────────────────────────────────────────────────────────────────┐
    │                     AMocCharacter                               │
    │                   (Executor Agent)                              │
    │                                                                  │
    │  ┌────────────────────────────────────────────────────────┐   │
    │  │ SetCommandedStrategy(EStrategyType NewStrategy)        │   │
    │  │                                                         │   │
    │  │ 1. Store in member: CommandedStrategy = NewStrategy   │   │
    │  │                                                         │   │
    │  │ 2. Forward to Schola Agent:                           │   │
    │  │    ScholaAgent->UpdateCommandedStrategy(NewStrategy)  │   │
    │  │                                                         │   │
    │  │ 3. Update AI Blackboard:                              │   │
    │  │    BB->SetValueAsEnum("CurrentStrategy", Strategy)    │   │
    │  └────────────────────────────────────────────────────────┘   │
    │                                                                  │
    │  Components:                                                     │
    │  ├─ UHealthComponent                                            │
    │  ├─ UWeaponComponent                                            │
    │  ├─ UScholaMocAgent ◄─── Integration Layer                     │
    │  └─ UAIPerceptionStimuliSourceComponent                        │
    └─────────────────────────┬────────────────────────────────────────┘
                              │
                              ▼
    ┌────────────────────────────────────────────────────────────────┐
    │                   UScholaMocAgent                               │
    │              (Schola Integration Layer)                         │
    │                                                                  │
    │  ┌────────────────────────────────────────────────────────┐   │
    │  │ UpdateCommandedStrategy(EStrategyType NewStrategy)     │   │
    │  │                                                         │   │
    │  │ CommandedStrategy = NewStrategy;                       │   │
    │  │                                                         │   │
    │  │ // Strategy now available to Observers via getter     │   │
    │  └────────────────────────────────────────────────────────┘   │
    │                                                                  │
    │  Inherited from UInferenceComponent:                            │
    │  ├─ TArray<UAbstractObserver*> Observers ◄─── Collect State   │
    │  ├─ UAbstractPolicy* Policy              ◄─── Make Decisions  │
    │  ├─ UAbstractBrain* Brain                ◄─── Think/Act Cycle │
    │  └─ TArray<UActuator*> Actuators         ◄─── Execute Actions │
    └─────────────┬───────────────────────────────────────────────────┘
                  │
                  │ Schola Framework Pipeline (Automatic)
                  │
    ┌─────────────▼───────────────────────────────────────────────────┐
    │                  Think-Act Cycle                                 │
    │                                                                   │
    │  ┌─────────────────────────────────────────────────────────┐   │
    │  │ THINK PHASE (Brain)                                     │   │
    │  │                                                          │   │
    │  │ 1. Observers Collect State                             │   │
    │  │    ├─ MocObserver:                                      │   │
    │  │    │   ├─ Agent position, velocity                     │   │
    │  │    │   ├─ Health, ammo                                 │   │
    │  │    │   ├─ Visible enemies                              │   │
    │  │    │   ├─ Objective states                             │   │
    │  │    │   └─ Commanded Strategy ◄───────────────┐        │   │
    │  │    │                                          │        │   │
    │  │    └─ Other Observers...                     │        │   │
    │  │                                              │        │   │
    │  │ 2. Brain Requests Decision                   │        │   │
    │  │    ├─ Training Mode: Send to Python RLlib   │        │   │
    │  │    └─ Inference Mode: Use Local ONNX Model  │        │   │
    │  │                                              │        │   │
    │  │ 3. Policy Makes Decision                     │        │   │
    │  │    Input: (State + Commanded Strategy) ─────┘        │   │
    │  │    Output: EQS Weights [8-dim]                       │   │
    │  │      ├─ CoverPreference                              │   │
    │  │      ├─ DistanceToEnemy                              │   │
    │  │      ├─ TeamSpread                                   │   │
    │  │      ├─ ObjectiveProximity                           │   │
    │  │      ├─ Aggression                                   │   │
    │  │      ├─ FlankingAngle                                │   │
    │  │      ├─ VisibilityToEnemies                          │   │
    │  │      └─ PathToObjective                              │   │
    │  └──────────────────────────────────────────────────────┘   │
    │                                                               │
    │  ┌─────────────────────────────────────────────────────────┐   │
    │  │ ACT PHASE (Actuators)                                   │   │
    │  │                                                          │   │
    │  │ 1. TacticalParameterActuator Receives Weights          │   │
    │  │    TakeAction(EQS Weights [8-dim])                     │   │
    │  │                                                          │   │
    │  │ 2. Apply to EQS Dynamic Weight System                  │   │
    │  │    MocAIController->SetEQSWeights(Weights)            │   │
    │  │                                                          │   │
    │  │ 3. EQS Spatial Reasoning                               │   │
    │  │    ├─ Query 48 tactical positions                      │   │
    │  │    ├─ Evaluate each with 8 weighted tests             │   │
    │  │    └─ Select best position                             │   │
    │  │                                                          │   │
    │  │ 4. Navigate to Selected Position                       │   │
    │  │    UE5 Navigation System (NavMesh)                     │   │
    │  └──────────────────────────────────────────────────────────┘   │
    └───────────────────────────────────────────────────────────────────┘
```

---

## Component Responsibilities Matrix

| Component | Planning | Observation | Decision | Execution | Logging |
|-----------|----------|-------------|----------|-----------|---------|
| **ASquadManager** | ✅ Team MCTS | ✅ Team State | ✅ Tactical Play | ❌ | ❌ |
| **AMocCharacter** | ❌ | ❌ | ❌ | ✅ Movement | ❌ |
| **UScholaMocAgent** | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Schola Observers** | ❌ | ✅ Individual | ❌ | ❌ | ❌ |
| **Schola Policy** | ❌ | ❌ | ✅ EQS Weights | ❌ | ❌ |
| **Schola Brain** | ❌ | ❌ | ✅ Coordinate | ❌ | ❌ |
| **Schola Actuators** | ❌ | ❌ | ❌ | ✅ Apply Weights | ❌ |
| **ScholaEnvironment** | ❌ | ✅ Episodes | ❌ | ❌ | ✅ Transitions |
| **MocTrainer** | ❌ | ❌ | ❌ | ❌ | ✅ Training Mgmt |

**Key:** ✅ = Primary Responsibility, ❌ = Not Responsible

---

## Data Flow Example: Full Decision Cycle

```
Time: t=0.0s
─────────────────────────────────────────────────────────────────────

1. ASquadManager::Tick()
   └─> PerformTacticalPlanning()
       ├─ Collect FTeamState from 5 agents
       ├─ Run MCTS (15ms)
       ├─ Select: ETacticalPlay::Phalanx
       └─ Distribute roles:
           Agent[0] = EStrategyType::Defend
           Agent[1] = EStrategyType::Defend
           Agent[2] = EStrategyType::Support
           Agent[3] = EStrategyType::Support
           Agent[4] = EStrategyType::Support

2. AMocCharacter[0]::SetCommandedStrategy(EStrategyType::Defend)
   ├─ CommandedStrategy = Defend
   ├─ ScholaAgent->UpdateCommandedStrategy(Defend)
   └─ Blackboard->SetValueAsEnum("CurrentStrategy", Defend)

3. UScholaMocAgent[0]::UpdateCommandedStrategy(Defend)
   └─ CommandedStrategy = Defend
       (Now available to Observers via GetCommandedStrategy())

─────────────────────────────────────────────────────────────────────
Time: t=0.1s (Think Phase)
─────────────────────────────────────────────────────────────────────

4. Schola Brain triggers Think()
   └─> Observers collect state:

       MocObserver gathers:
       ├─ Position: (1200, 800, 0)
       ├─ Velocity: (0, 0, 0)
       ├─ Health: 85/100
       ├─ Ammo: 120/150
       ├─ Visible Enemies: 2
       ├─ Nearest Objective: CapturePoint_B (800cm away)
       ├─ Team Formation: Spread
       └─ Commanded Strategy: Defend ◄─────────── From ScholaAgent!

       → Observation Vector: [100-dim float array]

5. Schola Brain sends to Policy

   Training Mode:
   └─> Python RLlib receives observation
       └─> Neural Network processes
           └─> Returns: EQS Weights [8-dim]

   Inference Mode:
   └─> Local ONNX Model processes
       └─> Returns: EQS Weights [8-dim]

   Example Output:
   [
     0.85,  // CoverPreference      (high - defensive)
     0.30,  // DistanceToEnemy      (close - hold ground)
     0.60,  // TeamSpread           (moderate)
     0.90,  // ObjectiveProximity   (high - defend objective)
     0.20,  // Aggression           (low - defensive)
     0.40,  // FlankingAngle        (moderate)
     0.50,  // VisibilityToEnemies  (moderate)
     0.95   // PathToObjective      (high - prioritize)
   ]

─────────────────────────────────────────────────────────────────────
Time: t=0.15s (Act Phase)
─────────────────────────────────────────────────────────────────────

6. TacticalParameterActuator::TakeAction(EQS_Weights)
   └─> Apply weights to EQS system

       MocAIController::SetEQSWeights(Weights)
       └─> Update weight parameters in EQS context

7. EQS Query Execution
   ├─ Generate 48 candidate positions (6m radius)
   ├─ Evaluate each with 8 weighted tests:
   │   Test 1: CoverPreference (weight=0.85) → Prefers positions with cover
   │   Test 2: DistanceToEnemy (weight=0.30) → Prefers closer positions
   │   Test 3: TeamSpread (weight=0.60) → Moderate spacing
   │   Test 4: ObjectiveProximity (weight=0.90) → Very close to objective
   │   Test 5: Aggression (weight=0.20) → Avoids aggressive positions
   │   Test 6: FlankingAngle (weight=0.40) → Some flanking
   │   Test 7: VisibilityToEnemies (weight=0.50) → Moderate visibility
   │   Test 8: PathToObjective (weight=0.95) → Must have clear path
   ├─ Calculate weighted scores for all positions
   └─> Best Position: (1150, 780, 0) - behind cover near objective

8. Navigation
   └─> UE5 NavMesh pathfinding
       └─> Character moves to position

─────────────────────────────────────────────────────────────────────
Time: t=0.5s (Next Planning Cycle)
─────────────────────────────────────────────────────────────────────

9. SquadManager updates again (0.5s interval or event-driven)
   └─> Repeat from step 1...
```

---

## Mode Comparison

### Training Mode (Python RLlib)
```
[Observation] → [Python/RLlib] → [EQS Weights] → [EQS] → [Navigation]
                     ↑                                ↓
              [Experience Buffer]              [Logging]
                     ↓
              [Policy Update]
```

**Characteristics:**
- Policy runs in Python (RLlib)
- Data logged to ScholaEnvironment
- Slow but learning
- Used for training new behaviors

### Inference Mode (Local ONNX)
```
[Observation] → [ONNX Model] → [EQS Weights] → [EQS] → [Navigation]
                    Fast
```

**Characteristics:**
- Policy runs locally (ONNX)
- No logging needed
- Fast (~2ms inference)
- Used for deployment/competition

---

## Key Design Principles

1. **Single Responsibility**
   - Each component does ONE thing well
   - No overlap between systems

2. **Dependency Inversion**
   - Components depend on interfaces, not concrete implementations
   - Easy to swap Policy/Observers/Actuators

3. **Command Pattern**
   - SquadManager commands, agents execute
   - Clean separation of planning and execution

4. **Observer Pattern**
   - Observers passively collect state
   - No side effects during observation

5. **Strategy Pattern**
   - Different policies for different modes
   - Runtime mode switching

6. **Delegation over Implementation**
   - ScholaMocAgent delegates to parent class
   - Minimal custom logic

---

## Testing Strategy

### Unit Tests
```cpp
// Test 1: Strategy propagation
SquadManager->SetAgentStrategy(0, EStrategyType::Assault);
Character->SetCommandedStrategy(EStrategyType::Assault);
ASSERT_EQ(ScholaAgent->GetCommandedStrategy(), EStrategyType::Assault);

// Test 2: Observer includes strategy
TArray<float> Obs = Observer->CollectObservation();
ASSERT_TRUE(ContainsStrategy(Obs, EStrategyType::Assault));

// Test 3: Policy responds to strategy
TArray<float> Weights = Policy->Decide(Obs);
ASSERT_EQ(Weights.Num(), 8);
ASSERT_TRUE(IsDefensiveWeights(Weights, EStrategyType::Defend));
```

### Integration Tests
```cpp
// Test end-to-end flow
SquadManager->PerformTacticalPlanning();
Wait(0.2s); // Allow Think-Act cycle
ASSERT_TRUE(AgentMovedTowardObjective(Character));
ASSERT_TRUE(UsedCoverAppropriately(Character));
```

### Performance Tests
```cpp
// Test computational budget
float MCTSTime = MeasureTime([]{ SquadManager->PerformTacticalPlanning(); });
ASSERT_LT(MCTSTime, 15.0f); // Must be under 15ms

float PolicyTime = MeasureTime([]{ Policy->Decide(Obs); });
ASSERT_LT(PolicyTime, 2.0f); // Must be under 2ms
```

---

## Common Pitfalls to Avoid

❌ **Don't add MCTS back to agents**
```cpp
// BAD: Violates v10.2 architecture
class UScholaMocAgent {
    UTeamMCTS* MCTSAlgorithm; // ❌ Should be in SquadManager only
};
```

❌ **Don't bypass Schola pipeline**
```cpp
// BAD: Bypasses actuators
void UScholaMocAgent::ExecuteOption(const FTacticalOption& Option) {
    Character->MoveToLocation(Option.Position); // ❌ Should use actuators
}
```

❌ **Don't mix responsibilities**
```cpp
// BAD: Agent shouldn't do logging
class UScholaMocAgent {
    UScholaTransitionLogger* Logger; // ❌ Should be in Environment
    void LogTransition(); // ❌ Not agent's job
};
```

✅ **Do keep it minimal**
```cpp
// GOOD: Thin integration layer
class UScholaMocAgent {
    void UpdateCommandedStrategy(EStrategyType);
    EStrategyType GetCommandedStrategy() const;
private:
    EStrategyType CommandedStrategy;
};
```

---

## Summary

The refactored architecture achieves:
- ✅ **Clean separation** of centralized planning (SquadManager) and decentralized execution (Agents)
- ✅ **Proper Schola integration** using parent class features
- ✅ **v10.2 compatibility** with command-driven architecture
- ✅ **Minimal agent code** - thin integration layer only
- ✅ **No duplicate responsibilities** - each component has one job
- ✅ **Easy to test** - clear interfaces and dependencies
- ✅ **Easy to extend** - add new strategies/observers/actuators without changing core

The ScholaMocAgent is now a **lightweight bridge** between the centralized commander and the Schola framework, doing exactly what it should and nothing more.
