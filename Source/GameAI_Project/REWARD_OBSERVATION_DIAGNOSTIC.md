# Reward & Observation Design Diagnostic

**Date:** 2025-12-06
**Version:** v3.1 Real-Time Training
**Status:** Critical gaps identified in reward shaping for desired behaviors

---

## Training Goals vs. Implementation Status

### Goal 1: Move without firing when enemies are out of sight ⚠️

**Desired Behavior:**
- Agent should move tactically when no enemies visible
- Agent should NOT fire randomly (wastes ammo, reveals position)

**Current Implementation:**

✅ **Observations (COMPLETE):**
- `VisibleEnemyCount` (ObservationElement.cpp:81) - Agent knows if enemies are visible
- `NearbyEnemies` array (ObservationElement.cpp:85) - Distance/angle to up to 5 enemies
- `DistanceToNearestEnemy` (ObservationElement.cpp:61) - Proximity metric

❌ **Rewards (INCOMPLETE):**
- **Missing:** No penalty for firing when `VisibleEnemyCount == 0`
- **Current:** Agent can learn this indirectly via lack of damage rewards, but no explicit shaping
- **Problem:** Agent may develop "spray and pray" behavior early in training

**Implementation Gap:**
```cpp
// RewardCalculator.cpp (Missing)
float URewardCalculator::CalculateIndividualReward()
{
    float Reward = AccumulatedIndividualReward;

    // ADD THIS:
    // Penalty for wasted ammo (firing with no visible targets)
    if (bFiredThisTick && FollowerComponent->GetLocalObservation().VisibleEnemyCount == 0)
    {
        Reward -= 1.0f; // -1 per wasted shot
    }

    // Existing rewards...
}
```

**Required Changes:**
1. Track `bFiredThisTick` flag in RewardCalculator
2. Add wasted ammo penalty in `CalculateIndividualReward()`
3. Reset flag each tick

**Priority:** Medium (agent may learn without this, but training will be slower)

---

### Goal 2: Aim and fire when encountering enemies ✅

**Desired Behavior:**
- Agent should rotate to face visible enemies
- Agent should fire when aimed at enemy

**Current Implementation:**

✅ **Observations (COMPLETE):**
- `NearbyEnemies[i].RelativeAngle` (ObservationTypes.h) - Direction to each enemy
- `NearbyEnemies[i].Distance` - Range to target
- `NearbyEnemies[i].HealthPercent` - Target priority info
- `WeaponCooldown` - Agent knows when weapon is ready

✅ **Actions (COMPLETE):**
- `LookDirection` [3-4] - 2D aiming control
- `bFire` [5] - Fire trigger
- **FIXED (2025-12-06):** Look action now uses **agent-relative** coordinates (was world-based)

✅ **Rewards (COMPLETE):**
- `+5` per 100 damage dealt (RewardCalculator.cpp:97)
- `+10` per kill (RewardCalculator.cpp:96)
- `+15` kill during strategic command (on-objective bonus) (RewardCalculator.cpp:237)

**Status:** FULLY SUPPORTED (after look action fix)

---

### Goal 3: Hide behind cover ✅

**Desired Behavior:**
- Agent should move to cover when under fire
- Agent should crouch in cover for better protection
- Agent should avoid staying exposed when enemies are visible

**Current Implementation:**

✅ **Observations (COMPLETE):**
- `bHasCover` (ObservationElement.cpp:93) - Binary cover availability
- `NearestCoverDistance` (ObservationElement.cpp:97) - Distance to nearest cover
- `CoverDirection` (ObservationElement.cpp:101) - 2D direction vector to cover
- `RaycastHitTypes[16]` (ObservationElement.cpp:73) - 360° obstacle detection

✅ **Actions (COMPLETE):**
- `MoveDirection` [0-1] - 2D movement control
- `bCrouch` [6] - Crouch toggle

✅ **Rewards (IMPLEMENTED):**
```cpp
// RewardCalculator.h (Current values)
float CoverUnderFireReward = 10.0f;     // In cover while taking damage (RewardCalculator.cpp:218)
float ExposedPenalty = -5.0f;           // Exposed with enemies visible (RewardCalculator.cpp:224)
float CrouchInCoverReward = 5.0f;       // Crouching in cover (RewardCalculator.cpp:231)
```

**Implemented Cover Behaviors:**

| Behavior | Reward | Implementation |
|----------|--------|----------------|
| In cover while under fire | +10.0 | RewardCalculator.cpp:216-219 |
| Exposed with enemies nearby | -5.0 | RewardCalculator.cpp:222-225 |
| Crouching in cover | +5.0 | RewardCalculator.cpp:227-232 |
| Moving toward cover when under fire | Up to +3.0 | RewardCalculator.cpp:234-247 |

