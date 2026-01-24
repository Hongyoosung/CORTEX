"""
SBDAPM Environment for Schola/RLlib Training (v8.0 - Simplified)

v8.0 Changes:
    - REMOVED: Strategy assignment management (MCTS in UE5 controls strategies)
    - SIMPLIFIED: Code reduced from 900 lines to ~300 lines
    - ADDED: Debug logging for termination detection
    - FIXED: Episode boundary synchronization (UE5 is single source of truth)

Architecture:
    - Observation: 50 features (46 base from UE5 + 4 strategy one-hot added by Python)
    - Action: 5 continuous values (4 tactical params + 1 combat priority)
    - Episode termination: Controlled by UE5 (MaxEpisodeDuration=60s)
"""

from gymnasium import spaces
import numpy as np
import time

try:
    from ray.rllib.env.multi_agent_env import MultiAgentEnv
    RLLIB_AVAILABLE = True
except ImportError:
    RLLIB_AVAILABLE = False
    class MultiAgentEnv:
        pass

try:
    from schola.core.env import ScholaEnv, AutoResetType
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
    SCHOLA_AVAILABLE = True
except ImportError:
    SCHOLA_AVAILABLE = False
    print("Warning: schola not installed")

# Import RLConfig
try:
    from training_env.config import RLConfig
except ImportError:
    class RLConfig:
        OBSERVATION_SIZE = 50
        NUM_TOTAL_OUTPUTS = 5


