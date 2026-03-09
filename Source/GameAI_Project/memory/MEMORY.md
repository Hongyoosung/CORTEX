# CORTEX Project Memory

## Active Investigation
- **Freeze bug**: See `freeze_investigation.md` — dead-agent auto-reset loop causes permanent freeze. Root cause is `IsEpisodeDone()` returning true for dead agents while `ComputeStatus()` returns Running, creating conflicting signals that desync UE5 and Python.

## Key Architecture (v10.2)
- Centralized Commander-Executor: ASquadManager runs MCTS, assigns roles to 5 agents
- Training: RLlib PPO via Schola 1.3.0 gRPC bridge (Docker Python ↔ Windows UE5)
- Schola tick cycle: ResolveEnvironmentStateUpdate (blocking) → UpdateEnvironments → CollectStates → SubmitStates → AutoReset

## Important Files
- Training config: `MOC_Training/training/phase1_policy_training_v10_2.py` (MOCv10_2TrainingConfig class)
- Environment: `MOC_Training/training/moc_v10_2_env.py`
- MocTrainer: `Private/Schola/Trainers/MocTrainer.cpp` — agent tick, reward, episode logic
- Schola subsystem: `Plugins/Schola-1.3.0/Source/Schola/Private/Subsystem/ScholaManagerSubsystem.cpp`

## Schola Plugin Internals
- Timeout set via Project Settings → Schola → CommunicatorSettings → Timeout (read at PythonGymConnector.cpp:42)
- ExternalGymConnector.cpp:20 — WaitFor(Timeout), sets Error on timeout (permanent, no recovery)
- AbstractTrainer::Think() calls ComputeStatus(), ComputeReward(), GetInfo() in sequence
- TrainerState::IsDone() checks Completed || Truncated status
- AutoResetType::SAME_STEP resets completed environments within same tick

## Lessons Learned
- NUM_WORKERS=1 does NOT decouple training from stepping in synchronous RLlib PPO
- Schola timeout is NOT the freeze cause — the real issue is dead-agent episode cycling
- IsEpisodeDone() and ComputeStatus() MUST be consistent — conflicting signals cause desync loops