**Balance Analysis:**

| Scenario | Cover Reward | Combat Reward | Total |
|----------|--------------|---------------|-------|
| Kill enemy while exposed | -5 (exposed) | +10 (kill) | +5 |
| Hide in cover, survive | +10 (cover) + 5 (crouch) | 0 | +15 |
| Kill from cover | +10 (cover) + 5 (crouch) | +10 (kill) | +25 |

**Status:** Cover rewards are balanced with combat rewards, encouraging tactical defensive behavior while rewarding successful engagement from protected positions.

**Priority:** ✅ Complete

---

### Goal 4: Follow MCTS strategy (assault = close distance, retreat = increase distance) ❌

**Desired Behavior:**
- **Eliminate objective:** Agent should move toward `TargetActor` and engage
- **DefendObjective:** Agent should stay within `ObjectiveRadiusThreshold` of `TargetLocation`
- **Retreat:** Agent should increase distance from enemies
- **SupportAlly:** Agent should move toward ally and provide covering fire
- Penalty for disobeying strategic commands

**Current Implementation:**

✅ **Observations (COMPLETE):**
- `CurrentObjective->Type` (Objective.h:113) - Agent knows MCTS strategic command
- `CurrentObjective->TargetLocation` (Objective.h:119) - Where to go
- `CurrentObjective->TargetActor` (Objective.h:116) - What to target
- Agent position/rotation in observation

✅ **Objective Context:**
- Objective types: Eliminate, CaptureObjective, DefendObjective, SupportAlly, FormationMove, Retreat, RescueAlly
- Objective info passed to policy network as 7-element one-hot encoding (RLTypes.h:147)

❌ **Rewards (NOT IMPLEMENTED):**
```cpp
// RewardCalculator.h:34 (Exists but never triggered)
// Coordination bonuses:
+15  Kill during strategic command
+10  Combined action (crossfire, covering)
+5   Formation maintenance
-15  Disobey strategic command  // ← THIS IS NEVER ACTIVATED
```

**Critical Gap:**

`bDisobeyedObjective` flag is defined (RewardCalculator.h:198) but **NEVER set to true** anywhere in the codebase.

**Search Results:**
```bash
$ grep -r "bDisobeyedObjective = true" Source/
# No matches found
```

**Missing Logic:**

No code exists to:
1. Monitor agent behavior relative to strategic command
2. Detect when behavior contradicts objective
3. Set `bDisobeyedObjective = true` when violation occurs

**Implementation Required:**

```cpp
// RewardCalculator.cpp (NEW METHOD)
void URewardCalculator::CheckObjectiveCompliance()
{
    if (!CurrentObjective || !FollowerComponent)
    {
        return;
    }

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    FVector AgentLocation = Owner->GetActorLocation();
    FObservationElement Obs = FollowerComponent->GetLocalObservation();

    switch (CurrentObjective->Type)
    {
        case EObjectiveType::Eliminate:
        {
            // Disobey if moving AWAY from target enemy
            if (CurrentObjective->TargetActor)
            {
                FVector ToTarget = CurrentObjective->TargetActor->GetActorLocation() - AgentLocation;
                FVector Velocity = Owner->GetVelocity();

                if (Velocity.SizeSquared() > 100.0f) // Agent is moving
                {
                    float Alignment = FVector::DotProduct(Velocity.GetSafeNormal(), ToTarget.GetSafeNormal());
                    if (Alignment < -0.5f) // Moving away
                    {
                        bDisobeyedObjective = true;
                    }
                }
            }
            break;
        }

        case EObjectiveType::DefendObjective:
        case EObjectiveType::CaptureObjective:
        {
            // Disobey if leaving objective zone
            float DistToObjective = FVector::Dist(AgentLocation, CurrentObjective->TargetLocation);
            if (DistToObjective > ObjectiveRadiusThreshold * 1.5f) // 50% buffer
            {
                bDisobeyedObjective = true;
            }
            break;
        }

        case EObjectiveType::Retreat:
        {
            // Disobey if moving TOWARD enemies instead of away
            if (Obs.VisibleEnemyCount > 0 && Obs.NearbyEnemies.Num() > 0)
            {
                FVector ToEnemy = FVector(Obs.NearbyEnemies[0].RelativeAngle, 0.0f, 0.0f); // Simplified
                FVector Velocity = Owner->GetVelocity();

                if (Velocity.SizeSquared() > 100.0f)
                {
                    float Alignment = FVector::DotProduct(Velocity.GetSafeNormal(), ToEnemy.GetSafeNormal());
                    if (Alignment > 0.5f) // Moving toward enemies
                    {
                        bDisobeyedObjective = true;
                    }
                }
            }
            break;
        }

        case EObjectiveType::SupportAlly:
        {
            // Disobey if NOT moving toward ally or NOT providing cover
            if (CurrentObjective->TargetActor)
            {
                float DistToAlly = FVector::Dist(AgentLocation, CurrentObjective->TargetActor->GetActorLocation());
                if (DistToAlly > 3000.0f && !FollowerComponent->LastTacticalAction.bFire)
                {
                    // Too far from ally and not providing covering fire
                    bDisobeyedObjective = true;
                }
            }
            break;
        }

        case EObjectiveType::FormationMove:
        {
            // Disobey if breaking formation (checked via IsInFormation)
            if (!IsInFormation())
            {
                bDisobeyedObjective = true;
            }
            break;
        }

        default:
            break;
    }
}
```

