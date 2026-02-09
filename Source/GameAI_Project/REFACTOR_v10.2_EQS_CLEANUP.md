# MOC v10.2 Refactoring: EQS Weight Applicator Cleanup

**Date:** 2026-02-10
**Scope:** Remove v10.1 legacy code and consolidate EQS query creation
**Status:** ✅ Complete

---

## Executive Summary

Removed unnecessary abstraction layer (`UEQSDynamicWeightApplicator`) and v10.1 MCTS-related code from the executor layer. Consolidated EQS query creation into a single inline method in `AMocAIController`.

### Key Benefits
- **Reduced complexity:** Removed 65-line wrapper class that added no value
- **Fixed bug:** Eliminated `GetOwner()` call on UObject (compilation error)
- **Eliminated duplication:** Consolidated two identical EQS query creation methods
- **Cleaner v10.2 architecture:** Removed MCTS cache and v10.1 tactical option handling

---

## Changes Made

### 1. **Deleted Files**
- ❌ `Public/AI/EQS/EQSDynamicWeightApplicator.h` (65 lines removed)

### 2. **Modified: `AMocAIController`** (`Public/AI/AIController/MocAIController.h`)

**Removed:**
- Forward declaration: `class UEQSDynamicWeightApplicator`
- Forward declaration: `class FTeamTreeNode`
- Component: `UEQSDynamicWeightApplicator* EQSApplicator`
- Method: `UpdateBlackboard(const FTacticalOption&, const FEQSWeightParameters&)`
- State: `TSharedPtr<FTeamTreeNode> CachedPlanRoot` (v10.1 MCTS cache)

**Added/Updated:**
- Inlined `CreateDynamicEQSQuery()` implementation (47 lines)
- Updated class documentation to reflect v10.2 executor role
- Added normalization logic directly in method ([-1,1] → [-2,2])

**Before:**
```cpp
// Header declaration only
FEnvQueryRequest CreateDynamicEQSQuery(const FEQSWeightParameters& Weights);

// Required external EQSApplicator component
UEQSDynamicWeightApplicator* EQSApplicator;
```

**After:**
```cpp
// Fully inline implementation
FEnvQueryRequest CreateDynamicEQSQuery(const FEQSWeightParameters& Weights) const
{
    if (!EQS_TacticalMovement) return FEnvQueryRequest();

    FEnvQueryRequest QueryRequest(EQS_TacticalMovement, this);

    auto Normalize = [](float RLOutput) {
        return FMath::Clamp(RLOutput * 2.0f, -2.0f, 2.0f);
    };

    QueryRequest.SetFloatParam(TEXT("EnemyObjectiveWeight"),
        Normalize(Weights.EnemyObjectiveProximity));
    // ... 7 more parameters

    return QueryRequest;
}
```

### 3. **Modified: `MocAIController.cpp`** (`Private/AI/AIController/MocAIController.cpp`)

**Removed:**
- `#include "AI/EQS/EQSDynamicWeightApplicator.h"`
- Component creation: `EQSApplicator = CreateDefaultSubobject<UEQSDynamicWeightApplicator>(...)`
- Method implementation: `UpdateBlackboard(FTacticalOption, FEQSWeightParameters)` (14 lines)

**Updated:**
- `OnPossess()`: Direct Blackboard update instead of calling `UpdateBlackboard()`
- Comments: Added note about EQSDynamicWeightApplicator removal

### 4. **Modified: `BTTask_MoveToEQSLocation.h`** (`Public/AI/BT/Tasks/BTTask_MoveToEQSLocation.h`)

**Removed:**
- `#include "AI/EQS/EQSDynamicWeightApplicator.h"`
- Member variable: `UEQSDynamicWeightApplicator* WeightApplicator`
- Member variable: `UEnvQuery* EQS_TacticalMovement` (unused)

**Updated:**
- `ExecuteTask()`: Now reads weights from Blackboard and calls `AIController->CreateDynamicEQSQuery()`
- `OnQueryFinished()`: Added proper `FinishLatentTask()` handling
- Added member: `UBehaviorTreeComponent* CachedOwnerComp` for async callback

**Before:**
```cpp
// Required WeightApplicator component
FEnvQueryRequest QueryRequest = WeightApplicator->CreateDynamicQuery(
    EQS_TacticalMovement, RLWeights
);
```

**After:**
```cpp
// Direct controller method call
FEQSWeightParameters Weights;
Weights.EnemyObjectiveProximity = BB->GetValueAsFloat(TEXT("Weight_EnemyObj"));
// ... read 7 more weights from Blackboard

FEnvQueryRequest QueryRequest = AIController->CreateDynamicEQSQuery(Weights);
```

---

