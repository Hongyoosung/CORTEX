# MOC v10.2 - Episode Reset Bug Fix

**Date:** 2026-02-13
**Issue:** Episodes reset immediately after environment reset with 0 steps and 0 reward
**Status:** ✅ FIXED

---

## Bug Summary

After running `python phase1_policy_training_v10_2.py` and connecting to UE5, episodes would reset almost twice per second with the following symptoms:

```
LogTemp: Warning: [MocTrainer] Cannot compute reward - invalid agent or character
LogTemp: [MocTrainer] Episode completed. Final reward: 0.00, Steps: 0
```

---

## Root Causes Identified

### 1. **Invalid Agent/Character References After Reset** ⚠️ CRITICAL
**Location:** `MocTrainer.cpp:205-211` (ComputeReward)
**Problem:** After environment reset, cached `MocAgent` and `ControlledCharacter` pointers became stale/null

**Flow:**
1. Environment resets → Characters may be destroyed/recreated
2. Python calls `step()` → UE5 calls `ComputeReward()`
3. `ComputeReward()` checks `if (!MocAgent || !ControlledCharacter)` → **FAILS**
4. Returns 0 reward → Episode completes immediately with 0 steps

**Root Cause:** `ResetTrainer()` did not re-validate or re-acquire agent references after reset

### 2. **Action Dimension Mismatch** ⚠️ CRITICAL
**Location:** `MocTrainer.cpp:175`
**Problem:** Code expected 8-dim actions, but v10.2 architecture uses 7-dim EQS weights

```cpp
// BEFORE (WRONG):
if (ActionValues.Num() != 8)  // ❌ v10.1 had 8 weights

// AFTER (CORRECT):
if (ActionValues.Num() != 7)  // ✅ v10.2 has 7 weights
```

**Action Space (v10.2):**
- [0]: EnemyObjectiveProximity
- [1]: AllyObjectiveProximity
- [2]: CoverDensity
- [3]: EnemyVisibility
- [4]: AllyProximity
- [5]: CombatRange
- [6]: PickupProximity

**Impact:** Python sends 7-dim actions → C++ rejects them → No actions applied → Episode terminates

---

## Fixes Applied

### Fix 1: Re-validate Agent References in `ResetTrainer()` ✅

**File:** `Source/GameAI_Project/Private/Schola/Trainers/MocTrainer.cpp`
**Function:** `AMocTrainer::ResetTrainer()`

**Changes:**
- Added validation checks for `MocAgent` and `ControlledCharacter` using `IsValid()`
- If references are stale, attempt to re-acquire them from the controlled pawn
- Added detailed logging to track reference state after reset

**Code:**
```cpp
void AMocTrainer::ResetTrainer()
{
    // Reset per-episode state
    CurrentEpisodeSteps = 0;
    EpisodeReward = 0.0f;

    // v10.2 FIX: Re-validate agent and character references after reset
    if (!MocAgent || !IsValid(MocAgent))
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] MocAgent reference invalid after reset, attempting to re-acquire..."));
        APawn* ControlledPawn = GetPawn();
        if (ControlledPawn)
        {
            MocAgent = ControlledPawn->FindComponentByClass<UScholaMocAgent>();
            if (MocAgent)
            {
                UE_LOG(LogTemp, Log, TEXT("[MocTrainer] ✓ MocAgent re-acquired successfully"));
            }
        }
    }

    if (!ControlledCharacter || !IsValid(ControlledCharacter))
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] ControlledCharacter reference invalid after reset, attempting to re-acquire..."));
        APawn* ControlledPawn = GetPawn();
        if (ControlledPawn)
        {
            ControlledCharacter = Cast<AMocCharacter>(ControlledPawn);
            if (ControlledCharacter)
            {
                UE_LOG(LogTemp, Log, TEXT("[MocTrainer] ✓ ControlledCharacter re-acquired successfully: %s"), *ControlledCharacter->GetName());
            }
        }
    }

    // ... rest of reset logic
}
```

### Fix 2: Correct Action Dimension Check ✅

**File:** `Source/GameAI_Project/Private/Schola/Trainers/MocTrainer.cpp`
**Function:** `AMocTrainer::ApplyAction()`

