# Training Diagnosis & Action Plan
**Date:** 2026-03-21
**Checkpoint:** 1M steps (~2.3 hours), branch `feature-defendrework`

---

## 1. Training Results Summary (1M Steps)

| Metric | Vanguard | Support | Strike |
| :--- | :--- | :--- | :--- |
| **episode_max** | ~1,800 | ~1,800 | ~800 |
| **episode_min** | Turned positive | Negative (~-600) | Worsening (~-1,000+) |
| **explained_var** | 0.52 (dropped from 0.8) | 0.28 (lowest) | 0.59 (stable) |
| **Loss trend** | Stable | vf_loss rising | Stable but stagnant |
| **Entropy** | Normal | Normal | Collapsing (local optima) |

**Vanguard** is performing best. **Support** has high reward potential but an unstable critic. **Strike** is stuck in a local optimum with collapsing entropy and worsening worst-case performance.

---

## 2. Bugs Fixed

### 2.1 Vanguard: Damage Absorption Reward Removed
**File:** `Private/Data/Reward/DEVanguardReward.cpp`

`ZoneDurabilityBonus` rewarded Vanguard for taking damage (HP absorbed). This directly conflicted with `HealthBonus`, which rewards staying above 70% HP. The two signals created a conflicting gradient around the 70% HP boundary, making value estimation unstable and producing oscillatory behavior. Removed the damage absorption block entirely.

### 2.2 Team Reward Mixing Removed
**File:** `Private/Core/Subsystems/DERewardSubsystem.cpp`

The team reward blending (`TeamRewardMixingRatio = 0.2`) mixed each agent's step reward with the previous step's average teammate reward. Problems:
- One-step temporal lag (stale teammate data)
- Added non-stationarity: an agent's reward depended on what teammates were doing, making each policy's credit assignment harder
- Post-mix reward was not re-clamped, allowing slight bound violations

Removed. Each agent now receives only its individual step reward.

### 2.3 Hyperparameter Adjustments (train.py)
Applied in response to per-policy training diagnostics:

| Parameter | Old | New | Reason |
| :--- | :--- | :--- | :--- |
| `ENTROPY_SCHEDULE` 1.5M step | 0.001 | 0.002 | Strike entropy collapsing; keep exploration budget |
| `ENTROPY_SCHEDULE` 2.5M step | 0.0005 | 0.001 | Set hard floor; prevent local optima lock-in |
| `ENTROPY_SCHEDULE` 5.0M step | — | 0.001 | Hold floor; new entry |
| `LR_SCHEDULE` first decay | 2M → 1e-4 | 1M → 1e-4 | Vanguard explained_var dropped ~1M; stabilize earlier |
| `VF_LOSS_COEFF` | 0.5 | 1.0 | Support critic underfitting (vf_loss rising, expl_var 0.28) |
| `VF_CLIP_PARAM` | 10.0 | 20.0 | Allow larger critic updates to close Support value gap |
| `NUM_SGD_ITER` | 6 | 8 | Extra critic gradient steps per batch for Support |

---

## 3. Identified Issues (Pending Action)

### 3.1 [CRITICAL] Step Penalty Sign Ambiguity — Vanguard & Support
**File:** `Private/Core/Subsystems/DERewardSubsystem.cpp:263`

```cpp
const float EffectiveStepPenalty = (Class == EDEClassType::Strike)
    ? Settings->StrikeReward.TimePenalty
    : -Settings->StepPenalty;   // sign depends on base class convention
Reward -= EffectiveStepPenalty;
```

If `StepPenalty` in `UDynamicEQSRewardData` is stored as a positive magnitude, then `-StepPenalty` is negative, and `Reward -= (negative)` adds a bonus every step. This would trivially explain why Vanguard and Support episode rewards are ~2× Strike's. **Must verify the base class value sign before next training run.**

### 3.2 [CRITICAL] Strike Cannot Earn Zone Rewards in Contested Situations
**File:** `Private/Data/Reward/DEStrikeReward.cpp:134`

When Strike enters a capture zone with a nearby enemy:
- Earns `ZonePresenceBonus × 0.3 = +0.9/step`
- Pays `TooCloseEnemyPenalty = -15.0/step`
- **Net: −14.1/step**

Enemies are always present at contested zones. Strike learns to never enter zones during actual combat — exactly when zone control matters most. This explains Strike's ~800 episode_max vs ~1800 for other classes.

**Fix:** Remove the `ZoneScale = 0.3` reduction when enemy is too close. Range discipline is already enforced by `TooCloseEnemyPenalty`; scaling down the zone bonus on top of it eliminates all zone incentive entirely.

### 3.3 [HIGH] Dead Code: `bWasTooCloseAtKill` Redundant 400cm Check
**File:** `Private/Core/Subsystems/DERewardSubsystem.cpp:226–239`