## Architecture Alignment (v10.2)

### Before (v10.1 Legacy):
```
AMocAIController
├── MCTSPlanner (removed ✓)
├── WorldModel (removed ✓)
├── EQSApplicator (removed ✓)  ← Unnecessary wrapper
└── CachedPlanRoot (removed ✓) ← MCTS cache
```

### After (v10.2 Clean):
```
AMocAIController (Executor Layer)
├── PolicyExecutor → Generates EQS weights
└── CreateDynamicEQSQuery() → Direct EQS query creation
    ↓
ASquadManager (Commander Layer)
├── TeamMCTSPlanner → Centralized planning
└── TeamWorldModel → Global state management
```

---

## Issues Fixed

### 🐛 **Bug: `GetOwner()` on UObject**
**File:** `EQSDynamicWeightApplicator.h:26`

**Problem:**
```cpp
FEnvQueryRequest QueryRequest(QueryTemplate, GetOwner());
                                              ^^^^^^^^^
// ERROR: UObject doesn't have GetOwner() method
```

**Root Cause:**
- UObjects have `GetOuter()`, not `GetOwner()`
- The class was created as a subobject but never properly tested
- This would have caused compilation errors when actually used

**Resolution:** Class deleted entirely

### 🔁 **Duplicate Functionality**

**Two methods doing the same thing:**
1. `MocAIController::CreateDynamicEQSQuery()` (used by `BTTask_RunDynamicEQS`)
2. `EQSDynamicWeightApplicator::CreateDynamicQuery()` (used by `BTTask_MoveToEQSLocation`)

**Resolution:** Consolidated into single inline method in controller

### 🧹 **v10.1 Legacy Cleanup**

**Removed obsolete v10.1 concepts:**
- `CachedPlanRoot` - Never used, MCTS is centralized in v10.2
- `UpdateBlackboard(FTacticalOption)` - v10.1 concept, v10.2 uses commanded strategies
- Individual MCTS planning - Now centralized in `ASquadManager`

---

## Testing Checklist

- [ ] Verify compilation (no `EQSDynamicWeightApplicator` references)
- [ ] Test `BTTask_RunDynamicEQS` (uses `CreateDynamicEQSQuery` via Blackboard)
- [ ] Test `BTTask_MoveToEQSLocation` (updated to use controller method)
- [ ] Verify EQS weights flow: PolicyExecutor → Blackboard → BT Tasks
- [ ] Confirm no runtime errors from removed components

---

## Code Metrics

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Files | 4 | 3 | -1 (deleted) |
| Total Lines | ~400 | ~335 | -65 lines |
| Component Count (AMocAIController) | 5 | 4 | -1 |
| EQS Query Creation Methods | 2 | 1 | Consolidated |
| v10.1 Legacy Code | Yes | No | ✅ Clean |

---

## Migration Notes

### For Developers Using These Classes:

**If you were using `EQSDynamicWeightApplicator`:**
```cpp
// OLD (v10.1):
UEQSDynamicWeightApplicator* Applicator = ...;
FEnvQueryRequest Query = Applicator->CreateDynamicQuery(Template, Weights);

// NEW (v10.2):
AMocAIController* AIController = ...;
FEnvQueryRequest Query = AIController->CreateDynamicEQSQuery(Weights);
```

**If you were caching MCTS plans:**
```cpp
// OLD (v10.1):
TSharedPtr<FTeamTreeNode> CachedPlanRoot = AIController->CachedPlanRoot;

// NEW (v10.2):
// Planning is centralized in ASquadManager
ASquadManager* Commander = TeamManager->GetSquadCommander();
// Access centralized planning through Commander
```

**If you were calling `UpdateBlackboard(FTacticalOption, Weights)`:**
```cpp
// OLD (v10.1):
AIController->UpdateBlackboard(TacticalOption, Weights);

// NEW (v10.2):
// Set strategy directly
BlackboardComp->SetValueAsEnum(TEXT("CurrentStrategy"),
    static_cast<uint8>(CommandedStrategy));
AIController->UpdateBlackboardWeights(Weights);
```

---

## Next Steps

1. **Compile and test** the refactored code
2. **Update documentation** referencing `EQSDynamicWeightApplicator`
3. **Train RL policies** that work with v10.2 commanded strategies
4. **Integrate with `ASquadManager`** centralized planning

---

## Related Documents
- `CLAUDE.md` - v10.2 architecture overview
- `v10.2Architecture.md` - Centralized Commander-Executor design
- `WEEK3_IMPLEMENTATION_COMPLETE.md` - Previous v10.2 implementation

---

**Refactored by:** Claude Code
**Reviewed by:** [Pending]
**Approved by:** [Pending]
