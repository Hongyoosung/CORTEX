# Schola Vectorization vs RLlib Vectorization

## The Confusion

There are **two different types of vectorization**, and mixing them up causes connection failures:

### RLlib's Native Vectorization (❌ NOT compatible with Schola)

```python
NUM_ENVS_PER_WORKER = 4  # Creates 4 separate Python env objects

# RLlib creates:
env_1 = SBDAPMMultiAgentEnv()  # Connects to UE5 port 50051
env_2 = SBDAPMMultiAgentEnv()  # Tries to connect to port 50051 again → FAILS
env_3 = SBDAPMMultiAgentEnv()  # Never reached
env_4 = SBDAPMMultiAgentEnv()  # Never reached
```

**Problem**: All 4 Python objects try to connect to the same UE5 gRPC server, causing conflicts.

---

### Schola's Built-in Vectorization (✓ Correct approach)

```python
NUM_ENVS_PER_WORKER = 1      # Create ONLY 1 Python env object
NUM_UE5_ENVIRONMENTS = 4     # That object manages 4 UE5 environments

# RLlib creates:
env = SBDAPMMultiAgentEnv(num_envs=4)  # Single connection to UE5 port 50051
   └─ ScholaEnv connection
      └─ Manages 4 UE5 environments via nested dictionaries:
         ├─ obs_nested[0][agent_idx] → Environment 0 (8 agents)
         ├─ obs_nested[1][agent_idx] → Environment 1 (8 agents)
         ├─ obs_nested[2][agent_idx] → Environment 2 (8 agents)
         └─ obs_nested[3][agent_idx] → Environment 3 (8 agents)
```

**Advantage**: Single gRPC connection manages all 4 environments efficiently.

---

## How Schola Achieves Vectorization

Schola uses **nested dictionaries** in the gRPC protocol:

```python
# Single hard_reset() call returns observations for ALL environments
obs_nested = env.hard_reset()

# Structure:
obs_nested = {
    0: {0: obs, 1: obs, ..., 7: obs},  # Environment 0, 8 agents
    1: {0: obs, 1: obs, ..., 7: obs},  # Environment 1, 8 agents
    2: {0: obs, 1: obs, ..., 7: obs},  # Environment 2, 8 agents
    3: {0: obs, 1: obs, ..., 7: obs},  # Environment 3, 8 agents
}

# SBDAPMMultiAgentEnv flattens this for RLlib:
obs_flat = {
    "agent_0": obs,   # Env 0, Agent 0
    "agent_1": obs,   # Env 0, Agent 1
    ...
    "agent_8": obs,   # Env 1, Agent 0
    ...
    "agent_31": obs,  # Env 3, Agent 7
}
```

---

## Correct Configuration

### train_rllib.py

```python
class SBDAPMConfig:
    # RLlib settings
    NUM_WORKERS = 0              # Single process (Windows)
    NUM_ENVS_PER_WORKER = 1      # ✓ Only 1 Python env object
    NUM_UE5_ENVIRONMENTS = 4     # ✓ Manage 4 UE5 environments

    # Batch sizes (based on data volume, not number of Python objects)
    TRAIN_BATCH_SIZE = 32000     # 32 agents → 4× data volume
    SGD_MINIBATCH_SIZE = 2048    # Scaled proportionally
```

### Why This Works

- **Single gRPC connection**: No conflicts, no duplicate connections
- **Schola handles parallelism**: 4 UE5 environments run simultaneously
- **RLlib sees 32 agents**: Treats them as a single multi-agent environment
- **4× speedup**: All environments step in parallel on UE5 side

---

## Common Mistakes

### ❌ Mistake 1: Setting NUM_ENVS_PER_WORKER = 4

**Result**: RLlib creates 4 Python env objects → all try to connect to port 50051 → timeout/conflict

**Logs**:
```
[ENV v8.5] Connecting to host.docker.internal:50051...
[ENV v8.0] Connected!
[ENV v8.5] Connecting to host.docker.internal:50051...  ← Hangs here
```

**Fix**: Set `NUM_ENVS_PER_WORKER = 1`

---

### ❌ Mistake 2: Forgetting to pass num_envs=4

**Result**: Single Python env only manages 1 UE5 environment (3 environments wasted)

**Fix**: Pass `num_envs=4` in env_config:
```python
config = {
    "host": "localhost",
    "port": 50051,
    "num_envs": 4,  # ✓ Tell env to manage 4 UE5 environments
}
```

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│  Python Training Process (Docker Container)                 │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ RLlib PPO Algorithm                                   │  │
│  │ NUM_WORKERS=0, NUM_ENVS_PER_WORKER=1                  │  │
│  └────────────────────┬──────────────────────────────────┘  │
│                       │                                      │
│  ┌────────────────────▼──────────────────────────────────┐  │
│  │ SBDAPMMultiAgentEnv (single instance)                 │  │
│  │ num_envs=4                                            │  │
│  └────────────────────┬──────────────────────────────────┘  │
│                       │ gRPC                                 │
└───────────────────────┼──────────────────────────────────────┘
                        │ host.docker.internal:50051
┌───────────────────────▼──────────────────────────────────────┐
│  UE5 Instance (Windows Host)                                 │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ Schola gRPC Server (Port 50051)                       │  │
│  └────────────────────┬──────────────────────────────────┘  │
│                       │                                      │
│  ┌────────────────────▼──────────────────────────────────┐  │
│  │ ScholaManagerSubsystem                                │  │
│  │ Manages nested dictionary protocol                    │  │
│  └─────────┬────────────────────────────────────────────┘  │
│            │                                                 │
│  ┌─────────▼─────────┬─────────────┬──────────────┐        │
│  │ ScholaEnv_0       │ ScholaEnv_1 │ ScholaEnv_2  │...     │
│  │ 8 agents          │ 8 agents    │ 8 agents     │        │
│  │ (Team 0,1)        │ (Team 2,3)  │ (Team 4,5)   │        │
│  └───────────────────┴─────────────┴──────────────┘        │
└──────────────────────────────────────────────────────────────┘
```

---

## Performance

| Configuration | Python Env Objects | UE5 Environments | Total Agents | Speedup |
|---------------|--------------------|------------------|--------------|---------|
| **Single** | 1 | 1 | 8 | 1× |
| **Vectorized (Correct)** | 1 | 4 | 32 | **4×** |
| **Wrong** | 4 | 1 | 8 | ❌ Crashes |

---

## Verification

After fixing the configuration, you should see:

```bash
# Logs when training starts
CORTEX v8.5 Vectorized Training
  Host: host.docker.internal:50051
  Workers: 0
  UE5 Environments: 4
  Total Agents: 32 (4 envs × 8 agents)

[ENV v8.5] Connecting to host.docker.internal:50051...
[ENV v8.5] Multi-environment support: ENABLED (4 parallel environments)
[ENV v8.0] Connected!
[RESET] 32 agents detected (8 per environment)  ✓ Correct!
```

**Key indicator**: Only **ONE** connection log, but managing **4 environments (32 agents)**.

---

## Summary

**Rule of thumb for Schola vectorization**:
- `NUM_ENVS_PER_WORKER = 1` (always)
- `NUM_UE5_ENVIRONMENTS = N` (number of ScholaCombatEnvironment actors in your map)
- Batch sizes scale with total agents, not Python env objects

This is fundamentally different from standard RLlib vectorization where you create multiple environment instances.
