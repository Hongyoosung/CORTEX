# Agent Freeze Investigation (v10.2 Training)

## Status: RESOLVED — Two fixes applied (Option A + dead-agent step counter fix)

## Symptom
Training freezes around step ~1000 (Python) / step ~501 (UE5 per-agent). The freeze is **permanent** — agents never recover. Appears every training run regardless of:
- `NUM_WORKERS=0` or `NUM_WORKERS=1` (both freeze)
- Schola timeout increased to 120s (gRPC connection survives, but agents still stuck)
- Reduced SGD iterations (5 epochs × 512 minibatch — training gap ~11s)

## Key Log Evidence

### Python side
```
[FREEZE DIAG] ⚠️ RLlib took 42.7s between step() calls (step #1022, env_steps={0: 21})
```
- Total Python steps: ~1022, but per-env steps only 21
- This means many step() calls service dead-agent cycling, not real simulation progress

### UE5 side (repeating every ~0.5s during freeze)
```
[MocTrainer] BP_Agent_C_1 (DEAD): Action drained to unblock step barrier
[MocTrainer] BP_Agent_C_2 (DEAD): Action drained to unblock step barrier
[MocTrainer] BP_Agent_C_4 (DEAD): Action drained to unblock step barrier
[MocTrainer] FREEZE WATCHDOG: BP_Agent_C_7 — 2400 ticks (40.1s) without new weights | Alive=true | Steps=501
Episode ended: Agent died
Episode ended: Agent died
Submitted Environment States to External Gym Connector
```
- Dead agents (C_1, C_2, C_4) continuously cycle: drain action → compute reward → IsEpisodeDone()=true → "Episode ended: Agent died" → auto-reset → repeat
- Alive agents (C_0, C_6, C_7) have Steps=501 and NEVER receive new weights
- "Submitted Environment States" confirms gRPC IS working — this is NOT a timeout issue

## Root Cause Analysis

### The Problem: Two Episode Ending Systems Fighting Each Other

There are **two conflicting episode termination mechanisms**:

1. **UE5 side — `IsEpisodeDone()` (MocTrainer.cpp:291)**
   - Returns `true` when agent dies (line 300-303)
   - This is called during `ComputeReward()` → `LogTransition()` (line 280)
   - But more critically: Schola uses this (or the equivalent death state) to trigger auto-reset via `AutoResetType::SAME_STEP`

2. **Python side — termination suppression (moc_v10_2_env.py:615-620)**
   - Forces ALL `terminated_dict` and `truncated_dict` to `False`
   - Only Python's `_max_episode_steps` timeout ends episodes
   - Python thinks the episode is still running when UE5 has already reset dead agents

3. **UE5 side — `ComputeStatus()` (MocTrainer.cpp:1048-1051)**
   - Always returns `Running` even for dead agents
   - This was intended to prevent Schola from ending the episode
   - But `IsEpisodeDone()` still returns `true` for dead agents, creating conflicting signals

### The Desync Loop

```
Timeline:
1. Agents C_1, C_2, C_4 die in combat
2. UE5 MocTrainer::Tick() for dead agents:
   - ConsumeNewWeights() drains action ✓
   - bHasNewReward = true ✓
   - Returns early (dead path, line 118-119)
3. Schola Think() calls ComputeStatus() → Running (no termination)
4. Schola Think() calls ComputeReward() → triggers IsEpisodeDone() → true → "Episode ended"
5. But ComputeStatus() said Running, so Schola is confused
6. AutoResetType::SAME_STEP: Schola resets the dead agent's sub-environment
7. Dead agent gets reset, immediately dies again (spawns into combat? or health stays 0?)
8. Cycle repeats every tick for dead agents
9. Meanwhile, alive agents' actions are consumed by dead agent cycling — the step barrier
   advances but alive agents never get their turn
```

### Why Alive Agents Stop Getting Weights

The critical insight: **Steps=501 for alive agents but Python env_steps={0: 21}**.
- UE5 processes ~501 Schola steps for alive agents, then they stop
- Python only counts 21 environment steps (each Python step services ALL agents)
- The dead-agent cycling consumes Schola step cycles. Each cycle:
  - Dead agents drain their actions and submit new states
  - But the actions sent from Python may not include alive agents' weights
  - OR: the mapping between Python agent IDs and UE5 agents becomes corrupted after auto-resets

### Key Counter Discrepancy
- Python `_step_call_count` = 1022 (total step() calls)
- Python `_env_episode_steps[0]` = 21 (steps in current episode for env 0)
- UE5 `CurrentEpisodeSteps` = 501 (per-agent, set in MocTrainer::Tick)
- This means UE5 is running ~25x more steps than Python thinks for this environment

## What Was Already Tried (Didn't Fix It)
1. ✅ Increased Schola timeout to 120s → gRPC survives, but freeze still occurs
2. ✅ Set NUM_WORKERS=1 → training gap still exists (synchronous PPO)
3. ✅ Reduced SGD to 5 epochs × 512 minibatch → faster training, but freeze is NOT timeout-related
4. ✅ ComputeStatus() returns Running for dead agents → prevents Schola termination but IsEpisodeDone() still returns true
5. ✅ Python suppresses termination signals → but UE5 auto-reset still cycles dead agents

## Applied Fixes

### Fix 1: Option A — Remove death check from IsEpisodeDone() (APPLIED)
Makes `IsEpisodeDone()` consistent with `ComputeStatus()`. Dead agents return `Running` from
`ComputeStatus()` so `IsEpisodeDone()` must also return `false`. This stops the pre-Option A
auto-reset cycling loop.

