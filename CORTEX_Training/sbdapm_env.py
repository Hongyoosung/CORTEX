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
    from schola.core.env import ScholaEnv
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
                    verbosity=1
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
            self.episode_steps = 0
            self._episode_start_time = None
            self._first_reset_done = False

            # v8.5 VECTORIZED TRAINING: Per-environment episode tracking
            self._env_episode_steps = {i: 0 for i in range(self.num_envs)}
            self._env_episode_start_time = {i: None for i in range(self.num_envs)}
            self._env_episodes_completed = {i: 0 for i in range(self.num_envs)}
            self._env_done_flags = {i: False for i in range(self.num_envs)}
            self._envs_waiting_for_reset = set()  # Environments that finished but waiting for others

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

            if hasattr(self.schola_env, 'ids'):
                for env_idx, agent_list in enumerate(self.schola_env.ids):
                    for agent_idx in agent_list:
                        # Include environment ID to ensure unique agent identifiers across all environments
                        # Format: "agent_<env_idx>_<agent_idx>" (e.g., "agent_0_1", "agent_3_2")
                        flat_id = f"agent_{env_idx}_{agent_idx}"
                        self.agent_map[flat_id] = (env_idx, agent_idx)
                        self.reverse_map[(env_idx, agent_idx)] = flat_id
                        self._agent_ids.add(flat_id)

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
            """Reset environment (resets ALL environments simultaneously)."""
            reset_start = time.time()

            # v8.5 VECTORIZED TRAINING: Log completion for each environment
            if self._episode_start_time and self.episode_steps > 0:
                print(f"\n{'='*80}")
                print(f"[EPISODE COMPLETION SUMMARY]")
                for env_idx in range(self.num_envs):
                    if self._env_episode_start_time[env_idx]:
                        duration = time.time() - self._env_episode_start_time[env_idx]
                        print(f"  Env {env_idx}: Episode {self._env_episodes_completed[env_idx]}, Steps={self._env_episode_steps[env_idx]}, Duration={duration:.1f}s")
                        self._env_episodes_completed[env_idx] += 1
                print(f"{'='*80}")

            # Print new episode start
            print(f"\n{'='*80}")
            for env_idx in range(self.num_envs):
                print(f"  Env {env_idx}: Starting episode {self._env_episodes_completed[env_idx]}")
            print(f"  Reset Time={reset_start:.2f}")

            # Reset global counters
            self.episode_steps = 0
            is_first = not self._first_reset_done

            # v8.5 VECTORIZED TRAINING: Reset per-environment tracking
            current_time = time.time()
            for env_idx in range(self.num_envs):
                self._env_episode_steps[env_idx] = 0
                self._env_episode_start_time[env_idx] = current_time
                self._env_done_flags[env_idx] = False

            self._envs_waiting_for_reset.clear()

            try:
                if is_first:
                    print("[RESET] First reset - synchronizing with UE5...")

                print(f"[RESET] Calling schola_env.hard_reset() (resets ALL {self.num_envs} environments)... Time={time.time():.2f}")
                raw_obs = self.schola_env.hard_reset()
                print(f"[RESET] hard_reset() returned successfully. Time={time.time():.2f}, Duration={time.time()-reset_start:.2f}s")

                self._first_reset_done = True
                self._episode_start_time = current_time

                # Only update agent map on first reset (agents don't change during training)
                if is_first:
                    self._update_agent_map()
                    print(f"[RESET] {len(self._agent_ids)} agents detected ({len(self._agent_ids) // self.num_envs} per environment)")

                result = self._process_obs(raw_obs)
                print(f"[RESET COMPLETE] Duration={time.time()-reset_start:.2f}s, Agents={len(result[0])}")
                print(f"{'='*80}\n")
                return result

            except Exception as e:
                print(f"[RESET ERROR] {e}")
                import traceback
                traceback.print_exc()
                print(f"[RESET FAILED] Duration={time.time()-reset_start:.2f}s")
                print(f"{'='*80}\n")
                return {}, {}

        def step(self, action_dict):
            """Execute one step."""
            try:
                all_action_keys = self._get_all_action_keys()

                # Format actions
                formatted_actions = {}
                for flat_id, action in action_dict.items():
                    if flat_id not in self.agent_map:
                        continue

                    env_idx, agent_idx = self.agent_map[flat_id]
                    if env_idx not in formatted_actions:
                        formatted_actions[env_idx] = {}

                    # Clip action to [0, 1]
                    if isinstance(action, np.ndarray) and action.shape[0] == 5:
                        action_array = np.clip(action, 0.0, 1.0).astype(np.float32)
                    else:
                        action_array = np.array([0.5, 0.5, 0.5, 0.5, 0.0], dtype=np.float32)

                    agent_keys = all_action_keys.get((env_idx, agent_idx), [])
                    if agent_keys:
                        formatted_actions[env_idx][agent_idx] = {
                            key: action_array for key in agent_keys
                        }

                # Send and receive (with diagnostics)
                send_start = time.time()
                self.schola_env.send_actions(formatted_actions)
                send_duration = time.time() - send_start

                poll_start = time.time()
                step_result = self.schola_env.poll()
                poll_duration = time.time() - poll_start

                #if send_duration > 0.1 or poll_duration > 0.1:
                #    print(f"[STEP SLOW] send={send_duration*1000:.1f}ms, poll={poll_duration*1000:.1f}ms")

                # Parse response
                if len(step_result) == 5:
                    obs_nested, rew_nested, term_nested, trunc_nested, info_nested = step_result
                elif len(step_result) == 4:
                    obs_nested, rew_nested, term_nested, info_nested = step_result
                    trunc_nested = term_nested
                else:
                    raise ValueError(f"Unexpected poll() result: {len(step_result)} items")

                # Build outputs
                obs_dict = {}
                reward_dict = {}
                terminated_dict = {}
                truncated_dict = {}
                info_dict = {}

                # v8.5 VECTORIZED TRAINING: Track termination PER ENVIRONMENT
                # Note: Episode timeouts are handled by UE5
                # (SimulationManagerGameMode::MaxEpisodeDuration)
                # Python receives termination signals via term_nested/trunc_nested from Schola
                newly_finished_envs = []

                # DEBUG: Track termination signals per environment
                env_termination_signals = {env_idx: {'term': False, 'trunc': False} for env_idx in range(self.num_envs)}

                for flat_id in self._agent_ids:
                    env_idx, agent_idx = self.agent_map[flat_id]

                    # Get observation
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

                    # Get reward
                    reward_dict[flat_id] = float(rew_nested.get(env_idx, {}).get(agent_idx, 0.0))

                    # Check termination from UE5 for THIS ENVIRONMENT
                    is_term = term_nested.get(env_idx, {}).get(agent_idx, False)
                    is_trunc = trunc_nested.get(env_idx, {}).get(agent_idx, False) if isinstance(trunc_nested, dict) else False

                    # Track if any agent in this environment reported termination
                    if is_term or is_trunc:
                        env_termination_signals[env_idx]['term'] = env_termination_signals[env_idx]['term'] or is_term
                        env_termination_signals[env_idx]['trunc'] = env_termination_signals[env_idx]['trunc'] or is_trunc
                    
                    # Mark environment as done if ANY agent from that environment reports termination
                    if (is_term or is_trunc) and not self._env_done_flags[env_idx]:
                        self._env_done_flags[env_idx] = True
                        newly_finished_envs.append(env_idx)
                        self._envs_waiting_for_reset.add(env_idx)

                        # Log environment completion
                        if self._env_episode_start_time[env_idx]:
                            elapsed = time.time() - self._env_episode_start_time[env_idx]
                            termination_reason = "Team Eliminated" if is_term else "Timeout" if is_trunc else "Unknown"
                            print(f"\n{'┌'+'─'*78+'┐'}")
                            print(f"│ [ENV {env_idx} DONE] Episode {self._env_episodes_completed[env_idx]} completed ( {termination_reason}){' '*(44-len(termination_reason))}│")
                            print(f"│   Agent: {flat_id} (env={env_idx}, agent={agent_idx}){' '*35}│")
                            print(f"│   Steps: {self._env_episode_steps[env_idx]:<10} Time: {elapsed:.1f}s{' '*47}│")
                            print(f"│   Status: Waiting for other environments to finish...{' '*21}│")
                            print(f"└{'─'*78}┘\n")

                    # Set done flags ONLY for agents in finished environments
                    # Agents in running environments continue with done=False
                    terminated_dict[flat_id] = False
                    truncated_dict[flat_id] = self._env_done_flags[env_idx]
                    info_dict[flat_id] = {"env_idx": env_idx}

                # DEBUG: Log termination signals received in this step
                if newly_finished_envs:
                    print(f"[TERMINATION DEBUG] Step {self.episode_steps}: Newly finished environments = {newly_finished_envs}")
                    for env_idx in newly_finished_envs:
                        signals = env_termination_signals[env_idx]
                        print(f"  Env {env_idx}: terminated={signals['term']}, truncated={signals['trunc']}")

                # Increment step counter for each environment
                for env_idx in range(self.num_envs):
                    if not self._env_done_flags[env_idx]:
                        self._env_episode_steps[env_idx] += 1

                self.episode_steps += 1

                # __all__ = True ONLY when ALL environments are done
                all_envs_done = all(self._env_done_flags.values())

                if all_envs_done:
                    # All environments finished - trigger global reset
                    truncated_dict["__all__"] = True
                    terminated_dict["__all__"] = False

                    # Calculate per-environment and total rewards
                    total_reward = sum(reward_dict.values())
                    avg_reward = total_reward / len(self._agent_ids) if self._agent_ids else 0

                    print(f"\n{'='*80}")
                    print(f"[ALL ENVS DONE] All {self.num_envs} environments finished!")
                    for env_idx in range(self.num_envs):
                        elapsed = time.time() - self._env_episode_start_time[env_idx] if self._env_episode_start_time[env_idx] else 0
                        print(f"  Env {env_idx}: Episode {self._env_episodes_completed[env_idx] + 1}, Steps={self._env_episode_steps[env_idx]}, Duration={elapsed:.1f}s")
                    print(f"  Total reward={total_reward:.2f}, Avg={avg_reward:.2f}")
                    print(f"[ALL ENVS DONE] Next call should be reset(). Waiting for RLlib...")
                    print(f"{'='*80}\n")
                else:
                    # Some environments still running
                    truncated_dict["__all__"] = False
                    terminated_dict["__all__"] = False

                    # Log status of newly finished environments
                    if newly_finished_envs:
                        running_envs = [i for i in range(self.num_envs) if not self._env_done_flags[i]]
                        print(f"[ASYNC STATUS] Finished: {list(self._envs_waiting_for_reset)}, Running: {running_envs}")

                # Periodic progress logging (every 100 steps) - v8.5: Show per-environment status
                if self.episode_steps % 100 == 0:
                    elapsed = time.time() - self._episode_start_time
                    total_reward = sum(reward_dict.values())
                    avg_reward = total_reward / len(self._agent_ids) if self._agent_ids else 0

                    # Show per-environment status
                    running_count = sum(1 for flag in self._env_done_flags.values() if not flag)
                    done_count = sum(1 for flag in self._env_done_flags.values() if flag)

                    print(f"\n{'='*80}")
                    print(f"[PROGRESS] Step={self.episode_steps}, Elapsed={elapsed:.1f}s, Total Reward={total_reward:.2f}, Avg Reward={avg_reward:.2f}")
                    print(f"{'─'*80}")

                    # Show individual environment status with better formatting
                    for env_idx in range(self.num_envs):
                        if not self._env_done_flags[env_idx]:
                            env_elapsed = time.time() - self._env_episode_start_time[env_idx] if self._env_episode_start_time[env_idx] else 0
                            status_icon = "🔄" if env_elapsed < 60 else "⚠️"
                            print(f"  {status_icon} Env {env_idx}: Episode {self._env_episodes_completed[env_idx]}, Steps={self._env_episode_steps[env_idx]}, Time={env_elapsed:.1f}s")
                        else:
                            print(f"  ✓ Env {env_idx}: DONE (Episode {self._env_episodes_completed[env_idx] + 1} completed, waiting for reset)")
                    print(f"{'='*80}\n")

                    # Warning if any running environment is taking too long
                    for env_idx in range(self.num_envs):
                        if not self._env_done_flags[env_idx] and self._env_episode_start_time[env_idx]:
                            env_elapsed = time.time() - self._env_episode_start_time[env_idx]
                            if env_elapsed > 90.0:
                                print(f"[WARNING] Env {env_idx} has been running for {env_elapsed:.1f}s (expected max: 60s)")
                                print(f"[WARNING] Check if UE5 MaxEpisodeDuration is set correctly or if termination signals are being sent")

                return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict

            except Exception as e:
                print(f"[STEP ERROR] {e}")
                import traceback
                traceback.print_exc()

                # Return terminal state
                fallback_obs = {
                    flat_id: self._build_observation(np.zeros(46, dtype=np.float32))
                    for flat_id in self._agent_ids
                }
                return (
                    fallback_obs,
                    {flat_id: 0.0 for flat_id in self._agent_ids},
                    {flat_id: True for flat_id in self._agent_ids} | {"__all__": True},
                    {flat_id: False for flat_id in self._agent_ids} | {"__all__": False},
                    {flat_id: {} for flat_id in self._agent_ids}
                )

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


# Fallback dummy environment
class SBDAPMEnv:
    """Dummy environment for testing without Schola."""

    def __init__(self, **kwargs):
        print("[SBDAPMEnv] Using dummy environment")
        self._obs_space = spaces.Box(low=-np.inf, high=np.inf, shape=(50,), dtype=np.float32)
        self._action_space = spaces.Box(low=-np.inf, high=np.inf, shape=(5,), dtype=np.float32)
        self.episode_steps = 0

    @property
    def observation_space(self):
        return self._obs_space

    @property
    def action_space(self):
        return self._action_space

    def reset(self, *, seed=None, options=None):
        self.episode_steps = 0
        obs = np.zeros(50, dtype=np.float32)
        obs[46] = 1.0  # Assault one-hot
        return obs, {}

    def step(self, action):
        self.episode_steps += 1
        obs = np.zeros(50, dtype=np.float32)
        obs[46] = 1.0
        truncated = self.episode_steps >= 100
        return obs, 0.0, False, truncated, {}

    def render(self):
        return None

    def close(self):
        pass
