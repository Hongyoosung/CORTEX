# v8.0 Week 3 Implementation Summary: Unified Reward System

**Date:** 2026-01-14
**Status:** ✅ COMPLETED
**Implementation Time:** Week 3 of 5

---

## Overview

Week 3 focused on implementing the **unified reward system** with strategy-specific weight profiles, replacing the v7.0 approach of separate reward functions per strategy. The new system uses a single reward calculator with different weight profiles, ensuring maintainability, debuggability, and easy hyperparameter tuning.

---

## Key Achievements

### 1. Python Unified Reward Calculator ✅

**File:** `CORTEX_Training/training_env/unified_reward.py`

**Key Features:**
- `StrategyRewardCalculator` class - Single implementation for all strategies
- Strategy-specific weight profiles (`REWARD_WEIGHTS` dictionary)
- Component-based reward calculation (5 components):
  1. **Objective Progress** - Moving closer, volume retention
  2. **Combat Effectiveness** - Damage dealt, kills (with efficient targeting bonus)
  3. **Survival** - Death penalty
  4. **Cover Usage** - Tactical positioning rewards
  5. **Team Coordination** - Formation bonuses

**Weight Profiles:**
```python
REWARD_WEIGHTS = {
    'Assault': {
        'objective_progress': 1.0,    # High: push objectives
        'combat_effectiveness': 0.8,  # High: engage enemies
        'survival': 0.6,              # Medium: acceptable risk
        'cover_usage': 0.3,           # Low: don't camp
        'team_coordination': 0.4      # Medium: loose formation
    },
    'Defend': {
        'objective_progress': 0.2,    # Low: stay near objective
        'combat_effectiveness': 0.6,  # Medium: suppress threats
        'survival': 1.0,              # High: must survive
        'cover_usage': 0.9,           # High: maximize cover
        'team_coordination': 0.5      # Medium: defensive formation
    },
    'Support': {
        'objective_progress': 0.5,    # Medium: follow ally
        'combat_effectiveness': 0.4,  # Medium-low: assist
        'survival': 0.7,              # Medium-high: stay alive
        'cover_usage': 0.5,           # Medium: balanced
        'team_coordination': 1.0      # High: stick with ally
    },
    'Retreat': {
        'objective_progress': 0.8,    # High: reach safe zone
        'combat_effectiveness': 0.0,  # None: avoid combat
        'survival': 1.2,              # Highest: survival paramount
        'cover_usage': 0.7,           # High: use cover
        'team_coordination': 0.3      # Low: self-preservation
    }
}
```

**TensorBoard Logging:**
- `log_reward_breakdown_to_tensorboard()` helper function
- Logs total reward, individual components, and component ratios
- Enables visualization of which components dominate learning

### 2. C++ RewardCalculator Updates ✅

**Files Updated:**
- `Source/GameAI_Project/Public/RL/RewardCalculator.h`
- `Source/GameAI_Project/Private/RL/RewardCalculator.cpp`

**Key Changes:**

#### a) New Reward Configuration (RewardCalculator.h)
```cpp
namespace RewardConfig {
    // Base component values
    constexpr float OBJECTIVE_ADVANCE_REWARD = 0.1f;
    constexpr float OBJECTIVE_VOLUME_REWARD = 0.1f;
    constexpr float DAMAGE_REWARD_SCALE = 0.1f;
    constexpr float KILL_REWARD_BASE = 10.0f;
    constexpr float KILL_REWARD_EFFICIENT = 12.0f;  // v8.0: Efficient targeting bonus
    constexpr float DEATH_PENALTY = -10.0f;
    constexpr float COVER_BONUS = 0.1f;
    constexpr float FORMATION_BONUS = 0.1f;

    // Strategy-specific weight profiles (mirrored from Python)
    struct FStrategyWeights {
        float ObjectiveProgress;
        float CombatEffectiveness;
        float Survival;
        float CoverUsage;
        float TeamCoordination;
    };

    constexpr FStrategyWeights ASSAULT_WEIGHTS = {1.0f, 0.8f, 0.6f, 0.3f, 0.4f};
    constexpr FStrategyWeights DEFEND_WEIGHTS = {0.2f, 0.6f, 1.0f, 0.9f, 0.5f};
    constexpr FStrategyWeights SUPPORT_WEIGHTS = {0.5f, 0.4f, 0.7f, 0.5f, 1.0f};
    constexpr FStrategyWeights RETREAT_WEIGHTS = {0.8f, 0.0f, 1.2f, 0.7f, 0.3f};
}
```

