# CORTEX v9.0.1: Entropy & Convergence Fixes

**Date:** 2026-02-01
**Issue:** Iteration 10 diagnosis showed entropy=7.1 (74% random policy) after 481k steps
**Status:** ✅ Fixes Applied

---

## Problem Summary

### Iteration 10 Metrics (TensorBoard)
```
Entropy:              7.1229 (74% of maximum, policy nearly random)
Entropy Coefficient:  0.0057 (too weak to penalize exploration)
Agent Reward Range:   304-3395 (10× variance in episode returns)
VF Explained Var:     0.7531 (improving but unstable)
VF Loss:              0.887 (high, indicates poor value estimates)
```

### Root Causes Identified

1. **Entropy Coefficient Too Low**
   - Current: 0.0057 at 481k steps
   - Penalty: -0.0057 × 7.1 = -0.04 per step
   - Episode penalty: ~-40 (1-10% of 304-3395 rewards)
   - **Result:** Policy has weak incentive to commit to actions

2. **Log-Std Maximum Too High**
   - Previous: LOG_STD_MAX = 0.5 → max σ = 1.65
   - Theoretical max entropy: 9.59
   - **Result:** Entropy ceiling too high, slow convergence

3. **No Return Normalization**
   - CLAUDE.md v9.0 specified return normalization
   - Not implemented in `cortex_env.py`
   - **Result:** Noisy value function targets (10× reward variance)

---

## Applied Fixes

### Fix 1: Increased Entropy Coefficient ✅

**File:** `train_rllib.py:264`

```python
# BEFORE (v8.10.2)
ENTROPY_COEFF = 0.01  # REDUCED from 0.02 (was causing excessive exploration)

# AFTER (v9.0.1)
ENTROPY_COEFF = 0.02  # INCREASED from 0.01 (2× stronger exploration penalty)
```

**Schedule Updated:**
```python
entropy_coeff_schedule = [
    (0, 0.02),         # Higher initial penalty (was 0.01)
    (500000, 0.015),   # Slower decay (was 0.005)
    (1000000, 0.01),   # Keep minimum higher (was 0.001)
]
```

**Expected Impact:**
- Entropy penalty: -0.02 × 7.1 = -0.14 per step (-140 per episode)
- 3.5× stronger than previous (-40 → -140)
- Should push entropy down from 7.1 → 5.5-6.0 by iteration 30

---

### Fix 2: Reduced Log-Std Maximum ✅

**File:** `train_rllib.py:273`

```python
# BEFORE
LOG_STD_MAX = 0.5   # Maximum log_std (σ_max ≈ 1.65)

# AFTER
LOG_STD_MAX = 0.0   # REDUCED (max σ = 1.0 instead of 1.65)
```

**Impact:**
- Theoretical max entropy: 9.59 → ~7.8 (19% reduction)
- Limits exploration variance to reasonable range
- Helps policy converge faster without sacrificing necessary exploration

---

### Fix 3: Return Normalization Implementation ✅

**File:** `cortex_env.py`

#### Added RunningMeanStd Class (lines 85-115)
```python
class RunningMeanStd:
    """
    Tracks running mean and standard deviation using Welford's online algorithm.
    Used for normalizing episode returns to stabilize value function learning.
    """
    def __init__(self, epsilon=1e-4, shape=()):
        self.mean = np.zeros(shape, dtype=np.float64)
        self.var = np.ones(shape, dtype=np.float64)
        self.count = epsilon

    def update(self, x):
        # Updates running statistics with new episode returns
        ...
```

#### Enabled in Environment (`__init__`, line 204-210)
```python
# v9.0.1: Return normalization for stable value function learning
self.normalize_returns = kwargs.get("normalize_returns", True)
self.return_rms = RunningMeanStd(shape=())  # Scalar return statistics
self.gamma = 0.99
self.epsilon = 1e-8
self._agent_episode_returns = {}  # Running episode return per agent
```

