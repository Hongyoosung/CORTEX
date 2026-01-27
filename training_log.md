[PROGRESS] Step Milestone=1000
  ⚡ Env 0: ▶️ ACTIVE, Episode 0, Steps=1000, EpisodeTime=19.5s, CurrentReward=4528.31
  ⚡ Env 1: ▶️ ACTIVE, Episode 0, Steps=1000, EpisodeTime=19.5s, CurrentReward=4715.81
  ⚡ Env 2: ▶️ ACTIVE, Episode 0, Steps=1000, EpisodeTime=19.5s, CurrentReward=4518.31
  ⚡ Env 3: ▶️ ACTIVE, Episode 0, Steps=1000, EpisodeTime=19.5s, CurrentReward=4694.81
  📊 Total Training Elapsed: 19.5s
  ⏱️ Avg poll=108.7ms, send=0.0ms
================================================================================
[CALLBACK] Progress: 32000 agent-steps, 32 agents tracked
2026-01-27 17:47:15,505 WARNING deprecation.py:50 -- DeprecationWarning: `_get_slice_indices` has been deprecated. This will raise an error in the future!
[gRPC Thread] Episode completion queued for Env 0 (TRUNCATED)
[gRPC Thread] Episode completion queued for Env 1 (TRUNCATED)
[gRPC Thread] Episode completion queued for Env 2 (TRUNCATED)
[gRPC Thread] Episode completion queued for Env 3 (TRUNCATED)
[gRPC Thread] Auto-reset detected for Env 0
[gRPC Thread] Auto-reset detected for Env 1
[gRPC Thread] Auto-reset detected for Env 2
[gRPC Thread] Auto-reset detected for Env 3
→   1/10        0.00      0.0          0        32000    56.0s       -inf

  [ITERATION 1 DETAILS]
    No episodes completed this iteration (still collecting samples)
    Agent steps this iteration: 32000
    Cumulative: 0 episodes, 32000 steps
    Loss: total=0.3508, policy=0.0007, vf=0.7744
    KL divergence: 0.001995, Entropy: 7.5153
    ⚠️ WARNING: Entropy (7.52) > 7.0 - possible exploration collapse
    Current KL coefficient: 0.2000

  >> NEW BEST REWARD: 0.00 (iteration 1)

[STEP] Episode completion pending for Env 0 (TRUNCATED)
[STEP] Episode completion pending for Env 1 (TRUNCATED)
[STEP] Episode completion pending for Env 2 (TRUNCATED)
[STEP] Episode completion pending for Env 3 (TRUNCATED)
================================================================================
🏁 [ENV 0 EPISODE COMPLETE] Episode 0 - TRUNCATED (timeout)
  Duration: 56.1s, Steps: 1001
  Total Reward: 3216.21
  Agent Rewards (final step):
    agent_0_1: 579.50
    agent_0_7: 584.00
    agent_0_0: 642.50
    agent_0_5: 438.00
    ... and 4 more agents
================================================================================
  ✅ Episode reset complete. Next episode: 1
================================================================================
🏁 [ENV 1 EPISODE COMPLETE] Episode 0 - TRUNCATED (timeout)
  Duration: 56.1s, Steps: 1001
  Total Reward: 3355.21
  Agent Rewards (final step):
    agent_1_4: 615.00
    agent_1_2: 668.00
    agent_1_6: 636.50
    agent_1_5: 598.00
    ... and 4 more agents
================================================================================
  ✅ Episode reset complete. Next episode: 1
================================================================================
🏁 [ENV 2 EPISODE COMPLETE] Episode 0 - TRUNCATED (timeout)
  Duration: 56.1s, Steps: 1001
  Total Reward: 3246.21
  Agent Rewards (final step):
    agent_2_4: 624.50
    agent_2_3: 438.00
    agent_2_7: 624.00
    agent_2_1: 648.50
    ... and 4 more agents
================================================================================
  ✅ Episode reset complete. Next episode: 1
================================================================================
🏁 [ENV 3 EPISODE COMPLETE] Episode 0 - TRUNCATED (timeout)
  Duration: 56.1s, Steps: 1001
  Total Reward: 3329.21
  Agent Rewards (final step):
    agent_3_6: 643.50
    agent_3_2: 503.50
    agent_3_3: 562.50
    agent_3_7: 470.00
    ... and 4 more agents
================================================================================
  ✅ Episode reset complete. Next episode: 1

================================================================================
📊 EPISODE COMPLETE (RLlib batch boundary)
================================================================================
  RLlib Episode ID: 953005113280305771 (internal tracking ID)
  Episode Length: 1002
  Total Return: 13146.83
  Agent-Steps: 32064

  Per-Agent Statistics (32 agents):
    Mean Reward: 410.84
    Min Reward:  294.90
    Max Reward:  496.90
    Std Dev:     56.96
================================================================================