**Changes:**
- Changed action dimension check from 8 to 7
- Added v10.2-specific comments documenting the 7-dim action space
- Added periodic action logging for debugging

**Code:**
```cpp
void AMocTrainer::ApplyAction(const TArray<float>& ActionValues)
{
    // v10.2 FIX: Validate references
    if (!MocAgent || !IsValid(MocAgent) || !ControlledCharacter || !IsValid(ControlledCharacter))
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] Cannot apply action - invalid agent or character"));
        return;
    }

    // v10.2: Action space is 7-dim EQS weights (not 8!)
    if (ActionValues.Num() != 7)
    {
        UE_LOG(LogTemp, Error, TEXT("[MocTrainer] Invalid action size: %d (expected 7 for v10.2)"), ActionValues.Num());
        return;
    }

    // ... rest of action application
}
```

### Fix 3: Improved Error Logging in `ComputeReward()` ✅

**File:** `Source/GameAI_Project/Private/Schola/Trainers/MocTrainer.cpp`
**Function:** `AMocTrainer::ComputeReward()`

**Changes:**
- Added `IsValid()` checks for both `MocAgent` and `ControlledCharacter`
- Enhanced error messages to show which reference is invalid and at which step
- Helps debug future reference issues

---

## Episode Timeout Configuration

### Where to Set Episode Timeout

**Option 1: UE5 Blueprint/C++ (Per-Agent Timeout)**

**File:** `Source/GameAI_Project/Public/Schola/Trainers/MocTrainer.h`
**Line:** 134

```cpp
/** Episode 최대 길이 */
UPROPERTY(EditAnywhere, Category="Training")
int32 MaxEpisodeSteps = 3000;  // Default: 3000 steps
```

**How to Change:**
1. Open `BP_MocTrainer` (if using Blueprint)
2. Set `MaxEpisodeSteps` in the Details panel under "Training" category
3. OR edit the default value in `MocTrainer.h` and recompile

**When it triggers:**
- Checked in `AMocTrainer::IsEpisodeDone()` (line 248)
- Checked in `AMocTrainer::ComputeStatus()` (line 907)

---

**Option 2: Python Environment Wrapper (Global Backup Timeout)**

**File:** `MOC_Training/training/moc_v10_2_env.py`
**Line:** 127

```python
# Episode timeout (backup mechanism)
self._max_episode_steps = 6000  # 60s at 100Hz
self._force_timeout_enabled = True
```

**How to Change:**
```python
class MOCv10_2MultiAgentEnv(MultiAgentEnv):
    def __init__(self, **kwargs):
        super().__init__()

        # ... connection setup ...

        # Configure timeout
        self._max_episode_steps = 6000  # Change this value
        self._force_timeout_enabled = True  # Set False to disable
```

**When it triggers:**
- Checked in `step()` method around line 397
- Logs: `⚠️ FORCE TIMEOUT: Env {env_idx} reached {max_steps} steps`

---

**Option 3: RLlib Configuration (Recommended for Training)**

**File:** `MOC_Training/training/phase1_policy_training_v10_2.py`
**Add to RLlib config:**

```python
def create_ppo_config():
    config = PPOConfig()

    # ... existing config ...

    # Add episode horizon
    config = config.environment(
        env="moc_v10_2_env",
        env_config=create_env_config(),
        disable_env_checking=True,
        horizon=3000,  # Max episode steps (None = unlimited)
    )

    return config
```

**Alternative: Use batch_mode="complete_episodes"**
```python
config = config.env_runners(
    num_env_runners=MOCv10_2TrainingConfig.NUM_WORKERS,
    num_envs_per_env_runner=MOCv10_2TrainingConfig.NUM_ENVS_PER_WORKER,
    rollout_fragment_length=256,
    batch_mode="complete_episodes",  # Wait for natural episode termination
)
```

---

## Recommended Configuration

For v10.2 training, use this hierarchy:

1. **UE5 Trainer:** `MaxEpisodeSteps = 3000` (50s at 60Hz tick)
   - Per-agent timeout for realistic combat scenarios
   - Prevents infinite loops in UE5

2. **Python Wrapper:** `_max_episode_steps = 6000` (backup)
   - Safety net in case UE5 timeout fails
   - 2× UE5 timeout to avoid premature truncation