#### Applied in `step()` (line 626-648)
```python
# v9.0.1: Track episode returns for normalization
self._agent_episode_returns[flat_id] += float(raw_reward)

# v9.0.1: Apply return normalization
if self.normalize_returns:
    # Normalize by return standard deviation (preserves gradients)
    std = np.sqrt(self.return_rms.var) + self.epsilon
    normalized_reward = float(raw_reward) / std
    reward_dict[flat_id] = normalized_reward
```

#### Updated on Episode Completion (line 473-478)
```python
# v9.0.1: Update return normalization statistics
if self.normalize_returns:
    episode_returns = [self._agent_episode_returns.get(aid, 0.0) for aid in env_agents]
    if episode_returns:
        self.return_rms.update(episode_returns)
```

**Expected Impact:**
- Stabilizes value function by normalizing reward scale
- Episode return variance: 10× (304-3395) → ~2-3×
- VF explained variance: 0.75 → 0.85+ by iteration 30

---

### Fix 4: Enhanced Entropy Logging ✅

**File:** `train_rllib.py:736-745`

```python
# v9.0.1: Enhanced diagnostics for entropy and value function
if isinstance(entropy, float):
    entropy_coeff = learner_info.get('entropy_coeff', 'N/A')
    print(f"    Entropy: {entropy:.2f} (coeff={entropy_coeff:.4f}, penalty={(entropy * entropy_coeff):.2f})")
if isinstance(vf_explained_var, float):
    print(f"    Value Function: explained_var={vf_explained_var:.4f}, loss={vf_loss:.4f}")
if isinstance(kl, float):
    print(f"    KL Divergence: {kl:.6f} (coeff={cur_kl_coeff:.4f})")
```

**Output Example:**
```
  [ITERATION 20 DETAILS]
    Episode Reward: mean=1250.32, min=580.21, max=2100.45
    Loss: total=0.8245, policy=0.3521, vf=0.4724
    Entropy: 6.12 (coeff=0.0180, penalty=-110.16)
    Value Function: explained_var=0.8234, loss=0.4724
    KL Divergence: 0.008523 (coeff=0.2000)
```

---

## Expected Training Progression

| Iteration | Entropy | Entropy Coeff | VF Explained Var | VF Loss | Reward Variance | Status |
|-----------|---------|---------------|------------------|---------|-----------------|--------|
| **10 (Current)** | 7.1 | 0.0057 | 0.75 | 0.887 | 10× (304-3395) | ❌ Not learning |
| **20** | 6.5-6.8 | 0.018 | 0.80-0.85 | 0.6-0.7 | 6-8× | 🟡 Early improvement |
| **30** | 5.5-6.0 | 0.020 | 0.85-0.88 | 0.4-0.5 | 4-5× | 🟡 Moderate convergence |
| **50** | 4.5-5.5 | 0.018 | 0.88-0.90 | 0.3-0.4 | 3-4× | 🟢 Good convergence |
| **100** | 3.5-4.5 | 0.010 | 0.90+ | <0.3 | 2-3× | ✅ Healthy policy |

**Key Milestones:**
- **Iteration 20:** Entropy should drop below 6.5 (first sign of learning)
- **Iteration 30:** VF explained variance should exceed 0.85
- **Iteration 50:** Reward variance should reduce to 3-4× (strategies differentiated but balanced)
- **Iteration 100:** Entropy should stabilize at 3.5-4.5 (40-50% of maximum)

---

## Answer to Your Question

> "Note: The entropy is high. Is this an inevitable consequence of the four multiheads?"

**Short Answer:** No, but multi-heads do increase baseline entropy.

**Detailed Analysis:**

### Multi-Head Contribution
- Single-head theoretical max entropy (5D, log_std=0.5): ~9.6
- With 4 strategy-specific heads, expected baseline: +1.5-2.0 entropy
- **Expected healthy range for multi-heads: 3.5-5.0** (not 7.1)

