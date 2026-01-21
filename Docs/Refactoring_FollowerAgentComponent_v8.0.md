# FollowerAgentComponent Refactoring Guide (v8.0)

**Date:** 2026-01-22
**Status:** Implementation Complete
**Breaking Changes:** Minimal (backwards compatible where possible)

---

## Executive Summary

The `FollowerAgentComponent` class has been refactored from a **1,461-line monolithic component** into **5 focused components** following the Single Responsibility Principle. This improves maintainability, testability, and allows for parallel development.

### Before Refactoring:
```
FollowerAgentComponent.cpp: 1,461 lines (12+ responsibilities)
```

### After Refactoring:
```
FollowerAgentComponent.cpp:        ~300 lines (Team coordination & lifecycle)
TacticalStateComponent.cpp:        ~150 lines (State management)
ObservationBuilderComponent.cpp:   ~350 lines (Observation building)
RLAgentComponent.cpp:              ~200 lines (RL & rewards)
CombatExecutorComponent.cpp:       ~300 lines (Combat execution)
----------------------------------------
Total:                             ~1,300 lines (cleaner, more maintainable)
```

---

## New Component Architecture

### 1. **FollowerAgentComponent** (Core Coordinator)
**Responsibilities:**
- Team leader registration/communication
- Component lifecycle (BeginPlay, Tick, EndPlay)
- Strategy assignment reception (v8.0)
- Component coordination
- Main update loop

**Key Methods:**
- `RegisterWithTeamLeader()`
- `UnregisterFromTeamLeader()`
- `SignalEventToLeader()`
- `SetStrategyAssignment()` (delegates to TacticalStateComponent)
- `TickComponent()` (coordinates sub-components)

**File:** `Source/GameAI_Project/Public/Team/FollowerAgentComponent.h`

---

### 2. **TacticalStateComponent** (NEW)
**Responsibilities:**
- Store tactical and combat parameters (from RL)
- Store strategy assignment (from MCTS)
- Manage agent state (alive/dead)
- Provide state queries

**Key Methods:**
- `SetStrategyAssignment(const FStrategyAssignment&)`
- `SetTacticalParameters(const FTacticalParameters&)`
- `SetCombatParameters(const FCombatParameters&)`
- `GetAssignedStrategy()`
- `GetTacticalParameters()`
- `MarkAsDead()` / `MarkAsAlive()`

**File:** `Source/GameAI_Project/Public/Team/TacticalStateComponent.h`

**Usage Example:**
```cpp
// Get tactical state component
UTacticalStateComponent* TacticalState = GetOwner()->FindComponentByClass<UTacticalStateComponent>();

// Query current strategy
EStrategyType Strategy = TacticalState->GetAssignedStrategy();

// Update tactical parameters
FTacticalParameters Params;
Params.Aggression = 0.8f;
Params.CoverPreference = 0.3f;
TacticalState->SetTacticalParameters(Params);
```

---

### 3. **ObservationBuilderComponent** (NEW)
**Responsibilities:**
- Build local observations (71 features)
- Cover detection and caching
- Raycast calculations
- Ally context building

**Key Methods:**
- `BuildLocalObservation()` → Returns `FObservationElement`
- `FindNearestCover(FVector&, float&, TArray<AActor*>&)`
- `GetCachedCoverLocation()`
- `ResetEpisode()`

**File:** `Source/GameAI_Project/Public/Team/ObservationBuilderComponent.h`

**Usage Example:**
```cpp
// Get observation builder
UObservationBuilderComponent* ObsBuilder = GetOwner()->FindComponentByClass<UObservationBuilderComponent>();

// Build observation
FObservationElement Obs = ObsBuilder->BuildLocalObservation();

// Query cover
FVector CoverLoc;
float CoverDist;
if (ObsBuilder->FindNearestCover(CoverLoc, CoverDist, Enemies))
{
    // Move to cover
}
```

---

### 4. **RLAgentComponent** (NEW)
**Responsibilities:**
- Accumulated reward tracking
- Episode management
- Policy network interaction
- Experience collection
- Reward calculator integration

**Key Methods:**
- `ProvideReward(float, bool bTerminal)`
- `GetAccumulatedReward()`
- `ResetEpisode()`
- `OnEpisodeEnded(float)`
- `GetTacticalPolicy()`

**File:** `Source/GameAI_Project/Public/Team/RLAgentComponent.h`

**Usage Example:**
```cpp
// Get RL agent component
URLAgentComponent* RLAgent = GetOwner()->FindComponentByClass<URLAgentComponent>();

// Provide reward
RLAgent->ProvideReward(5.0f, false);

// Check accumulated reward
float TotalReward = RLAgent->GetAccumulatedReward();

// Reset episode
RLAgent->ResetEpisode();
```

---

### 5. **CombatExecutorComponent** (NEW)
**Responsibilities:**
- Execute combat actions using learned target priority
- Target selection (Closest, LowestHP)
- Combat event handling (damage, kill, death)
- Auto-aim and auto-fire

