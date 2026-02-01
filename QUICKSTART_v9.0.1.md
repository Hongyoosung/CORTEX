# CORTEX v9.0.1 Quick Start Guide

## What Changed?

**Problem:** Entropy = 7.1 (policy 74% random) after 481k steps
**Fix:** Increased entropy coefficient + reduced log-std max + added return normalization

## Resume Training (Recommended)

```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\CORTEX_Training
python train_rllib.py --iterations 50 --resume "training_results/20260201_081838"
```

**What to expect:**
- Training resumes from iteration 10 (481k steps)
- New hyperparameters apply immediately
- By iteration 30: Entropy should drop to 5.5-6.0
- By iteration 50: VF explained var should exceed 0.88

## Fresh Start (Clean Logs)

```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\CORTEX_Training
python train_rllib.py --iterations 100
```

**Trade-off:** Loses 481k steps but gives clean TensorBoard comparison

## Watch For (TensorBoard)

### Good Signs ✅
- Entropy declining: 7.1 → 6.5 → 6.0 → 5.5
- VF explained var improving: 0.75 → 0.80 → 0.85 → 0.90
- Episode reward mean increasing
- Reward variance reducing: 10× → 5× → 3×

### Bad Signs ⚠️
- Entropy stuck above 6.5 after 20 iterations
- VF loss increasing
- Episode rewards collapsing

## TensorBoard

```bash
# View training progress
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\CORTEX_Training
tensorboard --logdir training_results
```

Open: http://localhost:6006

**Metrics to watch:**
1. `ray/tune/info/learner/.../entropy` → Should decrease
2. `ray/tune/info/learner/.../vf_explained_var` → Should increase
3. `ray/tune/info/learner/.../vf_loss` → Should decrease
4. `ray/.../agent_reward_mean` → Should stabilize

## Key Changes Summary

| Hyperparameter | Before (v8.10.2) | After (v9.0.1) | Impact |
|----------------|------------------|----------------|--------|
| `ENTROPY_COEFF` | 0.01 | 0.02 | 2× stronger exploration penalty |
| `LOG_STD_MAX` | 0.5 | 0.0 | Max σ: 1.65 → 1.0 (lower entropy ceiling) |
| Return Normalization | ❌ Not implemented | ✅ Enabled | Stabilizes value function |

## Expected Timeline

| Iteration | Entropy | VF Explained Var | Status |
|-----------|---------|------------------|--------|
| 10 (now) | 7.1 | 0.75 | ❌ Not learning |
| 20 | 6.5-6.8 | 0.80-0.85 | 🟡 Improving |
| 30 | 5.5-6.0 | 0.85-0.88 | 🟢 Converging |
| 50 | 4.5-5.5 | 0.88-0.90 | ✅ Good |
| 100 | 3.5-4.5 | 0.90+ | ✅ Excellent |

## Files Changed

- `train_rllib.py`: Entropy coeff, log-std max, enhanced logging
- `cortex_env.py`: Return normalization implementation

## Rollback

If needed:
```bash
git checkout HEAD~1 CORTEX_Training/train_rllib.py CORTEX_Training/cortex_env.py
```

## Help

- **Detailed diagnosis:** `FIXES_v9.0.1_Entropy_Issue.md`
- **Architecture docs:** `CLAUDE.md`
- **Issue tracker:** Create GitHub issue

---

**TL;DR:** Run `python train_rllib.py --iterations 50 --resume "training_results/20260201_081838"` and watch entropy drop!
