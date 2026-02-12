# EQS Asset Creation Guide
**MOC v10.2 - Environmental Query System**

---

## 1. Overview

Environmental Query System (EQS) is UE5's spatial reasoning system. In MOC v10.2, EQS translates 7-dimensional tactical parameters from the RL policy into actual navigation positions.

**Architecture Position:**
```
RL Policy → 7-dim EQS Weights → EQS Query → 48 Sample Points → Best Position → Navigation
```

---

## 2. The 7 EQS Weights (v10.2 Reference)

**CRITICAL:** These weight names are defined in `FEQSWeightParameters` (EQSTypes.h) and must match exactly:

| Index | Weight Name | Purpose | Range | Example |
|-------|-------------|---------|-------|---------|
| **[0]** | **EnemyObjectiveProximity** | Distance to enemy base | [-1, 1] | +1.0 = rush enemy, -1.0 = avoid |
| **[1]** | **AllyObjectiveProximity** | Distance to friendly base | [-1, 1] | +1.0 = defend home, -1.0 = abandon |
| **[2]** | **CoverDensity** | Proximity to cover | [-1, 1] | +1.0 = seek cover, -1.0 = avoid cover |
| **[3]** | **EnemyVisibility** | Line-of-sight to enemies | [-1, 1] | +1.0 = expose/engage, -1.0 = hide |
| **[4]** | **AllyProximity** | Distance to teammates | [-1, 1] | +1.0 = group up, -1.0 = solo |
| **[5]** | **CombatRange** | Engagement distance preference | [-1, 1] | +1.0 = long range, -1.0 = melee |
| **[6]** | **PickupProximity** | Distance to health/ammo | [-1, 1] | +1.0 = collect, -1.0 = ignore |

**Source Files:**
- Struct Definition: `Public/Types/EQSTypes.h` (FEQSWeightParameters)
- Actuator: `Public/Schola/Actuators/TacticalParameterActuator.h`
- EQS Contexts: `Public/AI/EQS/MocEQSContext.h/.cpp`

---

## 3. Available EQS Contexts

EQS Contexts provide spatial information for tests. MOC v10.2 includes these context providers:

| Context Class | What It Provides | Used By Weights |
|---------------|------------------|-----------------|
| **EnvQueryContext_MocQuerier** | Agent's own position | All tests (as origin) |
| **EnvQueryContext_MocEnemies** | Visible enemy positions (via fog of war) | [3] EnemyVisibility, [5] CombatRange|
| **EnvQueryContext_MocAllies** | Teammate positions | [4] AllyProximity |
| **EnvQueryContext_MocEnemyObjective** | Enemy team's base (PointA/PointE) | [0] EnemyObjectiveProximity |
| **EnvQueryContext_MocAllyObjective** | Friendly team's base (PointA/PointE) | [1] AllyObjectiveProximity |
| **EnvQueryContext_MocCoverPoints** | Cover locations (tagged "Cover") | [2] CoverDensity |
| **EnvQueryContext_MocPickups** | Health/ammo pickups (tagged "Pickup") | [6] PickupProximity |
| **EnvQueryContext_MocCapturePoints** | All capture points (deprecated for v10.2) | ⚠️ Use MocEnemyObjective/MocAllyObjective instead |

**Important Notes:**
- **Cover Points:** Requires actors tagged "Cover" in the level. Without cover points, CoverDensity weight [2] will have no effect.
- **Pickups:** Requires actors tagged "Pickup" in the level for PickupProximity weight [6].
- **Objectives:** Automatically detects PointA (Red base) and PointE (Blue base) from placed ACapturePoint actors.

---

## 4. Quick Setup: Creating an EQS Query Asset

### Step 1: Create the EQS Query
1. Open Content Browser in Unreal Editor
2. Navigate to: `Content/Game/AI/EQS/`
3. Right-click → **Artificial Intelligence** → **Environment Query**
4. Name it: `EQS_MOC_TacticalPositioning`

### Step 2: Configure the Query Root
1. Double-click to open the EQS Editor
2. Select the **Root** node
3. Set properties:
   - **Query Mode:** `All Generators`
   - **Run Mode:** `Single Result` (we want one best position)

---

## 5. Core Components

### 5.1 Generator: Points in Cone
This generates candidate positions around the agent.

**Setup:**
1. Right-click Root → Add Generator → **Points in Cone**
2. Configure:
   - **Aligned Points Per Ring:** 8 (circular distribution)
   - **Number of Rings:** 6 (layered depth)
   - **Ring Distance:** 500.0 (units between rings)
   - **Cone Direction:** Forward Vector
   - **Cone Angle:** 180.0 (half-sphere in front)
   - **Total Points:** 48 samples (8 × 6)

**Result:** Generates 48 candidate positions in a cone ahead of the agent.

---

## 6. The 8 EQS Tests (Weighted by RL Policy)

Each test scores the 48 candidate positions. The RL policy provides dynamic weights via the TacticalParameterActuator.

