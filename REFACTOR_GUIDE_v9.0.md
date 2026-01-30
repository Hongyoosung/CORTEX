# CORTEX v9.0 Refactoring Guide: Reward-Driven Objective System

**Target:** Remove explicit objective assignment from MCTS, implement strategy-specific reward shaping, fix observation-action gap and reward normalization issues

**Rationale:** Simplify architecture by encoding objectives in RL rewards + improve learning signals

**Estimated Impact:** -400 lines code, +15% explanation clarity, maintained performance

**Status:** 🔴 Not Started | Target: v9.0

---

## Executive Summary

### Current Architecture (v8.20)
```
MCTS → [Strategy + TargetObjective] → RL Policy → Rewards (use assigned objective)
       └─ 8 batch prototypes with explicit objective mapping
       └─ Objective tracking through entire pipeline
       └─ Binary tactical parameter rewards (loses gradient information)
       └─ Hard reward clamping (information loss)
```

### Target Architecture (v9.0)
```
MCTS → [Strategy only] → RL Policy → Rewards (strategy-dependent implicit objectives)
       └─ 8 batch prototypes define strategy composition only
       └─ Reward functions encode "where to go" per strategy
       └─ Gradient-based tactical parameter rewards (continuous learning signals)
       └─ Per-component normalization + soft scaling (preserves gradients)
```

### Key Changes Summary

| Component | v8.20 (Current) | v9.0 (Target) | Impact |
|-----------|-----------------|---------------|--------|
| **MCTS Output** | Strategy + TargetObjective | Strategy only | -30% data |
| **Observation** | 46 base features | 52 base features (+6 obj context) | +13% input |
| **Reward Calc** | Dynamic objective queries + binary thresholds | Observation fields + gradients | +efficiency, +learning |
| **Batch Logic** | Maps strategies → objectives | Defines strategy mix only | -40% code |
| **Tactical Rewards** | Binary alignment (>0.5 threshold) | Continuous gradients (target matching) | Better RL signals |
| **Reward Norm** | Hard clamping [-10, 10] | Per-component + tanh scaling | Preserves gradients |

---

## Problem Statement (v9.0 Integration)

### Core Issues Being Addressed

**Problem 1: Explicit Objective Assignment Complexity**
- MCTS tracks both strategy AND objective assignments
- Redundant: objectives are implicit in strategy definitions
- 40% of MCTS code dedicated to objective mapping logic

**Problem 2: Observation-Action Gap** ⭐ NEW in v9.0
- Current tactical parameter rewards use binary thresholds (e.g., `Aggression > 0.5`)
- RL doesn't see gradient: "how much more aggression is needed?"
- EQS interpretation of parameters invisible to RL (no feedback loop)

**Problem 3: Reward Normalization Issues** ⭐ NEW in v9.0
- Hard clamping `[-10, 10]` loses information when raw rewards exceed bounds
- Components have vastly different scales (Objective: 200, Cover: 0.1)
- Value function struggles with multi-modal return distributions

---

## Phase 1: MCTS Simplification

### 1.1 Remove Objective Assignment from Batch Generation

**File:** `AI/MCTS/MCTS.cpp::GenerateCompleteBatches()`

**Changes:**
```cpp
// PSEUDOCODE: Remove objective assignment logic

// OLD (v8.20):
for each batch prototype:
    for each agent:
        assignment.Strategy = prototype.strategies[i]
        assignment.TargetObjective = DetermineObjective(prototype, objectives)  // ← REMOVE

// NEW (v9.0):
for each batch prototype:
    for each agent:
        assignment.Strategy = prototype.strategies[i]
        // No objective assignment - implicit in reward functions
```

**Impact:** Remove lines 327-353 (DetermineObjective logic)

---

### 1.2 Simplify Batch Prototypes

**File:** `AI/MCTS/MCTS.h`

**Changes:**
```cpp
// PSEUDOCODE: Struct simplification

// OLD (v8.20):
struct FBatchPrototype {
    TArray<EStrategyType> Strategies;
    EObjectiveType PrimaryObjective;  // ← REMOVE
    float EstimatedValue;
}

// NEW (v9.0):
struct FBatchPrototype {
    FString Name;  // "TightAssault", "WideDefense"
    TArray<EStrategyType> Strategies;  // [Assault, Assault, Assault, Support]
    float EstimatedValue;  // Prior for UCB1
    FString Description;  // For logging
}
```

---

### 1.3 Update Batch Key Generation

**File:** `AI/MCTS/MCTS.cpp::GetBatchKey()`

