# CORTEX v6.0 Training Documentation

## Overview

CORTEX v6.0 implements a hierarchical MCTS-RL coordination architecture that separates concerns between strategic planning and tactical execution.

**Key Change:** MCTS assigns objectives → RL selects strategies → Rules execute tactics

## Architecture Summary

### v5.0 (Old) vs v6.0 (New)

| Component | v5.0 | v6.0 |
|-----------|------|------|
| **MCTS Role** | Assigns strategies | Assigns objectives |
| **RL Role** | Selects micro-actions (Position/Target/Fire) | Selects strategies (Assault/Defend/Support/Retreat) |
| **Rules Role** | None | Execute tactics deterministically |
| **Observation** | 65 features (64 base + 1 strategy index) | 68 features (64 base + 4 objective context) |
| **Action Space** | MultiDiscrete([4, 6, 3]) - 72 combinations | Discrete(4) - 4 strategies |
| **Network** | Multi-head (4 strategy heads × 13 outputs) | Single-head (4 strategy logits + 1 value) |
| **ONNX Export** | 4 models (assault/defend/support/retreat) | 1 model (cortex_policy_v6.onnx) |

## Changes in Detail

### 1. Observation Space: 65 → 68 Features

**Added:** 4-dimensional objective context (replaces 1-dim strategy index)

```python
# v6.0 Observation Breakdown (68 features total)
- Agent State (7):      pos(3), vel(3), health(1)
- Combat (1):           enemy_dist(1)
- Perception (32):      raycasts(16), hit_types(16)
- Enemy Info (16):      count(1), nearby(15)
- Tactical (4):         cover features(4)
- Support Context (4):  ally context(4)
- Objective Context (4): type_encoded(1), distance(1), direction(2)  # NEW
```

**Objective Context Features:**
- `type_encoded`: Normalized objective type [0.0, 0.2, 0.4, 0.6, 0.8, 1.0]
- `distance`: Normalized distance to objective [0, 1]
- `direction`: 2D direction vector to objective (X, Y)

### 2. Action Space: MultiDiscrete([4, 6, 3]) → Discrete(4)

**Simplified from 72 combinations to 4 strategies:**

```python
# v5.0 (Old)
action_space = MultiDiscrete([4, 6, 3])  # Position × Target × Fire
# 4 positions × 6 targets × 3 fire modes = 72 combinations

# v6.0 (New)
action_space = Discrete(4)  # Strategy only
# 0 = Assault, 1 = Defend, 2 = Support, 3 = Retreat
```

**Benefits:**
- 11× faster learning (4 vs 44 effective actions)
- Clearer separation of concerns (strategy vs tactics)
- Better suited for hierarchical MCTS-RL coordination

### 3. Network Architecture: Multi-Head → Single-Head

**v5.0 Multi-Head Architecture:**
```
Input (64 base + 1 strategy) → Shared Trunk [128, 128, 64]
                              ↓
                    ┌─────────┴─────────┬─────────┬─────────┐
                 Assault    Defend   Support   Retreat
                 Head (13)  Head (13) Head (13) Head (13)
                              ↓
                         Value Head (1)
```

**v6.0 Single-Head Architecture:**
```
Input (64 base + 4 objective) → Shared Trunk [128, 128, 64]
                              ↓
                    ┌─────────┴─────────┐
                Policy Head (4)    Value Head (1)
                  [Assault,           [State
                   Defend,             Value]
                   Support,
                   Retreat]
```

**Code Changes:**

```python
# v5.0 (Old) - Multi-head network
class MultiHeadTacticalPolicy(TorchModelV2, nn.Module):
    def __init__(self, ...):
        self.assault_head = SlimFC(64, 13)   # Position(4) + Target(6) + Fire(3)
        self.defend_head = SlimFC(64, 13)
        self.support_head = SlimFC(64, 13)
        self.retreat_head = SlimFC(64, 13)
        self.value_head = SlimFC(64, 1)

# v6.0 (New) - Single-head network
class SingleHeadStrategyPolicy(TorchModelV2, nn.Module):
    def __init__(self, ...):
        self.policy_head = SlimFC(64, 4)   # 4 strategy logits
        self.value_head = SlimFC(64, 1)    # State value
```

