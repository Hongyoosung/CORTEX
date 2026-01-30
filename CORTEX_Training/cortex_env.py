"""
CORTEX Synchronous Multi-Agent Environment (v10.0)

This is a complete refactor from v9.0.2 async architecture to a fully synchronous design.

WHY SYNCHRONOUS?
================
1. ✅ Guaranteed 1:1 action-observation matching
2. ✅ Natural UE5 idle during policy updates (RLlib doesn't call step())
3. ✅ Clean episode boundaries (no queue-based completion tracking)
4. ✅ Simplified codebase (no threads, queues, or locks)
5. ✅ Better PPO alignment (PPO is inherently synchronous)

ARCHITECTURE:
=============
Main Thread Only:
    step() → send_actions() → poll() (BLOCKS) → return obs

During Policy Updates:
    - RLlib does NOT call step()
    - UE5 naturally waits for next action
    - No desync possible

Key Differences from v9.0.2:
- ❌ No gRPC worker thread
- ❌ No action_queue
- ❌ No buffer_lock
- ❌ No episode_completion_queue
- ✅ Simple blocking send/poll pattern
- ✅ Automatic pause during policy updates (RLlib behavior)

Usage:
    In train_rllib.py:
        env_config = {
            "host": "localhost",
            "port": 50051,
            "num_envs": 4
        }

        config = (
            PPOConfig()
            .environment(
                env=CORTEXSyncMultiAgentEnv,
                env_config=env_config
            )
            .env_runners(
                num_env_runners=4,  # Parallel workers for throughput
                num_envs_per_env_runner=1
            )
        )
"""

from gymnasium import spaces
import numpy as np
import time
from typing import Dict, Tuple, Optional
from collections import deque

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

try:
    from training_env.config import RLConfig
except ImportError:
    class RLConfig:
        OBSERVATION_SIZE = 50
        NUM_TOTAL_OUTPUTS = 5