#### b) Reward Component Breakdown Struct
```cpp
USTRUCT(BlueprintType)
struct FRewardComponentBreakdown {
    float ObjectiveProgress = 0.0f;
    float CombatEffectiveness = 0.0f;
    float Survival = 0.0f;
    float CoverUsage = 0.0f;
    float TeamCoordination = 0.0f;
    float Total = 0.0f;

    FString ToString() const;  // For debugging
};
```

#### c) New Unified Reward Methods
```cpp
// Main unified reward calculation
FRewardComponentBreakdown CalculateUnifiedReward(
    EStrategyType Strategy,
    const FObservationElement& PrevObs,
    const FObservationElement& CurrentObs
);

// Component calculation methods (strategy-agnostic)
float CalculateObjectiveProgressComponent(...);
float CalculateCombatEffectivenessComponent(...);
float CalculateSurvivalComponent(...);
float CalculateCoverUsageComponent(...);
float CalculateTeamCoordinationComponent(...);
```

#### d) Enhanced Kill Tracking
```cpp
// v8.0: Track efficient targeting for bonus
void OnKillEnemyWithPriority(AActor* Enemy, bool bWasLowestHP);
```

### 3. v7.0 Code Deprecation ✅

**Deprecated Methods:**
- `CalculateStrategyReward()` - Now delegates to `CalculateUnifiedReward()`
- `CalculateMissionProgressReward()` - Now delegates to `CalculateObjectiveProgressComponent()`
- `CalculateAlignmentBonus()` - Removed in v8.0 (strategy assigned by MCTS, not RL)

**Deprecated Logic:**
- Strategy-specific switch statements in reward calculation
- Separate reward functions per strategy
- Mission-strategy alignment bonuses

All deprecated methods log warnings when called and delegate to v8.0 equivalents for transition support.

### 4. Enhanced Logging ✅

**Before (v7.0):**
```
[REWARD TICK] 'Agent1': Total=12.50 (Events=10.00, Continuous=2.50)
```

**After (v8.0):**
```
[REWARD TICK v8.0] 'Agent1' (Assault): Total=12.50 | Obj=0.10, Combat=11.60, Surv=0.00, Cover=0.00, Coord=0.00, Total=11.70 | Events=0.80
```

**Benefits:**
- Clear visibility into which components contribute to reward
- Strategy-specific logging for debugging
- Component breakdown enables TensorBoard visualization

---

## Architecture Benefits

### 1. Maintainability
- **Single Source of Truth:** One reward calculator, not four separate implementations
- **Easy Tuning:** Change weights in `REWARD_WEIGHTS`, not code logic
- **No Duplication:** Component calculations shared across all strategies

### 2. Debuggability
- **Component Breakdown:** See exactly what contributes to each reward
- **TensorBoard Visualization:** Plot component contributions over time
- **Strategy Comparison:** Compare reward profiles across strategies

### 3. Consistency
- **Guaranteed Alignment:** Python and C++ weights synced (manual sync for now)
- **Same Logic:** All strategies use identical component calculations
- **Predictable Behavior:** Weight changes affect all strategies consistently

---

## Implementation Details

### Reward Component Calculation

#### 1. Objective Progress Component
```cpp
float CalculateObjectiveProgressComponent(prev, current) {
    float reward = 0.0f;

    // Volume retention (continuous)
    if (IsInObjectiveVolume) {
        reward += 0.1f;  // +0.1 per step
    }

    // Advancement (moving closer)
    if (currentDistance < prevDistance && !IsInVolume) {
        reward += 0.1f;  // +0.1 per step when advancing
    }

    return reward;
}
```

**Strategy Impact:**
- Assault (weight=1.0): Full reward for advancing
- Defend (weight=0.2): Low reward for advancement (wants to stay put)
- Support (weight=0.5): Medium reward (follows ally)
- Retreat (weight=0.8): High reward (reach safe zone)