### Fix 2: Increment CurrentEpisodeSteps in the dead-agent drain path (APPLIED)
**Root cause of the NEW freeze (post-Option A, step ~501 for alive agents):**
- Alive agents hit `MaxEpisodeSteps` (~500 Blueprint value) → `ComputeStatus()` = Truncated
- AllAgentsAct() skips Truncated agents → alive agents stop receiving new actions
- Dead agent C_1 returns `ComputeStatus()` = Running indefinitely (no step increment in dead path)
- AllAgentsThink()'s `AllDone` check is blocked by C_1 (not yet Truncated)
- SAME_STEP auto-reset never fires → alive agents permanently stuck in Truncated

**Fix** (`MocTrainer.cpp` dead-agent drain path, line ~120):
```cpp
if (ControlledCharacter && ControlledCharacter->ConsumeNewWeights())
{
    bHasNewReward = true;
    TicksWithoutNewWeights = 0;
    CurrentEpisodeSteps++;  // ← THE FIX: keeps dead agent in step-sync with alive agents
    UE_LOG(LogTemp, Log, TEXT("[MocTrainer] %s (DEAD): action drained (step %d/%d)"),
        *ControlledCharacter->GetName(), CurrentEpisodeSteps, MaxEpisodeSteps);
}
```
Dead agents now reach MaxEpisodeSteps at the same step as alive agents → AllDone=true →
SAME_STEP auto-reset fires → all agents reset to Running.

## Proposed Fix (Archived)

### Option A: Make `IsEpisodeDone()` consistent with `ComputeStatus()` (Recommended)
```cpp
// MocTrainer.cpp — IsEpisodeDone()
// REMOVE the agent-death check. Only timeout ends episodes.
bool AMocTrainer::IsEpisodeDone()
{
    if (CurrentEpisodeSteps >= MaxEpisodeSteps)
    {
        UE_LOG(LogTemp, Log, TEXT("Episode ended: Max steps reached"));
        return true;
    }
    // DO NOT check agent death here — ComputeStatus() returns Running for dead agents,
    // so IsEpisodeDone() must agree. Dead agents continue the episode until Python's
    // force timeout truncates everyone simultaneously.
    return false;
}
```
**Risk:** Low — this makes `IsEpisodeDone()` match `ComputeStatus()`, which already returns Running. The death check was only used for transition logging anyway.

### Option B: Disable AutoResetType (use Disabled instead of SAME_STEP)
In Python `moc_v10_2_env.py`, change:
```python
auto_reset_type=AutoResetType.DISABLED  # was SAME_STEP
```
Then handle resets manually in Python. **Risk:** Requires manual reset logic.

### Option C: Fix the step barrier to prioritize alive agents
Ensure that when Schola distributes actions from Python, alive agents always receive their weights before dead agents consume theirs. **Risk:** Requires modifying Schola plugin internals.

### Recommended: Option A first, then verify with a short training run.

## Additional Investigation Needed
1. **Verify the auto-reset path**: Trace exactly how Schola's `AutoReset()` (ScholaManagerSubsystem.cpp:64) handles an environment where some agents have `ComputeStatus()=Running` but `IsEpisodeDone()=true`. The `SharedEnvironmentState.h:54` ToProto filter only sends agents with `LastStatus==Running || TrainingStatus==Running`. If auto-reset changes an agent's status, it could be filtered out.

2. **Check agent ID remapping**: After auto-reset, do agent IDs change? If dead agents get new IDs after reset, Python's `agent_map` becomes stale.

3. **Log the actual action distribution**: Add logging to confirm which agents receive actions from Python's step() response vs which get zero-filled defaults.

## Files Modified So Far
- `MOC_Training/training/moc_v10_2_env.py` — freeze diagnostics, termination suppression
- `MOC_Training/training/phase1_policy_training_v10_2.py` — reduced SGD (5 epochs, 512 minibatch)
- `Private/Schola/Trainers/MocTrainer.cpp` — freeze watchdog, dead-agent action drain, ComputeStatus() always Running
- `Private/Characters/MocCharacter.cpp` — inference-branch warning
- `Private/Schola/Actuators/TacticalParameterActuator.cpp` — action-dropped error
- `Public/Schola/Trainers/MocTrainer.h` — TicksWithoutNewWeights counter, FreezeWatchdogInterval

## Key Schola Source Locations
- `Plugins/Schola-1.3.0/Source/Schola/Private/Subsystem/ScholaManagerSubsystem.cpp` — main tick loop (Tick at line 17)
- `Plugins/Schola-1.3.0/Source/Schola/Private/GymConnectors/ExternalGymConnector.cpp:17-31` — ResolveEnvironmentStateUpdate timeout logic
- `Plugins/Schola-1.3.0/Source/Schola/Private/GymConnectors/PythonGymConnector.cpp:42` — timeout read from settings
- `Plugins/Schola-1.3.0/Source/Schola/Private/Training/AbstractTrainer.cpp:121-156` — Think() flow: IsDone→ComputeStatus→ComputeReward→GetInfo
- `Plugins/Schola-1.3.0/Source/Schola/Public/Training/StateStructs/TrainerState.h:136` — IsDone() checks Completed/Truncated
- `Plugins/Schola-1.3.0/Source/Schola/Public/Training/StateStructs/SharedEnvironmentState.h:54` — ToProto agent filter
- `Plugins/Schola-1.3.0/Source/Schola/Private/Environment/AbstractEnvironment.cpp:115-141` — AllAgentsThink()