**Changes:**
```cpp
// PSEUDOCODE: Exclude objectives from cache keys

// OLD format: "A0→Assault→Obj1,A1→Defend→Obj0,..."
// NEW format: "A0→Assault,A1→Defend,A2→Support,A3→Retreat"

GetBatchKey(batch):
    parts = []
    for each assignment in batch:
        parts.add(AgentName + "→" + StrategyName)
        // REMOVE: + "→" + ObjectiveName
    sort(parts)
    return join(parts, ",")
```

**Impact:** Batch cache collapses similar strategies regardless of objectives

---

### 1.4 Remove Objective Parameters

**Files:**
- `MCTS.cpp::RunStrategyAssignment_v820()` - Remove objective usage in batch generation
- `TeamLeaderComponent.cpp` - Still discover objectives (for ObservationProvider)
- `FollowerAgentComponent.cpp` - Remove `CachedTargetObjective` member

**Key Insight:** Objectives still exist as world actors, but MCTS doesn't assign them

---

## Phase 2: Reward System Refactoring

### 2.1 Strategy-Specific Reward Functions

**File:** `RL/Components/RewardCalculator.cpp`

**Changes:**
```cpp
// PSEUDOCODE: Switch from objective-based to strategy-based rewards

// OLD (v8.20):
CalculateReward(agent, assignment):
    reward = CalculateObjectiveDistance(agent, assignment.TargetObjective)

// NEW (v9.0):
CalculateStrategyReward(agent, strategy, observation):
    switch strategy:
        case Assault:  return CalculateAssaultReward(observation)
        case Defend:   return CalculateDefendReward(observation)
        case Support:  return CalculateSupportReward(observation)
        case Retreat:  return CalculateRetreatReward(observation)
```

---

### 2.2 Individual Strategy Reward Implementations

**Assault Reward (Hostile Objective Focus):**
```cpp
// PSEUDOCODE: Proximity to hostile objective + combat engagement

CalculateAssaultReward(obs):
    reward = 0

    // Base: Closer to hostile objective = better
    distance = obs.HostileObjectiveDistance  // [0, 1] normalized
    reward += (1.0 - distance) * 10.0  // Max +10 at objective

    // Bonus: Very close (<10% max range)
    if distance < 0.1:
        reward += 15.0

    // Bonus: Engaging enemies near objective
    if obs.VisibleEnemyCount > 0 AND distance < 0.2:
        reward += 5.0 * obs.VisibleEnemyCount

    return reward
```

**Defend Reward (Friendly Objective Focus):**
```cpp
// PSEUDOCODE: Stay near friendly objective + defend against threats

CalculateDefendReward(obs):
    reward = 0

    // Base: Closer to friendly objective = better
    distance = obs.FriendlyObjectiveDistance  // [0, 1] normalized
    reward += (1.0 - distance) * 10.0

    // Bonus: Inside defense perimeter (<20% max range)
    if distance < 0.2:
        reward += 10.0

    // Bonus: Defending against nearby enemies
    if obs.VisibleEnemyCount > 0 AND distance < 0.2:
        reward += 8.0 * obs.VisibleEnemyCount

    return reward
```

**Support Reward (Ally Focus):**
```cpp
// PSEUDOCODE: Stay close to allies in need

CalculateSupportReward(obs):
    reward = 0

    if obs.bAllyNeedsHelp:
        distance = obs.AllyDistance  // [0, 1] normalized
        reward += (1.0 - distance) * 12.0

        // Bonus: Very close support (<5% max range)
        if distance < 0.05:
            reward += 15.0

        // Bonus: Covering ally (enemies nearby)
        if obs.VisibleEnemyCount > 0 AND distance < 0.08:
            reward += 5.0

    return reward
```

**Retreat Reward (Enemy Avoidance):**
```cpp
// PSEUDOCODE: Maximize distance from enemies

CalculateRetreatReward(obs):
    reward = 0

    // Base: Farther from enemies = better
    reward += obs.DistanceToNearestEnemy * 10.0  // [0, 1] normalized

    // Bonus: Increasing distance over time
    if obs.DistanceToNearestEnemy > lastDistance:
        reward += 5.0

    // Bonus: Reaching safe distance (>80% max range)
    if obs.DistanceToNearestEnemy > 0.8:
        reward += 10.0

    // Penalty: Being surrounded
    if obs.VisibleEnemyCount > 2:
        reward -= 5.0

    return reward
```

---

### 2.3 Gradient-Based Tactical Parameter Rewards ⭐ NEW

**Problem:** Current v8.20 uses binary thresholds (`Aggression > 0.5`) which lose gradient information

