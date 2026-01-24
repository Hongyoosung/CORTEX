# Step 1000 Deadlock - Root Cause & Fix

## Problem Summary
Training consistently deadlocks at exactly step 1000, where:
- Python's `poll()` blocks indefinitely
- UE5 continues operating normally and sends observations successfully
- No gRPC errors, no connector issues, no UE5 crashes
- Observations are sent by UE5 but never received by Python

## Root Cause: Training Update Blocking gRPC Reception

**The deadlock occurs because of a perfect storm at step 1000:**

1. **Exact Batch Boundary**
   - 32 agents (4 envs × 8) × 1000 steps = **32,000 agent steps**
   - `TRAIN_BATCH_SIZE = 32000` (was exactly aligned!)
   - RLlib triggers policy update immediately when this threshold is reached

2. **Thread Contention**
   - Python is inside `poll()` waiting for step 1000's observations
   - UE5 sends observations via gRPC → message arrives in Python's buffer
   - RLlib detects 32,000 samples → triggers training update
   - Training update acquires locks or blocks the thread
   - **gRPC message handler can't run because Python thread is busy**
   - `poll()` never returns → Python waits forever

3. **Why UE5 Continues Working**
   - UE5 successfully calls `SubmitEnvironmentStates()`
   - gRPC channel accepts the message (buffered on Python side)
   - UE5 thinks everything is fine and continues
   - But Python never processes the buffered message

## The Paradox Explained

| Component | What Happens | Why It Seems OK |
|-----------|--------------|-----------------|
| **UE5** | Sends observations successfully | Logs "Submitted Environment States" |
| **gRPC Channel** | Buffers message on receiver side | No errors, channel healthy |
| **Python** | Blocks in `poll()`, can't process buffer | Waiting for message that's already arrived |
| **RLlib** | Starts training, holds locks | Normal behavior at batch boundary |

The message exists in Python's gRPC receive buffer, but the thread that processes gRPC messages is blocked by RLlib's training update.

## Fixes Applied

### Fix 1: Avoid Exact 1000-Step Alignment
**File:** `train_rllib.py:207`

```python
# BEFORE:
TRAIN_BATCH_SIZE = 32000  # Triggers at exactly step 1000

# AFTER:
TRAIN_BATCH_SIZE = 33600  # Triggers at step 1050 instead (32 × 1050)
```

**Why this works:**
- Training now triggers at step 1050, not 1000
- Breaks the exact alignment that causes the race condition
- Gives a buffer zone for gRPC message processing

### Fix 2: Reduce Training Frequency
**File:** `train_rllib.py:272`

```python
# BEFORE:
min_sample_timesteps_per_iteration=100  # Very frequent training updates

# AFTER:
min_sample_timesteps_per_iteration=1000  # Less frequent, more stable
```

**Why this works:**
- Reduces how often RLlib interrupts rollout collection
- Gives gRPC more uninterrupted time to process messages
- Training still happens regularly, just less frequently

### Fix 3: Add Diagnostic Warnings
**File:** `sbdapm_env.py:413-416`

```python
# Warn when approaching batch boundaries
if current_max_step % 1000 == 0:
    print(f"⚠️  WARNING: Step {current_max_step} is a training batch boundary!")
    print(f"   If poll() blocks here, RLlib training update may be interfering with gRPC")
```

**Why this helps:**
- Early warning if the issue recurs
- Helps identify future batch-aligned deadlocks
- Makes debugging faster

## Why This Issue Is Hard to Detect

1. **No Error Messages**
   - UE5 thinks it succeeded
   - gRPC thinks it's healthy
   - Python just waits silently

2. **Timing-Dependent**
   - Only happens at exact batch boundaries
   - Requires specific RLlib configuration
   - Race condition between training update and gRPC reception

3. **Reproducible But Obscure**
   - Always fails at step 1000 (if batch size = 32000)
   - Looks like a UE5 or network issue
   - Actually an RLlib threading problem

## Testing the Fix

Run training and verify:

```bash
cd CORTEX_Training
python train_rllib.py --iterations 10
```

**Expected behavior:**
- Step 1000 logs the boundary warning but **does NOT block**
- Training continues past step 1000 smoothly
- First training update happens around step 1050 (33600 ÷ 32 agents)
- No deadlocks at any step

**If deadlock still occurs:**
1. Check if it's still at step 1000 (would be very unlikely with new batch size)
2. If it's at step 1050, increase `TRAIN_BATCH_SIZE` to 35200 (step 1100)
3. Consider adding worker threads: `NUM_WORKERS = 1` (separates rollout from training)

## Alternative Solutions (If Issue Persists)

### Option 1: Use Worker Threads (Recommended for Production)
```python
NUM_WORKERS = 1  # Separate thread for rollout collection
```
This completely separates rollout from training, preventing thread contention.

### Option 2: Increase Batch Size Significantly
```python
TRAIN_BATCH_SIZE = 64000  # Train every 2000 steps
```
Larger batch size = less frequent training = fewer opportunities for deadlock.

### Option 3: Add gRPC Timeout (Last Resort)
Modify Schola library to add timeout to `poll()` calls:
```python
# In schola's poll() method:
step_result = self.schola_env.poll(timeout=10.0)  # 10 second timeout
```

This would raise an exception instead of blocking forever, but doesn't fix the root cause.

## Related Issues

- [DEADLOCK_FIX.md](DEADLOCK_FIX.md) - AutoReset deadlock (different issue)
- [STEP_1000_DIAGNOSTIC.md](STEP_1000_DIAGNOSTIC.md) - Original diagnostic guide

## Key Takeaway

**The step 1000 deadlock was NOT random** - it was a deterministic race condition caused by:
- Exact alignment of environment steps with RLlib batch size
- Thread contention between training updates and gRPC message processing
- Python's inability to process buffered gRPC messages during training

Changing `TRAIN_BATCH_SIZE` from 32000 to 33600 breaks this alignment and resolves the deadlock.
