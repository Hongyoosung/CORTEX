# Curriculum Training Best Practices for SBDAPM
**Version:** v3.1 | **Date:** 2025-12-13

---

## Table of Contents
1. [Core Principles](#core-principles)
2. [Curriculum Design Guidelines](#curriculum-design-guidelines)
3. [Training Hyperparameter Tuning](#training-hyperparameter-tuning)
4. [Common Pitfalls & Solutions](#common-pitfalls--solutions)
5. [Performance Optimization](#performance-optimization)
6. [Evaluation & Metrics](#evaluation--metrics)

---

## Core Principles

### 1. Gradual Complexity Increase

**DO:**
- Start with simplest possible task (T1: 2v2 flat ground, no cover)
- Add ONE new mechanic per level (T2: add cover, T3: add elevation)
- Increase agent count gradually (2v2 → 3v3 → 4v4)

**DON'T:**
- Jump from basic combat to complex multi-objective scenarios
- Add multiple mechanics simultaneously (e.g., cover + elevation + rescue)
- Change agent count and mechanics in same level

**Example Progression:**
```
T1: Basic Combat (2v2, flat) → Learn to shoot
T2: Cover Usage (2v2, flat + cover) → Learn to use cover
T3: Positioning (3v3, elevation + cover) → Learn formation
```

### 2. Transfer Learning Preservation

**Critical:** Always load previous checkpoint when advancing levels

```python
# CORRECT: Continue from T1 checkpoint
algo = PPOConfig().build()
algo.restore("training_results/T1_final/checkpoint_000100")
# Continue training on T2

# WRONG: Start from scratch for each level
algo = PPOConfig().build()  # Random initialization
# Training on T2 starts over
```

**Why:** Learned skills (aiming, movement) transfer to new levels. Starting from scratch wastes training time.

### 3. Convergence Validation

**Convergence Criteria Per Level:**

| Metric | Threshold | Window |
|--------|-----------|--------|
| Win Rate (Self-Play) | 48-52% | Last 100 episodes |
| Reward Variance | < 20% | Last 50 episodes |
| Policy Entropy | > 0.3 | Current |
| Episode Length | Stable ± 10% | Last 100 episodes |

**Validation Script:**
```python
def is_converged(metrics, level_config):
    """Check if level has converged."""
    # Win rate balanced?
    win_rate = metrics["win_rate"][-100:].mean()
    if not (0.48 <= win_rate <= 0.52):
        return False, f"Win rate {win_rate:.2f} not balanced"

    # Low variance?
    reward_var = metrics["reward"][-50:].std()
    if reward_var > 20:
        return False, f"High reward variance: {reward_var:.1f}"

    # Still exploring?
    entropy = metrics["policy_entropy"][-1]
    if entropy < 0.3:
        return False, f"Low exploration: {entropy:.2f}"

    # Episode length stable?
    ep_len_var = metrics["episode_len"][-100:].std()
    ep_len_mean = metrics["episode_len"][-100:].mean()
    if ep_len_var / ep_len_mean > 0.10:
        return False, f"Unstable episode length: {ep_len_var/ep_len_mean:.2%}"

    return True, "Converged"
```

---

## Curriculum Design Guidelines

### Level Difficulty Progression

**Complexity Score Formula:**
```
Complexity = (AgentCount × 2) + (ObjectiveCount × 5) + (TerrainComplexity × 3)

Where:
- AgentCount: 2, 3, or 4 per team
- ObjectiveCount: 0 (elimination only), 1, 2, 3+
- TerrainComplexity: 0 (flat), 1 (cover), 2 (elevation), 3 (urban)
```

**Example Scores:**
```
T1 (2v2 flat): (2×2) + (0×5) + (0×3) = 4
T2 (2v2 cover): (2×2) + (0×5) + (1×3) = 7
T3 (3v3 elevation): (3×2) + (0×5) + (2×3) = 12
T5 (4v4 flanking): (4×2) + (1×5) + (2×3) = 19
T10 (4v4 full): (4×2) + (6×5) + (3×3) = 47
```

**Guideline:** Increase complexity score by ≤ 50% per level

### Spawn Point Design

**Symmetrical Levels (Training):**
```
Team A Spawn          Team B Spawn
     ↓                     ↓
  [A1][A2]            [B1][B2]
     |                     |
  ←─────── 5000cm ─────────→
```

**Key Principles:**
- Mirror layout (perfectly symmetrical)
- Spawn distance: 4000-6000cm (40-60m) for 2v2, 6000-8000cm for 4v4
- Spawn spacing: 200cm between agents in same team
- No line-of-sight at spawn (prevents instant kills)

**Anti-Patterns:**
- Asymmetric spawns (creates imbalanced win rates)
- Too close (<3000cm): instant combat, no positioning
- Too far (>10000cm): excessive travel time, boring episodes

### Objective Placement

**Capture Zones:**
```
Placement: Equidistant from both spawns (center of map)
Radius: 1500-3000cm
Example:
  [Team A]
     ↓
  3000cm
     ↓
  [Capture Zone] (radius 2000cm)
     ↓
  3000cm
     ↓
  [Team B]
```

**Defend Zones:**
```
Placement: Near one team's spawn (asymmetric scenario)
Radius: 1500cm
Example:
  [Team A Spawn] ←500cm→ [Defend Zone]
                           ↑
                        4000cm
                           ↓
                     [Team B Spawn]
```

**Rescue Targets:**
```
Placement: Behind cover, mid-map
Health: 30-50% (wounded, not dead)
Example:
     [Rescue Target]
           ↓
       [Cover Box]
```

### Cover Element Distribution

**Density by Level:**
```
T1 (Basic Combat): 0 cover elements
T2 (Cover Usage): 8-12 elements (boxes, walls)
T3 (Positioning): 12-16 elements + elevation
T4+ (Advanced): 20+ elements, realistic placement
```

**Placement Patterns:**
```
Scattered (Good for learning):
  ■     ■
    ■       ■
  ■     ■

Clustered (Bad - creates chokepoints):
  ■■■
  ■■■

Perimeter (Good for flanking):
  ■─────■
  │     │
  ■─────■
```

---

## Training Hyperparameter Tuning

### PPO Hyperparameters (Recommended Ranges)

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| `LEARNING_RATE` | 3e-4 | [1e-5, 1e-3] | Higher = faster learning, unstable |
| `CLIP_PARAM` | 0.2 | [0.1, 0.3] | Lower = more conservative updates |
| `ENTROPY_COEFF` | 0.01 | [0.005, 0.05] | Higher = more exploration |
| `GAMMA` (discount) | 0.99 | [0.95, 0.995] | Higher = long-term planning |
| `GAE_LAMBDA` | 0.95 | [0.9, 0.98] | Advantage estimation smoothing |
| `TRAIN_BATCH_SIZE` | 4000 | [2000, 8000] | Larger = slower, more stable |
| `SGD_MINIBATCH_SIZE` | 128 | [64, 256] | Smaller = more updates, slower |
| `NUM_SGD_ITER` | 10 | [5, 20] | More = better optimization, slower |

### Adaptive Hyperparameters by Level

**Early Levels (T1-T3): High Exploration**
```python
config.lr = 5e-4  # Higher learning rate
config.entropy_coeff = 0.03  # More exploration
config.clip_param = 0.2  # Standard clipping
```

**Mid Levels (T4-T6): Balanced**
```python
config.lr = 3e-4  # Standard
config.entropy_coeff = 0.01  # Moderate exploration
config.clip_param = 0.2
```

**Late Levels (T7-T10): Fine-Tuning**
```python
config.lr = 1e-4  # Lower learning rate (refinement)
config.entropy_coeff = 0.005  # Less exploration (exploit learned skills)
config.clip_param = 0.1  # Conservative updates (preserve skills)
```

### Learning Rate Scheduling

**Option 1: Manual Decay**
```python
# Reduce LR by 50% every 500 iterations
def lr_schedule(iteration):
    base_lr = 3e-4
    decay_steps = 500
    decay_rate = 0.5
    return base_lr * (decay_rate ** (iteration // decay_steps))

config.lr = lr_schedule
```

**Option 2: Automatic (RLlib Built-in)**
```python
config.lr_schedule = [
    (0, 5e-4),      # Iterations 0-500: high LR
    (500, 3e-4),    # Iterations 500-1000: medium LR
    (1000, 1e-4)    # Iterations 1000+: low LR
]
```

---

## Common Pitfalls & Solutions

### Pitfall 1: Catastrophic Forgetting

**Symptom:** Agent performs well on T3, but fails on T1 after advancing to T4

**Cause:** Overwriting learned skills with new tasks

**Solutions:**
1. **Mixed Training:** Include T1-T3 episodes in T4 training
   ```python
   # 70% T4 episodes, 30% T1-T3 review
   level_distribution = {
       "T4": 0.7,
       "T1": 0.1,
       "T2": 0.1,
       "T3": 0.1
   }
   ```

2. **Elastic Weight Consolidation (EWC):**
   - Preserve important weights from previous tasks
   - Requires custom RLlib implementation (advanced)

3. **Progressive Networks:**
   - Add new network branches for new tasks
   - Keep old branches frozen (advanced)

### Pitfall 2: Reward Hacking

**Symptom:** Agents achieve high reward but don't learn intended behavior

**Example:** Agents camp in corner to avoid death (high survival reward)

**Solutions:**
1. **Shape Rewards Carefully:**
   ```python
   # BAD: High survival reward
   reward = 10.0 * survival_time

   # GOOD: Balance survival with engagement
   reward = (
       5.0 * damage_dealt
       + 10.0 * kills
       - 5.0 * damage_taken
       - 10.0 * death
       + 0.01 * survival_time  # Small survival bonus
   )
   ```

2. **Add Engagement Penalty:**
   ```python
   # Penalize agents for not engaging enemies
   if distance_to_enemy > 2000 and no_combat_for > 30s:
       reward -= 5.0  # Camping penalty
   ```

3. **Monitor Unintended Behaviors:**
   - Log episode videos during training
   - Check for repeated patterns (spinning, corner camping)

### Pitfall 3: Divergence (Policy Collapse)

**Symptom:** Win rate goes to 0% or 100%, reward drops suddenly

**Cause:** Unstable training, gradient explosion

**Solutions:**
1. **Reduce Learning Rate:**
   ```python
   config.lr = 1e-4  # From 3e-4
   ```

2. **Reduce Clipping:**
   ```python
   config.clip_param = 0.1  # From 0.2 (more conservative)
   ```

3. **Gradient Clipping:**
   ```python
   config.grad_clip = 0.5  # Limit gradient magnitude
   ```

4. **Rollback to Last Stable Checkpoint:**
   ```bash
   # Restore from earlier checkpoint
   algo.restore("training_results/checkpoint_000080")  # Before divergence
   ```

### Pitfall 4: Exploration Collapse

**Symptom:** Policy entropy drops below 0.3, agents repeat same actions

**Cause:** Premature convergence to local optimum

**Solutions:**
1. **Increase Entropy Bonus:**
   ```python
   config.entropy_coeff = 0.05  # From 0.01
   ```

2. **Add Intrinsic Curiosity:**
   ```python
   # Reward novel states (requires custom implementation)
   reward += 0.1 * state_novelty_score
   ```

3. **Epsilon-Greedy Exploration:**
   ```python
   # Add random action with 10% probability
   if random() < 0.1:
       action = sample_random_action()
   ```

### Pitfall 5: Slow Convergence

**Symptom:** Training takes >10 hours per level, reward plateaus

**Causes & Solutions:**

| Cause | Solution |
|-------|----------|
| Batch size too small | Increase `TRAIN_BATCH_SIZE` (4000 → 8000) |
| Too few workers | Add more parallel UE instances |
| Episode too long | Reduce `MaxEpisodeDuration` (120s → 60s for early levels) |
| Overly complex level | Simplify (reduce agents, objectives) |
| Poor reward signal | Add intermediate rewards (damage dealt, cover usage) |

---

## Performance Optimization

### Training Speed Optimization

**Baseline Performance (Single Worker):**
- ~100 env steps/sec
- ~400 agent steps/sec (4 agents)
- ~1.5 hours per 100 iterations (T1)

**Optimization Strategies:**

#### 1. Parallel Environments (Highest ROI)
```python
# 4 workers = 4x speedup
config.num_workers = 4

# Expected: ~400 env steps/sec, ~1600 agent steps/sec
# T1 training time: 1.5h → 0.4h
```

#### 2. Reduce Episode Length (Early Levels)
```python
# For T1-T3, reduce max duration
config.max_episode_duration = 60.0  # From 120s

# Episodes end faster, more episodes per hour
```

#### 3. GPU Acceleration (ONNX Inference)
```python
# In RLPolicyNetwork.cpp, use GPU provider
OrtSessionOptions options;
options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
options.AppendExecutionProvider_CUDA(0);  // Use GPU 0

# Inference speedup: 1-3ms → 0.5-1ms
```

#### 4. Async MCTS (Already Implemented ✅)
```cpp
// TeamLeaderComponent.cpp - MCTS runs on background thread
AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]() {
    MCTS->RunMCTS();
});

// Main thread not blocked, 0ms impact on frame rate
```

#### 5. Observation Caching
```cpp
// Cache static observations (terrain, cover positions)
class UObservationCache {
    TMap<FVector, TArray<FCoverInfo>> CoverCache;

    void CacheCoverLocations() {
        // Run EQS once, cache results
        // Reuse for all agents
    }
};

// Speedup: 5-10% (reduced EQS queries)
```

### Memory Optimization

**Baseline Memory Usage:**
- Single UE instance: ~4GB
- RLlib training: ~2GB
- Total (4 workers): ~18GB

**Optimization Strategies:**

#### 1. Reduce Replay Buffer Size
```python
# Not needed for on-policy PPO (uses rollouts, not replay)
# If using off-policy (SAC, TD3), reduce:
config.replay_buffer_size = 10000  # From 100000
```

#### 2. Lower Resolution UE Instances
```bash
# Launch with lower resolution (headless workers)
GameAI_Project.exe -game -ResX=640 -ResY=480 -nullrhi

# Memory savings: 4GB → 2GB per instance
```

#### 3. Model Quantization
```python
# Export INT8 quantized ONNX model
import onnx
from onnxruntime.quantization import quantize_dynamic

model = onnx.load("rl_policy_network.onnx")
quantized_model = quantize_dynamic(
    model_input=model,
    weight_type=onnx.TensorProto.INT8
)
onnx.save(quantized_model, "rl_policy_network_int8.onnx")

# Memory: 500KB → 125KB, Inference: 1-3ms → 0.5-1.5ms
```

---

## Evaluation & Metrics

### Evaluation Protocol

**After Each Level Completes:**

1. **Checkpoint Evaluation (100 Episodes):**
   ```bash
   python evaluate_agents.py \
       --checkpoint training_results/T3_final/checkpoint_000500 \
       --episodes 100 \
       --map Training_Positioning_3v3_v01
   ```

2. **Cross-Level Evaluation:**
   ```bash
   # Test T3 model on T1 (should still perform well)
   python evaluate_agents.py \
       --checkpoint training_results/T3_final/checkpoint_000500 \
       --episodes 50 \
       --map Training_BasicCombat_2v2_v01
   ```

3. **Baseline Comparison:**
   ```bash
   # Compare against rule-based AI
   python evaluate_agents.py \
       --checkpoint training_results/T3_final/checkpoint_000500 \
       --baseline rule_based \
       --episodes 50
   ```

### Key Metrics to Track

**Performance Metrics:**
- **Win Rate:** Should be ~50% for self-play, >70% vs baselines
- **Episode Length:** Indicates combat complexity (too short = poor tactics)
- **K/D Ratio:** Kills per death (should improve over curriculum)

**Behavioral Metrics:**
- **Cover Usage Rate:** % time spent in cover (T2+)
- **Formation Coherence:** Team spatial coherence [0,1] (T3+)
- **Coordination Rate:** % kills from combined actions (T4+)
- **Objective Completion Time:** For capture/defend scenarios (T5+)

**Learning Metrics:**
- **Sample Efficiency:** Episodes to convergence (lower = better)
- **Transfer Quality:** Win rate on previous levels (should remain high)
- **Policy Entropy:** Exploration vs exploitation balance

### Benchmark Targets (by Level)

| Level | Win Rate | Avg Episode Len | Coordination Rate | Cover Usage |
|-------|----------|-----------------|-------------------|-------------|
| T1 | 50% ± 2% | 300-600s | N/A | 0% |
| T2 | 50% ± 2% | 400-700s | < 10% | > 60% |
| T3 | 50% ± 2% | 500-800s | > 10% | > 60% |
| T4 | 50% ± 2% | 600-900s | > 25% | > 60% |
| T5 | 50% ± 2% | 600-1000s | > 30% | > 60% |
| T10 | 50% ± 2% | 800-1200s | > 40% | > 70% |

### Regression Testing

**After Completing Curriculum:**

Run full regression suite:
```bash
# Test final model on all curriculum levels
for level in T1 T2 T3 T4 T5 T6 T7 T8 T9 T10; do
    python evaluate_agents.py \
        --checkpoint training_results/T10_final/checkpoint_002000 \
        --map Training_${level}_*.umap \
        --episodes 50 \
        --output results/${level}_regression.json
done
```

**Expected Results:**
- Win rate ≥ 45% on all levels (no catastrophic forgetting)
- Behavioral metrics maintained (cover usage, coordination)

---

## Quick Reference: Training Decision Tree

```
Starting New Level?
├─ Is previous level converged? (win rate ~50%, entropy >0.3)
│   ├─ No → Continue training previous level
│   └─ Yes → Advance
│
├─ Load previous checkpoint?
│   └─ Yes → algo.restore(previous_checkpoint)
│
├─ Complexity increase reasonable? (≤ 50%)
│   ├─ No → Simplify level (reduce agents/objectives)
│   └─ Yes → Proceed
│
├─ Training diverging? (reward dropping, entropy <0.3)
│   ├─ Yes → Reduce LR, rollback checkpoint
│   └─ No → Continue
│
└─ Convergence taking too long? (>10h)
    ├─ Yes → Add workers, reduce episode length
    └─ No → Continue
```

---

## Advanced Techniques (Optional)

### 1. Population-Based Training (PBT)

**Idea:** Train multiple policies with different hyperparameters, periodically copy best performers

```python
from ray import tune

config = PPOConfig()
config.lr = tune.grid_search([1e-4, 3e-4, 5e-4])
config.entropy_coeff = tune.grid_search([0.01, 0.03, 0.05])

# Ray Tune will run 3×3 = 9 parallel trainings
# Automatically identifies best hyperparameters
```

### 2. Self-Play Opponent Pool

**Idea:** Train against past versions of itself (prevents overfitting to current policy)

```python
opponent_pool = [
    "checkpoint_000100",  # Early policy
    "checkpoint_000300",  # Mid policy
    "checkpoint_000500"   # Recent policy
]

# Randomly sample opponent from pool each episode
```

### 3. Curriculum Auto-Tuning

**Idea:** Automatically generate curriculum based on performance

```python
def auto_curriculum(current_level_score):
    """Adjust next level difficulty based on current performance."""
    if current_level_score > 0.8:
        # Learning too easy, increase difficulty
        return increase_complexity(50%)
    elif current_level_score < 0.4:
        # Learning too hard, decrease difficulty
        return decrease_complexity(25%)
    else:
        # Goldilocks zone, standard progression
        return next_level()
```

---

## Summary Checklist

### Before Starting Training
- [ ] All curriculum levels designed (T1-T10)
- [ ] Spawn points added with correct tags
- [ ] Reward system tested and balanced
- [ ] Training infrastructure ready (local/Docker/AWS)
- [ ] Baseline metrics established

### During Training
- [ ] Monitor TensorBoard graphs every 100 iterations
- [ ] Check convergence criteria before advancing levels
- [ ] Load previous checkpoint when switching levels
- [ ] Log behavioral metrics (cover usage, coordination)
- [ ] Run periodic evaluations (every 500 iterations)

### After Training
- [ ] Export ONNX model
- [ ] Run regression tests on all levels
- [ ] Compare against baselines (rule-based AI)
- [ ] Document final performance metrics
- [ ] Deploy to production UE5 project

---

**End of Curriculum Training Best Practices**

For additional support:
- Check `TrainingWorkflow.md` for step-by-step instructions
- Check `LevelDesignTemplate.md` for curriculum specifications
- Check `CLAUDE.md` for architecture details
