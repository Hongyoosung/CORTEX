# CORTEX Training Setup Guide (Post-Fix)

## Critical Fixes Applied

### 1. **Parallel Data Collection (3x speedup)**
- **Before:** `NUM_WORKERS = 0` → 1 environment only
- **After:** `NUM_WORKERS = 2` → 3 total environments (1 local + 2 workers)
- **Impact:** ~3 hours/iteration → ~1 hour/iteration

### 2. **Dense Reward Shaping**
- **Before:** Sparse rewards from UE5 only (no learning signal)
- **After:** Dense rewards for:
  - Objective proximity (+0.05 at 1m, scaling with distance)
  - Survival bonus (+0.001/step)
  - Health preservation (±0.01 based on health)
  - Combat engagement (+0.01 for optimal range)
  - Progress toward objective (+0.01 per meter closer)
- **Impact:** Agents can now learn incremental progress

### 3. **Value Function Learning**
- **Before:** `VF_LOSS_COEFF = 0.5`, `vf_explained_var = 0.0114`
- **After:** `VF_LOSS_COEFF = 1.5`
- **Impact:** Value function should learn (target: vf_var > 0.3 by iter 50)

### 4. **Entropy Decay Schedule**
- **Before:** Constant `ENTROPY_COEFF = 0.5` (forced random policy)
- **After:** Decay schedule:
  - Iters 0-30: 0.5 (explore)
  - Iters 31-80: 0.5 → 0.05 (linear decay)
  - Iters 81+: 0.05 (exploit)
- **Impact:** Policy can now converge

### 5. **Network Capacity**
- **Before:** `[128, 128, 64]`
- **After:** `[256, 256, 128]`
- **Impact:** More capacity for complex value learning

---

## Setup Instructions

### **Option A: Local Multi-Instance (Recommended for Testing)**

You need **3 UE5 instances** running on ports 50051, 50051, 50052.

#### Step 1: Launch UE5 Instances

**Terminal 1 (Local worker - Port 50051):**
```bash
cd <UE5_Project_Path>
# Launch UE5 Editor or packaged game with Schola listening on port 50051
# (This is your normal UE5 instance)
```

**Terminal 2 (Worker 1 - Port 50051):**
```bash
cd <UE5_Project_Path>
# Launch second UE5 instance on port 50051
# Set in UE5: Edit → Project Settings → Schola → Port = 50051
```

**Terminal 3 (Worker 2 - Port 50052):**
```bash
cd <UE5_Project_Path>
# Launch third UE5 instance on port 50052
# Set in UE5: Edit → Project Settings → Schola → Port = 50052
```

#### Step 2: Start Training
```bash
cd CORTEX_Training
python train_rllib.py --iterations 200
```

---

### **Option B: Conservative (1 UE5 Instance Only)**

If you can only run 1 UE5 instance, you'll need to adjust the config:

#### Edit `train_rllib.py`:
```python
class SBDAPMConfig:
    NUM_WORKERS = 0  # Disable parallel workers
    TRAIN_BATCH_SIZE = 2000  # Reduce batch size for faster iterations
```

**Trade-off:** Training will be slower (~2-3 hours/iteration), but still better than before due to reward shaping and hyperparameter fixes.

---

### **Option C: Distributed (AWS/Multi-Machine)**

If you have multiple machines with UE5:

#### Machine 1 (192.168.1.100):
```bash
# Launch UE5 with Schola on port 50051
```

#### Machine 2 (192.168.1.101):
```bash
# Launch UE5 with Schola on port 50051
```

#### Machine 3 (192.168.1.102):
```bash
# Launch UE5 with Schola on port 50051
```

#### Training Machine:
```bash
export WORKER_IPS="192.168.1.100 192.168.1.101 192.168.1.102"
cd CORTEX_Training
python train_rllib.py --iterations 200
```

---

## Expected Training Metrics

### **Healthy Training Progress (Target by Iteration 100):**

| Metric | Iter 1 | Iter 30 | Iter 50 | Iter 100 |
|--------|--------|---------|---------|----------|
| **Episode Reward** | -2000 to -1500 | -800 to -500 | -400 to -200 | -100 to +50 |
| **Episode Length** | 1800-2000 | 1600-1800 | 1400-1600 | 1200-1500 |
| **vf_explained_var** | 0.01-0.05 | 0.15-0.30 | 0.35-0.55 | 0.50-0.75 |
| **Entropy** | 1.35-1.38 | 1.20-1.35 | 0.80-1.10 | 0.30-0.60 |
| **Entropy Coeff** | 0.500 | 0.500 | 0.275 | 0.050 |

