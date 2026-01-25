# CORTEX v8.0 Training Diagnostics - Issue Resolution

**Date:** 2026-01-25
**Issues Fixed:** Episode completion tracking, reward logging, batch configuration

---

## Problems Identified

### 1. Episodes Never Completing (CRITICAL)
**Symptoms:**
- `env_runners/num_episodes = 0` across all iterations
- All reward metrics showing `NaN`
- Console shows environment completion (`🏁 [ENV X DONE]`), but RLlib doesn't track it

**Root Cause:**
- Config used `batch_mode="truncate_episodes"`, which allows PPO to train on partial trajectories
- RLlib doesn't compute episode-level metrics when truncating
- Creates train/eval mismatch: environment finishes episodes, but RLlib never sees them

**Fix Applied:**
```python
# train_rllib.py:265
batch_mode="complete_episodes"  # Changed from "truncate_episodes"
```

**Expected Behavior After Fix:**
- `env_runners/num_episodes > 0` starting from iteration 1
- `episode_reward_mean`, `episode_len_mean` will show actual values (not NaN)
- Training will wait for full episodes before updating policy

**Trade-off:**
- Slightly longer iteration times (must complete episodes)
- Better training signal (full episode returns)
- Proper episode tracking for debugging

---

### 2. Missing Per-Agent Reward Logging
**Symptoms:**
- No visibility into individual agent performance
- No breakdown by logical environment (Env 0, Env 1, etc.)
- Iteration logs only show aggregated NaN values

**Root Cause:**
- RLlib's default logging doesn't track per-agent metrics in multi-agent settings
- No custom callback to accumulate agent-level statistics

**Fix Applied:**
Created `episode_logger_callback.py` with `EpisodeLoggerCallback` class:
- Tracks per-agent cumulative rewards
- Groups agents by logical environment ID
- Logs detailed statistics on episode completion

**New Metrics Added to `progress.csv`:**
```
custom_metrics/agent_reward_mean      # Mean reward across all agents
custom_metrics/agent_reward_min       # Min agent reward
custom_metrics/agent_reward_max       # Max agent reward
custom_metrics/agent_reward_std       # Std deviation of agent rewards
custom_metrics/env_reward_mean        # Mean reward per environment
custom_metrics/env_{i}_total_reward   # Total reward for environment i
custom_metrics/env_{i}_avg_reward     # Average reward for environment i
```

**New Console Output:**
```
================================================================================
📊 EPISODE COMPLETE - Episode 12345
================================================================================
  Episode Length: 450
  Total Return: 1234.56

  Per-Agent Statistics (32 agents):
    Mean Reward: 38.58
    Min Reward:  12.34
    Max Reward:  67.89
    Std Dev:     15.23

  Per-Environment Statistics (4 environments):
    Env 0: Total=350.12, Avg=43.77, Agents=8
    Env 1: Total=298.45, Avg=37.31, Agents=8
    Env 2: Total=412.33, Avg=51.54, Agents=8
    Env 3: Total=173.66, Avg=21.71, Agents=8

  Sample Agent Rewards (first 8):
    agent_0_0: 45.23 (450 steps, 0.1005/step)
    agent_0_1: 38.91 (450 steps, 0.0865/step)
    ...
================================================================================
```

---

### 3. Batch Size Configuration Mismatch
**Symptoms:**
- External diagnostic reported: "train_batch_size is 4000, but minibatch_size is 2048"
- Expected 15.6 minibatches per update (32000 / 2048), but only seeing 2

**Root Cause:**
- `TRAIN_BATCH_SIZE = 32000` defined in config class
- BUT never passed to RLlib's `.training()` method
- RLlib used default `train_batch_size = 4000`
- Result: 4000 / 2048 = ~2 minibatches per update (not 15.6)

**Fix Applied:**
```python
# train_rllib.py:284
config = config.training(
    train_batch_size=SBDAPMConfig.TRAIN_BATCH_SIZE,  # FIX: Explicitly set to 32000
    ...
)
```

**Expected Behavior After Fix:**
- `train_batch_size = 32000` in result.json
- 32000 / 2048 = ~15.6 minibatches per SGD epoch
- More stable gradient updates
- Better sample efficiency

**Calculation:**
```
Agents:        4 envs × 8 agents = 32 agents
Rollout:       256 steps per rollout
Total samples: 32 agents × 256 steps = 8192 agent_steps per iteration
Updates:       After ~4 iterations (32768 / 32000 ≈ 1 policy update)
SGD epochs:    15 epochs × 15.6 minibatches = 234 gradient updates per policy update
```

---

## Validation Checklist

### After Next Training Run, Verify:

#### ✅ Episode Completion
- [ ] `result.json` shows `env_runners/num_episodes > 0`
- [ ] `env_runners/episode_reward_mean` is NOT NaN
- [ ] `env_runners/episode_len_mean` shows actual episode length (~400-600 steps expected)
- [ ] Console shows both:
  - Environment completion: `🏁 [ENV X DONE]`
  - RLlib episode tracking: `📊 EPISODE COMPLETE`

#### ✅ Reward Logging
- [ ] Console shows detailed per-agent reward breakdown
- [ ] `progress.csv` contains `custom_metrics/agent_reward_mean` column
- [ ] Per-environment rewards are logged (`custom_metrics/env_0_total_reward`, etc.)
- [ ] Iteration details show:
  ```
  Per-Agent Reward: mean=X.XX, std=Y.YY
  Per-Env Reward: mean=Z.ZZ
  ```

