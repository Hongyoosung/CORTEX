# EQS Objective Setup - Testing vs Training

**Key Point:** Your project has an existing `UObjective` system (see `Team/Objective.h`). Objectives are **different** for testing and training.

---

## Understanding the Two Contexts

### 1. **EQS Testing Context** (Manual Testing in Editor)
When you place an EQS Testing Pawn to visualize queries, you need a **simple placeholder actor** to represent the objective location.

**Purpose:** Visual debugging only - see where EQS thinks agents should go

### 2. **Runtime/Training Context** (Actual Gameplay)
When your FollowerAgent runs with RLlib training, it uses `SharedContext.CurrentObjective` which is automatically set by your `TeamLeaderComponent` or game mode.

**Purpose:** Real tactical decision-making during gameplay/training

---

## Setup for EQS Testing (Visual Debugging)

### Step 1: Create a Simple Objective Marker Actor

**Option A: Use Existing Actor (Fastest)**
1. Place ANY actor in your test level (e.g., a Cube static mesh, a Sphere, or even a Player Start)
2. Give it a tag: Select actor → Details panel → Tags → Add "ObjectiveMarker"
3. Position it where you want agents to move toward (e.g., 1500 units from EQS Testing Pawn)

**Option B: Create Custom Blueprint (Recommended)**
1. Content Browser → Right-click → Blueprint Class → Actor
2. Name: `BP_ObjectiveMarker`
3. Add components:
   - Static Mesh (use a colored sphere or flag icon)
   - Billboard component (for editor visibility)
4. Place in level and position it

### Step 2: Create EQS_ObjectiveContext Blueprint

**This tells EQS queries where the "objective" is located for testing.**

1. **Create Context Blueprint:**
   - Content Browser → Navigate to `Content/AI/EQS/`
   - Right-click → Blueprint Class → Search "EnvQueryContext"
   - Select `EnvQueryContext_BlueprintBase`
   - Name: `EQS_ObjectiveContext`

2. **Implement Context Logic:**
   - Open `EQS_ObjectiveContext` blueprint
   - Override function: `ProvideLocationsSet` or `ProvideSingleLocation`

   **Blueprint Graph (ProvideLocationsSet):**
   ```
   Event Provide Locations Set
     → Get All Actors with Tag ("ObjectiveMarker")
     → Get (0)  // First actor in array
     → Get Actor Location
     → Make Array
     → Set Resulting Locations
   ```

   **Alternative (ProvideSingleLocation):**
   ```
   Event Provide Single Location
     → Get All Actors with Tag ("ObjectiveMarker")
     → Get (0)
     → Get Actor Location
     → Set Resulting Location
   ```

3. **Assign to EQS Queries:**
   - Open each EQS query (EQS_ForwardCover, etc.)
   - Find Distance Test or Dot Product Test nodes that need objective
   - Set "Distance To" or context parameter = `EQS_ObjectiveContext`

---

## Setup for Runtime/Training (Actual Gameplay)

### Your Current System (Already Working)

**Code Reference:** `STTask_ExecuteObjective.cpp:49-51`
```cpp
FString ObjectiveName = SharedContext.CurrentObjective
    ? UEnum::GetValueAsString(SharedContext.CurrentObjective->Type)
    : TEXT("None");
```

**The objective is automatically populated from:**
1. `TeamLeaderComponent` assigns objectives via MCTS planning
2. Sets `SharedContext.CurrentObjective` (type: `UObjective*`)
3. `UObjective` contains:
   - `TargetActor` (AActor*) - The actual target (enemy, capture zone, etc.)
   - `TargetLocation` (FVector) - Position in world
   - `Type` (EObjectiveType) - Eliminate, Capture, Defend, etc.

### Update EQS_ObjectiveContext for Runtime

**You need to modify the EQS_ObjectiveContext to handle BOTH cases:**

**Enhanced Blueprint (ProvideLocationsSet):**
```
Event Provide Locations Set
  → Get Querier Actor
  → Cast to [Your Follower Pawn Class]
  → Get Follower Agent Component
  → Get Current Objective (from component or SharedContext)

  Branch:
    True (Objective exists):
      → Get Objective.TargetLocation
      → Make Array
      → Set Resulting Locations

    False (Fallback for testing):
      → Get All Actors with Tag ("ObjectiveMarker")
      → Get (0)
      → Get Actor Location
      → Make Array
      → Set Resulting Locations
```

**Alternative (C++ Context - More Performant):**

If you want better performance, create a C++ context class:

**File:** `Source/GameAI_Project/Private/EQS/EnvQueryContext_Objective.h`
```cpp
#pragma once
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_Objective.generated.h"

UCLASS()
class UEnvQueryContext_Objective : public UEnvQueryContext
{
    GENERATED_BODY()

public:
    virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
```

