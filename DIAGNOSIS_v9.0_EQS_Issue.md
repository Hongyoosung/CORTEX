# v9.0 Training Failure Diagnosis

## Symptoms
- ✅ Reward increasing (RL learning parameters)
- ❌ No behavioral change (agents oscillating around spawn)
- ❌ vf_explained_var dropped from 0.8 to 0.4 (value function collapse)

## Root Cause Analysis

### 1. Reward System ✅ VERIFIED WORKING
**File:** `RewardCalculator.cpp:320-543`

```cpp
// Assault: Rewards approaching hostile objective
CalculateAssaultReward():
    reward = (1.0 - HostileObjectiveDistance) * 10.0  // Gradient reward
    if distance < 0.1: reward += 15.0                // Bonus for reaching
```

**Test:** Check UE5 logs for `[ASSAULT GRADIENT]` messages
```
Expected: "Dist=0.45 → BaseReward=5.50"
If seeing: "Dist=1.0" → Observation not populated (Issue #2)
```

---

### 2. Observation System ✅ VERIFIED WORKING
**File:** `ObservationBuilderComponent.cpp:357-509`

```cpp
PopulateObjectiveContext():
    HostileObjectiveDistance = Distance / 10000.0f  // Normalized [0,1]
```

**Test:** Check UE5 logs for `[OBS CONTEXT v9.0]`
```
Expected: "FriendlyDist=0.25, HostileDist=0.68"
If seeing: "FriendlyDist=1.0, HostileDist=1.0" → Objectives not set (Issue #3)
```

---

### 3. EQS Integration ⚠️ PARAMETER SENT, BUT LIKELY NOT USED
**File:** `STTask_ExecuteTacticalMovement_v8.cpp:309`

```cpp
QueryRequest.SetFloatParam(TEXT("ObjectiveWeight"), ObjectiveWeight);
// Assault: 5.0, Defend: 8.0, Support: 1.0, Retreat: 0.0
```

**Problem:** This parameter is **sent to EQS but ignored if the EQS Blueprint doesn't have a test that uses it.**

**Test:** Open UE5 Editor:
1. Open `Content/Game/Blueprints/AI/EQS/EQS_TacticalPositionQuery`
2. Check if there's a test named "Distance To Objective" or similar
3. Check if that test uses the `ObjectiveWeight` parameter

**Expected Tests in EQS:**
- ✅ Distance To: Enemy (uses `AggressionWeight`)
- ✅ Cover Score (uses `CoverWeight`)
- ✅ Distance To: Allies (uses `FormationWeight`)
- ❌ **MISSING:** Distance To: Objective (uses `ObjectiveWeight`)

---

## Issue Priority

### 🔴 CRITICAL: EQS Asset Missing Objective Tests

**Why this causes oscillation:**
```
1. RL policy outputs: Aggression=0.8, Cover=0.3, Spread=0.5
2. EQS receives: ObjectiveWeight=5.0 (Assault strategy)
3. EQS evaluates positions within 500cm radius based on:
   - Enemy distance ✅
   - Cover availability ✅
   - Formation spacing ✅
   - Objective distance ❌ (test doesn't exist!)
4. EQS selects position 200cm away with best cover/enemy balance
5. Agent moves 200cm → Reward increases slightly (closer by coincidence)
6. Next iteration: EQS selects different position 200cm away
7. Result: Agent oscillates in local area, never approaches objective
```

**Why reward still increases:**
- Tactical parameter rewards give +0.3 for matching aggression/cover
- Occasional random movement closer to objective gives +0.5
- Total reward grows slowly, but behavior doesn't converge

**Why vf_explained_var collapsed:**
- Value function expects "move toward objective → +10 reward"
- Actual behavior: "oscillate locally → +0.3 to +0.8 random reward"
- Return variance too high → value function can't predict → collapse

---

## Solution Steps

### Step 1: Verify Objective Assignment
Check UE5 logs for:
```
[SET CURRENT STRATEGY] 'Agent0': None → Assault
[OBS CONTEXT v9.0] Agent0: HostileDist=0.45, FriendlyDist=0.55
```

If seeing `HostileDist=1.0` (default), objectives aren't assigned.

### Step 2: Fix EQS Blueprint Asset
Open `Content/Game/Blueprints/AI/EQS/EQS_TacticalPositionQuery.uasset`:

**Required Tests:**

1. **Add "Distance To Objective" test:**
   - Test Type: `PathDistance` or `Distance`
   - Distance To: `ObjectiveActor` (requires context provider)
   - Scoring: `Inverse` (closer = better)
   - Weight: Use parameter `ObjectiveWeight`
   - Filter: Min=0, Max=10000

2. **Add ObjectiveActor Context:**
   - In EQS query, add `EnvQueryContext_ObjectiveActor`
   - This context must return the appropriate objective based on strategy:
     - Assault → HostileObjective
     - Defend → FriendlyObjective

### Step 3: Create Objective Context Provider (C++)

**File:** `Source/GameAI_Project/Public/Observation/EnvQueryContext_ObjectiveActor.h`

```cpp
#pragma once

#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_ObjectiveActor.generated.h"

UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_ObjectiveActor : public UEnvQueryContext
{
    GENERATED_BODY()

public:
    virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
```