### **What to Watch For:**

#### ✅ **Good Signs:**
- Episode reward increasing steadily
- Episode length decreasing (reaching objectives faster)
- vf_explained_var climbing above 0.3
- Entropy decreasing (policy converging)
- Log messages showing "objective proximity rewards"

#### ⚠️ **Warning Signs:**
- vf_explained_var stuck below 0.1 after 50 iterations
  - **Fix:** Increase `VF_LOSS_COEFF` to 2.0 or higher
- Entropy not decreasing after iteration 50
  - **Fix:** Check entropy coefficient schedule
- Episode reward not improving after 30 iterations
  - **Fix:** Check reward shaping weights (may need tuning)

#### 🔴 **Critical Problems:**
- Training crashes or hangs
  - **Check:** All UE5 instances are running and responsive
  - **Check:** Ports are not blocked by firewall
- "Connection refused" errors
  - **Check:** UE5 Schola plugin is active and listening on correct ports
  - **Check:** Port numbers match in both UE5 and Python config

---

## Troubleshooting

### Problem: "Too few episodes per iteration"
**Symptom:** Only 1-2 UE episodes completing per iteration

**Diagnosis:** Episodes are too long (timing out at 2000 steps)

**Fix:** Reduce `MAX_EPISODE_STEPS` for early training:
```python
class SBDAPMConfig:
    MAX_EPISODE_STEPS = 1000  # Reduce from 2000 for faster learning
```

### Problem: "vf_explained_var not improving"
**Symptom:** Stuck below 0.1 after 50 iterations

**Fix:** Increase value function learning:
```python
class SBDAPMConfig:
    VF_LOSS_COEFF = 2.0  # Increase from 1.5
```

### Problem: "Rewards still very negative"
**Symptom:** Episode rewards stuck below -1000

**Diagnosis:** UE5 penalties may be too harsh, or reward shaping too weak

**Fix:** Increase reward shaping weights in `sbdapm_env.py`:
```python
# In _shape_reward():
survival_bonus = 0.002  # Increase from 0.001
proximity_reward = 0.1 / max(obj_distance, 1.0)  # Increase from 0.05
```

---

## Monitoring Training

Use the provided monitoring script to track progress:

```bash
# In a separate terminal
python monitor_training.py training_results/<timestamp>
```

Or manually check TensorBoard:
```bash
tensorboard --logdir=training_results
# Open http://localhost:6006
```

---

## Next Steps After 100 Iterations

1. **Evaluate learned policy:**
   - Check if `vf_explained_var > 0.5`
   - Check if episode reward > -200
   - Check if episode length < 1500

2. **If learning is successful:**
   - Continue training to 200-300 iterations for refinement
   - Test exported ONNX model in UE5

3. **If learning is marginal:**
   - Increase `MAX_EPISODE_STEPS` back to 2000
   - Adjust reward shaping weights based on logs
   - Consider curriculum learning (progressive episode lengths)

4. **If learning failed:**
   - Check UE5 reward structure (may need adjustment C++-side)
   - Verify observation features are meaningful
   - Consider simpler task for initial training

---

## Port Configuration Reference

| Worker | Type | Port | UE5 Instance |
|--------|------|------|--------------|
| Local  | Main | 50051 | Required |
| Worker 1 | Remote | 50051 | Optional (2x speedup) |
| Worker 2 | Remote | 50052 | Optional (3x speedup) |
| Worker 3 | Remote | 50053 | Optional (4x speedup) |
| Worker 4 | Remote | 50054 | Optional (5x speedup) |

Current config uses **2 workers** (3 total environments).

To add more workers:
```python
NUM_WORKERS = 4  # 5 total environments
# Requires UE5 instances on ports: 50051, 50051, 50052, 50053, 50054
```

---

## Quick Reference: What Changed

### `train_rllib.py`:
- Line 167: Network `[256, 256, 128]` (was `[128, 128, 64]`)
- Line 170: Learning rate `1e-4` (was `3e-4`)
- Line 178: VF loss coeff `1.5` (was `0.5`)
- Line 181: NUM_WORKERS `2` (was `0`)
- Line 415-433: Added `get_entropy_coeff()` decay schedule
- Line 490-492: Entropy decay integrated into training loop
- Line 510-519: Enhanced metric logging (vf_var, entropy)

### `sbdapm_env.py`:
- Line 253-338: Added `_shape_reward()` dense reward shaping
- Line 363-364: Clear reward state on reset
- Line 567-569: Apply reward shaping in step()

Total changes: ~150 lines added/modified across 2 files.