**Solution:** Continuous target matching with gradient-based rewards

**File:** `RL/Components/RewardCalculator.cpp::CalculateTacticalParameterEffectiveness()`

**Implementation:**

```cpp
// PSEUDOCODE: Replace binary thresholds with continuous gradients

CalculateTacticalParameterEffectiveness(obs, tacticalParams):
    reward = 0

    // [1] Aggression Gradient
    //     High aggression should correlate with reduced enemy distance
    //     Target: Aggression=1.0 → distance=0.2 (very close)
    //             Aggression=0.0 → distance=0.8 (far)
    targetDistance = 0.8 - (tacticalParams.Aggression * 0.6)
    distanceError = abs(obs.DistanceToNearestEnemy - targetDistance)
    aggressionReward = (1.0 - distanceError) * 0.3  // Max +0.3
    reward += aggressionReward

    // Bonus: Achieving aggressive positioning under fire
    if tacticalParams.Aggression > 0.7 AND obs.DistanceToNearestEnemy < 0.25 AND obs.VisibleEnemyCount > 0:
        reward += 0.2  // Aggression execution bonus

    // [2] Cover Preference Gradient
    //     High cover preference should correlate with being in cover
    coverValue = obs.bHasCover ? 1.0 : 0.0
    coverAlignment = tacticalParams.CoverPreference * coverValue +
                     (1.0 - tacticalParams.CoverPreference) * (1.0 - coverValue)
    reward += coverAlignment * 0.25  // Max +0.25

    // Penalty: High cover preference but exposed under fire
    if tacticalParams.CoverPreference > 0.7 AND NOT obs.bHasCover AND obs.VisibleEnemyCount > 0:
        reward -= 0.15  // Cover failure penalty

    // [3] Spread Distance Gradient
    //     Low spread should correlate with close ally proximity
    //     Target: Spread=0.0 → allyDist=0.1 (tight, 200cm)
    //             Spread=1.0 → allyDist=0.6 (wide, 1200cm)
    if obs.AllyDistance > 0.01:  // Has ally
        targetAllyDist = 0.1 + (tacticalParams.SpreadDistance * 0.5)
        spreadError = abs(obs.AllyDistance - targetAllyDist)
        spreadReward = (1.0 - spreadError) * 0.25  // Max +0.25
        reward += spreadReward

    // [4] Risk Tolerance Gradient
    //     High risk tolerance should correlate with staying in danger despite low HP
    healthFactor = 1.0 - obs.AgentHealth  // [0,1] where 1=critical
    dangerFactor = obs.VisibleEnemyCount / 3.0  // Normalize enemy count
    threatLevel = clamp((healthFactor + dangerFactor) / 2.0, 0, 1)

    if threatLevel > 0.5:
        if tacticalParams.RiskTolerance > 0.7:
            // Reward for maintaining position under threat
            reward += threatLevel * 0.3
        else if tacticalParams.RiskTolerance < 0.3:
            // Reward for retreating when risk-averse
            if obs.DistanceToNearestEnemy > 0.6:
                reward += 0.3  // Successfully disengaged
            else:
                reward -= 0.2  // Penalty for not retreating

    // [5] Temporal Consistency Reward
    //     Reward maintaining parameter consistency (prevent thrashing)
    static prevParams = tacticalParams
    paramStability = 1.0 - (
        abs(tacticalParams.Aggression - prevParams.Aggression) +
        abs(tacticalParams.CoverPreference - prevParams.CoverPreference) +
        abs(tacticalParams.SpreadDistance - prevParams.SpreadDistance) +
        abs(tacticalParams.RiskTolerance - prevParams.RiskTolerance)
    ) / 4.0
    reward += paramStability * 0.1  // Small bonus for stable parameters
    prevParams = tacticalParams

    return clamp(reward, -0.5, 1.5)  // Total range: [-0.5, +1.5]
```

**Key Improvements:**

| Aspect | v8.20 (Binary) | v9.0 (Gradient) |
|--------|----------------|-----------------|
| Aggression | Threshold >0.5 | Continuous target distance (0.2-0.8) |
| Cover | Binary match | Probabilistic alignment + penalty under fire |
| Spread | Binary close/far | Continuous target ally distance (0.1-0.6) |
| Risk | Binary danger check | Gradient threat level × tolerance |
| Stability | None | Temporal consistency reward |
| Range | [0, 0.5] | [-0.5, 1.5] (penalties + bonuses) |

---

### 2.4 Per-Component Reward Normalization ⭐ NEW

**Problem:** Components have vastly different scales (Objective: 200, Cover: 0.1) → hard clamping loses information