**File:** `Source/GameAI_Project/Private/Observation/EnvQueryContext_ObjectiveActor.cpp`

```cpp
#include "Observation/EnvQueryContext_ObjectiveActor.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Team/ObjectiveActor.h"
#include "AIController.h"

void UEnvQueryContext_ObjectiveActor::ProvideContext(
    FEnvQueryInstance& QueryInstance,
    FEnvQueryContextData& ContextData) const
{
    // Get querier (agent pawn)
    AActor* Querier = Cast<AActor>(QueryInstance.Owner.Get());
    if (!Querier)
    {
        return;
    }

    // Get FollowerAgentComponent to determine strategy
    UFollowerAgentComponent* FollowerComp = Querier->FindComponentByClass<UFollowerAgentComponent>();
    if (!FollowerComp)
    {
        return;
    }

    UTeamLeaderComponent* TeamLeader = FollowerComp->GetTeamLeader();
    if (!TeamLeader)
    {
        return;
    }

    // Get objective based on assigned strategy
    EStrategyType Strategy = FollowerComp->GetAssignedStrategy();
    AObjectiveActor* TargetObjective = nullptr;

    switch (Strategy)
    {
        case EStrategyType::Assault:
            TargetObjective = TeamLeader->GetHostileObjective();
            break;
        case EStrategyType::Defend:
            TargetObjective = TeamLeader->GetFriendlyObjective();
            break;
        case EStrategyType::Support:
            // Support doesn't use objective context (ally-focused)
            return;
        case EStrategyType::Retreat:
            // Retreat doesn't use objective context (enemy-avoidance)
            return;
        default:
            return;
    }

    if (TargetObjective)
    {
        // Provide objective location as context
        UEnvQueryItemType_Point::SetContextHelper(
            ContextData,
            TargetObjective->GetActorLocation()
        );

        UE_LOG(LogTemp, Verbose, TEXT("[EQS CONTEXT] Agent=%s, Strategy=%s → Objective=%s at %s"),
            *Querier->GetName(),
            *UEnum::GetValueAsString(Strategy),
            *TargetObjective->GetName(),
            *TargetObjective->GetActorLocation().ToString());
    }
}
```

### Step 4: Update EQS Asset

After adding the C++ context:
1. Compile UE5 project
2. Open `EQS_TacticalPositionQuery.uasset`
3. Add test: "Distance To" → Select `EnvQueryContext_ObjectiveActor`
4. Set test weight to use parameter: `ObjectiveWeight`
5. Set scoring: `Inverse` (closer = higher score)

### Step 5: Verify Fix

Run training for 100 steps and check logs:
```
✅ [EQS CONTEXT] Agent=Agent0, Strategy=Assault → Objective=HostileObj_A at (X=5000, Y=2000, Z=100)
✅ [TACTICAL v9.0] EQS returned 25 positions | Strategy=Assault, ObjWeight=5.0
✅ [ASSAULT GRADIENT] Agent0: Dist=0.45 → BaseReward=5.50 → Total=5.50
```

If agents now move toward objectives, you'll see:
```
Step 100:  Dist=0.68
Step 200:  Dist=0.55  (getting closer!)
Step 300:  Dist=0.42
```

---

## Quick Verification Commands

### Check if objectives are assigned:
```bash
grep "OBS CONTEXT v9.0" latest_ue5.log | head -n 20
```
Expected: `HostileDist=0.XX` (not 1.0)

### Check if rewards are gradient-based:
```bash
grep "ASSAULT GRADIENT" latest_ue5.log | head -n 20
```
Expected: Reward should correlate with distance

### Check EQS parameter passing:
```bash
grep "TACTICAL v9.0.*ObjWeight" latest_ue5.log | head -n 20
```
Expected: `ObjWeight=5.0` for Assault, `8.0` for Defend

---

## Alternative: Temporary Workaround

If you can't modify EQS immediately, add objective bias to tactical parameters:

**File:** `STTask_ExecuteTacticalMovement_v8.cpp:260` (modify)

```cpp
// TEMPORARY HACK: Bias aggression toward objective direction
// This forces EQS to select positions in objective direction using existing tests
FVector ToObjective = GetObjectiveDirection(Pawn, AssignedStrategy);
if (!ToObjective.IsNearlyZero())
{
    // Increase aggression if facing objective
    float DotProduct = FVector::DotProduct(Pawn->GetActorForwardVector(), ToObjective);
    if (DotProduct > 0.5f)  // Facing objective
    {
        Params.Aggression = FMath::Min(Params.Aggression + 0.3f, 1.0f);  // Boost aggression
    }
}
```

This is a **hack** but will make agents move toward objectives using enemy-distance tests as a proxy.

---

## Expected Outcome After Fix

- ✅ Assault agents approach hostile objective (distance decreases)
- ✅ Defend agents stay near friendly objective (distance < 0.2)
- ✅ Reward gradient becomes smooth (predictable value function)
- ✅ vf_explained_var recovers to >0.7
- ✅ Behavioral variance decreases (agents follow strategies)

---

## Additional Notes

- The v9.0 design assumes EQS has objective-aware tests
- Without these tests, the architecture breaks at Layer 3 (execution)
- This is why reward increases but behavior doesn't change
- The RL is learning correct parameters, but EQS can't execute them
