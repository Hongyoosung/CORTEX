# Fix: SAME_STEP Dead Agent Terminal Flag Corruption

**Date:** 2026-03-16
**File Modified:** `Plugins/Schola-2.0.1/Source/ScholaTraining/Private/GymConnectors/AbstractGymConnector.cpp`
**Branch:** `feature-schola2-0-1`

---

## Symptoms

1. **Episodes ending at `steps=1`** — Episode count (`ep`) increases every second; Python log shows `dur=0.3s steps=1`.
2. **`ValueError` crash during `algo.train()`**:
   ```
   ValueError: ('Batches sent to postprocessing must only contain steps from a single trajectory.',
   SampleBatch(862: ['obs', 'new_obs', 'actions', ...]))
   ```

---

## Root Cause

The project uses `AutoresetMode.SAME_STEP` (set in `env_wrapper.py`).

In `AbstractGymConnector.cpp`, the dead-agent protection fix (snapshot → live-only step → restore) was applied **only to the `NextStep` branch**, not the `SameStep` branch:

```
EAutoResetType::NextStep   → ✅ PreviousDeadAgents snapshot + LiveActions filter + restore
EAutoResetType::SameStep   → ❌ Called Step(ALL actions) — no protection
```

### Failure Chain

1. **Step N**: Some agents die → `bTerminated = true` in `FAgentState`.
2. **Step N+1**: `SameStep` branch calls `Environments[i]->Step(EnvStep.Actions, StateRef.AgentStates)` with **all** agents, including dead ones.
3. `DEScholaEnvironment::Step_Implementation` overwrites `FAgentState` for every agent in `AgentTrainerMap`, calling `ComputeStatus()` on dead agents. This **clears** their `bTerminated`/`bTruncated` flags or resets them inconsistently.
4. `AllAgentsCompleted()` returns `false` → SAME_STEP reset **never fires** correctly.
5. Eventually all agents happen to be dead simultaneously → reset fires at wrong time, mixing two episode segments into one `SampleBatch` → **`ValueError`**.
6. On the step where the reset does fire, dead-agent state corruption causes the very first step of the new episode to immediately return `Truncated` → **`steps=1` rapid cycling**.

---

## Fix Applied

Added the same dead-agent protection to the `SameStep` branch that already existed in `NextStep`:

```cpp
case EAutoResetType::SameStep:
{
    for (int i = 0; i < InStep.EnvSteps.Num(); i++)
    {
        // 1. Snapshot previously-dead agents before Step()
        TMap<FString, FAgentState> PreviousDeadAgents;
        for (auto& Pair : StateRef.AgentStates)
        {
            if (Pair.Value.bTerminated || Pair.Value.bTruncated)
                PreviousDeadAgents.Add(Pair.Key, Pair.Value);
        }

        // 2. Build live-only action map
        TMap<FString, TInstancedStruct<FPoint>> LiveActions;
        for (const auto& ActionPair : EnvStep.Actions)
        {
            if (!PreviousDeadAgents.Contains(ActionPair.Key))
                LiveActions.Add(ActionPair.Key, ActionPair.Value);
        }

        // 3. Step with live agents only
        Environments[i]->Step(LiveActions, StateRef.AgentStates);

        // 4. Restore terminal flags — Step() may have overwritten them
        for (auto& DeadPair : PreviousDeadAgents)
        {
            FAgentState& AgentState = StateRef.AgentStates.FindOrAdd(DeadPair.Key);
            AgentState.bTerminated = DeadPair.Value.bTerminated;
            AgentState.bTruncated  = DeadPair.Value.bTruncated;
            AgentState.Reward      = 0.0f;
        }

        // 5. Reset only when ALL agents are truly done
        if (AllAgentsCompleted(StateRef))
        {
            FInitialEnvironmentState& EnvState = OutInitialState.EnvironmentStates.Emplace(i);
            Environments[i]->Reset(EnvState.AgentStates);
        }
    }
}
```

> **Important:** The Schola plugin is a third-party plugin under `Plugins/Schola-2.0.1/`. Changes to its source require a **full C++ rebuild** of the project before taking effect. The fix will not apply until the project is recompiled.

---

## Why the Fix Wasn't Applying Before

The fix description referenced in the commit correctly described the intended behavior, and the `NextStep` branch was updated. However, the active training mode is `SAME_STEP` — so the `NextStep` code path was never reached. The `SameStep` branch was left unprotected.

---

## Related Files

| File | Change |
|---|---|
| `Plugins/Schola-2.0.1/Source/ScholaTraining/Private/GymConnectors/AbstractGymConnector.cpp` | Added dead-agent protection to `SameStep` branch |
| `DE_Training/training/env_wrapper.py:90` | Uses `AutoresetMode.SAME_STEP` — no change needed |