**Solution:** Normalize each component BEFORE strategy weighting, then use soft scaling

**File:** `RL/Components/RewardCalculator.h`

**Configuration:**
```cpp
// PSEUDOCODE: Component-specific normalization ranges

namespace RewardConfig {
    struct ComponentNormalization {
        float Scale, Offset, ClipMin, ClipMax
    }

    // Objective: Raw [-10, 200] → Normalized [-1, 3]
    OBJECTIVE_NORM = { Scale: 0.02, Offset: -0.2, ClipMin: -1.0, ClipMax: 3.0 }

    // Combat: Raw [-20, 50] → Normalized [-0.5, 2.0]
    COMBAT_NORM = { Scale: 0.04, Offset: 0.0, ClipMin: -0.5, ClipMax: 2.0 }

    // Survival: Raw [-10, 0] → Normalized [-2.0, 0]
    SURVIVAL_NORM = { Scale: 0.2, Offset: 0.0, ClipMin: -2.0, ClipMax: 0.0 }

    // Cover: Raw [0, 0.3] → No scaling needed
    COVER_NORM = { Scale: 1.0, Offset: 0.0, ClipMin: 0.0, ClipMax: 0.5 }

    // Coordination: Raw [0, 20] → Normalized [0, 1.0]
    COORD_NORM = { Scale: 0.05, Offset: 0.0, ClipMin: 0.0, ClipMax: 1.5 }

    // Tactical: Raw [-0.5, 1.5] → Already normalized
    TACTICAL_NORM = { Scale: 1.0, Offset: 0.0, ClipMin: -0.5, ClipMax: 1.5 }
}
```

**Implementation:**
```cpp
// PSEUDOCODE: Component-wise normalization before strategy weighting

CalculateUnifiedReward(strategy, prevObs, currentObs):
    breakdown = {}
    weights = GetWeightsForStrategy(strategy)

    // Component 1: Objective Progress
    objRaw = CalculateObjectiveProgressComponent(prevObs, currentObs)
    objNormalized = clamp(
        objRaw * OBJECTIVE_NORM.Scale + OBJECTIVE_NORM.Offset,
        OBJECTIVE_NORM.ClipMin, OBJECTIVE_NORM.ClipMax
    )
    breakdown.ObjectiveProgress = objNormalized * weights.ObjectiveProgress

    // Component 2: Combat Effectiveness
    combatRaw = CalculateCombatEffectivenessComponent(currentObs)
    combatNormalized = clamp(
        combatRaw * COMBAT_NORM.Scale,
        COMBAT_NORM.ClipMin, COMBAT_NORM.ClipMax
    )
    breakdown.CombatEffectiveness = combatNormalized * weights.CombatEffectiveness

    // ... Similar for Survival, Cover, Coordination, Tactical ...

    // Sum all weighted components
    rawTotal = sum(breakdown.values())

    // Soft scaling: tanh to compress extreme values while preserving gradients
    // tanh(x/4) * 5 maps: [-10, 10] → [-4.9, 4.9], [-20, 20] → [-5, 5]
    breakdown.Total = tanh(rawTotal / 4.0) * 5.0

    // Log when compression occurs
    if abs(rawTotal) > 8.0:
        log("Reward compression: Raw=%.2f → Scaled=%.2f", rawTotal, breakdown.Total)

    return breakdown
```

**Key Improvements:**

| Aspect | v8.20 | v9.0 |
|--------|-------|------|
| Normalization | Global clamping [-10, 10] | Per-component normalization |
| Information Loss | Hard clip loses gradients | Soft tanh scaling preserves gradients |
| Component Scales | Objective (+200) >> Cover (+0.1) | All normalized to similar scales |
| Strategy Weights | Applied to unnormalized | Applied to normalized components |
| Value Function | Multi-modal distribution | Unimodal distribution (easier to learn) |

---

## Phase 3: Data Structure Updates

### 3.1 Add Objective Context to Observations

**File:** `Observation/ObservationElement.h`

**Current:** 46 base features
**Target:** 52 base features (+6 objective context)

