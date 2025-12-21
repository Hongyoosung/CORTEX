"""
SBDAPM Environment for Schola/RLlib Training (v7.3 Action Rate Limiting)

Solution: Provides actions for ALL keys in the DictSpace because Schola's
fill_proto() iterates through ALL sub-spaces and expects action data for each key.
Each agent's action is duplicated across all its component keys.

v7.3 Changes:
- Added action rate limiting (1 Hz default) to prevent action flooding
- Actions are cached and re-sent until decision interval passes
- Prevents movement/aiming interruption from rapid action spam
"""

from gymnasium import spaces
import numpy as np
import sys
import time

try:
    from ray.rllib.env.multi_agent_env import MultiAgentEnv
    RLLIB_AVAILABLE = True
except ImportError:
    RLLIB_AVAILABLE = False
    print("Warning: ray[rllib] not installed")
    class MultiAgentEnv:
        pass

try:
    from schola.core.env import ScholaEnv
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
    SCHOLA_AVAILABLE = True
except ImportError:
    SCHOLA_AVAILABLE = False
    print("Warning: schola not installed. Install with: pip install schola[rllib]")


if SCHOLA_AVAILABLE:

    class SBDAPMMultiAgentEnv(MultiAgentEnv):
        """
        Multi-Agent RLlib Environment for SBDAPM (v7.2)

        Solution:
        - Provides actions for ALL keys in each agent's DictSpace
        - Schola's fill_proto() iterates through all sub-spaces and expects all keys
        - Duplicates the same action array across all component keys per agent
        """

        def __init__(self, **kwargs):
            super().__init__()

            host = kwargs.get("host", "localhost")
            port = self._resolve_port(kwargs)
            self.max_episode_steps = kwargs.get("max_episode_steps", 100000)

            # Schola 연결
            connection = UnrealEditorConnection(url=host, port=port)
            self.schola_env = ScholaEnv(unreal_connection=connection, verbosity=1)

            # ID 매핑 초기화
            self.agent_map = {}
            self.reverse_map = {}
            self._agent_ids = set()

            # 초기 ID 파싱 (reset시 갱신됨)
            if hasattr(self.schola_env, 'ids'):
                self._update_agent_map()

            # Space 정의
            self._obs_space = spaces.Box(low=-np.inf, high=np.inf, shape=(78,), dtype=np.float32)
            self._action_space = spaces.MultiDiscrete([4, 11, 3, 3])

            self.episode_steps = 0

            # v7.3: Action rate limiting (Python-side)
            # CRITICAL: UE5's DecisionInterval only throttles REQUESTS to Python
            # It does NOT throttle actions RECEIVED from Python
            # Without Python-side throttling, RLlib spams 100s of actions/second
            # This causes constant movement interruption (agents never complete moves)
            self.decision_interval = kwargs.get("decision_interval", 1.0)  # 1 Hz default
            self.last_action_time = {}  # Per-agent last action timestamps
            self.cached_actions = {}     # Per-agent cached actions (re-sent until interval passes)

            print(f"[SBDAPMMultiAgentEnv] Initialized (host={host}, port={port})")
            print(f"  Python-side rate limiting: {self.decision_interval}s ({1.0/self.decision_interval:.1f} Hz)")
            
        def _resolve_port(self, kwargs):
            base_port = kwargs.get("base_port")
            if base_port is not None:
                try:
                    import ray
                    worker = ray.get_runtime_context()
                    worker_index = getattr(worker, 'worker_index', 0)
                    port = base_port + worker_index
                    return port
                except:
                    return base_port
            return kwargs.get("port", 50051)

        def _update_agent_map(self):
            """schola_env.ids를 기반으로 에이전트 매핑 갱신"""
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
            """
            Gets ALL action keys for each agent from action_defns.

            CRITICAL: Schola's fill_proto() iterates through ALL keys in the DictSpace
            and expects actions for each. We must provide actions for every key.

            Returns:
                dict: Mapping of (env_idx, agent_idx) -> list of component_keys
            """
            all_keys = {}
            action_defns = getattr(self.schola_env, 'action_defns', {})

            if not action_defns:
                return all_keys

            for flat_id in self._agent_ids:
                env_idx, agent_idx = self.agent_map[flat_id]
                agent_defn = action_defns.get(env_idx, {}).get(agent_idx, None)

                if agent_defn is None:
                    continue

                # Get ALL keys from the DictSpace
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

        def reset(self, *, seed=None, options=None):
            self.episode_steps = 0
            # v7.3: Clear rate limiting state on reset
            self.last_action_time.clear()
            self.cached_actions.clear()
            print("[SBDAPMMultiAgentEnv] Resetting environment via hard_reset()...")

            try:
                # 1. Hard Reset (UE5와 동기화)
                raw_obs = self.schola_env.hard_reset()

                # 2. Update agent mapping
                self._update_agent_map()

                # 3. Log all action keys
                all_keys = self._get_all_action_keys()
                print(f"[DEBUG] All action keys at reset:")
                for (env_idx, agent_idx), keys in all_keys.items():
                    print(f"  Agent ({env_idx},{agent_idx}): {len(keys)} keys")

                return self._process_obs(raw_obs)

            except Exception as e:
                print(f"[SBDAPMMultiAgentEnv] Reset error: {e}")
                try:
                    raw_obs = self.schola_env.poll()
                    self._update_agent_map()
                    return self._process_obs(raw_obs)
                except:
                    import traceback
                    traceback.print_exc()
                    return {}, {}

        def step(self, action_dict):
            # v7.3: Python-side rate limiting to prevent action flooding
            # RLlib calls step() 100s of times/second during training
            # Without throttling, each agent receives new action before previous completes
            # Result: MoveToLocation() constantly interrupted, agents never move
            try:
                current_time = time.time()

                # CRITICAL: Get ALL action keys because fill_proto iterates through all
                all_action_keys = self._get_all_action_keys()

                # Debug: Show key info
                if self.episode_steps == 0:
                    print(f"[DEBUG] All action keys at first step: {all_action_keys}")

                formatted_actions = {}
                actions_throttled = 0
                actions_sent = 0

                for flat_id, action in action_dict.items():
                    if flat_id not in self.agent_map:
                        continue

                    env_idx, agent_idx = self.agent_map[flat_id]
                    if env_idx not in formatted_actions:
                        formatted_actions[env_idx] = {}

                    # v7.3: Rate limiting per agent
                    last_time = self.last_action_time.get(flat_id, 0)
                    time_since_last = current_time - last_time

                    if time_since_last >= self.decision_interval:
                        # New action allowed - update cache and timestamp
                        self.cached_actions[flat_id] = action
                        self.last_action_time[flat_id] = current_time
                        actions_sent += 1
                    else:
                        # Throttled - use cached action (keep executing previous command)
                        action = self.cached_actions.get(flat_id, action)
                        actions_throttled += 1

                    # v7.3: Clamp target index to valid range
                    # Action space [4, 11, 3, 3] allows Target 0-10, but actual enemies may be 0-4
                    # Invalid indices cause "Target_X not found" and clear focus
                    action = np.array(action, dtype=np.int32)
                    # Target index (action[1]) should be 0 (no target) or 1-N (enemy index)
                    # Max valid is based on visible enemies, but we don't know count here
                    # Clamp to reasonable max (4 enemies = indices 0-4, action values 0-5)
                    max_target = 5  # Supports up to 4 visible enemies (values 1-4 = enemies 0-3)
                    if action[1] > max_target:
                        action[1] = 0  # Clear target if invalid (safer than wrapping)

                    # Get ALL component keys for this agent
                    agent_keys = all_action_keys.get((env_idx, agent_idx), [])

                    if not agent_keys:
                        print(f"[ERROR] No action keys found for agent ({env_idx}, {agent_idx})")
                        raise RuntimeError(f"Action keys missing for agent ({env_idx}, {agent_idx}).")

                    # Provide the SAME action for ALL keys (fill_proto expects all)
                    action_array = np.array(action, dtype=np.int32)
                    formatted_actions[env_idx][agent_idx] = {
                        key: action_array for key in agent_keys
                    }

                # Debug: Show throttling stats (first step and every 100 steps)
                if self.episode_steps == 0:
                    print(f"[DEBUG] First step - sending actions with structure:")
                    for env_idx, agents in formatted_actions.items():
                        for agent_idx, action_data in agents.items():
                            print(f"  [{env_idx}][{agent_idx}] = {list(action_data.keys())} ({len(action_data)} keys)")
                elif self.episode_steps % 100 == 0:
                    print(f"[RATE LIMIT] Step {self.episode_steps}: {actions_sent} new, {actions_throttled} throttled")

                # Action 전송
                self.schola_env.send_actions(formatted_actions)

                # 데이터 수신
                step_result = self.schola_env.poll()
                
                if len(step_result) == 5:
                    obs_nested, rew_nested, term_nested, trunc_nested, info_nested = step_result
                elif len(step_result) == 4:
                    obs_nested, rew_nested, term_nested, info_nested = step_result
                    trunc_nested = term_nested 
                else:
                    raise ValueError(f"Unexpected poll() result length: {len(step_result)}")

                # RLlib 포맷 변환
                obs_dict = {}
                reward_dict = {}
                terminated_dict = {}
                truncated_dict = {}
                info_dict = {}

                for flat_id in self._agent_ids:
                    env_idx, agent_idx = self.agent_map[flat_id]
                    
                    # Obs
                    agent_obs_data = obs_nested.get(env_idx, {}).get(agent_idx, None)
                    if agent_obs_data is not None:
                        if isinstance(agent_obs_data, dict):
                            obs_val = list(agent_obs_data.values())[0]
                        else:
                            obs_val = agent_obs_data
                        obs_dict[flat_id] = np.array(obs_val, dtype=np.float32).flatten()
                    else:
                        obs_dict[flat_id] = np.zeros(78, dtype=np.float32)

                    # Reward
                    reward_dict[flat_id] = float(rew_nested.get(env_idx, {}).get(agent_idx, 0.0))

                    # Done
                    is_term = term_nested.get(env_idx, {}).get(agent_idx, False)
                    is_trunc = trunc_nested.get(env_idx, {}).get(agent_idx, False)
                    terminated_dict[flat_id] = bool(is_term)
                    truncated_dict[flat_id] = bool(is_trunc)
                    
                    # Info
                    info_dict[flat_id] = info_nested.get(env_idx, {}).get(agent_idx, {})

                # Episode Check
                self.episode_steps += 1
                if self.episode_steps >= self.max_episode_steps:
                    for aid in self._agent_ids:
                        truncated_dict[aid] = True
                    truncated_dict["__all__"] = True
                    terminated_dict["__all__"] = True 
                else:
                    terminated_dict["__all__"] = all(terminated_dict.values())
                    truncated_dict["__all__"] = False

                return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict

            except Exception as e:
                print(f"[SBDAPMMultiAgentEnv] Step error: {e}")
                import traceback
                traceback.print_exc()

                return (
                    {aid: np.zeros(78, dtype=np.float32) for aid in self._agent_ids},
                    {aid: 0.0 for aid in self._agent_ids},
                    {aid: True for aid in self._agent_ids} | {"__all__": True},
                    {aid: False for aid in self._agent_ids} | {"__all__": True},
                    {aid: {} for aid in self._agent_ids}
                )
            
        def _process_obs(self, raw_data):
            obs_nested = raw_data
            if isinstance(raw_data, tuple):
                obs_nested = raw_data[0]

            obs_dict = {}
            for flat_id in self._agent_ids:
                env_idx, agent_idx = self.agent_map[flat_id]
                agent_obs_data = obs_nested.get(env_idx, {}).get(agent_idx, None)
                
                if agent_obs_data is not None:
                    if isinstance(agent_obs_data, dict):
                        obs_val = list(agent_obs_data.values())[0]
                    else:
                        obs_val = agent_obs_data
                    obs_dict[flat_id] = np.array(obs_val, dtype=np.float32).flatten()
                else:
                    obs_dict[flat_id] = np.zeros(78, dtype=np.float32)
            
            return obs_dict, {aid: {} for aid in self._agent_ids}

        def render(self):
            return self.schola_env.render() if hasattr(self.schola_env, 'render') else None

        def close(self):
            if hasattr(self.schola_env, 'close'):
                self.schola_env.close()