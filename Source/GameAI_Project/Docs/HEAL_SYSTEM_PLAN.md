# Heal System Implementation Plan

**Date:** 2026-02-26 | **Version:** v10.2.1 | **Status:** Planning

---

## 1. Problem Statement

The Support strategy's RL policy collapses during training (~660K steps) because it lacks a **causal, dense reward signal**. Currently, support agents only receive proximity-based rewards for being near injured allies — a weak signal that depends on ally behavior the agent cannot control. Unlike Assault (kill enemies) and Defend (hold capture points), Support has no unique *action* that produces a direct, attributable reward.

### Root Cause
- No heal mechanic exists despite the game spec documenting `Heal Ally (+12 reward)`
- Support reward is purely spatial: "walk near hurt teammates"
- The proximity reward is noisy (ally positions change unpredictably) and shallow
- No behavioral differentiation from Defend agents that happen to be near allies

---

## 2. Design Goals

1. Give Support agents a **unique, learnable action** with clear causal reward
2. **Zero changes to the RL action space** — keep 7-dim EQS weights
3. **Minimal UE5 C++ surface area** — extend existing systems, don't create new ones
4. Create a **dense, agent-attributable reward signal** for healing
5. Add **rear-guard positioning reward** to encourage "behind allies" formation

---

## 3. Heal Mechanic Specification

### 3.1 Core Parameters

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Heal Range | 800 cm (8m) | Tighter than proximity bonus range (1500cm) — forces spatial commitment |
| Heal Rate | 10 HP/sec | 5 seconds to fully heal from 50% HP. Meaningful but not instant |
| Heal Trigger | Automatic (proximity) | No extra action dimension. Agent just needs to learn "get close to hurt allies" |
| Heal Target | Nearest injured ally within range | Simple target selection, no prioritization complexity |
| Heal Cooldown | None | Keep Phase 1 simple. Can add cooldowns later if healing is too strong |
| Self-Heal | No | Support heals others only. Self-heal comes from health pickups |
| Heal While Dead | No | Dead agents cannot heal (existing `bIsAlive` check) |
| Min Ally Health to Trigger | < 100% HP | Any damage triggers heal eligibility |
| Max Heal Per Tick | HealRate * DeltaTime | Continuous, tick-based healing |

### 3.2 Behavior Flow

```
Every Tick (if Support strategy && alive):
  1. Find nearest alive ally within HealRange (800cm)
  2. If ally exists AND ally.Health < ally.MaxHealth:
     a. HealAmount = HealRate * DeltaTime  (= 10 * 0.1 = 1.0 HP per tick at 10Hz)
     b. Call ally->Heal(HealAmount) via existing ICombatStatsInterface
     c. Track cumulative heal amount for reward calculation
  3. Else: no healing this tick
```

### 3.3 Why Automatic (No Action Dimension Change)

The RL policy outputs 7-dim EQS spatial weights. Adding a "heal toggle" would require an 8th dimension or a discrete action branch, complicating:
- The ONNX export pipeline
- The UE5 actuator
- The RLlib action space definition

Instead, healing is **emergent from good positioning**. The agent learns to weight `AllyProximity` highly → gets within 800cm → healing happens automatically → reward signal fires. This creates a tight perception-action-reward loop without architectural changes.

---

## 4. Rear-Guard Positioning Reward

### 4.1 Concept

Reward support agents for positioning **behind their allies relative to enemies**. This produces natural "enemy → ally → self" formations without explicit formation detection.

### 4.2 Implementation

```
Rear-Guard Check (per step, Support strategy only):
  1. NearestEnemyDist = distance to nearest visible enemy
  2. NearestAllyToEnemyDist = distance from nearest ally to that same enemy
  3. If NearestEnemyDist > NearestAllyToEnemyDist:
     → Agent is BEHIND allies relative to enemy
     → Award RearGuardBonus (+0.3 per step)
  4. Else:
     → Agent is EXPOSED (in front of allies)
     → No bonus (no penalty either — don't discourage combat)
```

### 4.3 Parameters

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| RearGuardBonus | 0.3 per step | Moderate — shouldn't dominate over healing reward |
| Requires Visible Enemy | Yes | Only meaningful when enemies are present |
| Requires Alive Ally | Yes | Must have ally to be "behind" |

---

## 5. Reward Changes

### 5.1 New Reward Components (Support only)

| Component | Signal Type | Value | Condition |
|-----------|------------|-------|-----------|
| HealTickReward | Dense (per tick) | +0.6 per heal tick | Ally healed > 0 HP this tick |
| HealBurstBonus | Sparse (event) | +3.0 per 40HP healed | Cumulative heal reaches 40HP threshold on one ally |
| RearGuardBonus | Dense (per step) | +0.3 per step | Agent farther from enemy than nearest ally |

### 5.2 Updated Support Reward Budget (per step, max theoretical)

| Component | Before | After |
|-----------|--------|-------|
| Baseline | +0.3 | +0.3 |
| Health Bonus | +0.8 | +0.8 |
| Position Reward | +0.5 | +0.5 |
| Ally Proximity | +1.0 | +1.0 |
| Critical Ally Bonus | +0.5 | +0.5 |
| Approach Shaping | ~+1.0 | ~+1.0 |
| **Heal Tick** | — | **+0.6** |
| **Rear-Guard** | — | **+0.3** |
| **Max Total** | ~4.1 | **~5.0** |
| **After 0.1× scale** | ~0.41 | **~0.50** |