#### 2. Combat Effectiveness Component
```cpp
float CalculateCombatEffectivenessComponent(state) {
    float reward = 0.0f;

    // Damage dealt
    reward += damage * 0.1f;  // +0.1 per 1 damage

    // Kills (v8.0: efficient targeting bonus)
    if (killed_enemy) {
        if (bTargetWasLowestHP) {
            reward += 12.0f;  // Efficient targeting
        } else {
            reward += 10.0f;  // Base kill reward
        }
    }

    return reward;
}
```

**Strategy Impact:**
- Assault (weight=0.8): High combat reward
- Defend (weight=0.6): Medium combat reward
- Support (weight=0.4): Lower combat reward (assist, don't solo)
- Retreat (weight=0.0): NO combat reward (avoid fighting)

#### 3. Other Components
- **Survival:** Death penalty (-10.0), scaled by weight (Retreat=1.2, Assault=0.6)
- **Cover Usage:** +0.1 per step in cover (Defend=0.9, Assault=0.3)
- **Team Coordination:** +0.1 per step in formation (Support=1.0, Retreat=0.3)

### Total Reward Calculation
```cpp
FRewardComponentBreakdown CalculateUnifiedReward(strategy, prev, current) {
    FRewardComponentBreakdown breakdown;
    const auto& weights = GetWeightsForStrategy(strategy);

    // Calculate base components
    float obj = CalculateObjectiveProgressComponent(prev, current);
    float combat = CalculateCombatEffectivenessComponent(current);
    float survival = CalculateSurvivalComponent(current);
    float cover = CalculateCoverUsageComponent(current);
    float coord = CalculateTeamCoordinationComponent(current);

    // Apply strategy-specific weights
    breakdown.ObjectiveProgress = obj * weights.ObjectiveProgress;
    breakdown.CombatEffectiveness = combat * weights.CombatEffectiveness;
    breakdown.Survival = survival * weights.Survival;
    breakdown.CoverUsage = cover * weights.CoverUsage;
    breakdown.TeamCoordination = coord * weights.TeamCoordination;

    // Total reward
    breakdown.Total = breakdown.ObjectiveProgress +
                      breakdown.CombatEffectiveness +
                      breakdown.Survival +
                      breakdown.CoverUsage +
                      breakdown.TeamCoordination;

    return breakdown;
}
```

---

## Expected Behavioral Impact

### Assault Strategy (High Aggression)
**Weights:** Objective=1.0, Combat=0.8, Survival=0.6, Cover=0.3, Coord=0.4

**Learned Behavior:**
- Aggressively push toward objectives (high objective weight)
- Engage enemies actively (high combat weight)
- Accept moderate risk (medium survival weight)
- Use cover tactically but don't camp (low cover weight)

### Defend Strategy (Position Holding)
**Weights:** Objective=0.2, Combat=0.6, Survival=1.0, Cover=0.9, Coord=0.5

**Learned Behavior:**
- Stay near objective (low objective advancement weight)
- Suppress approaching threats (medium combat weight)
- Prioritize survival (highest survival weight)
- Maximize cover usage (high cover weight)

### Support Strategy (Ally Protection)
**Weights:** Objective=0.5, Combat=0.4, Survival=0.7, Coord=1.0

**Learned Behavior:**
- Follow ally's objective (medium objective weight)
- Assist in combat (medium-low combat weight)
- Stay alive to support (medium-high survival weight)
- Maintain proximity to ally (highest coordination weight)

### Retreat Strategy (Survival Focus)
**Weights:** Objective=0.8, Combat=0.0, Survival=1.2, Cover=0.7, Coord=0.3

**Learned Behavior:**
- Move toward safe zone (high objective weight)
- Avoid all combat (zero combat weight)
- Prioritize survival above all (highest survival weight)
- Use cover aggressively (high cover weight)

---

## Integration with Training Pipeline

### TensorBoard Visualization (Week 4)

The unified reward system enables comprehensive reward tracking:

```python
# In training loop
for step in range(max_steps):
    obs, reward, done, info = env.step(action)

    # Get reward breakdown from info
    breakdown = info['reward_breakdown']

    # Log to TensorBoard
    log_reward_breakdown_to_tensorboard(
        writer=writer,
        global_step=global_step,
        agent_id=agent_id,
        strategy_type=strategy,
        components=breakdown,
        total_reward=reward
    )
```

**TensorBoard Graphs:**
- `Rewards/{agent_id}/total` - Total reward over time
- `Rewards/{strategy}/components/objective_progress` - Objective component by strategy
- `Rewards/{strategy}/components/combat_effectiveness` - Combat component by strategy
- `Rewards/{strategy}/ratios/objective_progress` - Ratio of objective to total reward

---

## Testing & Validation

### Unit Test Example
```cpp
TEST(RewardCalculatorTest, UnifiedRewardCalculation) {
    URewardCalculator* calc = NewObject<URewardCalculator>();

    FObservationElement prev, current;
    // Setup: Agent moved closer to objective, killed enemy (lowest HP)
    prev.MissionContext.Distance = 0.5f;
    current.MissionContext.Distance = 0.4f;
    current.TacticalInfo.bHasCover = false;

    calc->OnKillEnemyWithPriority(enemy, true);  // Killed lowest HP target

    // Test Assault strategy
    auto breakdown = calc->CalculateUnifiedReward(
        EStrategyType::Assault, prev, current
    );

    // Expected: High objective + combat rewards
    EXPECT_GT(breakdown.ObjectiveProgress, 0.0f);
    EXPECT_GT(breakdown.CombatEffectiveness, 10.0f);  // Kill reward
    EXPECT_EQ(breakdown.Survival, 0.0f);  // No death
    EXPECT_EQ(breakdown.CoverUsage, 0.0f);  // Not in cover

    // Test Defend strategy (same state, different weights)
    auto breakdown_defend = calc->CalculateUnifiedReward(
        EStrategyType::Defend, prev, current
    );

    // Expected: LOWER total reward due to different weights
    EXPECT_LT(breakdown_defend.Total, breakdown.Total);
    EXPECT_LT(breakdown_defend.ObjectiveProgress, breakdown.ObjectiveProgress);
}
```

---

## Next Steps: Week 4

**Training Pipeline Integration:**
1. Update Python training environment to use `StrategyRewardCalculator`
2. Integrate TensorBoard logging for reward components
3. Run baseline training (1,000-2,000 episodes)
4. Validate strategy-specific parameter profiles emerge
5. Monitor component contribution graphs in TensorBoard

**Success Criteria:**
- [ ] Reward components logged to TensorBoard successfully
- [ ] Strategy-specific reward profiles differentiate (visible in graphs)
- [ ] Total rewards align with expected strategy behavior
- [ ] No "mode collapse" (all strategies learning same rewards)

---

## Files Modified

### Created
- `CORTEX_Training/training_env/unified_reward.py` - Python unified reward calculator

### Modified
- `Source/GameAI_Project/Public/RL/RewardCalculator.h` - v8.0 reward configuration + methods
- `Source/GameAI_Project/Private/RL/RewardCalculator.cpp` - Unified reward implementation
- `v8.0_PROPOSAL.md` - Marked Week 3 as complete

### Deprecated (Transition Support)
- `CalculateStrategyReward()` - Delegates to `CalculateUnifiedReward()`
- `CalculateMissionProgressReward()` - Delegates to `CalculateObjectiveProgressComponent()`
- `CalculateAlignmentBonus()` - Returns 0.0f (removed in v8.0)

---

## Summary

Week 3 successfully implemented the unified reward system with strategy-specific weight profiles, replacing v7.0's separate reward functions. The new architecture provides:

✅ **Single source of truth** for reward computation
✅ **Easy hyperparameter tuning** via weight profiles
✅ **TensorBoard-compatible** component breakdown
✅ **No code duplication** across strategies
✅ **Graceful v7.0 deprecation** with transition support

The system is now ready for Week 4 training pipeline integration, where we'll validate that strategy-specific behaviors emerge from the weight profiles during PPO training.

---

**Status:** ✅ Week 3 COMPLETE - Ready for Week 4 Training Pipeline
**Next:** Implement training environment integration and baseline training runs