**Integration Point:**

Call `CheckObjectiveCompliance()` in `URewardCalculator::TickComponent()`:

```cpp
// RewardCalculator.cpp:41-50 (Modified)
void URewardCalculator::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Clean up old combined fire records
    float CurrentTime = GetWorld()->GetTimeSeconds();
    RecentCombinedFires.RemoveAll([CurrentTime, this](const FCombinedFireRecord& Record) {
        return (CurrentTime - Record.Timestamp) > CombinedFireWindow;
    });

    // ADD THIS: Check objective compliance
    CheckObjectiveCompliance();
}
```

**Priority:** CRITICAL (this is core MCTS-RL alignment)

---

## Summary: Implementation Checklist

### Critical Priority (Blocking MCTS-RL alignment)
- [ ] **Implement `CheckObjectiveCompliance()` method** (RewardCalculator.cpp)
  - Detect disobedience for each objective type
  - Set `bDisobeyedObjective` flag when violated
- [ ] **Call compliance check in TickComponent()** (RewardCalculator.cpp:50)

### High Priority (Weak tactical shaping)
- [x] **Increase cover reward magnitudes** (RewardCalculator.h:156-164) ✅ COMPLETE
  - `CoverUnderFireReward: 10.0f` (implemented)
  - `ExposedPenalty: -5.0f` (implemented)
  - `CrouchInCoverReward: 5.0f` (implemented)
- [x] **Add reward for moving toward cover when under fire** (RewardCalculator.cpp:234-247) ✅ COMPLETE

### Medium Priority (Faster convergence)
- [ ] **Add wasted ammo penalty** (RewardCalculator.cpp:CalculateIndividualReward)
  - Track `bFiredThisTick` flag
  - Penalize firing when `VisibleEnemyCount == 0`

---

## Testing Plan

### Phase 1: Verify Objective Compliance Detection
1. Spawn agent with Eliminate objective
2. Manually move agent away from target → Expect `-15` penalty in logs
3. Move agent toward target → Penalty should stop

### Phase 2: Verify Cover Behavior Shaping
1. Spawn agent under enemy fire (no cover) → Should take damage, get exposed penalty
2. Place cover nearby → Agent should move toward cover (test new reward)
3. Agent reaches cover → Should crouch (test increased crouch reward)

### Phase 3: Integration Test
1. Run 100-episode training session
2. Monitor TensorBoard metrics:
   - `reward/disobey_penalty` (should decrease over episodes)
   - `reward/cover_usage` (should increase)
   - `reward/wasted_ammo` (should decrease)
3. Manual observation: Agents should follow MCTS commands more consistently

---

## Relevant Files

| Component | File | Line(s) |
|-----------|------|---------|
| Reward weights | `RL/RewardCalculator.h` | 152-161 |
| Individual rewards | `RL/RewardCalculator.cpp` | 91-101 |
| Coordination rewards | `RL/RewardCalculator.cpp` | 103-131 |
| Cover rewards | `RL/RewardCalculator.cpp` | 182-224 |
| Disobey flag | `RL/RewardCalculator.h` | 198 |
| Observation features | `Observation/ObservationElement.cpp` | 3-108 |
| Objective types | `Team/Objective.h` | 14-24 |

---

## Expected Behavior After Fixes

### Before Fixes (Current):
- Agents ignore MCTS retreat commands, rush toward enemies
- Agents fire randomly even when no enemies visible
- Agents prefer standing exposed (combat rewards > cover rewards)
- Training slow due to weak reward shaping

### After Fixes (Expected):
- **Assault:** Agents close distance to target, engage aggressively
- **Defend:** Agents hold objective zone, fire from cover
- **Retreat:** Agents fall back, increase distance from enemies
- **No enemies:** Agents move tactically, conserve ammo
- **Under fire:** Agents seek cover, crouch, return fire from protection
- Training faster due to explicit reward shaping for desired behaviors

---

**Document Version:** 1.0
**Last Updated:** 2025-12-06
