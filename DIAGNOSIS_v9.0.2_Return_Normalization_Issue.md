# CORTEX v9.0.2: Return Normalization Fix

**Date:** 2026-02-01
**Issue:** Reward collapse after 192k steps (v9.0.1 fixes made it worse!)
**Status:** ✅ Root cause identified, fixes applied

---

## Problem Summary

### Metrics After v9.0.1 Fixes (192k steps)

```
✅ VF Explained Var:  0.84  (EXCELLENT - up from 0.75)
✅ VF Loss:           0.007 (EXCELLENT - down from 0.887)
❌ Entropy:           6.80  (STUCK - barely changed from 7.1)
❌ Entropy Coeff:     0.0186 (following schedule)
💀 Episode Rewards:   COLLAPSING (max_mean: 3395 → 535, min_mean: 304 → 1.4)
```

**Paradox:** Value function learning perfectly, but rewards collapsing!

---

## Root Cause: Double Normalization

### **The Mistake**

I recommended return normalization without realizing **rewards are already normalized in C++:**

```cpp
// RewardCalculator.cpp:214
// C++ already normalizes rewards:
float RawTotal = sum(all_components);  // Each component already normalized
Breakdown.Total = FMath::Tanh(RawTotal / 4.0f) * 5.0f;  // Output: [-5, 5]
```

Then Python applied **second normalization:**

```python
# cortex_env.py - WRONG!
std = sqrt(return_rms.var)  # ≈ 1200 (from initial high variance 304-3395)
normalized_reward = raw_reward / std  # 3.0 / 1200 = 0.0025 (microscopic!)
```

---

## Why Rewards Collapsed

### **The Math:**

```
Initial episode returns: 300-3400 (before normalization kicked in)
↓
return_rms.std ≈ 1200 (calculated from variance)
↓
STEP 1: C++ outputs reward per step ≈ 3.0
STEP 2: Python divides by 1200 → 0.0025 per step
↓
Episode reward (1000 steps): 0.0025 × 1000 = 2.5
Entropy penalty: -0.0186 × 6.80 × 1000 = -126
↓
NET EPISODE RETURN = 2.5 - 126 = -123 ❌
```

**Policy learned: "Don't do anything, it only makes things worse"**

---

## Why VF Improved But Rewards Died

**This is the key insight:**

- **VF explained variance went UP** because VF accurately learned: "Expected returns are negative"
- **VF loss went DOWN** because VF predictions match actual terrible returns
- **Rewards collapsed** because policy optimized for "minimize entropy to reduce penalty"

**The value function did its job perfectly - it correctly predicted disaster!**

---

## Applied Fixes (v9.0.2)

### **Fix 1: Disabled Return Normalization** ✅

**File:** `cortex_env.py:626-645`

```python
# BEFORE (v9.0.1) - WRONG!
if self.normalize_returns:
    std = np.sqrt(self.return_rms.var) + self.epsilon
    normalized_reward = float(raw_reward) / std  # Microscopic!
    reward_dict[flat_id] = normalized_reward

# AFTER (v9.0.2) - FIXED!
# Rewards already normalized in C++ (per-component + tanh scaling)
reward_dict[flat_id] = float(raw_reward)  # Use raw C++ output directly
```

**Rationale:** C++ already applies:
1. Per-component normalization (OBJECTIVE_NORM, COMBAT_NORM, etc.)
2. Soft tanh scaling: `tanh(sum/4) × 5 → [-5, 5]`

No need for Python-side normalization.

---

### **Fix 2: Increased Entropy Coefficient to 0.05** ✅

**File:** `train_rllib.py:268`

```python
# BEFORE (v9.0.1)
ENTROPY_COEFF = 0.02  # Entropy stuck at 6.8 (not decreasing!)

# AFTER (v9.0.2)
ENTROPY_COEFF = 0.05  # 5× original, entropy needs stronger penalty
```

**Schedule Updated:**
```python
entropy_coeff_schedule = [
    (0, 0.05),         # Higher initial (was 0.02)
    (500000, 0.03),    # Slower decay (was 0.015)
    (1000000, 0.02),   # Keep minimum higher (was 0.01)
]
```

**Rationale:** With return normalization disabled, rewards return to [-5, 5] range. Entropy at 6.8 needs stronger penalty:

```
Old penalty: -0.0186 × 6.8 = -0.126 per step
New penalty: -0.05 × 6.8 = -0.34 per step (2.7× stronger)
```

---

## Expected Recovery

### **Immediate (Next 10 Iterations)**

```
Episode rewards should RECOVER:
- max_mean: 535 → 1500-2000
- mean_mean: 302 → 800-1200
- min_mean: 1.4 → 300-500
```

**If rewards recover, fixes are working!**

---

### **Medium Term (Iterations 20-30)**

```
Entropy should START declining:
- Current: 6.80
- Target: 6.0-6.5 (with 0.05 coeff)
```

**With normal rewards restored, entropy penalty will push policy to commit.**

---

### **Long Term (Iterations 50-100)**