**IMPORTANT:** Weight values are in range **[-1, 1]** where:
- **Negative values (-1):** Avoid/minimize the criterion
- **Zero (0):** Neutral/ignore the test
- **Positive values (+1):** Prefer/maximize the criterion

### Test 1: Enemy Objective Proximity
**Purpose:** Control approach to enemy base (offensive positioning)
- **Test Type:** Distance
- **Distance To:** **EnvQueryContext_MocEnemyObjective** (Enemy team's base - PointA for Red, PointE for Blue)
- **Scoring:** Inverse (closer = higher score when weight > 0)
- **Weight Source:** EQS Weight [0] - **EnemyObjectiveProximity**
- **Example:** +1.0 = "rush enemy base", -1.0 = "stay away from enemy base"

### Test 2: Ally Objective Proximity
**Purpose:** Control defensive positioning near friendly base
- **Test Type:** Distance
- **Distance To:** **EnvQueryContext_MocAllyObjective** (Friendly team's base - PointA for Red, PointE for Blue)
- **Scoring:** Inverse (closer = higher score when weight > 0)
- **Weight Source:** EQS Weight [1] - **AllyObjectiveProximity**
- **Example:** +1.0 = "defend home base", -1.0 = "abandon defense"

### Test 3: Cover Density
**Purpose:** Prioritize positions with available cover
- **Test Type:** Distance
- **Distance To:** **EnvQueryContext_MocCoverPoints** (Actors tagged "Cover" in level)
- **Scoring:** Inverse (closer to cover = higher score when weight > 0)
- **Weight Source:** EQS Weight [2] - **CoverDensity**
- **Example:** +1.0 = "seek cover", -1.0 = "avoid cover"
- **⚠️ Note:** Requires cover actors placed in level with "Cover" tag

### Test 4: Enemy Visibility
**Purpose:** Control line-of-sight exposure to enemies
- **Test Type:** Trace
- **Trace Channel:** Visibility
- **Trace To:** **EnvQueryContext_MocEnemies** (Visible enemy positions via fog of war)
- **Scoring:** Boolean (1.0 if visible, 0.0 if hidden)
- **Weight Source:** EQS Weight [3] - **EnemyVisibility**
- **Example:** +1.0 = "expose to engage", -1.0 = "stay hidden"

### Test 5: Ally Proximity
**Purpose:** Control formation cohesion with teammates
- **Test Type:** Distance
- **Distance To:** **EnvQueryContext_MocAllies** (Teammate positions)
- **Scoring:** Inverse (closer to teammates = higher score when weight > 0)
- **Weight Source:** EQS Weight [4] - **AllyProximity**
- **Example:** +1.0 = "group up", -1.0 = "solo play"

### Test 6: Combat Range
**Purpose:** Preferred engagement distance from enemies
- **Test Type:** Distance
- **Distance To:** **EnvQueryContext_MocEnemies** (Closest visible enemy)
- **Scoring:** Gaussian (peak at ideal engagement range ~1500cm)
- **Weight Source:** EQS Weight [5] - **CombatRange**
- **Example:** +1.0 = "long range preference", -1.0 = "close range preference", 0.0 = "neutral"

### Test 7: Pickup Proximity
**Purpose:** Prioritize resource collection (health/ammo)
- **Test Type:** Distance
- **Distance To:** **EnvQueryContext_MocPickups** (Health packs, ammo crates)
- **Scoring:** Inverse (closer to pickups = higher score when weight > 0)
- **Weight Source:** EQS Weight [6] - **PickupProximity**
- **Example:** +1.0 = "collect resources", -1.0 = "ignore pickups"


---

## 7. Adding Tests to Your Query

### Step-by-Step for Each Test:
1. Right-click Generator node → **Add Test**
2. Choose test type (Distance, Trace, PathFinding, etc.)
3. Configure test parameters (see above)
4. Set **Scoring Equation:**
   - Linear: Proportional scaling
   - Inverse: Reverse scaling (closer = higher score)
   - Gaussian: Bell curve (peak at ideal value)
   - Boolean: Binary (pass/fail)
5. Set **Test Purpose:** Filter or Score
   - **Filter:** Hard constraint (removes invalid points)
   - **Score:** Soft preference (weighted contribution)

### Weight Configuration:
Each test should have a **Weight** parameter. In v10.2, these are **dynamically set** by the RL policy via the actuator. For testing purposes, you can set default values (e.g., 1.0 for all).

---

## 8. Visualization and Testing

### In-Editor Debugging:
1. Select an AI agent in the level
2. Open **Gameplay Debugger** (Apostrophe key: `'`)
3. Press **[3]** to toggle EQS display
4. You'll see:
   - **Green Spheres:** High-scoring positions
   - **Red Spheres:** Low-scoring positions
   - **Best Result:** Largest sphere (selected position)

### Manual Testing:
1. Set default weights in Blueprint:
   ```cpp
   // Example: Aggressive Assault loadout
   // [EnemyObjProx, AllyObjProx, CoverDensity, EnemyVis, AllyProx, CombatRange, PickupProx]
   EQS Weights: [0.8, -0.3, 0.2, 0.6, 0.1, 0.5, -0.2]

   // Example: Defensive Support loadout
   EQS Weights: [-0.5, 0.9, 0.8, -0.2, 0.7, -0.3, 0.5]

   // Example: Balanced loadout
   EQS Weights: [0.3, 0.3, 0.5, 0.4, 0.4, 0.0, 0.2]
   ```
2. Run simulation and observe positioning behavior
3. Adjust individual weights to see impact

---

## 9. Integration with MOC v10.2

### C++ Side (TacticalParameterActuator):
```cpp
// Actuator receives 7-dim vector from Python RL policy
void UTacticalParameterActuator::TakeAction(const FBoxPoint& Action)
{
    // Action.Values = [-1.0, 1.0]^7
    // Dimension mapping:
    // [0] EnemyObjectiveProximity
    // [1] AllyObjectiveProximity
    // [2] CoverDensity
    // [3] EnemyVisibility
    // [4] AllyProximity
    // [5] CombatRange
    // [6] PickupProximity

    FEQSWeightParameters Weights = ActionToEQSWeights(Action);
    AIController->UpdateBlackboardWeights(Weights);
}
```

### Blueprint Side (MOC Character):
1. In `BP_MocCharacter`:
2. Find **EQS Query** component
3. Set **Query Template:** `EQS_MOC_TacticalPositioning`
4. Bind **Dynamic Weight Update** function:
   ```
   UpdateEQSWeights(float[] Weights) → Run EQS Query
   ```

### Runtime Flow (Training Time):
```
1. Squad Commander assigns role (Assault/Defend/Support)
2. SetCommandedStrategy() called on AMocCharacter
3. RL Policy (Python) observes commanded strategy + local state
4. Outputs 8 EQS weights [-1, 1]^8
5. TakeAction() converts to FEQSWeightParameters
6. AIController->UpdateBlackboardWeights(Weights)
7. EQS runs with dynamic weights, scores 48 samples
8. Best position selected → Navigation system
```

### Runtime Flow (Inference Time):
```
1. Squad Commander assigns role (Assault/Defend/Support)
2. SetCommandedStrategy() called on AMocCharacter
3. MocPolicyExecutor::InferWeights(Strategy, LocalObs) [C++ ONNX inference]
4. Returns 8 EQS weights [-1, 1]^8
5. AIController->UpdateBlackboardWeights(Weights)
6. EQS runs with dynamic weights, scores 48 samples
7. Best position selected → Navigation system
```

---

## 10. Advanced: Custom Tests

If you need additional tests (e.g., grenade avoidance, ammo pickups):

### Create Custom EnvQueryTest:
```cpp
UCLASS()
class UEnvQueryTest_GrenadeAvoidance : public UEnvQueryTest
{
    GENERATED_BODY()

public:
    virtual void RunTest(FEnvQueryInstance& QueryInstance) const override
    {
        // Custom scoring logic
    }
};
```

Then add it to your EQS Query like any standard test.

---

## 11. Common Issues & Solutions

| Issue | Cause | Solution |
|-------|-------|----------|
| All samples score 0.0 | Missing context actors (enemies, cover) | Ensure perception system is updating, check EQS contexts |
| Query returns same position | Weights not updating | Verify actuator is calling UpdateBlackboardWeights() |
| Character gets stuck | Navigability issues | Add a separate PathFinding filter test (hard constraint) |
| Ignores enemy base | EnemyObjectiveProximity weight = 0 | Check RL policy output distribution for [0] |
| Ignores friendly base | AllyObjectiveProximity weight = 0 | Check RL policy output distribution for [1] |
| Suicidal behavior | CoverDensity + EnemyVisibility misconfigured | Increase CoverDensity [2], decrease EnemyVisibility [3] |
| Always alone | AllyProximity weight negative | Check [4] weight - positive values encourage grouping |
| Ignores pickups at low health | PickupProximity weight = 0 or negative | Check [6] weight - should be positive when resources needed |

---

## 12. Reference Files

- **EQS Test Documentation:** [UE5 EQS Manual](https://docs.unrealengine.com/5.0/en-US/environmental-query-system-in-unreal-engine/)
- **MOC Architecture:** `CLAUDE.md` (v10.2 spec)
- **Actuator Implementation:** `TacticalParameterActuator.h/cpp`
- **Character Integration:** `AMocCharacter::RunEQSQuery()`

---

## Quick Checklist
- [ ] Create EQS Query asset in `Content/Game/AI/EQS/`
- [ ] Add Points in Cone generator (48 samples)
- [ ] Add all 8 weighted tests
- [ ] Configure scoring equations for each test
- [ ] Set default weights for testing
- [ ] Assign query to MOC Character Blueprint
- [ ] Test with Gameplay Debugger ([3] key)
- [ ] Verify dynamic weight updates from actuator

---

**Version:** v10.2
**Last Updated:** 2026-02-11
**Author:** MOC Development Team