#### ✅ Batch Configuration
- [ ] `result.json` shows `train_batch_size = 32000` (not 4000)
- [ ] Loss values stabilize faster (more minibatches = smoother updates)
- [ ] Check `info/learner/shared_policy/num_grad_updates_lifetime`:
  - Should increase by ~234 per policy update (15 epochs × 15.6 minibatches)

#### ✅ Training Progress
- [ ] `vf_loss` decreases over time (currently fluctuating 0.3 → 3.0)
- [ ] `total_loss` trends downward
- [ ] `vf_explained_var` increases from ~0 to >0.5
- [ ] `episode_reward_mean` increases over iterations

---

## Expected Training Behavior (Normal vs. Fixed)

### Before Fixes:
```
Iteration 7/20 0.00 0.0 0 229376 169.3s 0.00
  [ITERATION 7 DETAILS]
    No episodes completed this iteration (still collecting samples)
    Agent steps this iteration: 32768
    Cumulative: 0 episodes, 229376 steps
    Policy loss: N/A
```

### After Fixes (Expected):
```
Iteration 7/20 45.23 487.3 4 229376 172.5s 48.12
  [ITERATION 7 DETAILS]
    Episode Reward: mean=45.23, min=38.91, max=52.34
    Episode length: 487.3 steps
    Episodes this iteration: 4
    Per-Agent Reward: mean=38.58, std=15.23
    Per-Env Reward: mean=308.62
    Agent steps this iteration: 32768
    Cumulative: 28 episodes, 229376 steps
    Loss: total=1.2345, policy=0.0234, vf=0.8123
    KL divergence: 0.004567, Entropy: 7.234
```

---

## Debugging Tips

### If episodes still don't complete:
1. Check UE5 episode timeout settings (should be 60s)
2. Verify `AutoResetType.SAME_STEP` in `sbdapm_env_async.py:87`
3. Check environment done flags: Search logs for `🏁 [ENV X DONE]`
4. Monitor `rollout_fragment_length=256` - ensure episodes are longer than this

### If rewards are still NaN:
1. Verify callback is registered: Check for `[v8.0] Multi-head tactical policy registered`
2. Check episode completion: No episodes = no episode rewards
3. Inspect `result.json`: Look for `custom_metrics` field

### If batch size is still wrong:
1. Check `result.json` for `train_batch_size` field
2. Verify `config.training()` call includes `train_batch_size=32000`
3. Compare `num_agent_steps_sampled` vs `train_batch_size`

---

## Performance Expectations

### Convergence Timeline (After Fixes):
- **Iterations 1-10:** Exploration phase, rewards may be negative or near zero
- **Iterations 10-30:** Learning basic tactics, rewards should increase
- **Iterations 30-60:** Strategy differentiation emerges
- **Iterations 60-100:** Fine-tuning, rewards plateau

### Target Metrics (by iteration 100):
- Episode reward mean: >50.0 (depends on reward scale)
- Episode length: 400-600 steps
- VF explained variance: >0.6
- Total loss: <1.0
- KL divergence: <0.01

---

## Files Modified

1. **`train_rllib.py`**
   - Line 265: Changed `batch_mode` to `"complete_episodes"`
   - Line 284: Added `train_batch_size=SBDAPMConfig.TRAIN_BATCH_SIZE`
   - Line 277: Added callback registration
   - Lines 544-569: Enhanced iteration detail logging

2. **`episode_logger_callback.py`** (NEW)
   - Custom callback for detailed episode logging
   - Tracks per-agent and per-environment rewards
   - Logs comprehensive episode statistics

---

## Next Steps

1. **Run Training:**
   ```bash
   cd CORTEX_Training
   python train_rllib.py --iterations 20
   ```

2. **Monitor First Iteration:**
   - Should see `📊 EPISODE COMPLETE` messages
   - Should see non-NaN rewards in console
   - Check `progress.csv` for populated episode metrics

3. **Analyze Results:**
   ```bash
   # Check episode completion
   tail -20 training_results/*/progress.csv | grep episode_reward_mean

   # Check batch size
   grep "train_batch_size" training_results/*/params.json

   # View custom metrics
   grep "agent_reward_mean" training_results/*/progress.csv
   ```

4. **If Issues Persist:**
   - Share first 5 iterations of `progress.csv`
   - Share console output from first episode completion
   - Share `result.json` entries 1-3

---

## Reference: Current vs. Target Metrics

| Metric | Current (Broken) | Target (Fixed) |
|--------|------------------|----------------|
| `num_episodes` | 0 | >0 (4-8 per iteration) |
| `episode_reward_mean` | NaN | Real values (start near 0, increase) |
| `episode_len_mean` | NaN | 400-600 steps |
| `train_batch_size` | 4000 (default) | 32000 (configured) |
| Minibatches/update | ~2 | ~15.6 |
| Console episode logs | Only env-side | Both env + RLlib |
| Custom metrics | None | Per-agent, per-env rewards |
| Loss behavior | Random fluctuation | Decreasing trend |

---

**Status:** Ready for testing
**Priority:** HIGH - Episodes must complete for training to work
**Validation:** Run 1 iteration and verify episode completion