RESET: AutoReset mode (async)
RESET: Complete (Duration=0.00s)
[STEP] Auto-reset detected for Env 0 (Episode 1 continuing, already processed=True)
[STEP] Auto-reset detected for Env 1 (Episode 1 continuing, already processed=True)
[STEP] Auto-reset detected for Env 2 (Episode 1 continuing, already processed=True)
[STEP] Auto-reset detected for Env 3 (Episode 1 continuing, already processed=True)
================================================================================
[PROGRESS] Step Milestone=100
  ⚡ Env 0: ▶️ ACTIVE, Episode 1, Steps=100, EpisodeTime=1.9s, CurrentReward=557.70
  ⚡ Env 1: ▶️ ACTIVE, Episode 1, Steps=100, EpisodeTime=1.9s, CurrentReward=570.70
  ⚡ Env 2: ▶️ ACTIVE, Episode 1, Steps=100, EpisodeTime=1.9s, CurrentReward=513.69
  ⚡ Env 3: ▶️ ACTIVE, Episode 1, Steps=100, EpisodeTime=1.9s, CurrentReward=492.70
  📊 Total Training Elapsed: 58.0s
  ⏱️ Avg poll=99.2ms, send=0.0ms
================================================================================
[CALLBACK] Progress: 4000 agent-steps, 32 agents tracked
[gRPC Thread] Episode completion queued for Env 0 (TRUNCATED)
[gRPC Thread] Episode completion queued for Env 1 (TRUNCATED)
[gRPC Thread] Episode completion queued for Env 2 (TRUNCATED)
[gRPC Thread] Episode completion queued for Env 3 (TRUNCATED)
[STEP] Episode completion pending for Env 0 (TRUNCATED)
[STEP] Episode completion pending for Env 1 (TRUNCATED)
[STEP] Episode completion pending for Env 2 (TRUNCATED)
[STEP] Episode completion pending for Env 3 (TRUNCATED)
================================================================================
🏁 [ENV 0 EPISODE COMPLETE] Episode 1 - TRUNCATED (timeout)
  Duration: 3.8s, Steps: 190
  Total Reward: -369.01
  Agent Rewards (final step):
    agent_0_1: 121.60
    agent_0_7: 119.10
    agent_0_0: 147.10
    agent_0_5: 111.60
    ... and 4 more agents
================================================================================
  ✅ Episode reset complete. Next episode: 2
================================================================================
🏁 [ENV 1 EPISODE COMPLETE] Episode 1 - TRUNCATED (timeout)
  Duration: 3.8s, Steps: 190
  Total Reward: -365.51
  Agent Rewards (final step):
    agent_1_4: 138.10
    agent_1_2: 112.60
    agent_1_6: 123.10
    agent_1_5: 134.60
    ... and 4 more agents
================================================================================
  ✅ Episode reset complete. Next episode: 2
================================================================================
🏁 [ENV 2 EPISODE COMPLETE] Episode 1 - TRUNCATED (timeout)
  Duration: 3.8s, Steps: 190
  Total Reward: -368.01
  Agent Rewards (final step):
    agent_2_4: 137.60
    agent_2_3: 117.60
    agent_2_7: 148.10
    agent_2_1: 130.60
    ... and 4 more agents
================================================================================
  ✅ Episode reset complete. Next episode: 2
================================================================================
🏁 [ENV 3 EPISODE COMPLETE] Episode 1 - TRUNCATED (timeout)
  Duration: 3.8s, Steps: 190
  Total Reward: -359.51
  Agent Rewards (final step):
    agent_3_6: 139.60
    agent_3_2: 130.60
    agent_3_3: 134.10
    agent_3_7: 120.60
    ... and 4 more agents
================================================================================
  ✅ Episode reset complete. Next episode: 2

================================================================================
📊 EPISODE COMPLETE (RLlib batch boundary)
================================================================================
  RLlib Episode ID: 143273276117467819 (internal tracking ID)
  Episode Length: 191
  Total Return: -1462.03
  Agent-Steps: 6112

  Per-Agent Statistics (32 agents):
    Mean Reward: -45.69
    Min Reward:  -74.50
    Max Reward:  -5.50
    Std Dev:     18.97
================================================================================

