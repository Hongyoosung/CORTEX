# Python v10.2 Integration Guide

**Date:** 2026-02-11
**Architecture:** MOC v10.2 Commander-Executor
**Training Script:** `phase1_policy_training_v10_2.py`

---

## Overview

This guide shows how to integrate the v10.2 TacticalParameterActuator with Python training scripts. The key change is that **agents are pure executors** that receive commanded strategies from a centralized Squad Commander.

---

## Architecture Flow

```
┌─────────────────────────────────────────────────────┐
│ UE5: Squad Commander (ASquadManager)                │
│ • Performs MCTS planning                            │
│ • Outputs: Tactical Play → Role Distribution       │
│ • Calls: SetCommandedStrategy(Assault/Defend/...)  │
└─────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────┐
│ Python: Training Script (v10.2)                     │
│ • Receives: commanded_strategy (0/1/2)              │
│ • Receives: local_observation (52-dim)              │
│ • Runs: policy(obs, strategy) → eqs_weights (8-dim) │
│ • Sends: eqs_weights to UE5 via Schola              │
└─────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────┐
│ UE5: TacticalParameterActuator_v10_2                │
│ • TakeAction(eqs_weights)                           │
│ • Validates: 8-dim, range [-1, 1]                   │
│ • Applies: AIController->UpdateBlackboardWeights()  │
└─────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────┐
│ UE5: EQS System → Spatial Reasoning → Navigation    │
└─────────────────────────────────────────────────────┘
```

---

## Quick Start Example

### 1. Import and Initialize

```python
import torch
import numpy as np
from phase1_policy_training_v10_2 import MultiHeadRLPolicy_v10_2, PPOTrainer_v10_2

# Initialize v10.2 policy
policy = MultiHeadRLPolicy_v10_2(
    obs_dim=52,           # Local observation only
    num_strategies=3,     # Assault, Defend, Support
    eqs_dim=8,           # 8-dimensional EQS weights
    hidden_dims=[256, 256]
)

# Initialize trainer
trainer = PPOTrainer_v10_2(
    policy=policy,
    learning_rate=3e-4,
    device='cuda' if torch.cuda.is_available() else 'cpu'
)

print("v10.2 Policy Ready!")
policy.print_architecture()
```

### 2. Training Loop Integration

```python
# Connect to UE5 via Schola
from schola import ScholaEnvironment

env = ScholaEnvironment(host='localhost', port=50051)

# Training episode
obs = env.reset()  # Returns: {'observation': (52,), 'commanded_strategy': int}
done = False

while not done:
    # Extract commanded strategy from environment
    local_obs = obs['observation']          # (52,) local state
    commanded_strategy = obs['commanded_strategy']  # 0=Assault, 1=Defend, 2=Support

    # Convert to tensors
    obs_tensor = torch.tensor(local_obs, dtype=torch.float32).unsqueeze(0)
    strategy_tensor = torch.tensor([commanded_strategy], dtype=torch.long)

    # Inference
    with torch.no_grad():
        eqs_weights = policy(obs_tensor, strategy_tensor)  # Output: (1, 8) in [-1, 1]
        eqs_weights = eqs_weights.squeeze(0).cpu().numpy()  # Convert to numpy

    # Send action to UE5
    # The actuator will receive this as FBoxPoint(8-dim) and convert to FEQSWeightParameters
    next_obs, reward, done, info = env.step(action=eqs_weights)

    # Store transition for training
    # ... (add to replay buffer, update policy with PPO)

    obs = next_obs
```

---

## Action Space Specification

### v10.2 Action Space: Box([-1, 1]^8)

```python
import numpy as np

# Example EQS weights (valid action)
action = np.array([
    0.8,   # [0] EnemyObjectiveProximity (approach enemy)
    -0.3,  # [1] AllyObjectiveProximity (don't hug base)
    0.6,   # [2] CoverDensity (prefer cover)
    0.4,   # [3] EnemyVisibility (moderate exposure)
    0.2,   # [4] AllyProximity (loose formation)
    0.0,   # [5] CombatRange (neutral)
    -0.5,  # [6] PickupProximity (ignore pickups)
    0.7,   # [7] HeightAdvantage (seek high ground)
], dtype=np.float32)

# Validate range
assert action.shape == (8,), "Action must be 8-dimensional"
assert (action >= -1.0).all() and (action <= 1.0).all(), "Action must be in [-1, 1]"
```