**Key Methods:**
- `ExecuteCombat(const FCombatParameters&)`
- `GetClosestEnemy(const TArray<AActor*>&)`
- `GetLowestHPEnemy(const TArray<AActor*>&)`
- Event handlers: `OnDamageTakenEvent()`, `OnKillEvent()`, `OnDeathEvent()`

**File:** `Source/GameAI_Project/Public/Team/CombatExecutorComponent.h`

**Usage Example:**
```cpp
// Get combat executor
UCombatExecutorComponent* CombatExec = GetOwner()->FindComponentByClass<UCombatExecutorComponent>();

// Execute combat with learned parameters
FCombatParameters Combat;
Combat.Priority = ETargetPriority::LowestHP;
CombatExec->ExecuteCombat(Combat);

// Query target
AActor* Target = CombatExec->GetLowestHPEnemy(Enemies);
```

---

## Migration Guide

### Step 1: Add New Components to Actor Blueprint

In your AI Character Blueprint (or C++ class):

1. Add `TacticalStateComponent`
2. Add `ObservationBuilderComponent`
3. Add `RLAgentComponent`
4. Add `CombatExecutorComponent`

**C++ Example:**
```cpp
// In AGameAICharacter.h
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
UFollowerAgentComponent* FollowerComponent;

UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
UTacticalStateComponent* TacticalState;

UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
UObservationBuilderComponent* ObservationBuilder;

UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
URLAgentComponent* RLAgent;

UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
UCombatExecutorComponent* CombatExecutor;

// In AGameAICharacter.cpp (Constructor)
FollowerComponent = CreateDefaultSubobject<UFollowerAgentComponent>(TEXT("FollowerComponent"));
TacticalState = CreateDefaultSubobject<UTacticalStateComponent>(TEXT("TacticalState"));
ObservationBuilder = CreateDefaultSubobject<UObservationBuilderComponent>(TEXT("ObservationBuilder"));
RLAgent = CreateDefaultSubobject<URLAgentComponent>(TEXT("RLAgent"));
CombatExecutor = CreateDefaultSubobject<UCombatExecutorComponent>(TEXT("CombatExecutor"));
```

### Step 2: Update Code That Accesses FollowerAgentComponent

**Old Code:**
```cpp
UFollowerAgentComponent* Follower = GetOwner()->FindComponentByClass<UFollowerAgentComponent>();
EStrategyType Strategy = Follower->GetAssignedStrategy();
float Reward = Follower->GetAccumulatedReward();
FObservationElement Obs = Follower->BuildLocalObservation();
Follower->ExecuteCombat();
```

**New Code (Delegation Pattern):**
```cpp
// Option 1: Access components directly
UTacticalStateComponent* TacticalState = GetOwner()->FindComponentByClass<UTacticalStateComponent>();
URLAgentComponent* RLAgent = GetOwner()->FindComponentByClass<URLAgentComponent>();
UObservationBuilderComponent* ObsBuilder = GetOwner()->FindComponentByClass<UObservationBuilderComponent>();
UCombatExecutorComponent* CombatExec = GetOwner()->FindComponentByClass<UCombatExecutorComponent>();

EStrategyType Strategy = TacticalState->GetAssignedStrategy();
float Reward = RLAgent->GetAccumulatedReward();
FObservationElement Obs = ObsBuilder->BuildLocalObservation();
CombatExec->ExecuteCombat(TacticalState->GetCombatParameters());

// Option 2: Use FollowerAgentComponent convenience methods (backwards compatible)
UFollowerAgentComponent* Follower = GetOwner()->FindComponentByClass<UFollowerAgentComponent>();
EStrategyType Strategy = Follower->GetAssignedStrategy();  // Delegates to TacticalState
float Reward = Follower->GetAccumulatedReward();          // Delegates to RLAgent
// ... etc
```

### Step 3: Update StateTree Tasks

If you have StateTree tasks that access FollowerAgentComponent:

**Old Code (`STTask_ExecuteTacticalMovement_v8.cpp`):**
```cpp
UFollowerAgentComponent* Follower = Agent->FindComponentByClass<UFollowerAgentComponent>();
FTacticalParameters Params = Follower->GetTacticalParameters();
```

**New Code:**
```cpp
// Option 1: Direct access
UTacticalStateComponent* TacticalState = Agent->FindComponentByClass<UTacticalStateComponent>();
FTacticalParameters Params = TacticalState->GetTacticalParameters();

// Option 2: Backwards compatible (if we keep convenience methods)
UFollowerAgentComponent* Follower = Agent->FindComponentByClass<UFollowerAgentComponent>();
FTacticalParameters Params = Follower->GetTacticalParameters();  // Delegates to TacticalState
```

---

## Benefits