if SCHOLA_AVAILABLE:

    class SBDAPMMultiAgentEnv(MultiAgentEnv):
        """
        Multi-Agent RLlib Environment for SBDAPM (v8.0).

        Key Principle: UE5 is the SINGLE SOURCE OF TRUTH for episode termination.
        """

        def __init__(self, **kwargs):
            super().__init__()

            host = kwargs.get("host", "localhost")
            port = self._resolve_port(kwargs)
            timeout = kwargs.get("timeout", 60)  # Default 60s for Docker (increased from 30s)
            is_docker = kwargs.get("is_docker", False)

            # v8.5 VECTORIZED TRAINING: Multi-environment support validated
            # This environment already handles NUM_ENVS_PER_WORKER > 1 via nested dictionaries:
            # - Agent mapping: flat_id -> (env_idx, agent_idx)
            # - Observations: obs_nested[env_idx][agent_idx]
            # - Actions: formatted_actions[env_idx][agent_idx]
            # - Rewards/Dones: rew_nested[env_idx][agent_idx]
            self.num_envs = kwargs.get("num_envs", 4)  # Default to 4 environments (configurable)
            print(f"[ENV v8.5] Connecting to {host}:{port}...")
            print(f"[ENV v8.5] Multi-environment support: ENABLED ({self.num_envs} parallel environments)")
            print(f"[ENV v8.5] Async episode termination: ENABLED (environments finish independently)")
            if is_docker:
                print(f"[ENV v8.0] Docker mode enabled - using extended timeout ({timeout}s) and keepalive options")

            try:
                # Docker mode: Schola is already patched at build time (see patch_schola_insecure.py)
                # with 60s RPC timeout interceptor and keepalive options
                connection = UnrealEditorConnection(
                    url=host,
                    port=port
                )

                self.schola_env = ScholaEnv(
                    unreal_connection=connection,
                    verbosity=1,
                    auto_reset_type=AutoResetType.SAME_STEP  # 🔥 FIX: Async episode handling
                )
                print(f"[ENV v8.0] Connected!")

            except Exception as e:
                print(f"[ERROR] Connection failed: {e}")
                raise

            # Agent mapping
            self.agent_map = {}
            self.reverse_map = {}
            self._agent_ids = set()

            if hasattr(self.schola_env, 'ids'):
                self._update_agent_map()

            # Spaces
            self._obs_space = spaces.Box(
                low=-np.inf, high=np.inf,
                shape=(RLConfig.OBSERVATION_SIZE,),
                dtype=np.float32
            )
            self._action_space = spaces.Box(
                low=-np.inf, high=np.inf,
                shape=(RLConfig.NUM_TOTAL_OUTPUTS,),
                dtype=np.float32
            )

            # Episode tracking
            self._first_reset_done = False
            self._training_start_time = None  # Global timer for total training elapsed time

            # v8.5 VECTORIZED TRAINING: Per-environment episode tracking
            self._env_episode_steps = {i: 0 for i in range(self.num_envs)}
            self._env_episode_start_time = {i: None for i in range(self.num_envs)}
            self._env_episodes_completed = {i: 0 for i in range(self.num_envs)}
            self._env_done_flags = {i: False for i in range(self.num_envs)}

            # v8.5: Logical env mapping (populated from UE5 info dict on first step)
            self._logical_env_map_initialized = False
            self._agent_logical_env = {}

        def _resolve_port(self, kwargs):
            base_port = kwargs.get("base_port")
            if base_port is not None:
                try:
                    from ray.rllib.evaluation.rollout_worker import get_global_worker
                    worker = get_global_worker()
                    worker_index = worker.worker_index if worker else 0
                    return base_port + max(0, worker_index - 1)
                except:
                    return base_port
            return kwargs.get("port", 50051)

        def _update_agent_map(self):
            self.agent_map.clear()
            self.reverse_map.clear()
            self._agent_ids.clear()

            # v8.5 VECTORIZED TRAINING: Build initial agent map using Schola indices
            # Logical environment assignment happens in _update_logical_env_map() after first step
            # when we receive info dicts containing logical_env_id from UE5
            #
            # Initial format: "agent_<physical_env>_<schola_idx>" (temporary, updated after first step)

            if hasattr(self.schola_env, 'ids'):
                for physical_env_idx, agent_list in enumerate(self.schola_env.ids):
                    for schola_agent_idx in agent_list:
                        # Temporary flat_id using physical env and schola index
                        flat_id = f"agent_{physical_env_idx}_{schola_agent_idx}"

                        # Map stores: flat_id → (physical_env_idx, schola_agent_idx)
                        self.agent_map[flat_id] = (physical_env_idx, schola_agent_idx)
                        self.reverse_map[(physical_env_idx, schola_agent_idx)] = flat_id
                        self._agent_ids.add(flat_id)

            print(f"[ENV v8.5] Initial agent map: {len(self._agent_ids)} agents (logical env assignment pending)")

        def _update_logical_env_map(self, info_nested):
            """
            v8.5 VECTORIZED TRAINING: Update agent-to-logical-env mapping from UE5 info dict.
            Called after first step when we receive logical_env_id from each agent.
            """
            if hasattr(self, '_logical_env_map_initialized') and self._logical_env_map_initialized:
                return  # Already initialized

            # Build mapping: flat_id → logical_env_id
            self._agent_logical_env = {}
            env_counts = {}

            for flat_id in list(self._agent_ids):
                physical_env_idx, schola_agent_idx = self.agent_map[flat_id]

                # Get logical_env_id from info dict
                agent_info = info_nested.get(physical_env_idx, {}).get(schola_agent_idx, {})
                if isinstance(agent_info, dict):
                    logical_env_str = agent_info.get('logical_env_id', '-1')
                    try:
                        logical_env_id = int(logical_env_str)
                    except (ValueError, TypeError):
                        logical_env_id = -1
                else:
                    logical_env_id = -1

                if logical_env_id < 0:
                    # Fallback: compute from schola index if info not available
                    logical_env_id = schola_agent_idx // 8
                    print(f"[WARN] Agent {flat_id} missing logical_env_id in info, using fallback: {logical_env_id}")

                self._agent_logical_env[flat_id] = logical_env_id
                env_counts[logical_env_id] = env_counts.get(logical_env_id, 0) + 1

            self._logical_env_map_initialized = True
            print(f"[ENV v8.5] Logical env mapping initialized: {dict(sorted(env_counts.items()))}")

        def _get_agents_for_logical_env(self, logical_env_idx):
            """Get all agent IDs belonging to a logical environment."""
            if not hasattr(self, '_agent_logical_env'):
                # Fallback before mapping is initialized
                return [aid for aid in self._agent_ids if aid.startswith(f"agent_{logical_env_idx}_")]
            return [aid for aid, env_id in self._agent_logical_env.items() if env_id == logical_env_idx]

        def _get_all_action_keys(self):
            all_keys = {}
            action_defns = getattr(self.schola_env, 'action_defns', {})

            for flat_id in self._agent_ids:
                env_idx, agent_idx = self.agent_map[flat_id]
                agent_defn = action_defns.get(env_idx, {}).get(agent_idx, None)

                if agent_defn is None:
                    continue

                keys_list = []
                if hasattr(agent_defn, 'spaces'):
                    keys_list = list(agent_defn.spaces.keys())
                elif isinstance(agent_defn, dict):
                    keys_list = list(agent_defn.keys())

                if keys_list:
                    all_keys[(env_idx, agent_idx)] = keys_list

            return all_keys

        @property
        def observation_space(self):
            return self._obs_space

        @property
        def action_space(self):
            return self._action_space

        def _build_observation(self, base_obs):
            """Build 50-dim observation: pad/truncate to 46 + add strategy one-hot."""
            # Ensure 46 base features
            if len(base_obs) < 46:
                base_obs = np.pad(base_obs, (0, 46 - len(base_obs)), mode='constant')
            elif len(base_obs) > 46:
                base_obs = base_obs[:46]

            # Add strategy one-hot (default: Assault)
            # MCTS in UE5 controls actual strategy, this is just for network input
            strategy_onehot = np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32)

            return np.concatenate([base_obs, strategy_onehot]).astype(np.float32)

        def reset(self, *, seed=None, options=None):
            reset_start = time.time()
            
            # 에피소드 통계 출력 (카운터는 step()에서만 증가)
            if self._env_episode_start_time and any(v > 0 for v in self._env_episode_steps.values()):
                print("=" * 80)
                print("EPISODE COMPLETION SUMMARY")
                for env_idx in range(self.num_envs):
                    if self._env_episode_start_time[env_idx]:
                        duration = time.time() - self._env_episode_start_time[env_idx]
                        print(f"  Env {env_idx}: Episode {self._env_episodes_completed[env_idx]}, "
                            f"Steps={self._env_episode_steps[env_idx]}, Duration={duration:.1f}s")
                        # ❌ REMOVED: Episode counter incremented in step() only
                print("=" * 80)
            
            # ✅ 첫 번째 리셋만 hard_reset 호출
            is_first = not self._first_reset_done

            if is_first:
                print("=" * 80)
                print(f"RESET: First reset - calling hard_reset() at Time={time.time():.2f}")

                # Initialize global training timer (never resets)
                if self._training_start_time is None:
                    self._training_start_time = time.time()

                current_time = time.time()
                for env_idx in range(self.num_envs):
                    self._env_episode_steps[env_idx] = 0
                    self._env_episode_start_time[env_idx] = current_time
                    self._env_done_flags[env_idx] = False
                
                try:
                    rawobs = self.schola_env.hard_reset()
                    print(f"RESET: hard_reset returned successfully. Duration={time.time()-reset_start:.2f}s")
                    self._first_reset_done = True
                    self._env_episode_start_time[env_idx] = current_time

                    if is_first:
                        self._update_agent_map()
                        print(f"RESET: {len(self._agent_ids)} agents detected "
                            f"({len(self._agent_ids) // self.num_envs} per environment)")

                    result = self._process_obs(rawobs)
                    print(f"RESET: COMPLETE Duration={time.time()-reset_start:.2f}s, Agents={len(result[0])}")
                    print("=" * 80)
                    return result
                    
                except Exception as e:
                    print(f"RESET ERROR: {e}")
                    import traceback
                    traceback.print_exc()
                    print(f"RESET FAILED Duration={time.time()-reset_start:.2f}s")
                    print("=" * 80)
                    return {}, {}
            
            else:
                # 🔥 FIX: Poll for new episode observations from AutoReset
                print(f"RESET: AutoReset mode - polling for new episode observations (Time={time.time():.2f})")

                # Reset per-environment episode tracking
                current_time = time.time()
                for env_idx in range(self.num_envs):
                    self._env_episode_steps[env_idx] = 0
                    self._env_episode_start_time[env_idx] = current_time
                    self._env_done_flags[env_idx] = False

                try:
                    # 🔥 CRITICAL FIX: Send dummy actions to trigger new episode, then poll
                    # AutoResetType.NEXT_STEP requires send_actions() → poll() cycle to get new episode data

                    # Send dummy actions (zeros) to all agents to trigger episode restart
                    allactionkeys = self._get_all_action_keys()
                    dummy_actions = {}
                    for flat_id in self._agent_ids:
                        env_idx, agent_idx = self.agent_map[flat_id]
                        if env_idx not in dummy_actions:
                            dummy_actions[env_idx] = {}
                        agent_keys = allactionkeys.get((env_idx, agent_idx), [])
                        if agent_keys:
                            dummy_action = np.array([0.5, 0.5, 0.5, 0.5, 0.0], dtype=np.float32)
                            dummy_actions[env_idx][agent_idx] = {key: dummy_action for key in agent_keys}

                    print(f"RESET: Sending dummy actions to trigger new episode...")
                    print(f"  Sending actions for {len(dummy_actions)} physical environments")
                    self.schola_env.send_actions(dummy_actions)
                    print(f"RESET: send_actions() completed")

                    # Now poll for new episode observations
                    poll_start = time.time()
                    print(f"RESET: Polling for new episode data... (THIS MAY BLOCK IF UE5 NOT RESPONDING)")
                    rawobs = self.schola_env.poll()
                    poll_duration = time.time() - poll_start
                    print(f"RESET: poll() completed in {poll_duration:.2f}s")

                    if poll_duration > 5.0:
                        print(f"⚠️⚠️⚠️  CRITICAL: poll() took {poll_duration:.1f}s (expected <1s)")
                        print(f"  This indicates UE5 episode restart is very slow or blocked!")
                        print(f"  Check UE5 logs for:")
                        print(f"    - [EPISODE END] messages")
                        print(f"    - [SCHOLA RESET] ResetEnvironment() logs")
                        print(f"    - [EPISODE START] OnEpisodeStarted broadcast")

                    # Extract observations (poll returns 4 or 5-tuple)
                    if isinstance(rawobs, tuple):
                        obs_nested = rawobs[0]
                    else:
                        obs_nested = rawobs

                    result = self._process_obs(obs_nested)
                    print(f"RESET: New episode observations received, Agents={len(result[0])}, Duration={time.time()-reset_start:.2f}s")
                    print("=" * 80)
                    return result

                except Exception as e:
                    print(f"RESET ERROR during poll: {e}")
                    import traceback
                    traceback.print_exc()

                    # Fallback to zero observations (better than hanging)
                    fallback_obs = {
                        flat_id: self._build_observation(np.zeros(46, dtype=np.float32))
                        for flat_id in self._agent_ids
                    }
                    return fallback_obs, {flat_id: {} for flat_id in self._agent_ids}

        def step(self, actiondict):
            """Execute one step - RLlib standard async pattern"""
            try:
                allactionkeys = self._get_all_action_keys()
                formattedactions = {}
                
                # Format actions (기존 코드 유지)
                for flatid, action in actiondict.items():
                    if flatid not in self.agent_map:
                        continue
                    envidx, agentidx = self.agent_map[flatid]
                    if envidx not in formattedactions:
                        formattedactions[envidx] = {}
                    
                    if isinstance(action, np.ndarray) and action.shape[0] == 5:
                        actionarray = np.clip(action, 0.0, 1.0).astype(np.float32)
                    else:
                        actionarray = np.array([0.5, 0.5, 0.5, 0.5, 0.0], dtype=np.float32)
                    
                    agentkeys = allactionkeys.get((envidx, agentidx), [])
                    if agentkeys:
                        formattedactions[envidx][agentidx] = {key: actionarray for key in agentkeys}
                
                # Send actions and poll
                self.schola_env.send_actions(formattedactions)

                # 🔥 DEBUG: Monitor poll() duration to detect blocking
                poll_start_time = time.time()

                # 🔥 DIAGNOSTIC: Log before potentially blocking poll()
                any_env_at_milestone = any(self._env_episode_steps[i] % 100 == 0 for i in range(self.num_envs))
                if any_env_at_milestone:
                    current_max_step = max(self._env_episode_steps.values())
                    print(f"[STEP {current_max_step}] About to call poll()...")

                    # 🔥 FIX: Warn if approaching known deadlock boundaries (multiples of 1000)
                    if current_max_step % 1000 == 0:
                        print(f"⚠️  WARNING: Step {current_max_step} is a training batch boundary!")
                        print(f"   If poll() blocks here, RLlib training update may be interfering with gRPC")

                step_result = self.schola_env.poll()
                poll_duration = time.time() - poll_start_time

                if poll_duration > 2.0:
                    print(f"⚠️  WARNING: step poll() took {poll_duration:.1f}s (expected <0.5s)")
                    print(f"    Env steps: {list(self._env_episode_steps.values())}")
                    print(f"    Done flags: {list(self._env_done_flags.values())}")
                
                # Parse result
                if len(step_result) == 5:
                    obs_nested, rew_nested, term_nested, trunc_nested, info_nested = step_result
                elif len(step_result) == 4:
                    obs_nested, rew_nested, term_nested, info_nested = step_result
                    trunc_nested = term_nested
                else:
                    raise ValueError(f"Unexpected poll result: {len(step_result)} items")

                # v8.5: Initialize logical env mapping from UE5 info dict (once)
                self._update_logical_env_map(info_nested)

                # Debug: Check if any environment has done signals from UE5
                has_done_signals = any(
                    any(term_nested.get(env_idx, {}).values()) or
                    (isinstance(trunc_nested, dict) and any(trunc_nested.get(env_idx, {}).values()))
                    for env_idx in range(self.num_envs)
                )
                if has_done_signals:
                    print(f"[DEBUG UE5 DONE SIGNALS] Detected done signals from UE5:")
                    for env_idx in range(self.num_envs):
                        term_vals = list(term_nested.get(env_idx, {}).values())
                        trunc_vals = list(trunc_nested.get(env_idx, {}).values()) if isinstance(trunc_nested, dict) else []
                        if any(term_vals) or any(trunc_vals):
                            print(f"  Env {env_idx}: term={term_vals[:2]}, trunc={trunc_vals[:2]}")

                # ✅ NEW: Detect new episode starts (AutoReset sends new observations after done)
                for env_idx in range(self.num_envs):
                    if self._env_done_flags.get(env_idx, False):
                        # This environment finished an episode - check if new observations arrived
                        env_agents = self._get_agents_for_logical_env(env_idx)
                        has_new_obs = any(
                            obs_nested.get(self.agent_map[aid][0], {}).get(self.agent_map[aid][1], None) is not None
                            for aid in env_agents
                        )

                        if has_new_obs:
                            # New episode started - reset tracking for this environment
                            print(f"[ENV {env_idx} RESTART] Episode {self._env_episodes_completed[env_idx]} starting "
                                  f"(Training elapsed: {time.time() - self._training_start_time:.1f}s)")
                            self._env_done_flags[env_idx] = False
                            self._env_episode_steps[env_idx] = 0
                            self._env_episode_start_time[env_idx] = time.time()

                obs_dict = {}
                reward_dict = {}
                terminated_dict = {}
                truncated_dict = {}
                info_dict = {}
                
                # 🔥 핵심 변경: 각 에이전트의 done 상태를 UE5에서 받은 값 그대로 반환
                for flat_id in self._agent_ids:
                    env_idx, agent_idx = self.agent_map[flat_id]
                    
                    # Observation
                    agent_obs_data = obs_nested.get(env_idx, {}).get(agent_idx, None)
                    if agent_obs_data is not None:
                        if isinstance(agent_obs_data, dict):
                            obs_val = list(agent_obs_data.values())[0]
                        else:
                            obs_val = agent_obs_data
                        base_obs = np.array(obs_val, dtype=np.float32).flatten()
                    else:
                        base_obs = np.zeros(46, dtype=np.float32)
                    obs_dict[flat_id] = self._build_observation(base_obs)
                    
                    # Reward
                    reward_dict[flat_id] = float(rew_nested.get(env_idx, {}).get(agent_idx, 0.0))
                    
                    # 🔥 Termination - UE5 값을 그대로 전달 (동기화 로직 제거)
                    is_term = term_nested.get(env_idx, {}).get(agent_idx, False)
                    is_trunc = trunc_nested.get(env_idx, {}).get(agent_idx, False) if isinstance(trunc_nested, dict) else False
                    
                    terminated_dict[flat_id] = is_term
                    truncated_dict[flat_id] = is_trunc
                    info_dict[flat_id] = {"env_id": env_idx}

                # ✅ FIX 2: Per-environment episode completion (ONCE per environment)
                # v8.5: Uses logical env mapping from UE5 info dict
                for env_idx in range(self.num_envs):
                    # Get agents for this logical environment (from UE5 team mapping)
                    env_agents = self._get_agents_for_logical_env(env_idx)

                    # Skip if no agents in this environment
                    if not env_agents:
                        continue

                    # Debug: Check done status for each agent
                    done_states = [
                        (aid, terminated_dict.get(aid, False), truncated_dict.get(aid, False))
                        for aid in env_agents
                    ]
                    all_done = all(
                        terminated_dict.get(aid, False) or truncated_dict.get(aid, False)
                        for aid in env_agents
                    )

                    # Debug: Print done detection when any agent is done
                    any_agent_done = any(term or trunc for _, term, trunc in done_states)
                    if any_agent_done:
                        print(f"[DEBUG ENV {env_idx}] all_done={all_done}, done_flag={self._env_done_flags.get(env_idx, False)}, agents={len(env_agents)}")
                        for aid, term, trunc in done_states[:2]:  # Print first 2 agents only
                            print(f"  {aid}: term={term}, trunc={trunc}")

                    if all_done and not self._env_done_flags.get(env_idx, False):
                        episode_duration = time.time() - self._env_episode_start_time[env_idx]
                        training_elapsed = time.time() - self._training_start_time
                        print("="*80)
                        print(f"🏁 [ENV {env_idx} DONE] Episode {self._env_episodes_completed[env_idx]} FINISHED")
                        print(f"  Episode Duration: {episode_duration:.1f}s, Steps: {self._env_episode_steps[env_idx]}")
                        print(f"  Training Elapsed: {training_elapsed:.1f}s")
                        print(f"  🔥 CRITICAL: UE5 should now trigger episode restart via AutoReset")
                        print("="*80)
                        self._env_episodes_completed[env_idx] += 1
                        self._env_done_flags[env_idx] = True

                # ✅ FIX 3: Step counter (increment for active environments)
                for env_idx in range(self.num_envs):
                    if not self._env_done_flags.get(env_idx, False):
                        self._env_episode_steps[env_idx] += 1

                # ✅ FIX 4: Periodic logging (consolidated, print once every 100 steps)
                # Check if ANY environment hit the 100-step milestone (only print once)
                should_log = any(
                    self._env_episode_steps[i] % 100 == 0 and self._env_episode_steps[i] > 0
                    for i in range(self.num_envs)
                )

                if should_log:
                    # Only print once per step milestone, not once per environment
                    first_milestone_env = next(
                        i for i in range(self.num_envs)
                        if self._env_episode_steps[i] % 100 == 0 and self._env_episode_steps[i] > 0
                    )

                    print("="*80)
                    print(f"[PROGRESS] Step Milestone={self._env_episode_steps[first_milestone_env]}")

                    # Summary of all environments (using global training time)
                    training_elapsed = time.time() - self._training_start_time
                    for summary_env_idx in range(self.num_envs):
                        episode_time = time.time() - self._env_episode_start_time[summary_env_idx] if self._env_episode_start_time[summary_env_idx] else 0

                        # Calculate rewards for THIS logical environment
                        env_agents = self._get_agents_for_logical_env(summary_env_idx)
                        env_reward = sum(reward_dict.get(aid, 0.0) for aid in env_agents)

                        status = "⚡" if episode_time < 60 else "🔥"
                        done_flag = "✅ DONE" if self._env_done_flags.get(summary_env_idx, False) else "▶️ ACTIVE"
                        print(f"  {status} Env {summary_env_idx}: {done_flag}, Episode {self._env_episodes_completed[summary_env_idx]}, "
                            f"Steps={self._env_episode_steps[summary_env_idx]}, EpisodeTime={episode_time:.1f}s, "
                            f"Reward={env_reward:.2f}")
                    print(f"  📊 Total Training Elapsed: {training_elapsed:.1f}s")
                    print("="*80)

                # ✅ FIX 5: Add required '__all__' key for RLlib multi-agent environments
                # Check if ALL agents across ALL environments are done
                all_agents_done = all(
                    terminated_dict.get(aid, False) or truncated_dict.get(aid, False)
                    for aid in self._agent_ids
                )
                terminated_dict['__all__'] = all_agents_done
                truncated_dict['__all__'] = all_agents_done

                return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict
                
            except Exception as e:
                print(f"[STEP ERROR] {e}")
                import traceback
                traceback.print_exc()
                # Return terminal state
                fallback_obs = {flat_id: self._build_observation(np.zeros(46, dtype=np.float32))
                            for flat_id in self._agent_ids}
                terminated_fallback = {flat_id: True for flat_id in self._agent_ids}
                terminated_fallback['__all__'] = True
                truncated_fallback = {flat_id: False for flat_id in self._agent_ids}
                truncated_fallback['__all__'] = False
                return (fallback_obs,
                        {flat_id: 0.0 for flat_id in self._agent_ids},
                        terminated_fallback,
                        truncated_fallback,
                        {flat_id: {} for flat_id in self._agent_ids})


        def _process_obs(self, raw_data):
            """Process observation from reset."""
            obs_nested = raw_data
            if isinstance(raw_data, tuple):
                obs_nested = raw_data[0]

            obs_dict = {}
            info_dict = {}

            for flat_id in self._agent_ids:
                env_idx, agent_idx = self.agent_map[flat_id]
                agent_obs_data = obs_nested.get(env_idx, {}).get(agent_idx, None)

                if agent_obs_data is not None:
                    if isinstance(agent_obs_data, dict):
                        obs_val = list(agent_obs_data.values())[0]
                    else:
                        obs_val = agent_obs_data
                    base_obs = np.array(obs_val, dtype=np.float32).flatten()
                else:
                    base_obs = np.zeros(46, dtype=np.float32)

                obs_dict[flat_id] = self._build_observation(base_obs)
                info_dict[flat_id] = {}

            return obs_dict, info_dict

        def render(self):
            return None

        def close(self):
            if hasattr(self.schola_env, 'close'):
                self.schola_env.close()