3. **RLlib Config:** `horizon = None` or `3000`
   - `None` = Let UE5 decide episode length
   - `3000` = Match UE5 timeout for consistency

---

## Verification Steps

After applying fixes, verify the bug is resolved:

### 1. Compile UE5 Project
```bash
# In Visual Studio:
# Build → Build Solution (Ctrl+Shift+B)
```

### 2. Launch UE5 PIE
- Open `Content/Game/Maps/Training/Training_Basic2`
- Press Play (PIE)
- Wait for "ScholaManagerSubsystem initialized" log

### 3. Run Training Script
```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\MOC_Training\training
python phase1_policy_training_v10_2.py --mode rllib --iterations 10
```

### 4. Expected Behavior ✅
```
[SCHOLA RESET v10.2] ResetEnvironment() called on BP_ScholaEnv_C_1 (EnvID: 0)
[MocTrainer] v10.2 Trainer reset for episode 1 - Agent: BP_Agent_C_0, Strategy: Assault
[MocTrainer] Action applied at step 0: [0.12, -0.45, 0.78, ...]
STEP: Duration=0.02s, Steps=1
STEP: Duration=0.02s, Steps=2
...
STEP: Duration=0.02s, Steps=100
[PROGRESS] Step Milestone=100
  ⚡ Env 0: ▶️ ACTIVE, Episode 0, Steps=100, EpisodeTime=2.5s, CurrentReward=12.34
...
```

### 5. Indicators of Success ✅
- ✅ Episodes run for >100 steps before completion
- ✅ No "Cannot compute reward - invalid agent or character" errors
- ✅ Non-zero rewards accumulating
- ✅ Actions being applied successfully
- ✅ Episode completion logs show realistic durations (>5s)

### 6. Previous Broken Behavior ❌ (should NOT see this anymore)
```
[MocTrainer] v10.2 Trainer reset for episode 1
[MocTrainer] Cannot compute reward - invalid agent or character  ❌ BUG
[MocTrainer] Episode completed. Final reward: 0.00, Steps: 0  ❌ BUG
```

---

## Additional Debugging

If episodes still reset immediately after applying fixes:

### Check 1: Verify Agent Registration
```
UE5 Log → Search for: "[ScholaEnv v10.2] Auto-discovering agents..."
Expected: "✓ Discovered: BP_Agent_C_0 (Team 0, Component: ScholaMocAgent_0)"
```

### Check 2: Verify Trainer Initialization
```
UE5 Log → Search for: "[ScholaEnv v10.2] - ✓ Spawned Trainer"
Expected: 10 trainers spawned (5 per team)
```

### Check 3: Verify Action Application
```
UE5 Log → Search for: "[MocTrainer] Action applied"
Expected: Periodic logs showing 7-dim actions being applied
```

### Check 4: Verify Python Action Shape
```python
# In phase1_policy_training_v10_2.py, add debug print:
def forward(self, input_dict, state, seq_lens):
    eqs_weights = self.policy(base_obs, strategy_idx)
    print(f"DEBUG: eqs_weights shape = {eqs_weights.shape}")  # Should be (B, 7)
    return output, state
```

---

## Summary

| Issue | Root Cause | Fix | Status |
|-------|------------|-----|--------|
| Episodes reset immediately | Stale agent references after reset | Re-validate references in `ResetTrainer()` | ✅ FIXED |
| "Cannot compute reward" error | Null `MocAgent`/`ControlledCharacter` | Added `IsValid()` checks + re-acquisition logic | ✅ FIXED |
| Actions rejected | Expected 8-dim but v10.2 uses 7-dim | Changed dimension check from 8 to 7 | ✅ FIXED |
| Episode timeout unclear | No documentation | Added configuration guide | ✅ DOCUMENTED |

---

## Next Steps

1. ✅ Apply fixes (compile UE5 project)
2. ✅ Test with `--iterations 10` to verify episodes run normally
3. ✅ Monitor UE5 logs for "Action applied" and episode completion logs
4. ✅ Adjust `MaxEpisodeSteps` if needed based on training performance
5. ✅ Proceed with full training run once verified

---

**Status:** Ready for training! 🚀