The reward increase is moderate. The key improvement is not magnitude but **signal quality** — healing reward is directly caused by the agent's positioning decisions.

### 5.3 RewardTypes.h Changes

Add to `FSupportRewardSettings`:

```cpp
//========== Heal Properties =============

/** Per-tick reward when actively healing an ally */
UPROPERTY(EditAnywhere, Category = "Heal")
float HealTickReward = 0.6f;

/** Bonus when cumulative heal on one ally reaches this threshold */
UPROPERTY(EditAnywhere, Category = "Heal")
float HealBurstThreshold = 40.0f;

/** One-time bonus for reaching HealBurstThreshold */
UPROPERTY(EditAnywhere, Category = "Heal")
float HealBurstBonus = 3.0f;

//========== Positioning Properties =============

/** Per-step bonus for being behind allies relative to nearest enemy */
UPROPERTY(EditAnywhere, Category = "Positioning")
float RearGuardBonus = 0.3f;
```

---

## 6. Implementation Plan

### 6.1 UE5 C++ Changes

#### File 1: `MocCharacter.h / .cpp` — Add Heal Tick Logic

**Header additions:**
```cpp
// Heal System (Support strategy)
UPROPERTY(EditAnywhere, Category = "Support|Heal")
float HealRange = 800.0f;

UPROPERTY(EditAnywhere, Category = "Support|Heal")
float HealRate = 10.0f;  // HP per second

// Runtime tracking
float CumulativeHealAmount = 0.0f;
int32 CurrentHealTargetIdx = -1;

// Methods
void TickSupportHealing(float DeltaTime);
AMocCharacter* FindNearestInjuredAlly() const;
```

**Implementation — `TickSupportHealing()`:**
- Called from `Tick()` only when `CommandedStrategy == EStrategyType::Support`
- Finds nearest injured ally within `HealRange`
- Calls `ally->Heal(HealRate * DeltaTime)` via existing `ICombatStatsInterface`
- Tracks `CumulativeHealAmount` for reward burst detection
- Resets tracker on target change or heal burst threshold reached

**Integration point in `Tick()`:**
```cpp
void AMocCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!bIsAlive) return;

    HandleCombat();

    // NEW: Support healing
    if (CommandedStrategy == EStrategyType::Support)
    {
        TickSupportHealing(DeltaTime);
    }
}
```

#### File 2: `RewardTypes.h` — Add Heal & Positioning Rewards

Add `HealTickReward`, `HealBurstThreshold`, `HealBurstBonus`, `RearGuardBonus` to `FSupportRewardSettings` (see Section 5.3).

#### File 3: `MocRewardCalculator.cpp` — Extend Support Case Block

In `ComputeStepReward()`, extend the `EStrategyType::Support` case:

```cpp
// NEW: Heal tick reward
if (Character->GetLastTickHealAmount() > 0.0f)
{
    Reward += SupportReward.HealTickReward;  // +0.6
}

// NEW: Heal burst bonus (sparse)
if (Character->ConsumeHealBurst(SupportReward.HealBurstThreshold))
{
    Reward += SupportReward.HealBurstBonus;  // +3.0
}

// NEW: Rear-guard positioning bonus
if (HasVisibleEnemy)
{
    float SelfToEnemyDist = FVector::Dist(Current.Position, NearestEnemyPos);
    float AllyToEnemyDist = FVector::Dist(NearestAllyPos, NearestEnemyPos);
    if (SelfToEnemyDist > AllyToEnemyDist)
    {
        Reward += SupportReward.RearGuardBonus;  // +0.3
    }
}
```

#### File 4: `RewardEvent` enum — Add HealAlly event type

```cpp
enum class ERewardEventType : uint8
{
    Kill,
    Assist,
    Death,
    CapturePoint,
    LosePoint,
    PickupDeny,
    Survival,
    DistanceShaping,
    TeamVictory,
    StrategyDiversity,
    HealAlly        // NEW
};
```

### 6.2 Python Training Changes

**None required.** The heal system is entirely UE5-side:
- Action space unchanged (7-dim EQS weights)
- Observation space unchanged (52-dim)
- Reward flows through existing Schola reward channel
- No new actuator or sensor needed

### 6.3 No Observation Space Change Needed

The agent already observes `AllyHealths[4]` (4 ally health values) and `AllyPositions[4]` (4 ally positions) in its 49-dim observation. This is sufficient to learn "approach low-health allies." No additional observations are needed for the heal mechanic.

---

## 7. Risk Assessment

| Risk | Mitigation |
|------|-----------|
| Healing too powerful (support never fights) | Kill reward still at +3; death penalty at -10. Combat is always relevant |
| Healing makes games too long | 10 HP/s heal vs 100 DPS weapon damage (6.67 rounds × 15 dmg). Healing is 10% of damage — slows kills slightly, doesn't prevent them |
| Support clusters on one ally, ignores others | Target re-evaluation every 5 steps (existing `InjuredAllyReevalInterval`) |
| Reward scale imbalance with Assault/Defend | After 0.1× global scale, max support reward increases from ~0.41 to ~0.50 per step. Still within same order of magnitude as Assault/Defend |

---

## 8. Validation Criteria

- [ ] Support policy reward curve shows sustained growth (no collapse) over 500K+ steps
- [ ] Support agents visibly approach and stay near injured allies
- [ ] Ally health increases when support agent is nearby
- [ ] Rear-guard positioning observed in replays (support behind assault/defend)
- [ ] No regression in Assault/Defend policy performance
- [ ] Heal reward appears in TensorBoard per-strategy metrics