**Implementation:**
```cpp
#include "EnvQueryContext_Objective.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "Team/FollowerAgentComponent.h"
#include "AIController.h"

void UEnvQueryContext_Objective::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
    APawn* Querier = Cast<APawn>(QueryInstance.Owner.Get());
    if (!Querier)
        return;

    // Try to get objective from FollowerAgentComponent
    UFollowerAgentComponent* FollowerComp = Querier->FindComponentByClass<UFollowerAgentComponent>();
    if (FollowerComp && FollowerComp->GetCurrentObjective())
    {
        UObjective* Objective = FollowerComp->GetCurrentObjective();

        // Use TargetLocation if valid, otherwise TargetActor location
        FVector Location = Objective->TargetLocation;
        if (Objective->TargetActor)
        {
            Location = Objective->TargetActor->GetActorLocation();
        }

        ContextData.Locations.Add(Location);
        return;
    }

    // Fallback for testing: Find "ObjectiveMarker" actor
    for (TActorIterator<AActor> It(QueryInstance.World); It; ++It)
    {
        if (It->Tags.Contains("ObjectiveMarker"))
        {
            ContextData.Locations.Add(It->GetActorLocation());
            return;
        }
    }
}
```

---

## Setup for EQS_EnemiesContext

**Similar approach - provide enemy locations from SharedContext.**

### Blueprint Version:
```
Event Provide Locations Set
  → Get Querier Actor
  → Cast to [Your Follower Pawn Class]
  → Get Follower Agent Component
  → Get Visible Enemies (from SharedContext)

  ForEach Enemy:
    → Get Actor Location
    → Add to Array

  → Set Resulting Locations
```

**Code Reference:** `STTask_ExecuteObjective.cpp:597+`
Your code already has `GetEnemyByIndex()` which reads from `SharedContext.VisibleEnemies`.

### C++ Version (Recommended):
```cpp
// EnvQueryContext_VisibleEnemies.cpp
void UEnvQueryContext_VisibleEnemies::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
    APawn* Querier = Cast<APawn>(QueryInstance.Owner.Get());
    if (!Querier)
        return;

    UFollowerAgentComponent* FollowerComp = Querier->FindComponentByClass<UFollowerAgentComponent>();
    if (!FollowerComp)
        return;

    // Get visible enemies from SharedContext
    const TArray<AActor*>& VisibleEnemies = FollowerComp->GetVisibleEnemies();

    for (AActor* Enemy : VisibleEnemies)
    {
        if (Enemy)
        {
            ContextData.Locations.Add(Enemy->GetActorLocation());
        }
    }
}
```

---

## Testing Workflow

### Phase 1: Visual EQS Testing (No FollowerAgent)
1. Place EQS Testing Pawn
2. Place `BP_ObjectiveMarker` actor (with tag "ObjectiveMarker")
3. Place dummy enemy actors (with tag "Enemy") if needed
4. Set EQS Testing Pawn's Query Template
5. PIE and observe green/red spheres

**EQS_ObjectiveContext:** Returns location of "ObjectiveMarker" actor

### Phase 2: Integration Testing (With FollowerAgent)
1. Remove EQS Testing Pawn
2. Place your FollowerAgent pawn
3. Ensure TeamLeaderComponent sets `SharedContext.CurrentObjective`
4. PIE and trigger tactical actions
5. Enable EQS debugging: `` ` eqs debug [AgentName] ``

**EQS_ObjectiveContext:** Returns `SharedContext.CurrentObjective->TargetLocation`

### Phase 3: RLlib Training
1. Run `train_rllib.py`
2. Agents execute macro actions
3. EQS queries use runtime objectives from game logic
4. Monitor logs for EQS errors

---

## Quick Answer to Your Question

**"Should I attach each EQS to 'EQS_Pawn' and see what happens?"**

**Yes!** Use the **EQS Testing Pawn** (built-in UE tool) to test each query:

1. **Place EQS Testing Pawn** in level
2. **Assign Query Template** = EQS_ForwardCover (in Details panel)
3. **Place ObjectiveMarker actor** somewhere in front (1000-1500 units away)
4. **Place static mesh cubes** as cover walls
5. **PIE** and look for:
   - **Green spheres** = good positions (near cover, toward objective)
   - **Red spheres** = bad positions (no cover, wrong direction)
   - **Yellow lines** = trace tests checking cover

**"Is this object actor also used for agent learning?"**

**No, separate contexts:**
- **Testing:** Uses simple marker actor (via tag "ObjectiveMarker")
- **Training:** Uses `SharedContext.CurrentObjective` (set by TeamLeaderComponent)

Your EQS_ObjectiveContext blueprint should handle **both cases** (fallback to marker for testing, use SharedContext for runtime).

---

## Summary Table

| Context | Testing (EQS Testing Pawn) | Runtime (FollowerAgent Training) |
|---------|----------------------------|----------------------------------|
| **Objective Source** | Actor with tag "ObjectiveMarker" | `SharedContext.CurrentObjective->TargetLocation` |
| **Enemy Source** | Actors with tag "Enemy" | `SharedContext.VisibleEnemies` array |
| **Setup Method** | Manually place actors in level | Automatically populated by game logic |
| **Purpose** | Visual debugging of EQS queries | Real tactical AI decision-making |
| **EQS Context** | Blueprint fallback | Runtime code path |

---

## Next Steps

1. **Create BP_ObjectiveMarker** actor (place in test level)
2. **Create EQS_ObjectiveContext** blueprint (with fallback logic)
3. **Create EQS_EnemiesContext** blueprint (with fallback logic)
4. **Test each EQS query** with EQS Testing Pawn (5 minutes per query)
5. **Verify contexts work** by checking console logs (should show objective/enemy locations)
6. **Integrate with FollowerAgent** once visual testing passes

**Your existing `UObjective` system is already set up for training - you just need the EQS contexts to bridge the gap!**