### Weight Semantics

| Index | Parameter | Meaning (-1) | Meaning (0) | Meaning (+1) |
|-------|-----------|--------------|-------------|--------------|
| 0 | EnemyObjectiveProximity | Avoid enemy base | Neutral | Approach enemy base |
| 1 | AllyObjectiveProximity | Avoid friendly base | Neutral | Defend friendly base |
| 2 | CoverDensity | Ignore cover | Neutral | Prioritize cover |
| 3 | EnemyVisibility | Hide from enemies | Neutral | Expose/engage |
| 4 | AllyProximity | Solo play | Neutral | Group with teammates |
| 5 | CombatRange | Close range | Medium | Long range |
| 6 | PickupProximity | Ignore pickups | Neutral | Prioritize health/ammo |
| 7 | HeightAdvantage | Low ground | Neutral | High ground |

---

## Observation Format

### Input to Policy: 52-dim Local Observation

```python
# Observation structure (from UMocTacticalObserver)
observation = {
    # Self state (9-dim)
    'health': 0.8,              # [0] Health percentage
    'position_x': 1500.0,       # [1] X position (cm)
    'position_y': -2000.0,      # [2] Y position (cm)
    'rotation_yaw': 45.0,       # [3] Rotation (degrees)
    'velocity_x': 100.0,        # [4-6] Velocity vector
    'velocity_y': 50.0,
    'velocity_z': 0.0,
    'can_fire': 1.0,            # [7] Weapon ready
    'current_strategy': 0,      # [8] Currently assigned strategy

    # Allies (4 agents × 4 features = 16-dim)
    # For each ally: [rel_x, rel_y, health, distance]
    'allies': [...],            # [9-24]

    # Enemies (5 agents × 4 features = 20-dim)
    # For each enemy: [rel_x, rel_y, health, distance]
    'enemies': [...],           # [25-44]

    # Map state (7-dim)
    'ally_objective': [x, y, distance],     # [45-47]
    'enemy_objective': [x, y, distance],    # [48-50]
    'boundary_distance': 500.0,             # [51]
}

# Total: 52 dimensions
```

### Commanded Strategy Format

```python
# Strategy mapping
STRATEGY_MAP = {
    0: 'Assault',   # Aggressive, push forward
    1: 'Defend',    # Defensive, hold position
    2: 'Support',   # Supportive, help teammates
}

# Received from Squad Commander via Schola
commanded_strategy = env.get_commanded_strategy(agent_id)  # Returns: 0, 1, or 2
```

---

## Policy Architecture

### Network Structure

```
Input: [obs (52) + strategy_onehot (3)] = 55-dim
    ↓
Shared Encoder: [256 → 256] ReLU + LayerNorm
    ↓
┌────────────┬────────────┬────────────┐
│  Assault   │   Defend   │  Support   │
│   Head     │    Head    │    Head    │
│  64 → 8    │  64 → 8    │  64 → 8    │
│   Tanh     │   Tanh     │   Tanh     │
└────────────┴────────────┴────────────┘
    ↓
Output: 8-dim EQS weights in [-1, 1]
```

### Strategy-Specific Heads

Each strategy head learns specialized behaviors:

- **Assault Head**: Aggressive positioning, enemy engagement
- **Defend Head**: Defensive positioning, cover usage
- **Support Head**: Cooperative positioning, ally proximity

---

## Training Configuration

### Recommended Hyperparameters

```python
trainer = PPOTrainer_v10_2(
    policy=policy,
    learning_rate=3e-4,        # PPO learning rate
    clip_epsilon=0.2,          # PPO clip parameter
    value_coef=0.5,           # Value loss coefficient
    entropy_coef=0.01,        # Entropy bonus (exploration)
    max_grad_norm=0.5,        # Gradient clipping
    device='cuda'
)

# Replay buffer
replay_buffer = StrategyBalancedReplayBuffer(
    capacity=100000  # 100k transitions
)

# Training settings
BATCH_SIZE = 256
UPDATE_FREQUENCY = 2048  # Update every 2048 transitions
PPO_EPOCHS = 10
```

### Strategy Balancing

The `StrategyBalancedReplayBuffer` ensures equal representation:

```python
# Buffer maintains 3 sub-buffers (one per strategy)
# Each gets capacity // 3 = 33,333 transitions

# Sampling returns balanced batch
batch = replay_buffer.sample(batch_size=256)
# Result: ~85 Assault, ~85 Defend, ~86 Support transitions

# Check distribution
distribution = replay_buffer.get_strategy_distribution()
print(distribution)
# Output: {0: 0.33, 1: 0.34, 2: 0.33}
```

---

## ONNX Export for UE5

### Export Trained Policy

```python
# After training, export to ONNX
policy.export_onnx(
    filepath='policy_weights_v10_2.onnx',
    batch_size=1
)
```

### UE5 Loading (C++ Side)

```cpp
// In UMocPolicyExecutor.cpp
bool UMocPolicyExecutor::LoadModel(const FString& ModelPath)
{
    // Load ONNX model
    ONNXSession = LoadONNXModel(ModelPath);

    // Expected inputs:
    // - observation: (1, 52) float32
    // - strategy_index: (1,) int64

    // Expected output:
    // - eqs_weights: (1, 8) float32 in [-1, 1]

    bModelLoaded = true;
    return true;
}

FEQSWeightParameters UMocPolicyExecutor::InferWeights(
    EStrategyType CommandedStrategy,
    const FObservation& LocalObservation
)
{
    // Convert to tensors
    TArray<float> ObsArray = LocalObservation.ToArray();  // 52-dim
    int32 StrategyIdx = static_cast<int32>(CommandedStrategy);

    // Run inference
    TArray<float> Outputs = RunONNXInference(ONNXSession, ObsArray, StrategyIdx);

    // Convert to EQS weights
    return FEQSWeightParameters::FromArray(Outputs);
}
```

---

## Debugging and Validation

### 1. Check Action Range

```python
# After inference
eqs_weights = policy(obs_tensor, strategy_tensor).cpu().numpy()

print(f"EQS Weights: {eqs_weights}")
print(f"Min: {eqs_weights.min():.3f}, Max: {eqs_weights.max():.3f}")
print(f"In range [-1, 1]? {(eqs_weights >= -1).all() and (eqs_weights <= 1).all()}")
```

### 2. Visualize Strategy-Specific Outputs

```python
import matplotlib.pyplot as plt

# Test all strategies with same observation
obs = torch.randn(1, 52)

with torch.no_grad():
    assault_weights = policy(obs, torch.tensor([0])).numpy()[0]
    defend_weights = policy(obs, torch.tensor([1])).numpy()[0]
    support_weights = policy(obs, torch.tensor([2])).numpy()[0]

# Plot
labels = ['E_Obj', 'A_Obj', 'Cover', 'Vis', 'Ally', 'Rng', 'Pick', 'H_Adv']
x = np.arange(len(labels))

plt.figure(figsize=(12, 6))
plt.bar(x - 0.2, assault_weights, 0.2, label='Assault', alpha=0.8)
plt.bar(x, defend_weights, 0.2, label='Defend', alpha=0.8)
plt.bar(x + 0.2, support_weights, 0.2, label='Support', alpha=0.8)
plt.xticks(x, labels)
plt.ylabel('EQS Weight')
plt.ylim(-1.1, 1.1)
plt.axhline(0, color='k', linestyle='--', alpha=0.3)
plt.legend()
plt.title('Strategy-Specific EQS Weights (Same Observation)')
plt.grid(axis='y', alpha=0.3)
plt.tight_layout()
plt.savefig('strategy_weights_comparison.png')
print("Saved: strategy_weights_comparison.png")
```

### 3. Log Training Progress

```python
from torch.utils.tensorboard import SummaryWriter

writer = SummaryWriter(log_dir='runs/v10_2_training')

# During training
for step in range(num_steps):
    # ... training code ...

    # Log metrics
    writer.add_scalar('train/policy_loss', policy_loss, step)
    writer.add_scalar('train/value_loss', value_loss, step)
    writer.add_scalar('train/entropy', entropy, step)

    # Log strategy distribution
    dist = replay_buffer.get_strategy_distribution()
    writer.add_scalar('strategy/assault', dist[0], step)
    writer.add_scalar('strategy/defend', dist[1], step)
    writer.add_scalar('strategy/support', dist[2], step)

    # Log EQS weight ranges (per dimension)
    for i in range(8):
        writer.add_histogram(f'eqs_weights/dim_{i}', eqs_weights[:, i], step)

writer.close()

# View in TensorBoard
# $ tensorboard --logdir runs/v10_2_training
```

---

## Schola Environment Interface

