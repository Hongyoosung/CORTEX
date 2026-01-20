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
            print(f"[ENV v8.5] Connecting to {host}:{port}... (Multi-environment support: ENABLED)")
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
            self._episodes_completed = 0

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
                        flat_id = f"agent_{agent_idx}"
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
            """Reset environment."""
            reset_start = time.time()

            # Log previous episode completion FIRST (before printing new episode number)
            if self._episode_start_time and self.episode_steps > 0:
                duration = time.time() - self._episode_start_time
                print(f"\n{'='*80}")
                print(f"[EPISODE {self._episodes_completed} END] Steps={self.episode_steps}, Duration={duration:.1f}s")
                print(f"{'='*80}")
                self._episodes_completed += 1

            # Now print the NEW episode number (matches UE5's 1-indexed convention)
            print(f"\n{'='*80}")
            print(f"[RESET START] Episode={self._episodes_completed + 1}, Total Completed={self._episodes_completed}, Time={reset_start:.2f}")

            self.episode_steps = 0
            is_first = not self._first_reset_done

            try:
                if is_first:
                    print("[RESET] First reset - synchronizing with UE5...")

                print(f"[RESET] Calling schola_env.hard_reset()... Time={time.time():.2f}")
                raw_obs = self.schola_env.hard_reset()
                print(f"[RESET] hard_reset() returned successfully. Time={time.time():.2f}, Duration={time.time()-reset_start:.2f}s")

                self._first_reset_done = True
                self._episode_start_time = time.time()

                # Only update agent map on first reset (agents don't change during training)
                if is_first:
                    self._update_agent_map()
                    print(f"[RESET] {len(self._agent_ids)} agents detected")

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

                if send_duration > 0.1 or poll_duration > 0.1:
                    print(f"[STEP SLOW] send={send_duration*1000:.1f}ms, poll={poll_duration*1000:.1f}ms")

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

                # Check UE5 termination signal
                ue5_episode_done = False

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

                    # Check termination from UE5
                    is_term = term_nested.get(env_idx, {}).get(agent_idx, False)
                    is_trunc = trunc_nested.get(env_idx, {}).get(agent_idx, False) if isinstance(trunc_nested, dict) else False

                    if is_term or is_trunc:
                        if not ue5_episode_done:  # Only log once per episode
                            elapsed = time.time() - self._episode_start_time
                            print(f"\n[UE5 TERM SIGNAL] Episode={self._episodes_completed + 1}, Step={self.episode_steps}, Time={elapsed:.1f}s, Agent={flat_id}, term={is_term}, trunc={is_trunc}")
                        ue5_episode_done = True

                    # Always False for individual agents (only __all__ matters for RLlib)
                    terminated_dict[flat_id] = False
                    truncated_dict[flat_id] = False
                    info_dict[flat_id] = {}

                self.episode_steps += 1

                # Episode end: ONLY when UE5 signals
                if ue5_episode_done:
                    for flat_id in self._agent_ids:
                        truncated_dict[flat_id] = True
                    truncated_dict["__all__"] = True
                    terminated_dict["__all__"] = False

                    # Calculate total episode reward
                    total_reward = sum(reward_dict.values())
                    avg_reward = total_reward / len(self._agent_ids) if self._agent_ids else 0

                    print(f"\n[EPISODE {self._episodes_completed + 1} DONE] Step={self.episode_steps}, Total reward={total_reward:.2f}, Avg={avg_reward:.2f}")
                    print(f"[EPISODE DONE] Next call should be reset(). Waiting for RLlib...")
                else:
                    truncated_dict["__all__"] = False
                    terminated_dict["__all__"] = False

                # Periodic progress logging (every 100 steps)
                if self.episode_steps % 100 == 0:
                    elapsed = time.time() - self._episode_start_time
                    # Accumulate recent rewards for progress tracking
                    total_reward = sum(reward_dict.values())
                    avg_reward = total_reward / len(self._agent_ids) if self._agent_ids else 0
                    print(f"[STEP {self.episode_steps}] Episode={self._episodes_completed + 1}, Time={elapsed:.1f}s, StepReward={avg_reward:.2f}")

                    # Warning if episode is running too long (MaxEpisodeDuration should be 60s)
                    if elapsed > 90.0:
                        print(f"[WARNING] Episode {self._episodes_completed + 1} has been running for {elapsed:.1f}s (expected max: 60s)")
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