`bWasTooCloseAtKill` is set in `DEStrikeReward.cpp` when enemy is within `MinCombatRange = 800cm`. A second check in the subsystem sets it again when within `CloseRangeKillThreshold = 400cm`. Since 400 < 800, the first check always supersedes the second. The 400cm check and `CloseRangeKillThreshold` have no effect on behavior. Remove the redundant block.

### 3.4 [HIGH] Support: `AllyInjuryThreshold = 0.9` Makes Proximity Bonus Always Active
**File:** `Public/Data/Reward/DESupportReward.h:52`

Any ally who has taken a single hit is below 90% HP. `AllyProximityBonus (+2.0/step)` is therefore effectively always active, making it indistinguishable from `AllyFormationBonus (+1.5/step)`. Two signals encoding the same behavior wastes reward clarity. Support learns "follow allies" rather than "go to hurt allies."

**Fix:** Lower `AllyInjuryThreshold` from `0.9` to `0.55`.

### 3.5 [HIGH] Support: `PositionReward = 0.0` — Movement Reward Block is Dead Code
**File:** `Public/Data/Reward/DESupportReward.h:26`

`PositionReward = 0.0f`. Both branches in `DEComputeSupportStepReward` that reference it add `0.0 × anything = 0`. The movement reward block is entirely inert. Either assign a nonzero value (e.g. `0.5`) or remove the block.

