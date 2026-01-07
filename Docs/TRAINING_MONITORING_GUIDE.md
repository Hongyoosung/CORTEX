# CORTEX v6.0 Training & Monitoring Guide

**Complete guide for training, monitoring, and deploying the MCTS-RL coordination AI**

**Date:** 2026-01-07
**Version:** v6.0 Production-Ready
**Target:** PPO with MCTS-guided value function

---

## Table of Contents

1. [Overview](#overview)
2. [Environment Setup](#environment-setup)
3. [Training Pipeline](#training-pipeline)
4. [Monitoring & Debugging](#monitoring--debugging)
5. [Model Evaluation](#model-evaluation)
6. [ONNX Export & Deployment](#onnx-export--deployment)
7. [Sim2Real Validation](#sim2real-validation)
8. [Troubleshooting](#troubleshooting)

---

## Overview

### v6.0 Training Architecture

```
┌─────────────────────────────────────────────────────────┐
│  UE5 Simulation (C++)                                    │
│  ├─ MCTS: Assigns objectives to agents                  │
│  ├─ RL Policy: Selects strategies (Assault/Defend/...)  │
│  └─ StateTree: Executes strategies deterministically    │
└─────────────────────────────────────────────────────────┘
                    ↓ (Observations, Actions, Rewards)
┌─────────────────────────────────────────────────────────┐
│  Python Training Environment                             │
│  ├─ RLlib PPO Trainer                                   │
│  ├─ Custom Gym Environment (cortex_env.py)              │
│  └─ Training Metrics (TensorBoard)                       │
└─────────────────────────────────────────────────────────┘
                    ↓ (Trained Policy Weights)
┌─────────────────────────────────────────────────────────┐
│  ONNX Export & Deployment                                │
│  ├─ PyTorch → ONNX conversion                           │
│  ├─ UE5 NNE Runtime integration                         │
│  └─ In-game performance validation                       │
└─────────────────────────────────────────────────────────┘
```

### Key Training Principles (v6.0)

1. **Objective-Conditioned RL:**
   - Input: Observation (64) + Objective Context (4) = 68 features
   - Output: Strategy logits (4) + Value (1)
   - Learns to adapt strategy based on MCTS-assigned objective

2. **Value Alignment (CRITICAL):**
   - Objective completion reward (100) > Death penalty (10)
   - Ensures RL and MCTS optimize same goal
   - Prevents "hiding" behavior (RL avoiding risk)

3. **Curriculum Learning:**
   - Early: Simple assignments (all agents → same objective)
   - Mid: Mixed assignments (2-2 splits, 3-1 splits)
   - Late: Complex assignments (individual, dynamic)

4. **Sim2Real Consistency:**
   - Single source of truth (C++ `RLConfig` namespace)
   - Auto-sync script (Python ← C++)
   - Validation tests prevent drift

---

## Environment Setup

### Prerequisites

**Python Environment (3.9+):**
```bash
# Create virtual environment
python -m venv cortex_env
source cortex_env/bin/activate  # Linux/Mac
cortex_env\Scripts\activate      # Windows

# Install dependencies
pip install -r CORTEX_Training/requirements.txt
```

**Required Packages (`requirements.txt`):**
```
torch>=2.0.0
onnx>=1.14.0
onnxruntime>=1.15.0
ray[rllib]>=2.5.0
gym>=0.26.0
tensorboard>=2.13.0
numpy>=1.24.0
pandas>=2.0.0
matplotlib>=3.7.0
pyyaml>=6.0
```

**UE5 Integration:**
- UE5.6 with NNE (Neural Network Engine) plugin enabled
- CORTEX project compiled (see `README.md`)

---

### Sim2Real Configuration Sync

**CRITICAL: Always sync before training!**

**Step 1: Update C++ Config (if parameters changed)**

```cpp
// Source/GameAI_Project/Public/RL/RLTypes.h

namespace RLConfig {
    // === CRITICAL: These values MUST match Python training environment ===

    // Movement (must match UE5 CharacterMovement)
    constexpr float AGENT_WALK_SPEED = 600.0f;      // cm/s
    constexpr float AGENT_RUN_SPEED = 900.0f;
    constexpr float AGENT_SPRINT_SPEED = 1200.0f;

    // Perception (must match UE5 AIPerception)
    constexpr float PERCEPTION_RADIUS = 3000.0f;    // cm
    constexpr int32 RAYCAST_COUNT = 16;
    constexpr float RAYCAST_LENGTH = 2000.0f;       // cm

    // Combat (must match UE5 damage system)
    constexpr float BASE_DAMAGE = 10.0f;
    constexpr float MAX_HEALTH = 100.0f;
    constexpr float FIRE_RATE = 0.1f;               // seconds per shot

    // Observation Normalization
    constexpr float MAX_DISTANCE_NORMALIZATION = 5000.0f;  // cm
    constexpr float MAX_VELOCITY_NORMALIZATION = 1200.0f;

    // Action Space
    constexpr int32 NUM_STRATEGIES = 4;  // Assault, Defend, Support, Retreat
}
```

**Step 2: Run Sync Script**

```bash
# From CORTEX_Training/ directory
python tools/sync_config_from_cpp.py

# Expected output:
# ✅ Config synced to: training_env/config.py
# ✅ Sync verified successfully!
```

**Step 3: Verify Sync**

```python
# CORTEX_Training/training_env/config.py (auto-generated)

class RLConfig:
    """RL training configuration (synced from C++)

    CRITICAL: Values must match C++ RLConfig namespace exactly.
    Any drift will cause trained models to fail in-game.
    """

    # Movement
    AGENT_WALK_SPEED = 600.0
    AGENT_RUN_SPEED = 900.0
    AGENT_SPRINT_SPEED = 1200.0

    # Perception
    PERCEPTION_RADIUS = 3000.0
    RAYCAST_COUNT = 16
    RAYCAST_LENGTH = 2000.0

    # ... etc.
```

**⚠️ NEVER manually edit `config.py` - always sync from C++!**

---

## Training Pipeline

### Quick Start

```bash
# Step 1: Sync configuration
python tools/sync_config_from_cpp.py

# Step 2: Start training
python train.py \
    --config config/ppo_cortex_v6.yaml \
    --num-workers 4 \
    --num-gpus 0 \
    --checkpoint-freq 100

# Step 3: Monitor training (separate terminal)
tensorboard --logdir=runs/cortex_v6_0
```

### Training Configuration

**File:** `CORTEX_Training/config/ppo_cortex_v6.yaml`

```yaml
# v6.0 PPO Configuration
env_config:
  observation_space_size: 68  # 64 base + 4 objective context
  action_space_size: 4        # Assault, Defend, Support, Retreat
  max_episode_steps: 1000
  num_agents: 4
  reward_scale: 1.0

model:
  custom_model: "cortex_policy_v6"
  custom_model_config:
    hidden_layers: [128, 128, 64]  # Shared trunk
    activation: "relu"
    use_lstm: false

ppo:
  lr: 3.0e-4
  gamma: 0.99
  lambda: 0.95
  clip_param: 0.2
  vf_clip_param: 10.0
  entropy_coeff: 0.01
  kl_coeff: 0.2
  num_sgd_iter: 10
  sgd_minibatch_size: 128
  train_batch_size: 4000

training:
  num_workers: 4
  num_envs_per_worker: 2
  rollout_fragment_length: 200
  batch_mode: "truncate_episodes"

evaluation:
  evaluation_interval: 10
  evaluation_num_episodes: 5
  evaluation_config:
    explore: false  # Deterministic evaluation
```

### Curriculum Learning Schedule

**Training Phases:**

```python
# CORTEX_Training/train.py

class CurriculumScheduler:
    """Progressive difficulty increase"""

    def get_scenario(self, episode):
        if episode < 1000:
            # Phase 1: Simple (all agents → same objective)
            return Scenario(
                agents_per_objective=[4],
                objective_types=['Capture'],
                enemy_count=2,
                difficulty='Easy'
            )

        elif episode < 5000:
            # Phase 2: Mixed (2-2, 3-1 splits)
            return Scenario(
                agents_per_objective=[2, 2],
                objective_types=['Capture', 'Defend'],
                enemy_count=4,
                difficulty='Medium'
            )

        else:
            # Phase 3: Complex (individual assignments, dynamic)
            return Scenario(
                agents_per_objective=[1, 1, 1, 1],
                objective_types=['Capture', 'Defend', 'Support', 'Retreat'],
                enemy_count=4,
                enemy_strategy='Adaptive',
                difficulty='Hard'
            )
```

**Why Curriculum Works:**
- Early: RL learns basic strategies (Assault when healthy, Retreat when damaged)
- Mid: RL learns coordination (Support allies, Defend positions)
- Late: RL learns complex adaptation (Dynamic switching, tactical sacrifice)

---

### Reward Structure (Value Alignment)

**File:** `CORTEX_Training/cortex_env.py`

```python
class CortexEnv(gym.Env):
    """v6.0 Environment with objective-aware rewards"""

    def calculate_reward(self, state, action, next_state):
        reward = 0.0

        # ========================================
        # P0: OBJECTIVE COMPLETION (Dominant Term)
        # ========================================

        if next_state.objective_completed:
            # CRITICAL: This MUST be >> death_penalty
            reward += 100.0  # Mission success

        # ========================================
        # P1: OBJECTIVE PROGRESS
        # ========================================

        # Progress toward objective
        distance_delta = state.objective_distance - next_state.objective_distance
        if distance_delta > 0:
            reward += distance_delta * 0.5  # +0.5 per meter

        # Hold position (for Defend objective)
        if next_state.objective_type == 'Defend' and next_state.objective_distance < 10.0:
            reward += 0.3  # +0.3/sec for holding

        # ========================================
        # P2: COMBAT EFFICIENCY
        # ========================================

        # Kill enemy (only if threatens objective)
        if next_state.enemy_killed and next_state.enemy_threatened_objective:
            reward += 15.0

        # ========================================
        # P3: SURVIVAL (MUST be < Objective rewards)
        # ========================================

        # Death penalty (acceptable if objective achieved)
        if next_state.agent_died:
            reward -= 10.0  # CRITICAL: 100 > 10 (objective > death)

        # INVARIANT CHECK
        assert self.OBJECTIVE_REWARD > abs(self.DEATH_PENALTY), \
            "Objective reward must exceed death penalty for value alignment"

        return reward
```

**Why This Works:**
- **Dying to complete objective** = +100 (objective) - 10 (death) = **+90 net reward** ✅
- **Hiding to avoid death** = 0 (no objective) + 0 (survive) = **0 net reward** ❌
- RL learns to sacrifice when needed (matches MCTS optimization goal)

---

## Monitoring & Debugging

### TensorBoard Metrics

**Launch TensorBoard:**
```bash
tensorboard --logdir=runs/cortex_v6_0 --port=6006
```

**Key Metrics to Monitor:**

**1. Episode Rewards (Should Increase)**
```
Target: Episode reward > 50 after 1000 episodes
        Episode reward > 80 after 5000 episodes

If stagnant < 20: RL not learning (check reward structure)
If oscillating: Learning rate too high (reduce to 1e-4)
```

**2. Policy Loss (Should Decrease)**
```
Target: Policy loss < 1.0 after 1000 episodes
        Policy loss < 0.5 after 5000 episodes

If increasing: Gradient explosion (clip gradients)
If stuck: Local minimum (increase exploration)
```

**3. Value Loss (Should Decrease)**
```
Target: Value loss < 10.0 after 1000 episodes
        Value loss < 5.0 after 5000 episodes

If high: Value function not converging (check rewards alignment)
```

**4. Entropy (Should Decrease Gradually)**
```
Target: Entropy starts ~1.4 (random), ends ~0.5 (deterministic)

If stays high (>1.2): Not exploiting learned policy
If drops too fast (<0.3 early): Premature convergence (increase entropy_coeff)
```

**5. KL Divergence (Should Stay < 0.05)**
```
Target: KL divergence < 0.02 (policy changing smoothly)

If > 0.05: Policy changing too fast (increase kl_coeff)
If > 0.1: Training unstable (reduce learning rate)
```

**6. Strategy Distribution (Should Balance Over Time)**
```
Early: ~25% each strategy (uniform exploration)
Late:  Depends on scenario (e.g., 40% Assault, 30% Defend, 20% Support, 10% Retreat)

If single strategy dominates (>80%): Reward structure biased (check alignment)
```

---

### Debug Visualization (In-Game)

**Enable Debug Visualization:**

```cpp
// In UE5 Console:
ToggleMCTSDebug    // Show MCTS assignments (yellow arrows)
ToggleRLDebug       // Show RL strategies (colored spheres)
PrintMCTSStats      // Print MCTS statistics
```

**What to Look For:**

**MCTS Visualization:**
- **Yellow arrows:** Agent → Objective assignments
- **Green text:** RL value estimates (V=0.73)
- **Cyan text:** Objective types ("Capture", "Defend")

**RL Visualization:**
- **Red spheres:** Assault strategy
- **Blue spheres:** Defend strategy
- **Green spheres:** Support strategy
- **Yellow spheres:** Retreat strategy
- **Health bars:** Agent health status

**Example Debugging Session:**

```
Observation: Agent1 assigned "Capture A" but using "Retreat" strategy
Analysis: Agent1 health < 30% → RL correctly prioritizes survival
Expected: Temporary retreat, then return to assault when healed

Observation: Agent2 assigned "Defend B" but using "Support" strategy
Analysis: Agent3 (at Capture A) is critical health → RL adapts to protect ally
Expected: Dynamic strategy switching (RL learned coordination)

⚠️ Issue: Agent4 assigned "Capture C" but idle (no movement)
Diagnosis: StateTree execution failure (EQS query returned no positions)
Fix: Check EQS query configuration, add fallback position
```

---

### Training Logs

**File:** `runs/cortex_v6_0/training.log`

```
[2026-01-07 10:15:23] Episode 100  | Reward: 23.5 | Loss: 2.1 | Entropy: 1.38
[2026-01-07 10:18:45] Episode 200  | Reward: 35.2 | Loss: 1.5 | Entropy: 1.25
[2026-01-07 10:22:10] Episode 500  | Reward: 52.8 | Loss: 0.9 | Entropy: 1.05
[2026-01-07 10:30:55] Episode 1000 | Reward: 68.4 | Loss: 0.6 | Entropy: 0.85
...
[2026-01-07 12:15:30] Episode 5000 | Reward: 87.3 | Loss: 0.3 | Entropy: 0.52
[2026-01-07 12:15:35] ✅ Target reward reached (> 80), saving checkpoint...
[2026-01-07 12:15:40] ✅ Checkpoint saved: checkpoints/cortex_v6_episode_5000.pt
```

**Good Training Signs:**
- ✅ Reward increasing steadily
- ✅ Loss decreasing steadily
- ✅ Entropy decreasing gradually
- ✅ No NaN values
- ✅ KL divergence stable < 0.05

**Bad Training Signs:**
- ❌ Reward oscillating wildly
- ❌ Loss increasing or spiking
- ❌ Entropy stuck at 1.4 (not learning) or drops to 0.1 (premature convergence)
- ❌ NaN values (gradient explosion)
- ❌ KL divergence > 0.1 (unstable policy updates)

---

## Model Evaluation

### In-Simulation Evaluation

**Run Evaluation Episodes (Deterministic):**

```python
# CORTEX_Training/evaluate.py

python evaluate.py \
    --checkpoint checkpoints/cortex_v6_episode_5000.pt \
    --num-episodes 100 \
    --render  # Optional: show UE5 visualization

# Expected output:
# ✅ Average Reward: 85.3 (±5.2)
# ✅ Win Rate: 87% (87/100 episodes)
# ✅ Objective Completion: 92%
# ✅ Average Episode Length: 245 steps
```

**Evaluation Metrics:**

| Metric | Target | Notes |
|--------|--------|-------|
| **Average Reward** | > 80 | Objective-weighted reward |
| **Win Rate** | > 85% | Objective completion rate |
| **Avg Episode Length** | 200-300 steps | Efficiency indicator |
| **Strategy Distribution** | Balanced | No single strategy dominance |

### Ablation Studies (Validate MCTS-RL Synergy)

**Test 1: RL Without MCTS (Baseline)**

```python
# Disable MCTS assignment, use random objectives
config['use_mcts_assignment'] = False

# Expected: Lower win rate (~60-70%), less coordination
```

**Test 2: RL Without Objective Context**

```python
# Remove objective context from observation (68 → 64 features)
config['use_objective_context'] = False

# Expected: Strategy selection less optimal (~70-75% win rate)
```

**Test 3: MCTS Without RL Value**

```python
# Use heuristic value instead of RL value in MCTS
config['use_rl_value_in_mcts'] = False

# Expected: MCTS assignments less optimal (~75-80% win rate)
```

**Expected Results:**
```
Full System (MCTS + RL + Objective Context):  87% win rate ✅
RL without MCTS:                              68% win rate
RL without Objective Context:                 73% win rate
MCTS without RL Value:                        78% win rate

Conclusion: All components contribute to performance
```

---

## ONNX Export & Deployment

### Step 1: Export Trained Model to ONNX

**File:** `CORTEX_Training/export_onnx.py`

```python
import torch
import torch.onnx
from models.cortex_policy_v6 import CortexPolicyNetwork

# Load trained checkpoint
checkpoint = torch.load('checkpoints/cortex_v6_episode_5000.pt')
model = CortexPolicyNetwork(obs_dim=68, hidden_sizes=[128, 128, 64])
model.load_state_dict(checkpoint['policy_state_dict'])
model.eval()

# Dummy input (batch_size=1, obs_dim=68)
dummy_input = torch.randn(1, 68)

# Export to ONNX
torch.onnx.export(
    model,
    dummy_input,
    'cortex_policy_v6.onnx',
    input_names=['observation'],
    output_names=['policy_logits', 'value'],
    dynamic_axes={'observation': {0: 'batch_size'}},  # Allow batching
    opset_version=14
)

print('✅ ONNX model exported: cortex_policy_v6.onnx')

# Verify output shapes
import onnx
onnx_model = onnx.load('cortex_policy_v6.onnx')
print(f'Input: {onnx_model.graph.input[0]}')   # [batch, 68]
print(f'Output 0: {onnx_model.graph.output[0]}')  # [batch, 4] policy logits
print(f'Output 1: {onnx_model.graph.output[1]}')  # [batch, 1] value
```

**Run Export:**

```bash
python export_onnx.py

# Expected output:
# ✅ ONNX model exported: cortex_policy_v6.onnx
# Input: name: "observation" type { tensor_type { elem_type: 1 shape { dim { dim_param: "batch_size" } dim { dim_value: 68 } } } }
# Output 0: name: "policy_logits" type { tensor_type { elem_type: 1 shape { dim { dim_param: "batch_size" } dim { dim_value: 4 } } } }
# Output 1: name: "value" type { tensor_type { elem_type: 1 shape { dim { dim_param: "batch_size" } dim { dim_value: 1 } } } }
```

---

### Step 2: Validate ONNX Model

**File:** `CORTEX_Training/test_onnx.py`

```python
import onnxruntime as ort
import numpy as np

# Load ONNX model
session = ort.InferenceSession('cortex_policy_v6.onnx')

# Test inference
dummy_obs = np.random.randn(4, 68).astype(np.float32)  # Batch of 4 agents
outputs = session.run(None, {'observation': dummy_obs})

policy_logits = outputs[0]  # [4, 4]
values = outputs[1]         # [4, 1]

print(f'✅ Policy logits shape: {policy_logits.shape}')
print(f'✅ Values shape: {values.shape}')
print(f'✅ ONNX inference successful!')

# Benchmark performance
import time
num_runs = 1000
start = time.time()
for _ in range(num_runs):
    session.run(None, {'observation': dummy_obs})
end = time.time()

avg_time_ms = (end - start) / num_runs * 1000
print(f'✅ Average inference time: {avg_time_ms:.2f}ms (target: <4ms for 4 agents)')

# Expected: ~3-4ms for batched inference (4 agents)
```

---

### Step 3: Deploy to UE5

**Copy ONNX Model:**

```bash
# Windows:
copy cortex_policy_v6.onnx C:\Users\PC\Documents\GitHub\CORTEX\Content\Models\cortex_policy_v6.onnx

# Linux/Mac:
cp cortex_policy_v6.onnx /path/to/CORTEX/Content/Models/cortex_policy_v6.onnx
```

**Update Blueprint Configuration:**

1. Open UE5 Editor
2. Open `BP_TeamLeaderComponent`
3. Set `RL Policy Network` → `Model Path` = `Models/cortex_policy_v6.onnx`
4. Set `Use ONNX Model` = `True`
5. Save Blueprint

**Test In-Game:**

```cpp
// In UE5 Console:
ToggleRLDebug

// Verify:
// 1. Agents show strategy spheres (Red/Blue/Green/Yellow)
// 2. No "Fallback heuristic" warnings in log
// 3. Strategies change based on health, enemies, objectives
```

**Performance Validation:**

```cpp
// In UE5 Console:
stat STATGROUP_AI

// Verify:
// STAT_RLBatchedInference: <4ms (4 agents) ✅
// STAT_RLSingleInference: Should not be called ❌
// If single inference called: Batching not working, check GetStrategiesBatched()
```

---

## Sim2Real Validation

### Test Scenarios

**Scenario 1: Movement Speed Consistency**

**UE5 Test:**
```cpp
// Measure agent movement speed
float Speed = Agent->GetVelocity().Size();
// Expected: 600 cm/s (walk), 900 cm/s (run), 1200 cm/s (sprint)
```

**Python Test:**
```python
# Simulate agent movement in training environment
env.step(action='move_forward')
speed = env.agent.velocity.magnitude()
# Expected: 6.0 m/s (walk), 9.0 m/s (run), 12.0 m/s (sprint)
# Note: 600 cm/s = 6 m/s ✅
```

**Validation:**
- [ ] Walk speed matches: UE5 = 600 cm/s, Python = 6 m/s ✅
- [ ] Run speed matches: UE5 = 900 cm/s, Python = 9 m/s ✅
- [ ] Sprint speed matches: UE5 = 1200 cm/s, Python = 12 m/s ✅

---

**Scenario 2: Perception Radius Consistency**

**UE5 Test:**
```cpp
// Check AI perception radius
float Radius = AIPerceptionComponent->GetSightRadius();
// Expected: 3000 cm (30 meters)
```

**Python Test:**
```python
# Check perception radius in training environment
radius = env.agent.perception_radius
# Expected: 30.0 meters
```

**Validation:**
- [ ] Perception radius matches: UE5 = 3000 cm, Python = 30 m ✅

---

**Scenario 3: Damage System Consistency**

**UE5 Test:**
```cpp
// Apply damage to agent
Agent->TakeDamage(10.0f);
// Health: 100 → 90
// After 10 hits: Health = 0 (death)
```

**Python Test:**
```python
# Simulate damage in training environment
env.agent.take_damage(10.0)
# Health: 100 → 90
# After 10 hits: Health = 0 (death)
```

**Validation:**
- [ ] Damage values match: UE5 = 10.0, Python = 10.0 ✅
- [ ] Max health matches: UE5 = 100, Python = 100 ✅
- [ ] Death occurs at health = 0 ✅

---

### Deployment Checklist

- [ ] **Sim2Real config synced** (`sync_config_from_cpp.py` run successfully)
- [ ] **ONNX model exported** (`cortex_policy_v6.onnx` created)
- [ ] **ONNX model validated** (inference test passed, <4ms for 4 agents)
- [ ] **ONNX model deployed** (copied to `Content/Models/`)
- [ ] **In-game visualization working** (`ToggleRLDebug` shows strategies)
- [ ] **Performance targets met** (`stat STATGROUP_AI` shows <10ms total AI frame)
- [ ] **Sim2Real validation passed** (movement, perception, damage consistent)
- [ ] **No fallback heuristic warnings** (ONNX model loading correctly)
- [ ] **Strategy distribution reasonable** (no single strategy dominance)
- [ ] **Agents completing objectives** (win rate > 85%)

---

## Troubleshooting

### Issue: Training Reward Not Increasing

**Symptoms:**
- Reward stuck at low value (<20) after 1000 episodes
- Policy loss not decreasing
- Entropy stuck at ~1.4 (random exploration)

**Possible Causes:**
1. Reward structure broken (not aligned with MCTS)
2. Observation features not informative
3. Learning rate too low (not learning) or too high (unstable)
4. Curriculum too hard too early

**Solutions:**

1. **Check Reward Alignment:**
```python
# Verify objective reward > death penalty
assert OBJECTIVE_REWARD (100) > abs(DEATH_PENALTY) (10)  # ✅

# If reversed (death = -150), RL learns to hide ❌
```

2. **Simplify Curriculum:**
```python
# Start with easier scenario (all agents → same objective)
curriculum.start_phase = 'Simple'  # Not 'Complex'
```

3. **Adjust Learning Rate:**
```python
# If loss oscillating: Reduce LR
config['lr'] = 1.0e-4  # From 3.0e-4

# If loss not decreasing: Increase LR
config['lr'] = 5.0e-4  # From 3.0e-4
```

4. **Check Observation Features:**
```python
# Print observation statistics
obs = env.reset()
print(f'Obs mean: {np.mean(obs)}')  # Should be ~0.0
print(f'Obs std: {np.std(obs)}')    # Should be ~0.5-1.0
print(f'Obs min: {np.min(obs)}')    # Should be ~-1.0
print(f'Obs max: {np.max(obs)}')    # Should be ~1.0

# If all zeros: Feature extraction broken
# If out of range: Normalization broken
```

---

### Issue: ONNX Export Fails

**Symptoms:**
- `torch.onnx.export()` raises error
- ONNX model not created
- Export crashes

**Possible Causes:**
1. Model contains unsupported operations
2. Dynamic shapes not compatible
3. Opset version mismatch

**Solutions:**

1. **Check Unsupported Ops:**
```python
# Test with simple forward pass first
model.eval()
with torch.no_grad():
    test_input = torch.randn(1, 68)
    output = model(test_input)
    print(f'Forward pass successful: {output.shape}')

# If this fails: Model architecture has issues
```

2. **Simplify Dynamic Axes:**
```python
# Remove dynamic axes temporarily
torch.onnx.export(
    model, dummy_input, 'test.onnx',
    dynamic_axes={}  # Fixed batch size
)

# If this works: Dynamic axes causing issue
```

3. **Try Different Opset:**
```python
# Try opset 13 or 15 if 14 fails
torch.onnx.export(..., opset_version=13)
```

---

### Issue: ONNX Inference Slow in UE5

**Symptoms:**
- `STAT_RLBatchedInference` > 10ms (should be <4ms)
- Game stuttering during AI updates
- Performance worse than fallback heuristic

**Possible Causes:**
1. ONNX model not optimized
2. CPU fallback instead of optimized runtime
3. Batch size = 1 (not batching)
4. Model too large

**Solutions:**

1. **Verify Batching:**
```cpp
// In UE5 log, check:
[RL v6.0] Batched inference: 4 agents in 3.2ms ✅
[RL v6.0] Single inference: 1 agent in 2.1ms ❌  // Should not see this!

// If seeing single inference: Batching not working
// Fix: Ensure TeamLeader calls GetStrategiesBatched(), not individual GetStrategy()
```

2. **Optimize ONNX Model:**
```python
# Use ONNX optimizer
import onnxoptimizer

onnx_model = onnx.load('cortex_policy_v6.onnx')
optimized = onnxoptimizer.optimize(onnx_model)
onnx.save(optimized, 'cortex_policy_v6_optimized.onnx')
```

3. **Reduce Model Size:**
```python
# Train smaller network
hidden_sizes = [64, 64, 32]  # Instead of [128, 128, 64]

# Trade-off: Faster inference (~2ms) but slightly lower performance (~82% win rate vs 87%)
```

---

### Issue: Trained Model Fails In-Game

**Symptoms:**
- Model loads in UE5 but agents behave randomly
- Win rate in-game << evaluation win rate
- Agents ignore objectives

**Possible Causes:**
1. Sim2Real drift (config mismatch)
2. ONNX model corrupted
3. Observation normalization mismatch
4. Objective context not provided

**Solutions:**

1. **Verify Sim2Real Sync:**
```bash
# Re-run sync script
python tools/sync_config_from_cpp.py

# Check for mismatches
# If any found: Re-train model with corrected config
```

2. **Test ONNX Model Outside UE5:**
```python
# Validate ONNX inference matches PyTorch
import torch
import onnxruntime as ort

# PyTorch inference
model.eval()
with torch.no_grad():
    pytorch_output = model(test_input)

# ONNX inference
session = ort.InferenceSession('cortex_policy_v6.onnx')
onnx_output = session.run(None, {'observation': test_input.numpy()})

# Compare outputs
diff = np.abs(pytorch_output[0].numpy() - onnx_output[0])
print(f'Max diff: {np.max(diff)}')  # Should be <1e-5

# If diff > 1e-3: ONNX export corrupted, re-export
```

3. **Check Observation Normalization:**
```cpp
// In UE5, print observation values
FObservationElement Obs = BuildObservation();
TArray<float> Features = Obs.ToFeatureVector();
for (int32 i = 0; i < Features.Num(); ++i) {
    UE_LOG(LogTemp, Display, TEXT("Feature[%d] = %.3f"), i, Features[i]);
}

// Verify:
// - Health in [0, 1] ✅
// - Distances normalized [0, 1] ✅
// - Velocities in [-1, 1] ✅
// - No NaN or Inf values ✅
```

4. **Check Objective Context:**
```cpp
// Verify objective context is appended to observation
FObjectiveContext ObjCtx = BuildObjectiveContext(Objective);
TArray<float> ObjFeatures = ObjCtx.ToFeatureVector();
check(ObjFeatures.Num() == 4);  // Should be 4 features

// Verify these 4 features are appended to 64-feature base observation
// Total = 68 features ✅
```

---

## Advanced Topics

### Distributed Training (Multi-GPU)

```python
# Use Ray for distributed training
ray.init(num_gpus=4)

trainer = PPOTrainer(config={
    'num_gpus': 4,
    'num_workers': 16,
    'num_gpus_per_worker': 0.25
})

# Expected speedup: ~3-4x faster (4 GPUs)
```

### Hyperparameter Tuning

```python
# Use Ray Tune for hyperparameter search
from ray import tune

config = {
    'lr': tune.grid_search([1e-4, 3e-4, 5e-4]),
    'entropy_coeff': tune.grid_search([0.01, 0.02, 0.05]),
    'hidden_sizes': tune.grid_search([[64,64,32], [128,128,64], [256,128,64]])
}

tune.run(PPOTrainer, config=config, num_samples=3)
```

### Multi-Agent Training (Centralized Critic)

```python
# Train with centralized critic (sees all agents)
# Decentralized execution (each agent uses own policy)

class CentralizedCritic(nn.Module):
    def __init__(self, obs_dim, num_agents):
        super().__init__()
        self.critic = nn.Sequential(
            nn.Linear(obs_dim * num_agents, 256),
            nn.ReLU(),
            nn.Linear(256, 1)
        )

# Training uses global observation (all 4 agents)
# Execution uses local observation (single agent)
```

---

## Appendix

### File Structure

```
CORTEX/
├── Source/GameAI_Project/
│   ├── Public/RL/
│   │   └── RLTypes.h  # RLConfig namespace (single source of truth)
│   ├── Private/RL/
│   │   └── RLPolicyNetwork.cpp
│   └── ...
├── CORTEX_Training/
│   ├── train.py  # Main training script
│   ├── evaluate.py  # Evaluation script
│   ├── export_onnx.py  # ONNX export
│   ├── config/
│   │   └── ppo_cortex_v6.yaml  # Training config
│   ├── models/
│   │   └── cortex_policy_v6.py  # Network architecture
│   ├── tools/
│   │   └── sync_config_from_cpp.py  # Sim2Real sync
│   ├── training_env/
│   │   ├── cortex_env.py  # Gym environment
│   │   └── config.py  # Auto-generated (don't edit!)
│   └── checkpoints/
│       └── cortex_v6_episode_5000.pt  # Trained weights
├── Content/Models/
│   └── cortex_policy_v6.onnx  # Deployed ONNX model
├── Docs/
│   ├── TRAINING_MONITORING_GUIDE.md  # This file
│   ├── PROFILING_CHECKLIST_v6.0.md  # Performance validation
│   └── REFACTORING_PLAN_v6.0.md  # Implementation plan
└── ...
```

### Quick Reference Commands

```bash
# Sync configuration
python tools/sync_config_from_cpp.py

# Train model
python train.py --config config/ppo_cortex_v6.yaml --num-workers 4

# Monitor training
tensorboard --logdir=runs/cortex_v6_0

# Evaluate model
python evaluate.py --checkpoint checkpoints/cortex_v6_episode_5000.pt --num-episodes 100

# Export to ONNX
python export_onnx.py

# Validate ONNX
python test_onnx.py

# Deploy to UE5
cp cortex_policy_v6.onnx ../Content/Models/
```

---

**Last Updated:** 2026-01-07
**Maintained By:** CORTEX Development Team
**Version:** v6.0 Production-Ready

For questions or issues, refer to `CLAUDE.md` (architecture overview) and `REFACTORING_PLAN_v6.0.md` (implementation details).
