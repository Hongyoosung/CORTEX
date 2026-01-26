Diagnostic: Episode Logging Stops After Auto-Reset   

  Symptom                                                                       
  After all 4 logical environments auto-reset in UE5, Python logs stop       
  completely:

  [gRPC Thread] Auto-reset detected for Env 0-3
  📊 EPISODE COMPLETE - Episode 581171904794051279
  RESET: AutoReset mode (async)
  [CALLBACK] on_episode_start called for episode 417532301939748948  ← New   
  episode!
  [STEP] Processed auto-reset for Env 0-3 (Episode 1 starting)
  <SILENCE - No more logs>

  Missing logs that should appear:
  - [CALLBACK] on_episode_step: ... Total steps=500 (every 500 steps)        
  - [PROGRESS] Step Milestone=100 (every 100 steps)
  - 🏁 [ENV X DONE] (when environments complete)

  Root Cause Hypothesis

  RLlib's EnvRunner is stuck in a waiting state after episode completion.    

  The Flow:

  1. Episode completes: All 32 agents reach terminated=True, __all__=True    
  2. RLlib's response: Calls on_episode_end callback, then reset()
  3. Your reset() returns: New observations with
  episode_id=417532301939748948
  4. RLlib creates new episode object and calls on_episode_start
  5. First step() called: Processes auto-reset events, returns new data      
  6. PROBLEM: RLlib might be in batch_mode='complete_episodes' and waiting   
  for episode completion before processing more steps

  Key Evidence:

  2026-01-26 14:20:16,022 WARNING env_runner_v2.py:157 -- More than 482368   
  observations in 15074 env steps

  This warning indicates:
  - RLlib is buffering observations (482k observations from 32 agents × 15074
   steps)
  - It's waiting for episode termination to flush the buffer
  - batch_mode='complete_episodes' is likely enabled

  The Deadlock:

  RLlib EnvRunner:
    "Waiting for episode to complete before processing next batch"
    ↓
  UE5 continues running (async)
    ↓
  gRPC thread receives observations (buffer keeps updating)
    ↓
  But main thread never calls step() again
    ↓
  No logs appear

  Likely Causes

  1. Batch Mode Configuration (Most likely)

  # In train_rllib.py
  config = {
      "batch_mode": "complete_episodes",  # ← Waits for __all__=True
      "rollout_fragment_length": 200,
  }

  Problem: After __all__=True, RLlib's EnvRunner might not call step() again 
  until certain conditions are met.

  2. Episode Rollout Worker State

  After episode completion, EnvRunnerV2 might be:
  - Flushing episode buffer to replay buffer
  - Waiting for policy update to complete
  - Blocked on some internal synchronization

  3. Auto-Reset Timing Issue

  # In sbdapm_env_async.py reset()
  return obs_dict, info_dict  # Returns immediately

  # But gRPC thread detects auto-reset AFTER reset() returns
  [STEP] Processed auto-reset for Env 0 (Episode 1 starting)

  Problem: RLlib might not realize the episode has truly started until more  
  steps accumulate.

  Diagnostic Steps (For Next Session)

  1. Check batch_mode in training config

  # In train_rllib.py
  print(f"[TRAIN] batch_mode: {config['batch_mode']}")
  print(f"[TRAIN] rollout_fragment_length: 
  {config['rollout_fragment_length']}")

  2. Add verbose logging to step()

  # At top of step() in sbdapm_env_async.py
  step_count = getattr(self, '_debug_step_count', 0)
  self._debug_step_count = step_count + 1
  if step_count % 100 == 0:
      print(f"[STEP] Called {step_count} times, obs_buffer size:
  {len(self.obs_buffer)}")

  3. Check if step() is being called at all

  # Add to beginning of step()
  import traceback
  if not hasattr(self, '_step_called_after_reset'):
      self._step_called_after_reset = True
      print(f"[STEP] First step() call after reset!")
      traceback.print_stack(limit=5)

  4. Check RLlib's EnvRunner state

  # In callback on_episode_start
  print(f"[CALLBACK] EnvRunner worker_index: {worker.worker_index}")
  print(f"[CALLBACK] Episode ID changed: old vs new")

  Proposed Fixes (To Try Next Session)

  Fix A: Change batch_mode

  config = {
      "batch_mode": "truncate_episodes",  # Don't wait for completion        
  }

  Fix B: Force episode continuation signal

  # In reset() after auto-reset
  obs_dict, info_dict = ...
  # Add explicit "episode continuing" marker
  info_dict['__common__'] = {'episode_continuing': True}
  return obs_dict, info_dict

  Fix C: Don't set all=True for individual env completions

  # In _grpc_worker_loop
  # Only set __all__=True if ALL logical envs are done simultaneously        
  all_envs_done = all(
      all(terminated_dict.get(aid, False) for aid in 
  self._get_agents_for_logical_env(i))
      for i in range(self.num_envs)
  )
  terminated_dict['__all__'] = all_envs_done

  Summary

  The callback initialization works (you see on_episode_start), but RLlib    
  stops calling step() after episode completion. This is likely due to       
  batch_mode='complete_episodes' causing EnvRunner to wait for some condition
   that never arrives in your async auto-reset architecture.

  Next session: Check training config and try Fix A or Fix C.