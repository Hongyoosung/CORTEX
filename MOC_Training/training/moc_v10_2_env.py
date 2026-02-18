"""
MOC v10.2 Synchronous Multi-Agent Environment

- 3 strategies (Assault, Defend, Support)
- 57-dim base observation + 3-dim strategy one-hot = 60-dim total
- Action space: Box([-1, 1]^7) for EQS weights
- Commanders assign strategies; executors output EQS weights

Architecture:
    - Synchronous: step() blocks until UE5 responds
    - Multi-Agent: Multiple agents per environment
    - Command-Driven: Strategies come from Squad Commander (in UE5)
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
    print("Warning: schola not installed. Run: pip install schola")


class MOCv10_2Config:
    """Configuration for MOC v10.2 environment."""
    OBSERVATION_BASE_SIZE = 57  # Local observation (52 + 5 per-point capture statuses)
    NUM_STRATEGIES = 3  # Assault, Defend, Support
    OBSERVATION_SIZE = 60  # 57 + 3 strategy one-hot
    NUM_EQS_WEIGHTS = 7  # EQS weight outputs


if SCHOLA_AVAILABLE:

    class MOCv10_2MultiAgentEnv(MultiAgentEnv):
        """
        MOC v10.2 Synchronous Multi-Agent Environment.

        Design:
            - Receives commanded strategy from UE5 Squad Commander
            - Agents output 7-dim EQS weights in range [-1, 1]
            - Synchronous: step() blocks until UE5 responds
            - Clean episode boundaries with terminal state detection
        """

        def __init__(self, **kwargs):
            super().__init__()

            host = kwargs.get("host", "localhost")
            port = self._resolve_port(kwargs)
            self.num_envs = kwargs.get("num_envs", 4)
            self._agent_strategies = {}  # flat_id → strategy_idx (0=Assault, 1=Defend, 2=Support)

            print(f"[MOC v10.2] Connecting to {host}:{port}...")
            print(f"[MOC v10.2] Multi-environment: {self.num_envs} parallel UE5 envs")
            print(f"[MOC v10.2] Architecture: Command-Driven Executor (3 strategies)")
            print(f"[MOC v10.2] Action Space: Box([-1, 1]^7) EQS weights")

            # Connect to UE5
            try:
                connection = UnrealEditorConnection(url=host, port=port)
                self.schola_env = ScholaEnv(
                    unreal_connection=connection,
                    verbosity=1,
                    auto_reset_type=AutoResetType.SAME_STEP
                )
                print(f"[MOC v10.2] ✅ Connected!")

                # Auto-discover actual number of environments from UE5
                actual_num_envs = len(self.schola_env.ids)
                if actual_num_envs != self.num_envs:
                    print(f"[MOC v10.2] 🔄 Auto-adjusting: Expected {self.num_envs} environments, discovered {actual_num_envs} from UE5")
                    self.num_envs = actual_num_envs

                total_agents = sum(len(a) for a in self.schola_env.ids)
                print(f"[MOC v10.2] ✅ Verified {self.num_envs} environments with {total_agents} total agents")

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
            # v10.2: 60-dim input (57 local + 3 strategy)
            self._obs_space = spaces.Box(
                low=-np.inf, high=np.inf,
                shape=(MOCv10_2Config.OBSERVATION_SIZE,),
                dtype=np.float32
            )
            # v10.2: 7-dim output (EQS weights in [-1, 1])
            self._action_space = spaces.Box(
                low=-1.0, high=1.0,
                shape=(MOCv10_2Config.NUM_EQS_WEIGHTS,),
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

            # Episode timeout (backup mechanism - will sync from UE5)
            self._max_episode_steps = 300  # Default fallback, synced from UE5 on first step; 300 = ~2.5min at 2Hz
            self._max_episode_steps_synced = False
            self._force_timeout_enabled = True

            # Performance metrics
            self.step_durations = deque(maxlen=100)
            self.poll_durations = deque(maxlen=100)

            print(f"[MOC v10.2] Initialization complete!")

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

            print(f"[MOC v10.2] Agent map: {len(self._agent_ids)} agents")

        def _get_agents_for_env(self, physical_env_idx):
            """Get all agent IDs belonging to a physical Schola environment."""
            return [aid for aid in self._agent_ids if aid.startswith(f"agent_{physical_env_idx}_")]

        def _sync_max_episode_steps_from_ue5(self, info_dict):
            """
            Sync max_episode_steps from UE5 trainer info.

            UE5 MocTrainer includes 'MaxSteps' in GetInfo().
            This ensures Python uses the same timeout as UE5.
            """
            # Debug: Show first agent's info to verify data flow
            if info_dict:
                first_agent = next(iter(info_dict.keys()))
                first_info = info_dict[first_agent]
                print(f"[DEBUG] First agent info keys: {list(first_info.keys()) if isinstance(first_info, dict) else 'not a dict'}")

            for flat_id, info in info_dict.items():
                if isinstance(info, dict) and 'MaxSteps' in info:
                    try:
                        ue5_max_steps = int(info['MaxSteps'])
                        if ue5_max_steps > 0:
                            self._max_episode_steps = ue5_max_steps
                            self._max_episode_steps_synced = True
                            print(f"[MOC v10.2] ✅ Synced max_episode_steps from UE5: {self._max_episode_steps}")
                            return
                    except (ValueError, TypeError) as e:
                        print(f"[DEBUG] Error parsing MaxSteps: {e}")

            # If not synced, use default and warn
            if not self._max_episode_steps_synced:
                print(f"[MOC v10.2] ⚠️ Could not sync max_episode_steps from UE5, using default: {self._max_episode_steps}")
                print(f"[DEBUG] Checked {len(info_dict)} agents, none had 'MaxSteps' in info")
                self._max_episode_steps_synced = True

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
            """Build 60-dim observation: 57 base + 3 strategy one-hot.

            Args:
                base_obs: 57-dim observation vector (52 local + 5 capture point statuses)
                strategy_idx: Strategy index (0=Assault, 1=Defend, 2=Support)

            Returns:
                60-dim observation array
            """
            TARGET_BASE_SIZE = 57

            # Pad/truncate to 57 dimensions
            if len(base_obs) < TARGET_BASE_SIZE:
                base_obs = np.pad(base_obs[:TARGET_BASE_SIZE], (0, TARGET_BASE_SIZE - len(base_obs)), mode='constant')
            else:
                base_obs = base_obs[:TARGET_BASE_SIZE]

            # Strategy one-hot (3-dim for v10.2)
            strategy_onehot = np.zeros(3, dtype=np.float32)
            strategy_idx = min(int(strategy_idx), 2)  # Range check: [0,2]
            strategy_onehot[strategy_idx] = 1.0

            # Track strategy distribution
            if not hasattr(self, '_strategy_count'):
                self._strategy_count = [0, 0, 0]
            self._strategy_count[strategy_idx] += 1

            # Log every 10000 observations
            if sum(self._strategy_count) % 10000 == 0:
                total = sum(self._strategy_count)
                pct = [f"{100*c//total:>3}%" for c in self._strategy_count]
                names = ["Assault", "Defend", "Support"]
                dist_str = " | ".join([f"{n}={p}" for n, p in zip(names, pct)])
                print(f"[STRATEGY DIST] {dist_str}")

            # Final result: 57(Base) + 3(Strategy) = 60 floats
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
            """Reset environment."""
            reset_start = time.time()

            is_first = not self._first_reset_done

            if is_first:
                print("=" * 80)
                print(f"RESET: First reset (v10.2 SYNCHRONOUS mode)")

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
                        # v10.2: 7-dim EQS weights in [-1, 1]
                        dummy_action = np.zeros(7, dtype=np.float32)
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

            Args:
                actiondict: {agent_id: eqs_weights (7-dim array in [-1, 1])}

            Returns:
                obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict
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

                    # v10.2: Expect 7-dim EQS weights in [-1, 1]
                    if isinstance(action, np.ndarray) and action.shape[0] == 7:
                        actionarray = np.clip(action, -1.0, 1.0).astype(np.float32)
                    else:
                        actionarray = np.zeros(7, dtype=np.float32)

                    agentkeys = allactionkeys.get((envidx, agentidx), [])
                    if agentkeys:
                        formattedactions[envidx][agentidx] = {key: actionarray for key in agentkeys}

                # 2. Send actions (non-blocking)
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

                # 4.1 Sync max_episode_steps from UE5 on first step
                if not self._max_episode_steps_synced:
                    self._sync_max_episode_steps_from_ue5(info_dict)

                # 5. Update episode tracking
                # v10.2: Individual agent termination is suppressed in _parse_step_result_full().
                # Only the force timeout below ends episodes (all agents simultaneously).
                for env_idx in range(self.num_envs):
                    env_agents = self._get_agents_for_env(env_idx)
                    if not env_agents:
                        continue

                    # Clear done flag from previous episode (new episode starting)
                    if self._env_done_flags.get(env_idx, False):
                        self._env_done_flags[env_idx] = False

                    # Increment steps
                    self._env_episode_steps[env_idx] += 1

                    # Accumulate rewards
                    for aid in env_agents:
                        if aid in reward_dict:
                            self._agent_episode_rewards[aid] = self._agent_episode_rewards.get(aid, 0.0) + reward_dict[aid]

                    # Force timeout: THE primary mechanism for episode boundaries.
                    # Individual agent termination from UE5 is suppressed to prevent
                    # mixed-trajectory batches in RLlib's postprocessing.
                    if self._force_timeout_enabled and self._env_episode_steps[env_idx] >= self._max_episode_steps:
                        print(f"[STEP] Episode end: Env {env_idx} completed {self._max_episode_steps} steps")
                        for aid in env_agents:
                            truncated_dict[aid] = True
                        self._log_episode_completion(env_idx, env_agents, terminated_dict, truncated_dict)
                        self._env_done_flags[env_idx] = True
                        self._env_episodes_completed[env_idx] += 1
                        self._env_episode_steps[env_idx] = 0
                        self._env_episode_start_time[env_idx] = time.time()
                        for aid in env_agents:
                            self._agent_episode_rewards[aid] = 0.0

                # Set __all__ after processing all environments
                all_envs_done = all(self._env_done_flags.get(i, False) for i in range(self.num_envs))
                if all_envs_done:
                    terminated_dict['__all__'] = True
                    truncated_dict['__all__'] = True

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
                obs_nested, _, _, _, info_nested = step_result
            elif len(step_result) == 4:
                obs_nested, _, _, info_nested = step_result
            else:
                obs_nested = step_result[0] if step_result else {}
                info_nested = {}

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
                    full_obs = np.zeros(60, dtype=np.float32)

                # Extract strategy from observation (last 3 dimensions are one-hot for v10.2)
                obs_len = len(full_obs)
                if obs_len >= 60:
                    base_obs = full_obs[:57]
                    strategy_onehot = full_obs[57:60]
                    strategy_idx = int(np.argmax(strategy_onehot))
                else:
                    base_obs = full_obs[:57] if len(full_obs) >= 57 else np.pad(full_obs, (0, 57 - len(full_obs)))
                    strategy_idx = 0

                # Cache strategy for this agent
                self._agent_strategies[flat_id] = strategy_idx
                obs_dict[flat_id] = self._build_observation(base_obs, strategy_idx)

                # Info (extract from UE5's GetInfo())
                ue5_info = info_nested.get(env_idx, {}).get(agent_idx, {})
                if isinstance(ue5_info, dict):
                    info_dict[flat_id] = {"env_id": env_idx, **ue5_info}
                else:
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
                    full_obs = np.zeros(60, dtype=np.float32)

                # Extract strategy from observation (last 3 dimensions are one-hot)
                if len(full_obs) >= 60:
                    base_obs = full_obs[:57]
                    strategy_onehot = full_obs[57:60]
                    strategy_idx = int(np.argmax(strategy_onehot))
                else:
                    base_obs = full_obs[:57] if len(full_obs) >= 57 else np.pad(full_obs, (0, 57 - len(full_obs)))
                    strategy_idx = 0

                # Cache strategy for this agent
                self._agent_strategies[flat_id] = strategy_idx
                obs_dict[flat_id] = self._build_observation(base_obs, strategy_idx)

                # Reward (from UE5)
                raw_reward = rew_nested.get(env_idx, {}).get(agent_idx, 0.0)
                reward_dict[flat_id] = float(raw_reward)

                # Termination
                is_term = term_nested.get(env_idx, {}).get(agent_idx, False)
                is_trunc = trunc_nested.get(env_idx, {}).get(agent_idx, False) if isinstance(trunc_nested, dict) else False
                terminated_dict[flat_id] = is_term
                truncated_dict[flat_id] = is_trunc

                # Info (extract from UE5's GetInfo())
                ue5_info = info_nested.get(env_idx, {}).get(agent_idx, {})
                if isinstance(ue5_info, dict):
                    info_dict[flat_id] = {"env_id": env_idx, **ue5_info}
                else:
                    info_dict[flat_id] = {"env_id": env_idx}

            # v10.2 FIX: Suppress individual agent termination signals from UE5.
            # With AutoResetType.SAME_STEP, individual agent deaths trigger auto-reset
            # within the same step, creating new sub-episodes that mix trajectories.
            # RLlib's postprocessing then receives batches with multiple eps_id values,
            # causing "Batches must only contain steps from a single trajectory" error.
            # Solution: Only Python's force timeout (in step()) ends episodes,
            # ensuring all agents terminate simultaneously with clean boundaries.
            for flat_id in list(terminated_dict.keys()):
                if flat_id != '__all__':
                    terminated_dict[flat_id] = False
                    truncated_dict[flat_id] = False
            terminated_dict['__all__'] = False
            truncated_dict['__all__'] = False

            # v10.2 REWARD FIX: Use CumulativeLifetimeReward from info as fallback.
            # Schola calls ComputeReward() multiple times per step; only the last call's
            # return value is sent to Python. With idempotent ComputeReward(), all calls
            # now return the same cached value. But as a safety net, we also extract
            # rewards from the info channel using a monotonic cumulative counter.
            if not hasattr(self, '_prev_cumulative_rewards'):
                self._prev_cumulative_rewards = {}

            info_reward_used = False
            for flat_id in list(reward_dict.keys()):
                info = info_dict.get(flat_id, {})
                if 'CumulativeLifetimeReward' in info:
                    try:
                        curr_cumulative = float(info['CumulativeLifetimeReward'])
                        prev_cumulative = self._prev_cumulative_rewards.get(flat_id, 0.0)
                        info_step_reward = curr_cumulative - prev_cumulative
                        self._prev_cumulative_rewards[flat_id] = curr_cumulative

                        schola_reward = reward_dict[flat_id]
                        # Use info reward if Schola reward looks wrong (near-zero when info shows real reward)
                        if abs(schola_reward) < 1e-4 and abs(info_step_reward) > 1e-4:
                            reward_dict[flat_id] = info_step_reward
                            if not info_reward_used:
                                info_reward_used = True
                        elif abs(info_step_reward) > 1e-4:
                            # Both have values - prefer info channel (more reliable)
                            reward_dict[flat_id] = info_step_reward
                    except (ValueError, TypeError):
                        pass  # Keep Schola reward

            # Debug logging (first few steps)
            if not hasattr(self, '_reward_debug_count'):
                self._reward_debug_count = 0
            self._reward_debug_count += 1
            if self._reward_debug_count <= 3:
                sample_id = next(iter(reward_dict.keys()))
                sample_info = info_dict.get(sample_id, {})
                print(f"[REWARD DEBUG] Step {self._reward_debug_count}:")
                print(f"  Schola raw: {rew_nested.get(0, {})}")
                print(f"  reward_dict[{sample_id}] = {reward_dict[sample_id]:.6f}")
                print(f"  Info CumulativeLifetimeReward: {sample_info.get('CumulativeLifetimeReward', 'N/A')}")
                print(f"  Info LastStepReward: {sample_info.get('LastStepReward', 'N/A')}")
                print(f"  Info EpisodeReward: {sample_info.get('EpisodeReward', 'N/A')}")
            if info_reward_used and self._reward_debug_count == 1:
                print(f"[REWARD] Using info-channel rewards (CumulativeLifetimeReward delta) instead of Schola rewards")

            return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict

        def _process_obs(self, raw_data):
            """Process observation from reset."""
            obs_nested = raw_data
            info_nested = {}

            if isinstance(raw_data, tuple):
                if len(raw_data) >= 5:
                    obs_nested, _, _, _, info_nested = raw_data
                elif len(raw_data) >= 4:
                    obs_nested, _, _, info_nested = raw_data
                else:
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
                    full_obs = np.zeros(60, dtype=np.float32)

                # Extract strategy from observation
                if len(full_obs) >= 60:
                    base_obs = full_obs[:57]
                    strategy_onehot = full_obs[57:60]
                    strategy_idx = int(np.argmax(strategy_onehot))
                else:
                    base_obs = full_obs[:57] if len(full_obs) >= 57 else np.pad(full_obs, (0, 57 - len(full_obs)))
                    strategy_idx = 0

                # Cache strategy for this agent
                self._agent_strategies[flat_id] = strategy_idx
                obs_dict[flat_id] = self._build_observation(base_obs, strategy_idx)

                # Info (extract from UE5's GetInfo())
                ue5_info = info_nested.get(env_idx, {}).get(agent_idx, {})
                if isinstance(ue5_info, dict):
                    info_dict[flat_id] = {"env_id": env_idx, **ue5_info}
                else:
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
            print(f"  Agent Rewards (sample):")
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
                obs_dict[flat_id] = self._build_observation(np.zeros(57, dtype=np.float32), strategy_idx)

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
                fallback_obs[flat_id] = self._build_observation(np.zeros(57, dtype=np.float32), strategy_idx)

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
            print("[MOC v10.2] Closing environment...")
            if hasattr(self.schola_env, 'close'):
                self.schola_env.close()
            print("[MOC v10.2] Closed")