**Changes:**
```cpp
// PSEUDOCODE: Add 6 objective context features

struct FObservationElement {
    // ... existing 46 features ...

    // v9.0: NEW - Objective Context (6 features)
    float FriendlyObjectiveDistance = 1.0f;  // [0, 1] normalized
    FVector2D FriendlyObjectiveDirection = {0, 0};  // 2D normalized vector

    float HostileObjectiveDistance = 1.0f;  // [0, 1] normalized
    FVector2D HostileObjectiveDirection = {0, 0};  // 2D normalized vector

    // Total: 52 base features
}

ToFeatureVector():
    features = []
    features.add(/* ... existing 46 features ... */)

    // v9.0: Append objective context (6 features)
    features.add(clamp(FriendlyObjectiveDistance, 0, 1))
    features.add(FriendlyObjectiveDirection.X)
    features.add(FriendlyObjectiveDirection.Y)
    features.add(clamp(HostileObjectiveDistance, 0, 1))
    features.add(HostileObjectiveDirection.X)
    features.add(HostileObjectiveDirection.Y)

    check(features.size() == 52)
    return features
```

**Rationale:**
- Assault strategy needs `HostileObjectiveDistance` for reward shaping
- Defend strategy needs `FriendlyObjectiveDistance` for reward shaping
- Low risk: Only adding features, not removing any

---

### 3.2 Update Strategy Assignment Structure

**File:** `RL/RLTypes.h`

**Changes:**
```cpp
// PSEUDOCODE: Remove TargetObjective field

// OLD (v8.20):
struct FStrategyAssignment {
    AActor* Agent;
    EStrategyType Strategy;
    AObjectiveActor* TargetObjective;  // ← REMOVE
    int32 Priority;
    float ExpectedValue;
    // ...
}

// NEW (v9.0):
struct FStrategyAssignment {
    AActor* Agent;
    EStrategyType Strategy;
    int32 Priority;
    float ExpectedValue;
    // ... (no TargetObjective - implicit in rewards)
}
```

---

### 3.3 Update RLConfig Namespace

**File:** `RL/RLTypes.h`

**Changes:**
```cpp
// PSEUDOCODE: Update observation sizes and reward config

namespace RLConfig {
    // v9.0: Updated observation sizes
    OBSERVATION_BASE_SIZE = 52;  // 46 existing + 6 objective context
    OBSERVATION_SIZE = 56;       // 52 base + 4 strategy one-hot

    // v9.0: Feature breakdown (52 base features)
    // - Agent State: 4 (Position 3, Health 1)
    // - Combat State: 1 (DistanceToNearestEnemy)
    // - Environment: 16 (RaycastDistances)
    // - Enemy Info: 16 (VisibleEnemyCount 1, NearbyEnemies 15)
    // - Tactical Context: 4 (bHasCover, CoverDistance, CoverDirection 2)
    // - Ally Context: 5 (bAllyNeedsHelp, AllyHealth, AllyDistance, AllyDirection 2)
    // - Objective Context: 6 (FriendlyObj 3, HostileObj 3) ← NEW

    // v9.0: Strategy-specific reward weights
    ASSAULT_PROXIMITY_WEIGHT = 10.0f;
    DEFEND_PERIMETER_WEIGHT = 10.0f;
    SUPPORT_ALLY_WEIGHT = 12.0f;
    RETREAT_DISTANCE_WEIGHT = 10.0f;

    // v9.0: Distance normalization
    MAX_DISTANCE_NORMALIZATION = 10000.0f;
}
```

---

## Phase 4: Integration Points

### 4.1 ObservationProvider Updates

**File:** `Observation/ObservationProvider.cpp`

**Changes:**
```cpp
// PSEUDOCODE: Populate objective context fields

PopulateObjectiveContext(agent, outObservation):
    leader = GetTeamLeader(agent)
    if not leader: return

    friendlyObj = leader.GetFriendlyObjective()
    hostileObj = leader.GetHostileObjective()

    // Friendly objective context
    if friendlyObj:
        distance = Vector::Distance(agent.location, friendlyObj.location)
        outObservation.FriendlyObjectiveDistance =
            clamp(distance / MAX_DISTANCE_NORMALIZATION, 0, 1)

        direction = normalize2D(friendlyObj.location - agent.location)
        outObservation.FriendlyObjectiveDirection = direction

    // Hostile objective context
    if hostileObj:
        distance = Vector::Distance(agent.location, hostileObj.location)
        outObservation.HostileObjectiveDistance =
            clamp(distance / MAX_DISTANCE_NORMALIZATION, 0, 1)

        direction = normalize2D(hostileObj.location - agent.location)
        outObservation.HostileObjectiveDirection = direction

UpdateObservation(agent):
    obs = {}

    // Existing observation logic
    PopulateAgentState(agent, obs)
    PopulateCombatState(agent, obs)
    PopulateEnemyInfo(agent, obs)
    PopulateAllyContext(agent, obs)

    // v9.0: Add objective context
    PopulateObjectiveContext(agent, obs)

    CacheObservation(agent, obs)
```

---

### 4.2 TeamLeaderComponent Updates

