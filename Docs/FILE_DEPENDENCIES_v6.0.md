# CORTEX v6.0 File Dependencies

## ✅ Required Files (DO NOT DELETE)

### Core Observation Files
| File | Purpose | Used By |
|------|---------|---------|
| **ObservationElement.h/cpp** | Individual agent observation (68 features) | RL Policy, StateTree, TacticalObserver |
| **ObservationTypes.h** | Base types: ERaycastHitType, FEnemyObservation | ObservationElement.h |
| **TeamObservation.h/cpp** | Team-level observation for MCTS coordination | TeamLeaderComponent, MCTS |
| **TeamObservationTypes.h** | Team types: EEngagementRange, EMissionPhase | TeamObservation.h |

### Dependency Graph
```
ObservationElement.h
  ├─ includes: ObservationTypes.h (ERaycastHitType, FEnemyObservation)
  └─ includes: RLTypes.h (FObjectiveContext)

RLTypes.h
  ├─ forward declares: FObservationElement (avoids circular dependency)
  └─ defines: FObjectiveContext, FObjectiveAssignment

TeamObservation.h
  ├─ includes: ObservationElement.h (FObservationElement)
  ├─ includes: TeamObservationTypes.h (EEngagementRange, EMissionPhase)
  └─ used by: TeamLeaderComponent, MCTS
```

## 🔧 Circular Dependency Fix (Applied)

**Problem:**
```cpp
// BEFORE (Circular Dependency)
ObservationElement.h → includes RLTypes.h
RLTypes.h → includes ObservationElement.h
❌ COMPILE ERROR
```

**Solution:**
```cpp
// AFTER (Forward Declaration)
// RLTypes.h
struct FObservationElement;  // Forward declaration only

// ObservationElement.h
#include "RL/RLTypes.h"  // Full include
✅ COMPILES
```

**Changed File:**
- `Source/GameAI_Project/Public/RL/RLTypes.h` (line 6)
  - Removed: `#include "Observation/ObservationElement.h"`
  - Added: `struct FObservationElement;` (forward declaration)

## 📊 File Usage in v6.0 Architecture

### ObservationElement.h (68 features)
**Used by:**
- `RLPolicyNetwork::GetStrategy()` - Needs full observation
- `TacticalObserver::BuildObservation()` - Constructs observation
- `StateTree Tasks` - Reads observation for decision-making
- `TeamObservation` - Aggregates individual observations

**Features:**
```cpp
Agent State (7)
+ Combat (1)
+ Perception (32)
+ Enemy Info (16)
+ Tactical (4)
+ Support Context (4)
+ Objective Context (4)  // v6.0 NEW
= 68 total features
```

### ObservationTypes.h
**Used by:**
- `ObservationElement.h` - ERaycastHitType, FEnemyObservation
- `TacticalObserver.cpp` - Raycast hit type detection

**Defines:**
- `ERaycastHitType` - 8 types (None, Wall, Enemy, Ally, Cover, HealthPack, Weapon, Other)
- `FEnemyObservation` - 3 features per enemy (Distance, Health, RelativeAngle)

### TeamObservation.h
**Used by:**
- `TeamLeaderComponent` - MCTS coordination
- `MCTS::EvaluateAssignment()` - Team-level evaluation
- `CurriculumManager` - Training scenarios

**Features:**
```cpp
Team Composition (6)
+ Team Formation (9)
+ Enemy Intelligence (12)
+ Tactical Situation (8)
+ Mission Context (5)
+ Individual Followers (N × 68)
= 40 + (N × 68) features
```

### TeamObservationTypes.h
**Used by:**
- `TeamObservation.h` - EEngagementRange, EMissionPhase
- May be used by `CurriculumManager` for scenario classification

**Defines:**
- `EEngagementRange` - 5 ranges (VeryClose to VeryLong)
- `EMissionPhase` - 6 phases (Preparation to Failed)

## 🚫 NOT Needed (if any)

**None** - All observation files are required for v6.0 architecture!

## ✅ Compilation Order

To avoid errors, compile in this order:
```
1. ObservationTypes.h (base types)
2. TeamObservationTypes.h (base types)
3. RLTypes.h (forward declares FObservationElement)
4. ObservationElement.h (includes RLTypes.h)
5. TeamObservation.h (includes ObservationElement.h)
6. Everything else
```

UE5 build system handles this automatically via the includes, but if you manually compile, follow this order.

## 📝 Summary

**All observation files are REQUIRED for v6.0:**
- ✅ ObservationElement.h/cpp - Individual agent observations
- ✅ ObservationTypes.h - Base observation types
- ✅ TeamObservation.h/cpp - Team-level observations
- ✅ TeamObservationTypes.h - Team-level types

**Circular dependency FIXED:**
- ✅ RLTypes.h now uses forward declaration instead of full include
- ✅ Compilation should work correctly