RESET: AutoReset mode (async)
RESET: Complete (Duration=0.00s)
RESET: AutoReset mode (async)
[RESET] Warning: Action queue full, dropping oldest action
RESET: Complete (Duration=0.00s)
RESET: AutoReset mode (async)
[RESET] Warning: Action queue full, dropping oldest action
[gRPC Thread] Auto-reset detected for Env 0
RESET: Complete (Duration=0.00s)
[gRPC Thread] Auto-reset detected for Env 1
[gRPC Thread] Auto-reset detected for Env 2
[gRPC Thread] Auto-reset detected for Env 3
[STEP] Auto-reset detected for Env 0 (Episode 2 continuing, already processed=True)
[STEP] Auto-reset detected for Env 1 (Episode 2 continuing, already processed=True)
[STEP] Auto-reset detected for Env 2 (Episode 2 continuing, already processed=True)
[STEP] Auto-reset detected for Env 3 (Episode 2 continuing, already processed=True)
[CALLBACK] Progress: 4000 agent-steps, 32 agents tracked
[CALLBACK] Progress: 8000 agent-steps, 32 agents tracked
[CALLBACK] Progress: 12000 agent-steps, 32 agents tracked
[CALLBACK] Progress: 16000 agent-steps, 32 agents tracked
[CALLBACK] Progress: 20000 agent-steps, 32 agents tracked
[CALLBACK] Progress: 24000 agent-steps, 32 agents tracked
[gRPC Thread] Episode completion queued for Env 0 (TRUNCATED)
[gRPC Thread] Episode completion queued for Env 1 (TRUNCATED)
[gRPC Thread] Episode completion queued for Env 2 (TRUNCATED)
[gRPC Thread] Episode completion queued for Env 3 (TRUNCATED)
[gRPC Thread] Auto-reset detected for Env 0
[gRPC Thread] Auto-reset detected for Env 1
[gRPC Thread] Auto-reset detected for Env 2
[gRPC Thread] Auto-reset detected for Env 3
✓   2/10      181.60    298.8          4        64000    57.2s       0.00
  >> NEW BEST REWARD: 181.60 (iteration 2)

[STEP] Episode completion pending for Env 0 (TRUNCATED)
[STEP] Episode completion pending for Env 1 (TRUNCATED)
[STEP] Episode completion pending for Env 2 (TRUNCATED)
[STEP] Episode completion pending for Env 3 (TRUNCATED)
================================================================================
🏁 [ENV 0 EPISODE COMPLETE] Episode 2 - TRUNCATED (timeout)
  Duration: 53.5s, Steps: 1
  Total Reward: -174.39
  Agent Rewards (final step):
    agent_0_1: 184.40
    agent_0_7: 173.40
    agent_0_0: 147.90
    agent_0_5: 118.40
    ... and 4 more agents
================================================================================
  ✅ Episode reset complete. Next episode: 3
================================================================================
🏁 [ENV 1 EPISODE COMPLETE] Episode 2 - TRUNCATED (timeout)
  Duration: 53.5s, Steps: 1
  Total Reward: -408.89
  Agent Rewards (final step):
    agent_1_4: 195.90
    agent_1_2: -22.60
    agent_1_6: 130.40
    agent_1_5: 42.40
    ... and 4 more agents
================================================================================
  ✅ Episode reset complete. Next episode: 3
================================================================================
🏁 [ENV 2 EPISODE COMPLETE] Episode 2 - TRUNCATED (timeout)
  Duration: 53.5s, Steps: 1
  Total Reward: -666.39
  Agent Rewards (final step):
    agent_2_4: 25.40
    agent_2_3: 113.90
    agent_2_7: 89.90
    agent_2_1: 125.90
    ... and 4 more agents
================================================================================
  ✅ Episode reset complete. Next episode: 3
================================================================================
🏁 [ENV 3 EPISODE COMPLETE] Episode 2 - TRUNCATED (timeout)
  Duration: 53.5s, Steps: 1
  Total Reward: -361.39
  Agent Rewards (final step):
    agent_3_6: 152.40
    agent_3_2: -1.60
    agent_3_3: 182.40
    agent_3_7: -12.60
    ... and 4 more agents
================================================================================
  ✅ Episode reset complete. Next episode: 3

================================================================================
📊 EPISODE COMPLETE (RLlib batch boundary)
================================================================================
  RLlib Episode ID: 528174873583937764 (internal tracking ID)
  Episode Length: 807
  Total Return: 9347.32
  Agent-Steps: 25824

  Per-Agent Statistics (32 agents):
    Mean Reward: 292.10
    Min Reward:  208.40
    Max Reward:  369.90
    Std Dev:     43.05
================================================================================

RESET: AutoReset mode (async)
RESET: Complete (Duration=0.00s)
[STEP] Auto-reset detected for Env 0 (Episode 3 continuing, already processed=True)
[STEP] Auto-reset detected for Env 1 (Episode 3 continuing, already processed=True)
[STEP] Auto-reset detected for Env 2 (Episode 3 continuing, already processed=True)
[STEP] Auto-reset detected for Env 3 (Episode 3 continuing, already processed=True)
================================================================================
[PROGRESS] Step Milestone=100
  ⚡ Env 0: ▶️ ACTIVE, Episode 3, Steps=100, EpisodeTime=2.1s, CurrentReward=575.20
  ⚡ Env 1: ▶️ ACTIVE, Episode 3, Steps=100, EpisodeTime=2.1s, CurrentReward=513.19
  ⚡ Env 2: ▶️ ACTIVE, Episode 3, Steps=100, EpisodeTime=2.1s, CurrentReward=543.20
  ⚡ Env 3: ▶️ ACTIVE, Episode 3, Steps=100, EpisodeTime=2.1s, CurrentReward=449.20
  📊 Total Training Elapsed: 115.4s
  ⏱️ Avg poll=101.1ms, send=0.0ms
======================================