**File:** `Team/Components/TeamLeaderComponent.cpp`

**Changes:**
```cpp
// PSEUDOCODE: Still discover objectives, but don't pass to MCTS for assignment

RunStrategyAssignment():
    // Still discover objectives (needed for ObservationProvider)
    DiscoverObjectives()  // Sets FriendlyObjective, HostileObjective

    // MCTS returns strategy-only assignments (no objectives)
    assignments = StrategicMCTS.RunStrategyAssignment_v820(Agents, /* ... */)

    // Broadcast to followers (no TargetObjective in assignment)
    for each assignment in assignments:
        follower = GetFollowerComponent(assignment.Agent)
        follower.ReceiveStrategyAssignment(assignment)  // Strategy only
```

---

### 4.3 FollowerAgentComponent Updates

**File:** `Team/Components/FollowerAgentComponent.cpp`

**Changes:**
```cpp
// PSEUDOCODE: Remove cached objective, accept strategy-only

// Remove member variable: CachedTargetObjective

ReceiveStrategyAssignment(assignment):
    CurrentStrategy = assignment.Strategy
    // REMOVE: CachedTargetObjective = assignment.TargetObjective

    // Trigger policy update with new strategy
    if RLPolicyNetwork:
        RLPolicyNetwork.SetActiveStrategy(CurrentStrategy)
```

---

### 4.4 Python Training Environment Updates ⭐ NEW

**File:** `CORTEX_Training/cortex_env.py`

**Changes:**
```python
# PSEUDOCODE: Update observation size and return normalization

class CortexEnv(VectorEnv):
    def __init__(self, config):
        # v9.0: Update observation size
        self.OBSERVATION_SIZE = 56  # 52 base + 4 strategy one-hot

        # v9.0: Enable return normalization (move from C++ to Python)
        self.normalize_returns = config.get('normalize_returns', True)
        self.return_rms = RunningMeanStd(shape=())
        self.gamma = config.get('gamma', 0.99)
        self.epsilon = 1e-8
        self.episode_returns = zeros(num_envs)

    def step(self, actions):
        # Execute actions...
        raw_rewards = get_rewards_from_ue5()  # No C++ clamping

        # Update episode returns
        self.episode_returns += raw_rewards

        # v9.0: Normalize rewards using return statistics
        if self.normalize_returns:
            for i, done in enumerate(dones):
                if done:
                    self.return_rms.update([self.episode_returns[i]])
                    self.episode_returns[i] = 0.0

            # Normalize rewards by return std (preserves gradients)
            rewards = raw_rewards / (sqrt(self.return_rms.var) + epsilon)

        return obs, rewards, dones, infos
```

---

## Phase 5: Validation & Testing

### 5.1 Functional Tests

**Checklist:**
- [ ] MCTS returns 4 assignments (no partial batches)
- [ ] Each assignment contains Strategy only (no TargetObjective)
- [ ] Batch cache keys are strategy-composition based
- [ ] RL reward functions use observation fields (no dynamic queries)
- [ ] Observation vector is 52 base features (46 existing + 6 objective context)
- [ ] Policy network accepts 56 inputs (52 obs + 4 strategy one-hot)
- [ ] ObservationProvider populates objective context fields correctly
- [ ] Objective distances and directions are properly normalized
- [ ] Tactical parameter rewards use gradient-based targets (not binary thresholds)
- [ ] Per-component reward normalization preserves gradients
- [ ] Python training environment uses return normalization

---

### 5.2 Behavioral Validation

**Test Scenarios:**

1. **Assault Agents:**
   - Spawn 4 Assault agents
   - Verify they move toward HostileObjective (enemy base)
   - Check reward logs show positive rewards near hostile base

2. **Defend Agents:**
   - Spawn 4 Defend agents
   - Verify they stay near FriendlyObjective (own base)
   - Check reward logs show positive rewards near friendly base

3. **Support Agents:**
   - Spawn 3 Assault + 1 Support
   - Damage an Assault agent to <50% HP
   - Verify Support agent moves toward damaged ally

4. **Retreat Agents:**
   - Spawn 1 Retreat agent
   - Surround with 3 enemies
   - Verify agent increases distance from enemies

5. **Tactical Parameter Gradients:** ⭐ NEW
   - Log tactical parameter outputs over episode
   - Verify parameters adjust continuously (not binary jumps)
   - Check reward logs show gradient-based alignment scores

---

### 5.3 Performance Validation

**Metrics to Track:**