### Expected Modifications to Schola Env

```python
class MocEnvironment_v10_2(gym.Env):
    """
    v10.2 environment that provides commanded strategies.
    """

    def __init__(self, host='localhost', port=50051):
        self.client = ScholaClient(host, port)

        # v10.2: Action space is 8-dim EQS weights in [-1, 1]
        self.action_space = spaces.Box(
            low=-1.0,
            high=1.0,
            shape=(8,),
            dtype=np.float32
        )

        # Observation includes commanded strategy
        self.observation_space = spaces.Dict({
            'observation': spaces.Box(
                low=-np.inf,
                high=np.inf,
                shape=(52,),
                dtype=np.float32
            ),
            'commanded_strategy': spaces.Discrete(3)  # 0, 1, or 2
        })

    def reset(self):
        """Reset environment and get initial state."""
        response = self.client.Reset()

        return {
            'observation': np.array(response.observation, dtype=np.float32),
            'commanded_strategy': response.commanded_strategy
        }

    def step(self, action):
        """Execute action and get next state."""
        # Validate action
        assert action.shape == (8,), "Action must be 8-dimensional"
        assert (action >= -1.0).all() and (action <= 1.0).all(), "Action out of range"

        # Send to UE5
        response = self.client.Step(eqs_weights=action.tolist())

        next_obs = {
            'observation': np.array(response.observation, dtype=np.float32),
            'commanded_strategy': response.commanded_strategy
        }

        reward = response.reward
        done = response.done
        info = response.info

        return next_obs, reward, done, info
```

---

## Comparison: v8.0 vs v10.2

| Aspect | v8.0 | v10.2 |
|--------|------|-------|
| **Action Dim** | 5 (4 tactical + 1 combat) | 8 (EQS weights) |
| **Action Range** | [0, 1] | [-1, 1] |
| **Input** | 56-dim (52 + 4 strategy) | 55-dim (52 + 3 strategy) |
| **Strategy Context** | 4 strategies + Retreat | 3 strategies (Assault/Defend/Support) |
| **Planning** | Decentralized (per-agent MCTS) | Centralized (Squad Commander MCTS) |
| **Agent Role** | Planner + Executor | Pure Executor |
| **Coordination** | Implicit | Explicit (commanded roles) |

---

## Troubleshooting

### Issue 1: Actions Out of Range

**Symptom:** UE5 logs show weights > 1.0 or < -1.0

**Solution:** Check tanh activation is present in policy heads

```python
# Correct (v10.2)
self.assault_head = nn.Sequential(
    nn.Linear(256, 64),
    nn.ReLU(),
    nn.Linear(64, 8),
    nn.Tanh()  # ← Must have this!
)
```

### Issue 2: Strategy Not Received

**Symptom:** Policy always uses default strategy

**Solution:** Verify Schola environment returns `commanded_strategy`:

```python
# Check observation structure
obs = env.reset()
print(obs.keys())  # Should contain: ['observation', 'commanded_strategy']
print(obs['commanded_strategy'])  # Should be 0, 1, or 2
```

### Issue 3: Policy Not Learning

**Symptom:** Loss not decreasing, entropy constant

**Solution:** Check strategy distribution in replay buffer:

```python
dist = replay_buffer.get_strategy_distribution()
print(f"Strategy distribution: {dist}")
# Should be roughly: {0: 0.33, 1: 0.33, 2: 0.33}
# If imbalanced (e.g., {0: 0.9, 1: 0.05, 2: 0.05}), commander is not diversifying
```

---

## Next Steps

1. ✅ **Policy Architecture**: Implemented in `phase1_policy_training_v10_2.py`
2. ✅ **Actuator**: Implemented in `TacticalParameterActuator_v10_2.h/.cpp`
3. ⏳ **Schola Environment**: Update to provide `commanded_strategy`
4. ⏳ **Blueprint Integration**: Connect actuator to ScholaMocAgent
5. ⏳ **Training Loop**: Run `phase1_policy_training_v10_2.py`
6. ⏳ **ONNX Export**: Export trained model for UE5 inference

---

## References

- `TacticalParameterActuator_v10_2.h` - v10.2 actuator implementation
- `ACTUATOR_v10.2_SUMMARY.md` - Detailed actuator documentation
- `v10.2Architecture.md` - Full architecture specification
- `CLAUDE.md` - Project overview

---

**Status**: ✅ Python Integration Ready - Start Training!
