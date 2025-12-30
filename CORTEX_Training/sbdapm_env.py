"""
SBDAPM Environment for Schola/RLlib Training (v7.6 Trajectory Fix)

Solution: Provides actions for ALL keys in the DictSpace because Schola's
fill_proto() iterates through ALL sub-spaces and expects action data for each key.
Each agent's action is duplicated across all its component keys.

v7.6 Changes:
- FIXED: "Batches sent to postprocessing must only contain steps from a single trajectory"
- Root cause: Individual agents dying mid-episode were marked terminated=True, but
  episode continued for other agents. RLlib saw mixed trajectories in same batch.
- Solution: Track alive/dead agents separately. Dead agents get zero obs/rewards but
  terminated=False until the ENTIRE episode ends. All agents terminate/truncate together.
- Added _alive_agents and _dead_agents sets to track agent lifecycle

v7.5 Changes:
- UE5's ScholaAgentComponent::Think() is the rate limiter (not Python)
- Python's poll() blocks until UE5 sends new observations

Architecture:
1. UE5's Think() has time-based throttle (DecisionInterval = 1.0s default)
2. Think() only calls Super::Think() once per interval
3. Super::Think() sends observations to Python via gRPC
4. poll() in Python BLOCKS until UE5 sends data
5. When an agent dies, it's moved to _dead_agents but NOT marked terminated
6. Only when episode ends (all dead OR timeout) do ALL agents terminate/truncate together
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


# The maximum duration of an episode. After this time, the environment will reset.
MAX_EPISODE_DURATION = 180.0



if SCHOLA_AVAILABLE:

    class SBDAPMMultiAgentEnv(MultiAgentEnv):
        """
        Multi-Agent RLlib Environment for SBDAPM (v7.6)

        Key features:
        - Provides actions for ALL keys in each agent's DictSpace
        - Tracks alive/dead agents to ensure proper trajectory handling
        - All agents terminate/truncate TOGETHER at episode end (RLlib requirement)
        - Dead agents receive zero obs/rewards but stay in episode until end
        """

        def __init__(self, **kwargs):
            super().__init__()

            host = kwargs.get("host", "localhost")
            port = self._resolve_port(kwargs)
            self.max_episode_steps = kwargs.get("max_episode_steps", 100000)

            print(f"[SBDAPMMultiAgentEnv] Initializing connection to {host}:{port}")
            print(f"  Attempting to connect to UE instance...")

            # Schola 연결
            try:
                connection = UnrealEditorConnection(url=host, port=port)
                print(f"  UnrealEditorConnection created, establishing ScholaEnv...")
                self.schola_env = ScholaEnv(unreal_connection=connection, verbosity=1)
                print(f"  ScholaEnv initialized successfully!")
            except Exception as e:
                print(f"  [ERROR] Failed to connect to {host}:{port}")
                print(f"  Error: {e}")
                print(f"  Make sure UE instance is running and listening on port {port}")
                raise

            # ID 매핑 초기화
            self.agent_map = {}
            self.reverse_map = {}
            self._agent_ids = set()

            # 초기 ID 파싱 (reset시 갱신됨)
            if hasattr(self.schola_env, 'ids'):
                self._update_agent_map()

            # Space 정의 (v4.0: 70 observation + 4 objective embedding = 74 features)
            self._obs_space = spaces.Box(low=-np.inf, high=np.inf, shape=(74,), dtype=np.float32)
            self._action_space = spaces.MultiDiscrete([4, 6, 3])  # v4.0: Position(4) × Target(6) × Fire(3), stance removed

            self.episode_steps = 0

            # v7.3: Action rate limiting (Python-side)
            # CRITICAL: UE5's DecisionInterval only throttles REQUESTS to Python
            # It does NOT throttle actions RECEIVED from Python
            # Without Python-side throttling, RLlib spams 100s of actions/second
            # This causes constant movement interruption (agents never complete moves)
            self.decision_interval = kwargs.get("decision_interval", 1.0)  # 1 Hz default
            self.last_action_time = {}  # Per-agent last action timestamps
            self.cached_actions = {}     # Per-agent cached actions (re-sent until interval passes)

            # v7.6: Track alive agents for proper trajectory handling
            # CRITICAL: RLlib requires all agents in an episode to terminate/truncate together
            # Individual agent deaths should NOT be reported as terminated mid-episode
            self._alive_agents = set()  # Agents still alive in current episode
            self._dead_agents = set()   # Agents that died mid-episode (masked until episode ends)

            # v7.7: Track cumulative episode rewards for logging
            self._episode_rewards = {}  # Cumulative reward per agent in current episode

            print(f"[SBDAPMMultiAgentEnv] Initialized (host={host}, port={port})")
            print(f"  Python-side rate limiting: {self.decision_interval}s ({1.0/self.decision_interval:.1f} Hz)")
            
        def _resolve_port(self, kwargs):
            base_port = kwargs.get("base_port")
            if base_port is not None:
                try:
                    from ray.rllib.evaluation.rollout_worker import get_global_worker
                    worker = get_global_worker()
                    worker_index = worker.worker_index if worker else 0
                    # RLlib remote workers start at index 1, so subtract 1 to start from base_port
                    # Worker 1 -> base_port+0, Worker 2 -> base_port+1, etc.
                    port_offset = max(0, worker_index - 1)
                    port = base_port + port_offset
                    print(f"[Port Resolution] Worker index={worker_index}, offset={port_offset}, base_port={base_port}, resolved port={port}")
                    return port
                except Exception as e:
                    print(f"[Port Resolution] Failed to get worker index: {e}, using base_port={base_port}")
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
            # v7.4 & v7.7: Step rate and reward diagnostics
            if hasattr(self, '_episode_start_time') and self.episode_steps > 0:
                episode_duration = time.time() - self._episode_start_time
                step_rate = self.episode_steps / max(episode_duration, 0.001)

                # v7.7: Calculate total episode reward
                total_reward = sum(self._episode_rewards.values())
                avg_reward = total_reward / max(len(self._episode_rewards), 1)

                print(f"[EPISODE END] Steps={self.episode_steps}, Duration={episode_duration:.1f}s, Rate={step_rate:.1f} steps/sec")
                print(f"[EPISODE REWARD] Total={total_reward:.2f}, Avg={avg_reward:.2f}, Agents={len(self._episode_rewards)}")

                # Log individual agent rewards
                if self._episode_rewards:
                    reward_list = [f"{aid}:{rew:.1f}" for aid, rew in sorted(self._episode_rewards.items())]
                    print(f"[AGENT REWARDS] {', '.join(reward_list)}")

            self.episode_steps = 0
            self._episode_start_time = time.time()
            self._episode_rewards.clear()  # Reset reward tracking

            # v7.3: Clear rate limiting state on reset
            self.last_action_time.clear()
            self.cached_actions.clear()

            # v7.4: Clear first-step logging flag
            if hasattr(self, '_first_step_logged'):
                delattr(self, '_first_step_logged')

            # Track if this is the first reset (during worker initialization)
            is_first_reset = not hasattr(self, '_first_reset_done')
            if is_first_reset:
                print("[SBDAPMMultiAgentEnv] FIRST RESET - Initializing worker environment...")
            else:
                print("[SBDAPMMultiAgentEnv] Resetting environment via hard_reset()...")

            try:
                # 1. Hard Reset (UE5와 동기화)
                if is_first_reset:
                    print("  Calling hard_reset() to sync with UE5...")
                raw_obs = self.schola_env.hard_reset()
                if is_first_reset:
                    print("  hard_reset() completed successfully!")
                    self._first_reset_done = True

                # 2. Update agent mapping
                self._update_agent_map()

                # v7.6: Reset alive/dead agent tracking AFTER agent map is updated
                self._alive_agents = set(self._agent_ids)  # All agents start alive
                self._dead_agents = set()
                if is_first_reset:
                    print(f"[WORKER READY] {len(self._alive_agents)} agents detected and ready for training")
                else:
                    print(f"[v7.6] Episode start: {len(self._alive_agents)} agents alive")

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
                    # v7.6: Also reset alive/dead on fallback path
                    self._alive_agents = set(self._agent_ids)
                    self._dead_agents = set()
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

                # v7.6: First pass - check for newly dead agents from UE5
                for flat_id in self._agent_ids:
                    env_idx, agent_idx = self.agent_map[flat_id]
                    is_term_ue5 = term_nested.get(env_idx, {}).get(agent_idx, False)

                    # If UE5 reports this agent as terminated, move to dead set
                    if is_term_ue5 and flat_id in self._alive_agents:
                        self._alive_agents.discard(flat_id)
                        self._dead_agents.add(flat_id)
                        print(f"[v7.6] Agent {flat_id} died. Alive: {len(self._alive_agents)}, Dead: {len(self._dead_agents)}")

                # v7.6 & v7.7: Second pass - build RLlib outputs and track rewards
                for flat_id in self._agent_ids:
                    env_idx, agent_idx = self.agent_map[flat_id]

                    if flat_id in self._dead_agents:
                        # Dead agents get zero obs/rewards but NOT terminated (yet)
                        # They'll be terminated when the whole episode ends
                        obs_dict[flat_id] = np.zeros(74, dtype=np.float32)
                        reward_dict[flat_id] = 0.0
                        terminated_dict[flat_id] = False  # Will be set True when episode ends
                        truncated_dict[flat_id] = False
                        info_dict[flat_id] = {"dead": True}
                    else:
                        # Live agents get normal obs/rewards
                        agent_obs_data = obs_nested.get(env_idx, {}).get(agent_idx, None)
                        if agent_obs_data is not None:
                            if isinstance(agent_obs_data, dict):
                                obs_val = list(agent_obs_data.values())[0]
                            else:
                                obs_val = agent_obs_data
                            obs_dict[flat_id] = np.array(obs_val, dtype=np.float32).flatten()
                        else:
                            obs_dict[flat_id] = np.zeros(74, dtype=np.float32)

                        reward = float(rew_nested.get(env_idx, {}).get(agent_idx, 0.0))
                        reward_dict[flat_id] = reward

                        # v7.7: Accumulate episode rewards
                        if flat_id not in self._episode_rewards:
                            self._episode_rewards[flat_id] = 0.0
                        self._episode_rewards[flat_id] += reward

                        # Mid-episode: never set terminated/truncated for live agents
                        terminated_dict[flat_id] = False
                        truncated_dict[flat_id] = False
                        info_dict[flat_id] = info_nested.get(env_idx, {}).get(agent_idx, {})

                # Episode Termination Logic (v7.6)
                self.episode_steps += 1
                episode_duration = current_time - self._episode_start_time

                # DIAGNOSTIC: Log alive/dead agents on first few steps
                if self.episode_steps <= 3:
                    print(f"[DIAGNOSTIC] Step {self.episode_steps}: Alive={len(self._alive_agents)}, Dead={len(self._dead_agents)}")

                # v7.6: Check episode end conditions using tracked alive agents
                # Condition 1: All agents dead (team eliminated)
                all_agents_dead = len(self._alive_agents) == 0

                # Condition 2: Timeout
                timeout_reached = episode_duration >= MAX_EPISODE_DURATION

                # Condition 3: Max step safeguard (fallback)
                max_steps_reached = self.episode_steps >= self.max_episode_steps

                # v7.6: When episode ends, ALL agents terminate/truncate TOGETHER
                if all_agents_dead:
                    # Real termination: all agents eliminated
                    # ALL agents (including those who died earlier) get terminated=True
                    for aid in self._agent_ids:
                        terminated_dict[aid] = True
                        truncated_dict[aid] = False
                    terminated_dict["__all__"] = True
                    truncated_dict["__all__"] = False
                    print(f"[EPISODE END] All agents eliminated at step {self.episode_steps}")
                elif timeout_reached or max_steps_reached:
                    # Truncation: time limit reached
                    # ALL agents (including those who died earlier) get truncated=True
                    for aid in self._agent_ids:
                        truncated_dict[aid] = True
                        terminated_dict[aid] = False
                    truncated_dict["__all__"] = True
                    terminated_dict["__all__"] = False
                    reason = "timeout" if timeout_reached else f"max steps ({self.max_episode_steps})"
                    print(f"[EPISODE END] {reason} at step {self.episode_steps}, duration {episode_duration:.1f}s (Alive={len(self._alive_agents)}, Dead={len(self._dead_agents)})")
                else:
                    # Episode continues - all agents stay non-terminal
                    terminated_dict["__all__"] = False
                    truncated_dict["__all__"] = False

                return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict

            except Exception as e:
                print(f"[SBDAPMMultiAgentEnv] Step error: {e}")
                import traceback
                traceback.print_exc()

                return (
                    {aid: np.zeros(74, dtype=np.float32) for aid in self._agent_ids},  # v4.0: 70 obs + 4 objective
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
                    obs_dict[flat_id] = np.zeros(74, dtype=np.float32)  # v4.0: 70 obs + 4 objective

            return obs_dict, {aid: {} for aid in self._agent_ids}

        def render(self):
            return self.schola_env.render() if hasattr(self.schola_env, 'render') else None

        def close(self):
            if hasattr(self.schola_env, 'close'):
                self.schola_env.close()