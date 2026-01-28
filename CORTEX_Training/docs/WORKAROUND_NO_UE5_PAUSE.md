# Workaround: Training Without UE5 Pause (Temporary)

## Problem

If you haven't implemented UE5 `pause_simulation()` / `resume_simulation()` yet, you'll see:

```
⚠️  CRITICAL WARNING: UE5 PAUSE/RESUME NOT AVAILABLE
```

And episodes will be shorter than expected (e.g., 18s instead of 30s).

## Why This Happens

```
Timeline WITHOUT UE5 pause:

0s:   Episode starts in UE5
      Python collecting samples...

18s:  Python calls pause() (policy update starts)
      - Python gRPC thread stops
      - BUT UE5 timer keeps running!

30s:  UE5 times out and resets episode (12s into Python's pause)
      - Episode completion is queued
      - UE5 starts new episode

48s:  Python calls resume() (policy update done after 30s)
      - Python sees queued completion from old episode
      - UE5 is already 18s into the NEW episode
      - DESYNC: Python thinks episode just ended, UE5 is mid-episode

Result: Episode lengths are inconsistent, boundaries misaligned
```

## Temporary Workarounds

### Option 1: Increase Policy Update Frequency (Recommended)

Make policy updates happen LESS often by collecting more samples between updates:

**In `train_rllib.py`:**

```python
config = (
    PPOConfig()
    .training(
        train_batch_size=16000,  # Was 4000 → Increase to 16000+
        sgd_minibatch_size=2048,  # Was 512 → Increase to 2048
        num_sgd_iter=5,           # Was 10 → Reduce to 5
    )
    .environment(...)
)
```

**Effect:**
- Collects 4x more samples before updating (16000 vs 4000)
- Policy updates every ~3-4 episodes instead of every episode
- Reduces chance of mid-episode update
- **Trade-off:** Slower training convergence, more memory usage

### Option 2: Reduce Policy Update Duration

Speed up policy updates so they finish faster:

**In `train_rllib.py`:**

```python
config = (
    PPOConfig()
    .training(
        num_sgd_iter=3,           # Was 10 → Reduce to 3
        sgd_minibatch_size=1024,  # Was 512 → Increase (fewer batches)
    )
    .resources(
        num_gpus=1,               # Ensure GPU is used
    )
)
```

**Effect:**
- Policy update takes ~5-10s instead of 20-30s
- Less chance of UE5 timeout during update
- **Trade-off:** Less stable training, may need more episodes to converge

### Option 3: Increase UE5 Episode Timeout (Temporary Hack)

**In UE5 GameMode Blueprint or C++:**

```cpp
// Increase MaxEpisodeDuration from 30s to 120s
MaxEpisodeDuration = 120.0f;  // Was 30.0f
```

**Effect:**
- Episodes last longer (120s instead of 30s)
- Policy updates can complete without hitting timeout
- **Trade-off:** Episodes are much longer, slower iteration, not a real fix

### Option 4: Disable Timeout (Not Recommended)

**In UE5:**

```cpp
// Comment out episode timeout entirely
void AScholaGameMode::Tick(float DeltaTime)
{
    // CurrentEpisodeTime += DeltaTime;
    // if (CurrentEpisodeTime >= MaxEpisodeDuration)
    // {
    //     OnEpisodeTimeout();
    // }
}
```

**Effect:**
- Episodes never timeout
- **Trade-off:** Episodes only end on team annihilation, may run forever if no one dies

## Comparison Table

| Workaround | Effectiveness | Ease | Trade-offs |
|------------|---------------|------|------------|
| **Increase train_batch_size** | ⭐⭐⭐ Medium | ✅ Easy | Slower convergence, more RAM |
| **Reduce num_sgd_iter** | ⭐⭐ Low | ✅ Easy | Less stable training |
| **Increase UE5 timeout** | ⭐⭐⭐⭐ High | ✅ Easy | Slower iteration |
| **Disable timeout** | ⭐⭐⭐⭐⭐ Perfect | ✅ Easy | Episodes may never end |

## Recommended Temporary Solution

**Combine Option 1 + Option 3:**

1. **Python:** Increase `train_batch_size` to 16000
2. **UE5:** Increase `MaxEpisodeDuration` to 120s

This gives you:
- Policy updates every ~3-4 episodes (less frequent)
- 120s window for update to complete (more time)
- High chance of avoiding mid-episode updates

**Then work on implementing proper UE5 pause/resume for production.**

## Long-Term Solution

**YOU MUST IMPLEMENT UE5 PAUSE/RESUME FOR PRODUCTION TRAINING.**

Workarounds are acceptable for testing, but proper pause/resume is required for:
- Consistent episode lengths
- Reliable training metrics
- Episode boundary alignment
- Reproducible results

See `v9.0.2_UE5_PAUSE_INTEGRATION.md` for implementation instructions.

## Diagnostic Output

When running training, you'll see:

**Without UE5 pause (current state):**
```
UE5 PAUSE/RESUME CAPABILITY CHECK:
  pause_simulation():  ❌ MISSING
  resume_simulation(): ❌ MISSING

⚠️  CRITICAL WARNING: UE5 PAUSE/RESUME NOT AVAILABLE
```

**With UE5 pause (after implementation):**
```
UE5 PAUSE/RESUME CAPABILITY CHECK:
  pause_simulation():  ✅ FOUND
  resume_simulation(): ✅ FOUND
  ✅ UE5 pause/resume is AVAILABLE - episode timer will freeze during policy updates
```

## Testing

After applying workaround, verify:

1. **Episode lengths should stabilize** (closer to expected duration)
2. **Fewer mid-episode resets** (check logs for episode completions during pauses)
3. **Training should continue** (even if not perfect)

Run training and look for:
```
🏁 [ENV 0 EPISODE COMPLETE] Episode 1 - TRUNCATED (timeout)
Duration: 29.5s, Steps: 1475    <-- Should be close to MaxEpisodeDuration
```

If duration is still much shorter (e.g., 18s when MaxEpisodeDuration=30s), the workaround isn't effective enough - you need to implement UE5 pause.