### 3.6 [MEDIUM] Vanguard: No Per-Damage-Dealt Reward
Strike has `InRangeHitReward (+3.0/hit)` for actively dealing damage. Vanguard has no equivalent — only a kill reward. Since attacks are automatic, Vanguard has no per-step gradient distinguishing "standing in melee range idle" from "actively attacking in melee range." A `MeleeDamageDealtReward` (parallel to Strike's `InRangeHitReward`) would provide this signal.

---

## 4. Architecture: Observations & EQS Weights

### 4.1 Core Problem with Shared EQS Weights

All three classes currently output the same 7 EQS weights:
```
[0] EnemyObjectiveProximity
[1] AllyObjectiveProximity
[2] CoverDensity
[3] EnemyVisibility
[4] AllyProximity
[5] CombatRange
[6] AssignedBaseProximity
```

These are generic spatial preferences. Two structural problems:
- **Wasted capacity:** Support's optimal behavior never uses `CombatRange` or `EnemyObjectiveProximity`. The policy spends parameters learning to suppress irrelevant dimensions.
- **Missing expressivity:** Strike needs a kite/retreat direction. Vanguard needs target prioritization. Support needs injured-ally tracking. None of these can be expressed with the current weights.

### 4.2 Proposed Per-Class EQS Weight Sets

#### Strike (Ranged DPS)
| # | Weight | Purpose |
| :--- | :--- | :--- |
| 0 | `EnemyObjectiveProximity` | Pressure enemy/neutral cap points |
| 1 | `CoverDensity` | Prefer covered positions for survival |
| 2 | `EnemyVisibility` | Maintain LOS for damage output |
| 3 | `OptimalCombatRange` | Score positions 800–1500cm from nearest enemy |
| 4 | `RetreatFromEnemy` ★ | Flee gradient when inside MinCombatRange — enables kiting |
| 5 | `AllyProximity` | Avoid isolation |
| 6 | `AssignedBaseProximity` | Objective gradient |

★ **New.** Critical for kiting: allows Strike to move away from enemy while remaining near the zone, rather than binary approach/flee.

#### Vanguard (Melee Tank)
| # | Weight | Purpose |
| :--- | :--- | :--- |
| 0 | `EnemyObjectiveProximity` | Push toward enemy cap points |
| 1 | `MeleeEngagementRange` | Score positions within melee range of visible enemy |
| 2 | `WeakestEnemyProximity` ★ | Prefer positions near lowest-HP visible enemy |
| 3 | `AllySupport` ★ | Stay within Support heal range |
| 4 | `CoverApproachPath` ★ | Prefer covered approach paths to enemies |
| 5 | `AllyProximity` | Avoid isolation |
| 6 | `AssignedBaseProximity` | Objective gradient |

★ **New.** `WeakestEnemyProximity` enables target prioritization through positioning. `AllySupport` keeps Vanguard sustainable. `CoverApproachPath` is the primary tank skill — approach under cover.

#### Support (Healer)
| # | Weight | Purpose |
| :--- | :--- | :--- |
| 0 | `InjuredAllyProximity` ★ | Move toward most-injured alive ally |
| 1 | `AllyFormation` | Stay within the ally cluster |
| 2 | `RearGuardPosition` | Stay behind allies relative to nearest enemy |
| 3 | `CoverDensity` | Prefer covered positions |
| 4 | `FriendlyObjectiveProximity` ★ | Fall back toward friendly-held bases when isolated |
| 5 | `EnemyAvoidance` ★ | Flee gradient from nearest visible enemy |
| 6 | `HealRangeSatisfaction` ★ | Score positions by number of allies within heal range |

★ **New.** `InjuredAllyProximity` directly encodes the healer's core job (vs. generic ally proximity). `HealRangeSatisfaction` teaches Support to find positions that maximize simultaneous heal coverage. `EnemyAvoidance` replaces `CombatRange` (irrelevant for a non-attacker). `FriendlyObjectiveProximity` provides a fallback destination when isolated.

### 4.3 Observation Gap: Ally HP Delta (Support)

**File:** `Public/Types/DEObservationTypes.h` (ally token layout)

Current ally token (8-dim): `[rel_pos(3), health, alive, is_strike, is_vanguard, is_support]`

Support's reward is driven by ally HP changes, not static HP values. The critic sees "ally at 0.4 HP" but not "ally was at 0.9 HP 3 steps ago and falling fast." This temporal gap is the structural reason Support's `explained_var` is low — the information needed to predict future reward is absent from the observation.

**Fix:** Add `hp_delta` (previous HP − current HP, normalized) to each ally token → **9-dim ally token**.
This requires: `OBS_DIM` change in `DEObservationTypes.h`, Python layout constants update in `policy.py`, and retraining from scratch.

---

## 5. Action Priority Order

### Immediate (before next training run)
1. ~~**Verify `StepPenalty` sign**~~ — Investigated: sign is correct. `StepPenalty = -0.001f` (negative), negated then subtracted → net penalty. Not a bug; reward gap is due to other factors.
2. ✅ **Remove ZoneScale reduction** in `DEStrikeReward.cpp` — removed `ZoneScale = 0.3` multiplier; zone bonus now applies at full strength in contested zones. Range discipline handled by `TooCloseEnemyPenalty`.
3. ✅ **Lower `AllyInjuryThreshold`** from 0.9 → 0.70 in `DESupportReward.h` — fires when ally has taken ~30% damage (common in combat). Previous value of 0.55 created a bimodal reward cliff that destabilized the critic: most steps had no bonus, then suddenly +2.0 when ally hit 55%. Range reduced gradually, not in one jump.
4. ✅ **Disable `PositionReward`** (0.5 → 0.0) in `DESupportReward.h` — movement thresholds (`SupportMinMoveThreshold`, `SupportMaxMoveThreshold`) not verified; re-enable after confirming threshold values are sensible.
5. ✅ **Remove redundant 400cm kill threshold check** in `DERewardSubsystem.cpp` — 800cm per-step check in `DEStrikeReward.cpp` always superseded it.

### Support Collapse Post-Mortem (400k steps, 2026-03-22)
**Symptom:** episode_mean 1700→1100 (falling), episode_min -300→-730 (diving), entropy RISING (-0.51→-0.46), KL LOW (0.006).
**Root cause 1 — Critic oscillation:** `VF_CLIP_PARAM=20.0` × `NUM_SGD_ITER=8` × `VF_LOSS_COEFF=1.0` stacked too aggressively. With Support's reward range spanning ~2650 (max=1922, min=-730), these settings caused the critic to oscillate rather than converge, producing noisy advantage estimates.
**Root cause 2 — Bimodal reward cliff:** `AllyInjuryThreshold=0.55` made `AllyProximityBonus (+2.0/step)` sparse — fires only when ally is severely injured. Most steps had no proximity bonus; occasional steps had +2-3. This bimodal target is hard for the critic to fit, amplifying the oscillation.
**Cascade:** Bad advantages → entropy regularization (consistent direction) wins over noisy policy gradient → entropy rises → policy becomes random → Support stops following allies → isolation (-3/step) + frontline (-2/step) penalties cascade → episode_min -730.
**Fix applied (2026-03-22):**
- `VF_CLIP_PARAM: 20.0 → 10.0` (reverted)
- `NUM_SGD_ITER: 8 → 6` (reverted)
- `VF_LOSS_COEFF: 1.0 → 0.5` (reverted)
- `GRAD_CLIP: 0.5 → 1.0` (loosened to allow critic to recover)
- `AllyInjuryThreshold: 0.55 → 0.70` (smoother activation frequency)
- `PositionReward: 0.5 → 0.0` (disabled pending threshold verification)

### Medium Term (next architecture iteration)
6. **Refactor EQS weights** to per-class 7-weight sets (new EQS queries + policy retraining)
7. ✅ **Add `hp_delta` to ally observation token** (9-dim) — `DE_ALLY_DIM` 8→9, `DE_OBS_V2_DIM` 218→226. C++ observer tracks `LastAllyHealths` per ally, emits clamped delta. Python layout constants updated in `policy.py` and `env_wrapper.py`.
8. **Add `MeleeDamageDealtReward`** for Vanguard

### Already Applied
- ✅ Vanguard damage absorption reward removed
- ✅ Team reward mixing removed
- ✅ Hyperparameters adjusted (entropy floor, LR schedule)


### training script:
- "C:\Users\PC\Documents\GitHub\CORTEX\DE_Training\training\train.py"
- "C:\Users\PC\Documents\GitHub\CORTEX\DE_Training\training\policy.py"
- "C:\Users\PC\Documents\GitHub\CORTEX\DE_Training\training\env_wrapper.py”