### 1. **Single Responsibility Principle**
Each component has one clear purpose:
- `TacticalStateComponent` → State storage
- `ObservationBuilderComponent` → Observation building
- `RLAgentComponent` → Reinforcement learning
- `CombatExecutorComponent` → Combat execution
- `FollowerAgentComponent` → Coordination

### 2. **Improved Testability**
Components can be tested independently:
```cpp
// Test TacticalStateComponent in isolation
UTacticalStateComponent* State = NewObject<UTacticalStateComponent>();
State->SetStrategyAssignment(TestAssignment);
ASSERT_EQ(State->GetAssignedStrategy(), EStrategyType::Assault);
```

### 3. **Parallel Development**
Different developers can work on different components without conflicts:
- Developer A: Combat system (CombatExecutorComponent)
- Developer B: Observation system (ObservationBuilderComponent)
- Developer C: RL integration (RLAgentComponent)

### 4. **Reusability**
Components can be mixed and matched:
```cpp
// Create a melee-only agent (no ranged combat)
CreateDefaultSubobject<UFollowerAgentComponent>(TEXT("Follower"));
CreateDefaultSubobject<UTacticalStateComponent>(TEXT("TacticalState"));
CreateDefaultSubobject<UMeleeCombatExecutorComponent>(TEXT("MeleeCombat")); // Different combat executor
```

### 5. **Easier Debugging**
Smaller files are easier to navigate and debug:
- 300-line file: Easy to understand
- 1,461-line file: Difficult to navigate

### 6. **Better Performance Profiling**
Can profile each component independently:
```cpp
SCOPE_CYCLE_COUNTER(STAT_ObservationBuilding);
FObservationElement Obs = ObsBuilder->BuildLocalObservation();
```

---

## Backwards Compatibility

To maintain backwards compatibility, `FollowerAgentComponent` can provide convenience methods that delegate to the new components:

```cpp
// In FollowerAgentComponent.h
/** DEPRECATED: Use TacticalStateComponent->GetAssignedStrategy() instead */
UFUNCTION(BlueprintPure, Category = "Follower|Strategy", meta = (DeprecatedFunction))
EStrategyType GetAssignedStrategy() const;

// In FollowerAgentComponent.cpp
EStrategyType UFollowerAgentComponent::GetAssignedStrategy() const
{
    if (TacticalState)
    {
        return TacticalState->GetAssignedStrategy();
    }
    return EStrategyType::Assault; // Fallback
}
```

This allows old code to continue working while you migrate to the new architecture.

---

## File Structure

```
Source/GameAI_Project/
├── Public/Team/
│   ├── FollowerAgentComponent.h           (Coordinator)
│   ├── TacticalStateComponent.h           (NEW - State management)
│   ├── ObservationBuilderComponent.h      (NEW - Observation building)
│   ├── RLAgentComponent.h                 (NEW - RL & rewards)
│   └── CombatExecutorComponent.h          (NEW - Combat execution)
│
└── Private/Team/
    ├── FollowerAgentComponent.cpp         (Refactored)
    ├── TacticalStateComponent.cpp         (NEW)
    ├── ObservationBuilderComponent.cpp    (NEW)
    ├── RLAgentComponent.cpp               (NEW)
    └── CombatExecutorComponent.cpp        (NEW)
```

---

## Testing Checklist

- [ ] Compile project successfully
- [ ] Run existing unit tests (should pass with backwards compatibility)
- [ ] Test strategy assignment (MCTS → TacticalStateComponent)
- [ ] Test observation building (ObservationBuilderComponent)
- [ ] Test RL rewards (RLAgentComponent)
- [ ] Test combat execution (CombatExecutorComponent)
- [ ] Test episode reset (all components reset correctly)
- [ ] Profile performance (should be similar or better)
- [ ] Test in Training_BasicCombat_2v2_v01 map
- [ ] Verify no crashes during episode transitions

---

## Future Enhancements

### v8.5: Learned Aiming
Replace `CombatExecutorComponent` with `LearnedCombatExecutorComponent`:
```cpp
// Adds learned aiming to combat system
class ULearnedCombatExecutorComponent : public UCombatExecutorComponent
{
    virtual void ExecuteCombat(const FCombatParameters&) override;
    FVector2D CalculateAimOffset(AActor* Target);
};
```

### v9.0: Alternative Movement Systems
Replace EQS-based movement with raw movement control:
```cpp
// Replaces tactical movement with learned movement primitives
class URawMovementComponent : public UActorComponent
{
    void ExecuteMovementPrimitive(EMovementPrimitive Primitive);
};
```

---

## Questions & Support

For questions about this refactoring, contact:
- **Architecture:** See `CLAUDE.md` v8.0 documentation
- **Issues:** File a GitHub issue with tag `refactoring`
- **Code Review:** Pull request review by senior engineers required

---

**Status:** ✅ Implementation Complete
**Next Steps:** Update dependent code, run tests, merge to `v8.0-low-level-actions` branch