if SCHOLA_AVAILABLE:

    class CORTEXSyncMultiAgentEnv(MultiAgentEnv):
        """
        Fully Synchronous Multi-Agent Environment.

        Design Principles:
            1. step() blocks until UE5 responds (no queues)
            2. Policy updates naturally pause UE5 (RLlib doesn't call step())
            3. No threads, locks, or async complexity
            4. Clean episode boundaries with direct terminal state detection
        """

        def __init__(self, **kwargs):
            super().__init__()

            host = kwargs.get("host", "localhost")
            port = self._resolve_port(kwargs)
            self.num_envs = kwargs.get("num_envs", 4)
            self._agent_strategies = {}  # flat_id → strategy_idx (0=Assault, 1=Defend, 2=Support, 3=Retreat)

            print(f"[CORTEX v10.0] Connecting to {host}:{port}...")
            print(f"[CORTEX v10.0] Multi-environment: {self.num_envs} parallel UE5 envs")
            print(f"[CORTEX v10.0] Architecture: SYNCHRONOUS (blocking send/poll)")
            print(f"[CORTEX v10.0] Policy Update Behavior: Natural idle (no step() calls)")

            # Connect to UE5
            try:
                connection = UnrealEditorConnection(url=host, port=port)
                self.schola_env = ScholaEnv(
                    unreal_connection=connection,
                    verbosity=1,
                    auto_reset_type=AutoResetType.SAME_STEP
                )
                print(f"[CORTEX v10.0] ✅ Connected!")

                # Verify environment structure
                if len(self.schola_env.ids) == self.num_envs:
                    total_agents = sum(len(a) for a in self.schola_env.ids)
                    print(f"[CORTEX v10.0] ✅ Verified {self.num_envs} environments with {total_agents} total agents")
                else:
                    print(f"[CORTEX v10.0] ⚠️  WARNING: Expected {self.num_envs} environments but got {len(self.schola_env.ids)}")

            except Exception as e:
                print(f"[ERROR] Connection failed: {e}")
                raise

            # Agent mapping (flat agent IDs for RLlib)
            self.agent_map = {}  # flat_id -> (env_idx, schola_agent_idx)
            self.reverse_map = {}  # (env_idx, schola_agent_idx) -> flat_id
            self._agent_ids = set()

            if hasattr(self.schola_env, 'ids'):
                self._update_agent_map()

            # Observation/Action Spaces
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

            # Episode Tracking
            self._first_reset_done = False
            self._training_start_time = None
            self._env_episode_steps = {i: 0 for i in range(self.num_envs)}
            self._env_episode_start_time = {i: None for i in range(self.num_envs)}
            self._env_episodes_completed = {i: 0 for i in range(self.num_envs)}
            self._env_done_flags = {i: False for i in range(self.num_envs)}
            self._agent_episode_rewards = {}  # Cumulative rewards per agent per episode

            # Episode timeout (backup mechanism)
            self._max_episode_steps = 6000  # 60s at 100Hz
            self._force_timeout_enabled = True

            # Performance metrics
            self.step_durations = deque(maxlen=100)
            self.poll_durations = deque(maxlen=100)

        def _resolve_port(self, kwargs):
            """Resolve port for multi-worker RLlib setup."""
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
            """Build flat agent ID mapping for RLlib."""
            self.agent_map.clear()
            self.reverse_map.clear()
            self._agent_ids.clear()

            if hasattr(self.schola_env, 'ids'):
                for physical_env_idx, agent_list in enumerate(self.schola_env.ids):
                    for schola_agent_idx in agent_list:
                        flat_id = f"agent_{physical_env_idx}_{schola_agent_idx}"
                        self.agent_map[flat_id] = (physical_env_idx, schola_agent_idx)
                        self.reverse_map[(physical_env_idx, schola_agent_idx)] = flat_id
                        self._agent_ids.add(flat_id)

            print(f"[CORTEX v10.0] Agent map: {len(self._agent_ids)} agents")

        def _get_agents_for_env(self, physical_env_idx):
            """Get all agent IDs belonging to a physical Schola environment."""
            return [aid for aid in self._agent_ids if aid.startswith(f"agent_{physical_env_idx}_")]

        def _get_all_action_keys(self):
            """Extract action keys from Schola action definitions."""
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

        def _build_observation(self, base_obs, strategy_idx=0):
            """Build 50-dim observation: pad/truncate to 46 + add strategy one-hot.
            
            Args:
                base_obs: 46-dim observation vector
                strategy_idx: Strategy index (0=Assault, 1=Defend, 2=Support, 3=Retreat)
            """
            # Pad/truncate to 46 dimensions
            if len(base_obs) < 46:
                base_obs = np.pad(base_obs[:46], (0, 46 - len(base_obs)), mode='constant')
            else:
                base_obs = base_obs[:46]
            
            # ✅ Strategy one-hot을 파라미터에서 생성
            strategy_onehot = np.zeros(4, dtype=np.float32)
            strategy_idx = min(int(strategy_idx), 3)  # 범위 체크: [0,3]
            strategy_onehot[strategy_idx] = 1.0
            
            # ✅ Strategy 분포 추적
            if not hasattr(self, '_strategy_count'):
                self._strategy_count = [0, 0, 0, 0]
            self._strategy_count[strategy_idx] += 1
            
            # ✅ 매 10000 observation마다 로그
            if sum(self._strategy_count) % 10000 == 0:
                total = sum(self._strategy_count)
                pct = [f"{100*c//total:>3}%" for c in self._strategy_count]
                names = ["Assault", "Defend", "Support", "Retreat"]
                dist_str = " | ".join([f"{n}={p}" for n, p in zip(names, pct)])
                print(f"[STRATEGY DIST] {dist_str}")
            
            return np.concatenate([base_obs, strategy_onehot]).astype(np.float32)

        @property
        def observation_space(self):
            return self._obs_space

        @property
        def action_space(self):
            return self._action_space

        # ============================================================================
        # CORE INTERFACE: reset() and step()
        # ============================================================================

        def reset(self, *, seed=None, options=None):
            """
            Reset environment.

            Synchronous Behavior:
                - First reset: Blocking hard_reset() call to UE5
                - Subsequent resets: Auto-reset handled by UE5 (detected via terminal states)
            """
            reset_start = time.time()

            is_first = not self._first_reset_done

            if is_first:
                print("=" * 80)
                print(f"RESET: First reset (SYNCHRONOUS mode)")

                # Initialize training timer
                if self._training_start_time is None:
                    self._training_start_time = time.time()

                current_time = time.time()
                for env_idx in range(self.num_envs):
                    self._env_episode_steps[env_idx] = 0
                    self._env_episode_start_time[env_idx] = current_time
                    self._env_done_flags[env_idx] = False

                # Initialize reward tracking for all agents
                for flat_id in self._agent_ids:
                    self._agent_episode_rewards[flat_id] = 0.0

                # Hard reset (blocking call)
                rawobs = self.schola_env.hard_reset()
                self._first_reset_done = True

                self._update_agent_map()

                # Re-initialize reward tracking after agent map is built
                for flat_id in self._agent_ids:
                    self._agent_episode_rewards[flat_id] = 0.0

                result = self._process_obs(rawobs)

                print(f"RESET: Complete (Duration={time.time()-reset_start:.2f}s, Agents={len(result[0])})")
                print("=" * 80)
                return result

            else:
                # Subsequent resets: UE5 auto-resets are detected via __all__=True in step()
                # We just send dummy actions and poll for new observations
                print(f"RESET: Soft reset (auto-reset detected)")

                # Send dummy actions to trigger UE5 response
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

                # Blocking send + poll
                self.schola_env.send_actions(dummy_actions)
                step_result = self.schola_env.poll()

                obs_dict, info_dict = self._parse_step_result(step_result)

                print(f"RESET: Complete (Duration={time.time()-reset_start:.2f}s)")
                return obs_dict, info_dict

        def step(self, actiondict):
            """
            Execute one environment step.

            Synchronous Behavior:
                1. Format actions
                2. send_actions() to UE5 (non-blocking send)
                3. poll() from UE5 (BLOCKS until response)
                4. Parse observations, rewards, dones
                5. Return immediately

            During Policy Updates:
                - RLlib does NOT call step()
                - UE5 waits for next action
                - No episode desync possible
            """
            step_start = time.time()

            try:
                # 1. Format actions for Schola
                allactionkeys = self._get_all_action_keys()
                formattedactions = {}

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

                # 2. Send actions (non-blocking)
                send_start = time.time()
                self.schola_env.send_actions(formattedactions)

                # 3. Poll for observations (BLOCKING - waits for UE5 response)
                poll_start = time.time()
                step_result = self.schola_env.poll()
                poll_duration = time.time() - poll_start
                self.poll_durations.append(poll_duration)

                if poll_duration > 1.0:
                    print(f"[STEP] WARNING: poll() took {poll_duration:.1f}s")

                # 4. Parse step result
                obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict = self._parse_step_result_full(step_result)

                # 5. Update episode tracking
                for env_idx in range(self.num_envs):
                    env_agents = self._get_agents_for_env(env_idx)
                    if not env_agents:
                        continue

                    # Increment steps if not done
                    if not self._env_done_flags.get(env_idx, False):
                        self._env_episode_steps[env_idx] += 1

                    # Accumulate rewards
                    for aid in env_agents:
                        if aid in reward_dict:
                            self._agent_episode_rewards[aid] = self._agent_episode_rewards.get(aid, 0.0) + reward_dict[aid]

                    # Check if environment finished
                    all_done = all(
                        terminated_dict.get(aid, False) or truncated_dict.get(aid, False)
                        for aid in env_agents
                    )

                    if all_done and not self._env_done_flags.get(env_idx, False):
                        # Episode just completed!
                        self._log_episode_completion(env_idx, env_agents, terminated_dict, truncated_dict)
                        self._env_done_flags[env_idx] = True

                        # Reset tracking for next episode
                        self._env_episodes_completed[env_idx] += 1
                        self._env_episode_steps[env_idx] = 0
                        self._env_episode_start_time[env_idx] = time.time()
                        for aid in env_agents:
                            self._agent_episode_rewards[aid] = 0.0

                    # Detect auto-reset (done -> not done transition)
                    elif not all_done and self._env_done_flags.get(env_idx, False):
                        print(f"[STEP] Auto-reset detected for Env {env_idx} (Episode {self._env_episodes_completed[env_idx]})")
                        self._env_done_flags[env_idx] = False

                    # Backup force timeout
                    if self._force_timeout_enabled and self._env_episode_steps[env_idx] >= self._max_episode_steps:
                        if not self._env_done_flags.get(env_idx, False):
                            print(f"[STEP] ⚠️ FORCE TIMEOUT: Env {env_idx} reached {self._max_episode_steps} steps")
                            for aid in env_agents:
                                truncated_dict[aid] = True
                            self._log_episode_completion(env_idx, env_agents, terminated_dict, truncated_dict)
                            self._env_done_flags[env_idx] = True
                            self._env_episodes_completed[env_idx] += 1
                            self._env_episode_steps[env_idx] = 0
                            self._env_episode_start_time[env_idx] = time.time()
                            for aid in env_agents:
                                self._agent_episode_rewards[aid] = 0.0

                # 6. Periodic logging
                should_log = any(
                    self._env_episode_steps[i] % 100 == 0 and self._env_episode_steps[i] > 0
                    for i in range(self.num_envs)
                )
                if should_log:
                    self._log_progress()

                # 7. Record step duration
                step_duration = time.time() - step_start
                self.step_durations.append(step_duration)

                return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict

            except Exception as e:
                print(f"[STEP ERROR] {e}")
                import traceback
                traceback.print_exc()
                # Return terminal state
                return self._create_terminal_fallback()

        # ============================================================================
        # HELPER METHODS
        # ============================================================================

        def _parse_step_result(self, step_result):
            """Parse step result into observations and info (for reset)."""
            if len(step_result) == 5:
                obs_nested, _, _, _, _ = step_result
            elif len(step_result) == 4:
                obs_nested, _, _, _ = step_result
            else:
                obs_nested = step_result[0] if step_result else {}

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
                    full_obs = np.array(obs_val, dtype=np.float32).flatten()
                else:
                    full_obs = np.zeros(50, dtype=np.float32)

                # Extract strategy from observation (last 4 dimensions are one-hot)
                if len(full_obs) >= 50:
                    base_obs = full_obs[:46]
                    strategy_onehot = full_obs[46:50]
                    strategy_idx = int(np.argmax(strategy_onehot))
                else:
                    # Fallback for old observations
                    base_obs = full_obs[:46] if len(full_obs) >= 46 else np.pad(full_obs, (0, 46 - len(full_obs)))
                    strategy_idx = 0

                # Cache strategy for this agent
                self._agent_strategies[flat_id] = strategy_idx
                obs_dict[flat_id] = self._build_observation(base_obs, strategy_idx)
                info_dict[flat_id] = {"env_id": env_idx}

            return obs_dict, info_dict

        def _parse_step_result_full(self, step_result):
            """Parse full step result into obs, rewards, terminated, truncated, info."""
            if len(step_result) == 5:
                obs_nested, rew_nested, term_nested, trunc_nested, info_nested = step_result
            elif len(step_result) == 4:
                obs_nested, rew_nested, term_nested, info_nested = step_result
                trunc_nested = term_nested
            else:
                # Fallback
                return self._create_zero_step_result()

            # ✅ LOG: Debug raw reward structure
            if not hasattr(self, '_rew_log_counter'):
                self._rew_log_counter = 0
            self._rew_log_counter += 1

            if self._rew_log_counter % 50 == 0:  # Log every 50 steps
                print(f"\n🔍 [PYTHON RAW REWARDS] Step {self._rew_log_counter}")
                for env_idx in range(min(2, len(rew_nested))):  # First 2 envs
                    if env_idx in rew_nested:
                        env_rewards = rew_nested[env_idx]
                        if isinstance(env_rewards, dict):
                            sample_agents = list(env_rewards.keys())[:2]
                            for agent_idx in sample_agents:
                                raw_val = env_rewards.get(agent_idx, 0.0)
                                print(f"  Env{env_idx} Agent{agent_idx}: {raw_val:.4f}")

            obs_dict = {}
            reward_dict = {}
            terminated_dict = {}
            truncated_dict = {}
            info_dict = {}

            for flat_id in self._agent_ids:
                env_idx, agent_idx = self.agent_map[flat_id]

                # Observation
                agent_obs_data = obs_nested.get(env_idx, {}).get(agent_idx, None)
                if agent_obs_data is not None:
                    if isinstance(agent_obs_data, dict):
                        obs_val = list(agent_obs_data.values())[0]
                    else:
                        obs_val = agent_obs_data
                    full_obs = np.array(obs_val, dtype=np.float32).flatten()
                else:
                    full_obs = np.zeros(50, dtype=np.float32)

                # Extract strategy from observation (last 4 dimensions are one-hot)
                if len(full_obs) >= 50:
                    base_obs = full_obs[:46]
                    strategy_onehot = full_obs[46:50]
                    strategy_idx = int(np.argmax(strategy_onehot))
                else:
                    # Fallback for old observations
                    base_obs = full_obs[:46] if len(full_obs) >= 46 else np.pad(full_obs, (0, 46 - len(full_obs)))
                    strategy_idx = 0

                # Cache strategy for this agent
                self._agent_strategies[flat_id] = strategy_idx
                obs_dict[flat_id] = self._build_observation(base_obs, strategy_idx)

                # Reward
                raw_reward = rew_nested.get(env_idx, {}).get(agent_idx, 0.0)
                reward_dict[flat_id] = float(raw_reward)

                # ✅ LOG: Track non-zero rewards
                if abs(raw_reward) > 0.01 and self._rew_log_counter % 50 == 0:
                    print(f"    → Parsed {flat_id}: {raw_reward:.4f}")

                # Termination
                is_term = term_nested.get(env_idx, {}).get(agent_idx, False)
                is_trunc = trunc_nested.get(env_idx, {}).get(agent_idx, False) if isinstance(trunc_nested, dict) else False
                terminated_dict[flat_id] = is_term
                truncated_dict[flat_id] = is_trunc

                # Info
                info_dict[flat_id] = {"env_id": env_idx}

            # Check if all agents done
            all_agents_done = all(
                terminated_dict.get(aid, False) or truncated_dict.get(aid, False)
                for aid in self._agent_ids
            )
            terminated_dict['__all__'] = all_agents_done
            truncated_dict['__all__'] = all_agents_done

            return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict

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
                    full_obs = np.array(obs_val, dtype=np.float32).flatten()
                else:
                    full_obs = np.zeros(50, dtype=np.float32)

                # Extract strategy from observation (last 4 dimensions are one-hot)
                if len(full_obs) >= 50:
                    base_obs = full_obs[:46]
                    strategy_onehot = full_obs[46:50]
                    strategy_idx = int(np.argmax(strategy_onehot))
                else:
                    # Fallback for old observations
                    base_obs = full_obs[:46] if len(full_obs) >= 46 else np.pad(full_obs, (0, 46 - len(full_obs)))
                    strategy_idx = 0

                # Cache strategy for this agent
                self._agent_strategies[flat_id] = strategy_idx
                obs_dict[flat_id] = self._build_observation(base_obs, strategy_idx)
                info_dict[flat_id] = {"env_id": env_idx}

            return obs_dict, info_dict

        def _log_episode_completion(self, env_idx, env_agents, terminated_dict, truncated_dict):
            """Log episode completion with detailed stats."""
            episode_duration = time.time() - self._env_episode_start_time.get(env_idx, time.time())
            env_total_reward = sum(self._agent_episode_rewards.get(aid, 0.0) for aid in env_agents)

            was_truncated = any(truncated_dict.get(aid, False) for aid in env_agents)
            end_type = "TRUNCATED (timeout)" if was_truncated else "TERMINATED (team annihilation)"

            print("=" * 80)
            print(f"🏁 [ENV {env_idx} EPISODE COMPLETE] Episode {self._env_episodes_completed[env_idx]} - {end_type}")
            print(f"  Duration: {episode_duration:.1f}s, Steps: {self._env_episode_steps[env_idx]}")
            print(f"  Total Reward: {env_total_reward:.2f}")
            print(f"  Agent Rewards (final):")
            for aid in list(env_agents)[:4]:
                agent_reward = self._agent_episode_rewards.get(aid, 0.0)
                print(f"    {aid}: {agent_reward:.2f}")
            if len(env_agents) > 4:
                print(f"    ... and {len(env_agents) - 4} more agents")
            print("=" * 80)

        def _log_progress(self):
            """Log periodic progress for all environments."""
            first_milestone_env = next(
                i for i in range(self.num_envs)
                if self._env_episode_steps[i] % 100 == 0 and self._env_episode_steps[i] > 0
            )

            print("=" * 80)
            print(f"[PROGRESS] Step Milestone={self._env_episode_steps[first_milestone_env]}")

            training_elapsed = time.time() - self._training_start_time
            for env_idx in range(self.num_envs):
                episode_time = time.time() - self._env_episode_start_time[env_idx] if self._env_episode_start_time[env_idx] else 0

                env_agents = self._get_agents_for_env(env_idx)
                env_reward = sum(self._agent_episode_rewards.get(aid, 0.0) for aid in env_agents)

                sample_agents = list(env_agents)[:2]
                sample_rewards_str = ", ".join([f"{aid}={self._agent_episode_rewards.get(aid, 0.0):.1f}" for aid in sample_agents])

                status = "⚡" if episode_time < 60 else "🔥"
                done_flag = "✅ DONE" if self._env_done_flags.get(env_idx, False) else "▶️ ACTIVE"
                print(f"  {status} Env {env_idx}: {done_flag}, Episode {self._env_episodes_completed[env_idx]}, "
                      f"Steps={self._env_episode_steps[env_idx]}, EpisodeTime={episode_time:.1f}s, "
                      f"CurrentReward={env_reward:.2f} (samples: {sample_rewards_str})")

            # Performance metrics
            avg_poll = np.mean(self.poll_durations) if self.poll_durations else 0
            avg_step = np.mean(self.step_durations) if self.step_durations else 0
            print(f"  📊 Total Training Elapsed: {training_elapsed:.1f}s")
            print(f"  ⏱️ Avg step={avg_step*1000:.1f}ms, poll={avg_poll*1000:.1f}ms")
            print("=" * 80)

        def _create_zero_step_result(self):
            """Create zero-filled step result for fallback."""
            obs_dict = {}
            for flat_id in self._agent_ids:
                strategy_idx = self._agent_strategies.get(flat_id, 0)
                obs_dict[flat_id] = self._build_observation(np.zeros(46, dtype=np.float32), strategy_idx)

            reward_dict = {flat_id: 0.0 for flat_id in self._agent_ids}
            terminated_dict = {flat_id: False for flat_id in self._agent_ids}
            terminated_dict['__all__'] = False
            truncated_dict = {flat_id: False for flat_id in self._agent_ids}
            truncated_dict['__all__'] = False
            info_dict = {flat_id: {} for flat_id in self._agent_ids}

            return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict

        def _create_terminal_fallback(self):
            """Create terminal fallback for error handling."""
            fallback_obs = {}
            for flat_id in self._agent_ids:
                strategy_idx = self._agent_strategies.get(flat_id, 0)
                fallback_obs[flat_id] = self._build_observation(np.zeros(46, dtype=np.float32), strategy_idx)

            terminated_fallback = {flat_id: True for flat_id in self._agent_ids}
            terminated_fallback['__all__'] = True
            truncated_fallback = {flat_id: False for flat_id in self._agent_ids}
            truncated_fallback['__all__'] = False
            return (
                fallback_obs,
                {flat_id: 0.0 for flat_id in self._agent_ids},
                terminated_fallback,
                truncated_fallback,
                {flat_id: {} for flat_id in self._agent_ids}
            )

        # ============================================================================
        # CLEANUP
        # ============================================================================

        def render(self):
            return None

        def close(self):
            """Clean shutdown."""
            print("[CORTEX v10.0] Closing environment...")
            if hasattr(self.schola_env, 'close'):
                self.schola_env.close()
            print("[CORTEX v10.0] Closed")