| Metric | v8.20 Baseline | v9.0 Target | Pass Criteria |
|--------|----------------|-------------|---------------|
| MCTS Latency | 20-30ms | <25ms | ✅ Same or better |
| Observation Size | 46 base features | 52 base features | +13% (acceptable) |
| Network Input | 50 (46+4) | 56 (52+4) | +12% (acceptable) |
| Reward Clarity | Medium | High | ✅ Strategy-specific |
| Code Lines (MCTS) | ~1200 | <900 | ✅ 25% reduction |
| Win Rate | Baseline | >Baseline-5% | ✅ Maintained |
| Tactical Convergence | Baseline | 2-3× faster | ✅ Gradient improvement |
| Value Function Loss | Baseline | -30-40% reduction | ✅ Strategy-specific VFs |

---

## Phase 6: Migration & Versioning

### 6.1 Git Strategy

```bash
# Before refactoring, tag current state
git tag v8.20-explicit-objectives -m "Baseline: MCTS assigns Strategy + TargetObjective"

# Create feature branch
git checkout -b feature/v9.0-reward-driven-objectives

# After refactoring, tag new version
git tag v9.0-reward-driven -m "Refactor: MCTS strategy-only, gradient rewards, normalized returns"
```

---

### 6.2 Documentation Updates

**Files to Update:**
1. **CLAUDE.md:** Replace v8.20 architecture description with v9.0
2. **README.md:** Update system overview and architectural diagram
3. **ARCHITECTURE_EVOLUTION.md:** (New) Document v8.20 → v9.0 transition
4. **CORTEX_Training/cortex_env.py:** Update observation size to 52 base / 56 total

---

## Implementation Checklist

### Phase 1: MCTS Core (Priority 1)
- [ ] Remove objective assignment from `GenerateCompleteBatches()`
- [ ] Simplify `FBatchPrototype` struct (remove `PrimaryObjective`)
- [ ] Update `GetBatchKey()` to exclude objectives
- [ ] Remove objective mapping logic (lines 327-353 in MCTS.cpp)
- [ ] Update batch prototype definitions

### Phase 2: Rewards (Priority 1)
- [ ] Implement `CalculateAssaultReward()` (uses `Obs.HostileObjectiveDistance`)
- [ ] Implement `CalculateDefendReward()` (uses `Obs.FriendlyObjectiveDistance`)
- [ ] Implement `CalculateSupportReward()` (uses `Obs.AllyDistance`)
- [ ] Implement `CalculateRetreatReward()` (uses `Obs.DistanceToNearestEnemy`)
- [ ] **NEW:** Implement gradient-based tactical parameter effectiveness
- [ ] **NEW:** Implement per-component reward normalization
- [ ] Update main reward calculation entry point

### Phase 3: Data Structures (Priority 2)
- [ ] Remove `TargetObjective` from `FStrategyAssignment`
- [ ] Add 6 objective context fields to `FObservationElement`
- [ ] Update `ToFeatureVector()` to serialize 52 features
- [ ] Update `GetFeatureCount()` to return 52
- [ ] Update `Reset()` to initialize new fields
- [ ] Update `RLConfig::OBSERVATION_BASE_SIZE` to 52
- [ ] Update `RLConfig::OBSERVATION_SIZE` to 56
- [ ] Add reward normalization config to `RewardConfig`

### Phase 4: Integration (Priority 2)
- [ ] Update `ObservationProvider::UpdateObservation()`:
  - [ ] Add `PopulateObjectiveContext()` method
  - [ ] Query objectives from TeamLeaderComponent
  - [ ] Populate 6 objective context features
- [ ] Update `TeamLeaderComponent::RunStrategyAssignment()`
- [ ] Update `FollowerAgentComponent::ReceiveStrategyAssignment()`
- [ ] Remove `CachedTargetObjective` member variables
- [ ] **NEW:** Update Python training environment observation size to 52/56
- [ ] **NEW:** Implement return normalization in Python PPO trainer

### Phase 5: Validation (Priority 3)
- [ ] Functional tests (compilation, no crashes)
- [ ] Behavioral tests (agents follow strategies correctly)
- [ ] Performance benchmarks (latency, memory)
- [ ] Win rate validation (maintained performance)
- [ ] **NEW:** Tactical parameter convergence tests
- [ ] **NEW:** Value function loss comparison

### Phase 6: Documentation (Priority 3)
- [ ] Update CLAUDE.md with v9.0 architecture
- [ ] Create ARCHITECTURE_EVOLUTION.md
- [ ] Update README.md
- [ ] Git tagging and versioning

---

## Success Criteria

**Definition of Done:**