### 4. ONNX Export: 4 Models → 1 Model

**v5.0 Export:**
```bash
training_results/
├── assault_policy.onnx   # 64 → 13 (Position/Target/Fire)
├── defend_policy.onnx    # 64 → 13
├── support_policy.onnx   # 64 → 13
└── retreat_policy.onnx   # 64 → 13
```

**v6.0 Export:**
```bash
training_results/
└── cortex_policy_v6.onnx  # 68 → (4 policy + 1 value)
```

**Model Structure:**
- **Input:** `observation` [batch_size, 68]
- **Output 1:** `policy_logits` [batch_size, 4] - Strategy probabilities
- **Output 2:** `value` [batch_size, 1] - State value estimate

### 5. Action Masking Simplified

**v5.0:** Complex strategy-dependent masking
```python
# Different masks per strategy
if strategy == Assault:
    position_mask = [0, 1, 0, 1]  # Only forward moves
    target_mask = [1, 1, 1, 1, 1, 1]
    fire_mask = [1, 1, 1]
```

**v6.0:** All strategies always valid
```python
# MCTS assigns objectives, RL selects best strategy
action_mask = [1, 1, 1, 1]  # All 4 strategies valid
```

## Training Workflow

### 1. Environment Setup

```python
from sbdapm_env import SBDAPMMultiAgentEnv

# v6.0 Environment
env = SBDAPMMultiAgentEnv(
    host="localhost",
    port=50051,
    max_episode_steps=1000
)

# Observation space: Box(68,)
# Action space: Discrete(4)
```

### 2. Training Script

```bash
# Start UE5 with Schola plugin
# Run training
python train_rllib.py --iterations 100

# Model exports to: training_results/<timestamp>/cortex_policy_v6.onnx
```

### 3. Network Configuration

```python
config.model = {
    "custom_model": "single_head_strategy_policy",
    "custom_model_config": {
        "obs_dim": 68,           # 64 base + 4 objective context
        "hidden_layers": [128, 128, 64],
        "num_outputs": 4,        # 4 strategy logits
    }
}
```

### 4. ONNX Export

```python
# Automatic export after training
# Output: cortex_policy_v6.onnx
# Structure:
#   Input: observation [batch, 68]
#   Output 1: policy_logits [batch, 4]
#   Output 2: value [batch, 1]
```

## Deployment to UE5

### 1. Copy Model

```bash
cp training_results/<timestamp>/cortex_policy_v6.onnx \
   <UE_Project>/Content/Models/cortex_policy_v6.onnx
```

### 2. Update C++ Code

The C++ side should already be configured for v6.0 if you completed Phases 1-7:

```cpp
// RLPolicyNetwork.cpp (v6.0)
EStrategyType URLPolicyNetwork::GetStrategy(
    const FObservationElement& Observation,
    const FObjectiveContext& ObjectiveContext)
{
    // Build 68-feature input
    TArray<float> InputFeatures = BuildNetworkInput(Observation, ObjectiveContext);
    check(InputFeatures.Num() == 68);

    // Forward pass → 4 strategy logits
    FNetworkOutput Output = ForwardPass(InputFeatures);

    // Sample strategy
    return SampleStrategy(Output.PolicyLogits);
}
```

### 3. Verify Integration

```cpp
// Test in UE5
UFollowerAgentComponent* Follower = ...;
FObservationElement Obs = Follower->BuildObservation();
FObjectiveContext ObjCtx = Follower->BuildObjectiveContext(AssignedObjective);

EStrategyType Strategy = RLPolicy->GetStrategy(Obs, ObjCtx);
// Strategy ∈ {Assault, Defend, Support, Retreat}
```

## Testing

Run the test suite to verify Python changes:

```bash
cd CORTEX_Training
python test_v6_architecture.py
```

**Expected Output:**
```
============================================================
CORTEX v6.0 Architecture Test Suite
============================================================

=== Testing Observation Space ===
[OK] Observation space shape: (68,)
...

=== Testing Action Space ===
[OK] Action space: Discrete(4)
...

=== Testing Network Architecture ===
[OK] Network created successfully
...

============================================================
[OK] ALL TESTS PASSED
============================================================
```

## Migration Guide (v5.0 → v6.0)

### For Existing Training Checkpoints

⚠️ **v5.0 checkpoints are NOT compatible with v6.0** due to architecture changes.

**Options:**
1. **Start fresh:** Train new v6.0 model from scratch (recommended)
2. **Convert weights:** Extract shared trunk weights from v5.0, retrain heads (advanced)

### For Training Scripts

**Update environment config:**
```python
# v5.0
self._obs_space = spaces.Box(shape=(65,), ...)
self._action_space = spaces.MultiDiscrete([4, 6, 3])

# v6.0
self._obs_space = spaces.Box(shape=(68,), ...)
self._action_space = spaces.Discrete(4)
```

**Update network registration:**
```python
# v5.0
ModelCatalog.register_custom_model("multi_head_tactical_policy", MultiHeadTacticalPolicy)

# v6.0
ModelCatalog.register_custom_model("single_head_strategy_policy", SingleHeadStrategyPolicy)
```

## Performance Expectations

### Learning Speed

| Metric | v5.0 | v6.0 | Improvement |
|--------|------|------|-------------|
| Action Space Size | 72 (4×6×3) | 4 | 18× smaller |
| Effective Actions | 44 (after masking) | 4 | 11× smaller |
| Expected Convergence | ~50K steps | ~5K steps | 10× faster |

### Runtime Performance

| Operation | Target Latency | Notes |
|-----------|----------------|-------|
| RL Inference (single) | 1-3ms | Per agent |
| RL Inference (batched) | <4ms | 4 agents batched (2.6× faster) |
| MCTS Assignment | 30-50ms | Async, doesn't block |
| StateTree Execution | <0.5ms | Per agent |
| **Total AI Frame** | **<10ms** | **4v4 scenario** |

## Troubleshooting

### Issue: "Observation shape mismatch"

**Error:** `Expected 65 features, got 68`

**Solution:** Ensure C++ side sends 68-feature observations:
```cpp
// TacticalObserver.cpp
TArray<float> Features = BaseObservation.ToFeatureVector();  // 64 features
TArray<float> ObjectiveFeatures = ObjectiveContext.ToFeatureVector();  // 4 features
Features.Append(ObjectiveFeatures);  // 68 total
```

### Issue: "Action out of range"

**Error:** `Action 4 out of range for Discrete(4)`

**Solution:** Clamp actions in environment:
```python
# sbdapm_env.py
action_value = np.clip(action_value, 0, 3)
```

### Issue: "ONNX export failed"

**Error:** `No module named 'onnxscript'`

**Solution:** Install dependencies:
```bash
pip install onnxscript onnx
```

## References

- **Refactoring Plan:** `REFACTORING_PLAN_v6.0.md`
- **C++ Implementation:** Phases 1-7 in plan
- **Python Test Suite:** `test_v6_architecture.py`
- **Environment:** `sbdapm_env.py`
- **Training Script:** `train_rllib.py`

## Academic Merit

v6.0 architecture enables novel research contributions:

1. **Hierarchical MCTS-RL:** Strategic assignment + tactical adaptation
2. **Synergistic Learning:** MCTS benefits from RL value estimates, RL benefits from MCTS objectives
3. **Real-Time Coordination:** <10ms decision making for 4v4 combat
4. **Emergent Behaviors:** Complex team tactics from simple strategy primitives

**Target Venues:** CoG 2026, AAMAS 2026

---

**Version:** 6.0
**Last Updated:** 2026-01-06
**Author:** CORTEX Team