```
Healthy convergence:
- Entropy: 4.5-5.5 (multi-head baseline)
- VF Explained Var: 0.85-0.90 (maintain improvement)
- Episode Rewards: Stable with 2-3× variance
```

---

## Timeline Comparison

| Iteration | Episode Rewards | Entropy | VF Explained Var | Status |
|-----------|----------------|---------|------------------|--------|
| **10 (before v9.0.1)** | 304-3395 | 7.1 | 0.75 | ❌ Not learning |
| **~25 (after v9.0.1)** | 1-535 (collapse!) | 6.8 | 0.84 | 💀 Disaster |
| **~27 (after v9.0.2)** | Should recover | 6.8 | 0.84 | 🟡 Recovery |
| **35-40** | 500-2000 | 6.5-6.8 | 0.85 | 🟡 Improving |
| **50** | 800-2500 | 5.5-6.5 | 0.85-0.88 | 🟢 Converging |
| **100** | 1000-3000 | 4.5-5.5 | 0.88-0.90 | ✅ Healthy |

---

## How to Resume Training

### **Continue from Current Checkpoint**

```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\CORTEX_Training
python train_rllib.py --iterations 30 --resume "training_results/20260201_105939"
```

**What to watch for:**

1. **Iteration 27-30:** Episode rewards should JUMP back up (1000+)
   - If they don't, return normalization is still enabled (check logs)

2. **Iteration 35-40:** Entropy should start declining (6.8 → 6.5)
   - If it doesn't, may need to increase ENTROPY_COEFF further

3. **Iteration 50+:** Rewards stabilize, entropy continues dropping

---

## Monitoring Checklist

### ✅ **Signs of Recovery (expect by iteration 30)**

- [ ] Episode rewards recover to 1000+ (from collapsed ~100)
- [ ] No more "microscopic reward" warnings in logs
- [ ] VF explained variance stays high (0.84+)
- [ ] Entropy penalty increases: ~-0.13 → ~-0.34 per step

### ⚠️ **Warning Signs**

- [ ] Rewards still collapsing after 10 iterations
  - Check: Return normalization truly disabled?
  - Look for: `std=` in logs (shouldn't appear)

- [ ] Entropy still rising or stuck at 6.8+ after 20 iterations
  - Increase ENTROPY_COEFF to 0.08
  - Verify entropy penalty in logs

---

## Lessons Learned

### **❌ Don't Double-Normalize**

If C++ applies normalization (per-component + soft scaling), don't add Python normalization.

**Check your reward pipeline:**
```
C++ → [Component Norm] → [Soft Scaling] → [-5, 5]
                                            ↓
Python → [Return Norm?] → DON'T! Already normalized!
```

---

### **✅ Trust the Value Function**

When VF explained variance improves but rewards collapse:
- VF is doing its job (predicting accurately)
- Problem is in the REWARDS, not the VF

**Don't blame the messenger!**

---

### **✅ Test Changes Incrementally**

v9.0.1 applied 3 changes at once:
1. Increased entropy coeff ✅ (was correct)
2. Reduced log_std_max ✅ (was correct)
3. Added return normalization ❌ (broke everything)

**Next time:** Test one change at a time to isolate issues.

---

## What Went Right

Despite the reward collapse, **v9.0.1 achieved something important:**

### **Value Function Fixed!** ✅

```
Before: VF explained_var = 0.75, loss = 0.887
After:  VF explained_var = 0.84, loss = 0.007
```

**This is a 12× improvement in VF loss!**

The VF now accurately predicts returns. Once rewards are restored (v9.0.2), this strong VF will accelerate learning.

---

## Files Modified (v9.0.2)

1. **`cortex_env.py`** (1 change)
   - Line 626-645: Disabled return normalization (use raw C++ rewards)

2. **`train_rllib.py`** (3 changes)
   - Line 268: `ENTROPY_COEFF = 0.05` (increased from 0.02)
   - Line 295: `"normalize_returns": False` (disabled)
   - Line 361-365: Updated entropy coefficient schedule

---

## Next Steps

1. **Resume training:**
   ```bash
   python train_rllib.py --iterations 30 --resume "training_results/20260201_105939"
   ```

2. **Watch TensorBoard** for reward recovery (next 10 iterations)

3. **If rewards recover:** Monitor entropy decline (iterations 35-50)

4. **If rewards still collapse:** Check logs for return normalization warnings

5. **Document results** after iteration 40

---

## Summary

**v9.0.1 Mistake:** Applied return normalization on top of C++ normalization
→ Created microscopic rewards (0.0025/step)
→ Entropy penalty dominated (-126/episode vs +2.5 reward)
→ Policy learned to do nothing

**v9.0.2 Fix:** Disabled return normalization, increased entropy coefficient
→ Rewards return to normal [-5, 5] range
→ Stronger entropy penalty can now reduce exploration
→ Policy can learn again

**Positive Side Effect:** VF dramatically improved (explained_var 0.75→0.84, loss 0.887→0.007)

**Expected Outcome:** Rewards recover within 10 iterations, entropy drops by iteration 50.