1. ✅ **Compilation:** No errors, no warnings
2. ✅ **MCTS Output:** Always returns 4 complete assignments (strategy-only)
3. ✅ **Observation Size:** 52 base features (verified in logs)
4. ✅ **Behavioral Correctness:**
   - Assault agents approach hostile objective
   - Defend agents stay near friendly objective
   - Support agents follow weakest ally
   - Retreat agents avoid enemies
5. ✅ **Performance:** Win rate within 5% of v8.20 baseline
6. ✅ **Code Quality:** 25% reduction in MCTS code size
7. ✅ **Documentation:** CLAUDE.md updated, evolution doc created
8. ✅ **Gradient Learning:** 2-3× faster tactical parameter convergence ⭐ NEW
9. ✅ **Value Function:** 30-40% reduction in value loss ⭐ NEW

---

## Rollback Plan

**If v9.0 shows significant performance degradation:**

```bash
# Rollback to v8.20
git checkout v8.20-explicit-objectives

# Create hotfix branch
git checkout -b hotfix/v8.20-polish
```

**Criteria for Rollback:**
- Win rate drops >10% below v8.20 baseline
- MCTS latency increases >20%
- Training convergence significantly slower

---

## Implementation Priority

### Quick Wins (1-2 days) ⭐ START HERE
1. ✅ Gradient-based tactical parameter effectiveness rewards
2. ✅ Move reward normalization to Python PPO (remove C++ clamping)

### Core Refactoring (3-5 days)
3. ✅ Remove MCTS objective assignment (Phase 1)
4. ✅ Implement strategy-specific reward functions (Phase 2.1-2.2)
5. ✅ Add objective context to observations (Phase 3.1)

### Advanced Enhancements (1 week)
6. ✅ Per-component reward normalization (Phase 2.4)
7. ✅ Python training environment updates (Phase 4.4)
8. ✅ Strategy-specific value functions (optional)

---

## Expected Outcomes

| Problem | Solution | Expected Improvement |
|---------|----------|----------------------|
| **MCTS Complexity** | Strategy-only assignment | -40% code, +20% clarity |
| **Observation-Action Gap** | Gradient rewards | 2-3× faster tactical convergence |
| **Reward Normalization** | Per-component + return norm | Preserve gradients, stable learning |
| **Multi-Modal Returns** | Strategy-specific VFs | 30-40% reduction in value loss |
| **Learning Efficiency** | Combined improvements | +50% sample efficiency (estimated) |

---

## Reference: Key Files by Component

| Component | Primary Files | Lines Changed (Est.) |
|-----------|---------------|----------------------|
| **MCTS** | AI/MCTS/MCTS.cpp, MCTS.h | ~300 lines |
| **Rewards** | RL/Components/RewardCalculator.cpp | ~250 lines (+100 for gradients) |
| **Types** | RL/RLTypes.h | ~50 lines (+20 for norm config) |
| **Observation** | Observation/ObservationElement.h/cpp | ~80 lines |
| **ObsProvider** | Observation/ObservationProvider.cpp | ~60 lines |
| **Team** | Team/Components/TeamLeaderComponent.cpp | ~50 lines |
| **Followers** | Team/Components/FollowerAgentComponent.cpp | ~30 lines |
| **Python Env** | CORTEX_Training/cortex_env.py | ~40 lines (+20 for norm) |
| **Total** | | **~860 lines** |

---

## Notes for Implementation

1. **Incremental Testing:** Test each phase independently before moving to next
2. **Log Everything:** Add verbose logging for reward calculations during validation
3. **Preserve v8.20:** Keep old code in git history for comparison/ablation studies
4. **Python Sync:** Update `cortex_env.py` observation size to 52/56 immediately after C++ changes
5. **Performance First:** If behavioral tests pass but win rate drops, investigate reward tuning before rollback
6. **Conservative Approach:** v9.0 ADDS objective context without removing features (low risk)
7. **Observation Validation:** Log objective context values to verify normalization
8. **Gradient Validation:** Log tactical parameter alignment scores to verify continuous learning ⭐ NEW
9. **Return Monitoring:** Track return statistics (mean, std) to verify normalization effectiveness ⭐ NEW

---

**End of Refactoring Guide v9.0**

**Document Version:** 2.0

**Last Updated:** 2026-01-30

**Next Review:** After Phase 5 validation complete

**Key Changes from v1.0:**
- Integrated observation-action gap solutions (gradient-based rewards)
- Integrated reward normalization solutions (per-component + return normalization)
- Simplified code examples to pseudocode format
- Consolidated redundant content
- Added implementation priority guidance
- Expanded success criteria to include learning efficiency metrics
