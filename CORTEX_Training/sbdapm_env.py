"""
SBDAPM Environment for Schola/RLlib Training (v7.5 UE5-Controlled Rate)

Solution: Provides actions for ALL keys in the DictSpace because Schola's
fill_proto() iterates through ALL sub-spaces and expects action data for each key.
Each agent's action is duplicated across all its component keys.

v7.5 Changes:
- REMOVED Python-side rate limiting (was conflicting with UE5's throttle)
- UE5's ScholaAgentComponent::Think() is now the ONLY rate limiter
- Python's poll() should block until UE5 sends new observations
- Added step-rate diagnostics to verify rate limiting is working
- If step rate is too high, the issue is in UE5's Think() throttle, not Python

Architecture:
1. UE5's Think() has time-based throttle (DecisionInterval = 1.0s default)
2. Think() only calls Super::Think() once per interval
3. Super::Think() sends observations to Python via gRPC
4. poll() in Python BLOCKS until UE5 sends data
5. This naturally limits step rate to match UE5's decision rate

Debug: If step rate is still too high (>> 1 Hz), check:
- UE5 Output Log for "[THINK v7.4]" messages
- Verify ScholaAgentComponent.DecisionInterval is set correctly (default 1.0s)
- Verify bEnableTimeBasedDecisions is True
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
            # v7.4: Step rate diagnostics
            if hasattr(self, '_episode_start_time') and self.episode_steps > 0:
                episode_duration = time.time() - self._episode_start_time
                step_rate = self.episode_steps / max(episode_duration, 0.001)
                print(f"[EPISODE END] Steps={self.episode_steps}, Duration={episode_duration:.1f}s, Rate={step_rate:.1f} steps/sec")

            self.episode_steps = 0
            self._episode_start_time = time.time()

            # v7.3: Clear rate limiting state on reset
            self.last_action_time.clear()
            self.cached_actions.clear()

            # v7.4: Clear first-step logging flag
            if hasattr(self, '_first_step_logged'):
                delattr(self, '_first_step_logged')

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
            # v7.5: Let UE5 control the step rate via poll() blocking
            #
            # Architecture:
            # 1. UE5's Think() is throttled to DecisionInterval (default 1 Hz)
            # 2. Think() only calls Super::Think() once per interval
            # 3. Super::Think() sends observations to Python via gRPC
            # 4. poll() BLOCKS until UE5 sends new observations
            # 5. This naturally limits step rate to UE5's decision rate
            #
            # DO NOT add Python-side rate limiting here - it conflicts with UE5's throttle
            # and causes gRPC synchronization issues
            try:
                current_time = time.time()

                # Track episode start time for 30s timeout
                if not hasattr(self, '_episode_start_time'):
                    self._episode_start_time = current_time

                # v7.5: Track step timing for diagnostics
                if not hasattr(self, '_last_step_time'):
                    self._last_step_time = current_time
                    self._step_count_since_log = 0

                step_delta = current_time - self._last_step_time
                self._last_step_time = current_time
                self._step_count_since_log += 1

                # Log step rate every 10 steps (should be ~10 seconds at 1 Hz)
                if self._step_count_since_log >= 10:
                    avg_rate = self._step_count_since_log / max(step_delta * self._step_count_since_log, 0.001)
                    print(f"[STEP RATE] {self._step_count_since_log} steps, avg rate = {avg_rate:.2f} Hz (expected ~{1.0/self.decision_interval:.1f} Hz)")
                    self._step_count_since_log = 0

                # CRITICAL: Get ALL action keys because fill_proto iterates through all
                all_action_keys = self._get_all_action_keys()

                # Debug: Show key info (only on first step of first episode)
                if self.episode_steps == 0 and not hasattr(self, '_first_step_logged'):
                    print(f"[DEBUG] All action keys at first step: {all_action_keys}")
                    self._first_step_logged = True

                formatted_actions = {}

                for flat_id, action in action_dict.items():
                    if flat_id not in self.agent_map:
                        continue

                    env_idx, agent_idx = self.agent_map[flat_id]
                    if env_idx not in formatted_actions:
                        formatted_actions[env_idx] = {}

                    # v7.5: No Python-side rate limiting - UE5's Think() controls the rate
                    # Just format and send the action directly

                    # Clamp target index to valid range
                    action = np.array(action, dtype=np.int32)
                    max_target = 5  # Supports up to 4 visible enemies
                    if action[1] > max_target:
                        action[1] = 0  # Clear target if invalid

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

                # Debug: Show structure on first step only
                if self.episode_steps == 0:
                    print(f"[DEBUG] First step - sending actions with structure:")
                    for env_idx, agents in formatted_actions.items():
                        for agent_idx, action_data in agents.items():
                            print(f"  [{env_idx}][{agent_idx}] = {list(action_data.keys())} ({len(action_data)} keys)")

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

                # Episode Termination Logic (v4.0)
                self.episode_steps += 1
                episode_duration = current_time - self._episode_start_time

                # DIAGNOSTIC: Log terminated flags from UE5
                if self.episode_steps <= 3:  # Only log first 3 steps to avoid spam
                    print(f"[DIAGNOSTIC] Step {self.episode_steps}: terminated_dict = {terminated_dict}")

                # Condition 1: Team Elimination (all agents on one team dead)
                # Check if all agents are terminated (real terminal state from UE5)
                all_agents_dead = all(terminated_dict.values())

                # Condition 2: 30-second timeout
                timeout_reached = episode_duration >= 30.0

                # Condition 3: Max step safeguard (fallback)
                max_steps_reached = self.episode_steps >= self.max_episode_steps

                # Set episode-level flags
                if all_agents_dead:
                    # Real termination: team eliminated
                    terminated_dict["__all__"] = True
                    truncated_dict["__all__"] = False
                    print(f"[EPISODE END] Team eliminated at step {self.episode_steps}")
                elif timeout_reached or max_steps_reached:
                    # Truncation: time limit reached
                    for aid in self._agent_ids:
                        truncated_dict[aid] = True
                    truncated_dict["__all__"] = True
                    terminated_dict["__all__"] = False  # ✅ FIX: Don't set terminated on timeout
                    reason = "30s timeout" if timeout_reached else f"max steps ({self.max_episode_steps})"
                    print(f"[EPISODE END] {reason} at step {self.episode_steps}, duration {episode_duration:.1f}s")
                else:
                    # Episode continues
                    terminated_dict["__all__"] = False
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