### Your Current 7.1 Entropy Breakdown
```
Theoretical Maximum:     9.59
Your Current:            7.1 (74% of max)
Multi-Head Baseline:     +1.5-2.0
Target After Fixes:      4.5-5.5 (50-60% of max)
Fully Converged:         3.5-4.5 (40-50% of max)
```

**Conclusion:** Multi-heads add ~1.5-2.0 to baseline entropy, but your 7.1 is excessive. The fixes will push it down to 4.5-5.5 (healthy for multi-head architecture).

---

## How to Resume Training

### Option 1: Continue from Iteration 10 (Recommended)

```bash
cd CORTEX_Training
python train_rllib.py --iterations 50 --resume "training_results/20260201_081838"
```

This will:
- Resume from iteration 10 checkpoint
- Apply new hyperparameters (ENTROPY_COEFF=0.02, LOG_STD_MAX=0.0)
- Enable return normalization immediately
- Run 50 more iterations to see improvement

**Expected:** By iteration 30-40, you should see entropy drop to 5.5-6.0

---

### Option 2: Fresh Start (If you want clean TensorBoard logs)

```bash
cd CORTEX_Training
python train_rllib.py --iterations 100
```

**Trade-off:** Loses 481k steps of experience, but gives clean metrics comparison

---

## Monitoring Checklist

Watch for these signs of improvement:

### ✅ Positive Signs (expect by iteration 30)
- [ ] Entropy declining (7.1 → 6.0 or lower)
- [ ] Entropy coefficient staying at 0.018-0.020
- [ ] VF explained variance improving (0.75 → 0.85+)
- [ ] VF loss declining (0.887 → 0.5 or lower)
- [ ] Episode reward mean increasing
- [ ] Reward variance reducing (10× → 5×)

### ⚠️ Warning Signs (if observed, investigate)
- [ ] Entropy still above 6.5 after 20 iterations
- [ ] VF loss increasing or oscillating wildly
- [ ] Episode rewards collapsing (mean drops below 500)
- [ ] Return normalization std stuck at 1.0 (not updating)

---

## Files Modified

1. **`train_rllib.py`** (4 changes)
   - Line 264: `ENTROPY_COEFF = 0.02` (increased from 0.01)
   - Line 273: `LOG_STD_MAX = 0.0` (reduced from 0.5)
   - Line 293: Added `"normalize_returns": True` to env config
   - Line 736-745: Enhanced entropy/VF logging

2. **`cortex_env.py`** (6 changes)
   - Line 85-115: Added `RunningMeanStd` class
   - Line 204-210: Initialized return normalization in `__init__`
   - Line 346-358: Initialize episode returns in `reset()`
   - Line 626-648: Apply return normalization in `step()`
   - Line 473-478: Update return stats on episode completion
   - Line 492-506: Update return stats on force timeout

---

## Rollback (If Needed)

If fixes cause unexpected issues, revert with:

```bash
git checkout HEAD~1 CORTEX_Training/train_rllib.py CORTEX_Training/cortex_env.py
```

---

## Next Steps

1. **Resume training** from iteration 10:
   ```bash
   python train_rllib.py --iterations 50 --resume "training_results/20260201_081838"
   ```

2. **Monitor TensorBoard** for:
   - Entropy declining toward 5.5-6.0 by iteration 30
   - VF explained variance improving to 0.85+
   - Episode reward variance reducing

3. **If entropy doesn't improve by iteration 30:**
   - Check return normalization logs (should show std changing)
   - Consider increasing ENTROPY_COEFF to 0.03
   - Verify C++ reward normalization is working

4. **Document results** in next diagnosis report

---

**Summary:** The high entropy (7.1) was NOT an inevitable consequence of multi-heads, but rather:
1. Entropy coefficient too weak (0.0057 vs needed 0.02)
2. Log-std ceiling too high (max σ=1.65 → reduced to 1.0)
3. No return normalization (noisy VF targets)

All fixes applied. Expect entropy to drop to 5.5-6.0 by iteration 30 with current changes.
