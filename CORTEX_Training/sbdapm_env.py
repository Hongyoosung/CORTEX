"""
SBDAPM Environment for Schola/RLlib Training (v8.0 - Tactical Parameters)

v8.0 Architecture:
    - MCTS assigns strategies → RL outputs tactical parameters + combat priority
    - Multi-head network: 50 input (46 base + 4 strategy one-hot) → [256, 256, 128] → 4 strategy heads (4 params each) + combat head (1 priority)
    - Action space: 5 continuous outputs (4 tactical params + 1 combat priority)
    - Exports to: cortex_policy_v8.onnx

Action Space (v8.0):
    - Observation: 50 features (46 base from UE5 + 4 strategy one-hot added by Python)
    - Action: Box(5) - [Aggression, CoverPref, Spread, Risk, CombatPriority]
    - Tactical params (0-3): Continuous [0,1] values that modulate EQS weights
    - Combat priority (4): Continuous [0,1], thresholded at 0.5 (Closest/LowestHP)

Data Flow:
1. UE5's Think() throttled to DecisionInterval (default 1.0s)
2. Think() sends observations + strategy assignment via gRPC
3. Python RL policy returns tactical parameters + combat choice
4. UE5 applies parameters to EQS weights + combat targeting
5. Episode ends on timeout or objective completion (NOT individual agent death)

Note: Schola's fill_proto() iterates through ALL sub-spaces and expects action data
for each key. Each agent's action is duplicated across all component keys.
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

# Import RLConfig from auto-generated config (synced from C++)
try:
    from training_env.config import RLConfig
    CONFIG_AVAILABLE = True
except ImportError:
    CONFIG_AVAILABLE = False
    print("Warning: training_env/config.py not found. Run: python tools/sync_config_from_cpp.py")
    # Fallback values if config not available
    class RLConfig:
        OBSERVATION_SIZE = 50  # v8.0: 46 base + 4 strategy one-hot
        NUM_STRATEGIES = 4
        NUM_TACTICAL_PARAMS = 4
        NUM_COMBAT_CHOICES = 2
        NUM_TOTAL_OUTPUTS = 5  # 4 tactical + 1 combat priority (action dim)


# The maximum duration of an episode. After this time, the environment will reset.
# v8.0: Set to 60s for 4v4 objective capture scenario
# Timing breakdown:
#   - Traversal: 5-15s (agent speed 600-1200 cm/s, map ~5000-10000 cm)
#   - Combat: 5-15s (engagement with defenders)
#   - Capture: 10-15s (4 agents × 2 HP/sec = 8 HP/sec → 12.5s for 100 HP)
# 60s allows one complete capture cycle or multiple engagements
MAX_EPISODE_DURATION = 60.0



if SCHOLA_AVAILABLE:

    class SBDAPMMultiAgentEnv(MultiAgentEnv):
        """
        Multi-Agent RLlib Environment for SBDAPM (v8.0 - Tactical Parameters)

        v8.0 Key Features:
        - Action space: 5 continuous outputs (4 tactical params + 1 combat priority)
        - MCTS assigns strategy (part of observation), RL outputs tactical params
        - Multi-head policy: separate output heads per strategy type

        Environment Features:
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

            # v8.0: Strategy assignments for curriculum learning
            # Maps agent_id -> strategy_index (0=Assault, 1=Defend, 2=Support, 3=Retreat)
            # Must be initialized BEFORE _update_agent_map() which accesses it
            self._strategy_assignments = {}

            # 초기 ID 파싱 (reset시 갱신됨)
            if hasattr(self.schola_env, 'ids'):
                self._update_agent_map()

            # v8.0 Observation Space (50 features = 46 base + 4 strategy one-hot)
            # Base observation from UE5 (46 features):
            # - Position (3): pos(3)
            # - Health (1): health(1)
            # - Combat (1): enemy_dist(1)
            # - Perception (16): raycasts(16)
            # - Enemy Info (16): count(1), nearby(5x3=15)
            # - Tactical (4): has_cover(1), cover_dist(1), cover_dir(2)
            # - Support Context (5): ally_needs(1), ally_health(1), ally_dist(1), ally_dir(2)
            # Strategy one-hot (4 features, added by Python wrapper):
            # - [Assault, Defend, Support, Retreat]
            self._obs_space = spaces.Box(low=-np.inf, high=np.inf, shape=(RLConfig.OBSERVATION_SIZE,), dtype=np.float32)
            # v8.0: Continuous action space (4 tactical params + 1 combat priority)
            # Network outputs 5 values: [Aggression, CoverPref, Spread, Risk, CombatPriority]
            self._action_space = spaces.Box(
                low=-np.inf,
                high=np.inf,
                shape=(RLConfig.NUM_TOTAL_OUTPUTS,),  # 5
                dtype=np.float32
            )

            # DEBUG: Print what action space Schola received from UE5
            if hasattr(self.schola_env, 'action_defns'):
                print(f"[DEBUG] Schola action_defns from UE5:")
                for env_idx, agents in self.schola_env.action_defns.items():
                    for agent_idx, defn in agents.items():
                        print(f"  Agent ({env_idx},{agent_idx}): {defn}")
                        if hasattr(defn, 'shape'):
                            print(f"    Shape: {defn.shape}")
                        elif hasattr(defn, 'spaces'):
                            for key, space in defn.spaces.items():
                                print(f"    {key}: {space} (shape={getattr(space, 'shape', 'N/A')})")

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

            # v8.0: Initialize strategy assignments for new agents (default: Assault)
            for flat_id in self._agent_ids:
                if flat_id not in self._strategy_assignments:
                    self._strategy_assignments[flat_id] = 0  # Assault by default

        def set_strategy_assignments(self, assignments):
            """
            Set strategy assignments for curriculum learning (v8.0).

            Args:
                assignments: dict mapping agent_id -> strategy_index
                    OR list of strategy indices (applied to agents in order)

            Strategy indices:
                0 = Assault, 1 = Defend, 2 = Support, 3 = Retreat
            """
            if isinstance(assignments, dict):
                self._strategy_assignments.update(assignments)
            elif isinstance(assignments, (list, tuple)):
                # Apply in order to sorted agent IDs
                sorted_ids = sorted(self._agent_ids)
                for i, strategy_idx in enumerate(assignments):
                    if i < len(sorted_ids):
                        self._strategy_assignments[sorted_ids[i]] = strategy_idx

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

        def _get_action_mask(self, obs):
            """
            Generate action mask (v8.0 - not used for continuous actions).

            v8.0: RL outputs continuous tactical parameters, no discrete strategy selection.
            This mask is kept for backwards compatibility with RLlib info dict.

            Returns:
                np.array: [1, 1, 1, 1] - Placeholder (not used for continuous actions)
            """
            # v8.0: Placeholder mask - tactical parameters are continuous, no masking
            return np.array([1, 1, 1, 1], dtype=np.int8)

        def _shape_reward(self, base_reward, obs, flat_id):
            """
            Dense reward shaping to provide learning signal in long episodes.

            Observation structure (46 dims, v8.0):
            [0-2]: position (3)
            [3]: health (1)
            [4]: enemy_dist (1)
            [5-20]: raycasts (16)
            [21]: visible_enemy_count (1)
            [22-36]: nearby enemies (5x3=15)
            [37-40]: tactical/cover (4)
            [41-45]: support context (5)

            Args:
                base_reward: Raw reward from UE5
                obs: Observation array (46 dims)
                flat_id: Agent ID

            Returns:
                Shaped reward combining base + dense signals
            """
            shaped_reward = base_reward

            # Extract key features (v8.0 indices)
            health = obs[3] if len(obs) > 3 else 0.5
            enemy_dist = obs[4] if len(obs) > 4 else 1.0  # Normalized [0,1]

            # 1. SURVIVAL BONUS: Small reward for staying alive
            # Mitigates excessive negative rewards from time penalties
            survival_bonus = 0.001
            shaped_reward += survival_bonus

            # 2. HEALTH PRESERVATION: Penalize damage, reward high health
            # Encourages defensive behavior and survival
            if health < 0.5:
                # Low health penalty (escalating)
                health_penalty = -0.01 * (0.5 - health)
                shaped_reward += health_penalty
            elif health > 0.8:
                # High health bonus (surviving well)
                shaped_reward += 0.005

            # 3. COMBAT ENGAGEMENT: Reward appropriate enemy distance
            # enemy_dist is normalized [0,1], where 0 = very close, 1 = far/no enemy
            if enemy_dist < 0.5:  # Enemy detected (closer than half perception range)
                if 0.1 < enemy_dist < 0.3:
                    # Optimal combat range (10-30% of perception range)
                    shaped_reward += 0.01
                elif enemy_dist < 0.05:
                    # Too close - dangerous!
                    shaped_reward -= 0.005

            return shaped_reward

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
                else:
                    print(f"[RESET] Calling hard_reset() to start new episode (episode_steps_completed: {self.episode_steps})...")

                reset_start_time = time.time()
                raw_obs = self.schola_env.hard_reset()
                reset_duration = time.time() - reset_start_time

                if is_first_reset:
                    print("  hard_reset() completed successfully!")
                    self._first_reset_done = True
                else:
                    print(f"[RESET] hard_reset() completed in {reset_duration:.2f}s - UE5 ready for new episode")

                # 2. Update agent mapping
                self._update_agent_map()

                # v7.6: Reset alive/dead agent tracking AFTER agent map is updated
                self._alive_agents = set(self._agent_ids)  # All agents start alive
                self._dead_agents = set()
                if is_first_reset:
                    print(f"[WORKER READY] {len(self._alive_agents)} agents detected and ready for training")
                else:
                    print(f"[RESET COMPLETE] Episode reset successful: {len(self._alive_agents)} agents alive, ready for first step()")

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

        def _process_combined_action(self, action_array):
            """
            Process combined action from multi-head network output (v8.0).

            Network Output Structure (5 values, all continuous [0,1]):
                - output[0:4]: Tactical parameters (sigmoid-activated)
                  - [0]: Aggression
                  - [1]: CoverPreference
                  - [2]: SpreadDistance
                  - [3]: RiskTolerance
                - output[4]: Combat priority (sigmoid-activated)
                  - <0.5 = Closest enemy, >=0.5 = LowestHP enemy

            Args:
                action_array: np.array of shape (5,) from network forward pass

            Returns:
                np.array (5,) clipped to [0, 1] - ready to send to UE5
            """
            # Validate input
            if action_array.shape[0] != 5:
                raise ValueError(f"Expected action shape (5,), got {action_array.shape}")

            # Clip all values to [0,1] (should already be from sigmoid)
            return np.clip(action_array, 0.0, 1.0).astype(np.float32)

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

                # CRITICAL: Log first step after reset to confirm step() is being called
                if self.episode_steps == 0:
                    print(f"[STEP 0] First step() call after reset - beginning new episode")

                # v8.0: Log step entry to diagnose blocking issues
                # If we see "STEP ENTRY" but no subsequent logs, poll() is blocking
                if self.episode_steps > 0 and self.episode_steps % 30 == 0:
                    print(f"[STEP ENTRY] step {self.episode_steps}, episode_time={current_time - getattr(self, '_episode_start_time', current_time):.1f}s")

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

                    # v8.0: Combined action space (5 continuous values)
                    if isinstance(action, np.ndarray) and action.shape[0] == 5:
                        # Process combined action [4 tactical params + 1 combat priority]
                        action_array = self._process_combined_action(action)
                    else:
                        # Fallback for invalid action shape
                        print(f"[WARNING] Invalid action shape: {action.shape if isinstance(action, np.ndarray) else type(action)}, expected (5,)")
                        action_array = np.array([0.5, 0.5, 0.5, 0.5, 0.0], dtype=np.float32)  # Default: neutral tactical + closest combat

                    # Get ALL component keys for this agent
                    agent_keys = all_action_keys.get((env_idx, agent_idx), [])

                    if not agent_keys:
                        print(f"[ERROR] No action keys found for agent ({env_idx}, {agent_idx})")
                        raise RuntimeError(f"Action keys missing for agent ({env_idx}, {agent_idx}).")

                    # Provide the SAME action for ALL keys (fill_proto expects all)
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

                # 데이터 수신 (poll() blocks until UE5 responds)
                # Log before poll() to detect blocking issues
                if self.episode_steps > 0 and self.episode_steps % 50 == 0:
                    print(f"[POLL] Waiting for UE5 response (step {self.episode_steps})...")

                # CRITICAL: Log first few steps of each episode to diagnose iteration issues
                if self.episode_steps < 3:
                    print(f"[POLL] Episode step {self.episode_steps}: Waiting for UE5 observations...")
                    poll_start_time = time.time()

                step_result = self.schola_env.poll()

                if self.episode_steps < 3:
                    poll_duration = time.time() - poll_start_time
                    print(f"[POLL] Episode step {self.episode_steps}: Received observations in {poll_duration:.3f}s")

                # v8.0: Log poll() return to confirm data received
                if self.episode_steps > 0 and self.episode_steps % 30 == 0:
                    print(f"[POLL RETURN] step {self.episode_steps}, received {len(step_result)} items")

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

                # v8.0: First pass - check for newly dead agents from UE5
                # Also detect if UE5 signals episode-level termination
                any_term_from_ue5 = False
                for flat_id in self._agent_ids:
                    env_idx, agent_idx = self.agent_map[flat_id]
                    is_term_ue5 = term_nested.get(env_idx, {}).get(agent_idx, False)
                    is_trunc_ue5 = trunc_nested.get(env_idx, {}).get(agent_idx, False) if isinstance(trunc_nested, dict) else False

                    if is_term_ue5 or is_trunc_ue5:
                        any_term_from_ue5 = True

                    # If UE5 reports this agent as terminated, move to dead set
                    if is_term_ue5 and flat_id in self._alive_agents:
                        self._alive_agents.discard(flat_id)
                        self._dead_agents.add(flat_id)
                        print(f"[v8.0] Agent {flat_id} died. Alive: {len(self._alive_agents)}, Dead: {len(self._dead_agents)}")

                # Log if UE5 is signaling termination (helps diagnose sync issues)
                if any_term_from_ue5 and self.episode_steps % 10 == 0:
                    print(f"[v8.0] UE5 signaling termination at step {self.episode_steps}. Alive: {len(self._alive_agents)}, Dead: {len(self._dead_agents)}")

                # v7.6 & v7.7: Second pass - build RLlib outputs and track rewards
                for flat_id in self._agent_ids:
                    env_idx, agent_idx = self.agent_map[flat_id]

                    if flat_id in self._dead_agents:
                        # Dead agents get zero obs/rewards but NOT terminated (yet)
                        # They'll be terminated when the whole episode ends
                        # v8.0: Dead agents still need 50-dim obs (46 base zeros + 4 strategy one-hot)
                        base_obs = np.zeros(46, dtype=np.float32)
                        strategy_idx = self._strategy_assignments.get(flat_id, 0)
                        strategy_onehot = np.zeros(4, dtype=np.float32)
                        strategy_onehot[strategy_idx] = 1.0
                        obs_dict[flat_id] = np.concatenate([base_obs, strategy_onehot])  # 46 + 4 = 50
                        reward_dict[flat_id] = 0.0
                        terminated_dict[flat_id] = False  # Will be set True when episode ends
                        truncated_dict[flat_id] = False
                        # Dead agents: all actions masked
                        info_dict[flat_id] = {
                            "dead": True,
                            "action_mask": np.zeros(4, dtype=np.int8)  # Dead agent - no valid actions
                        }
                    else:
                        # Live agents get normal obs/rewards
                        agent_obs_data = obs_nested.get(env_idx, {}).get(agent_idx, None)
                        if agent_obs_data is not None:
                            if isinstance(agent_obs_data, dict):
                                obs_val = list(agent_obs_data.values())[0]
                            else:
                                obs_val = agent_obs_data
                            base_obs = np.array(obs_val, dtype=np.float32).flatten()
                        else:
                            base_obs = np.zeros(46, dtype=np.float32)  # v8.0: 46 base features

                        # v8.0: Append strategy one-hot (4 features) - MUST match _process_obs()
                        strategy_idx = self._strategy_assignments.get(flat_id, 0)  # Default: Assault
                        strategy_onehot = np.zeros(4, dtype=np.float32)
                        strategy_onehot[strategy_idx] = 1.0
                        obs_dict[flat_id] = np.concatenate([base_obs, strategy_onehot])  # 46 + 4 = 50

                        # Get base reward from UE5
                        base_reward = float(rew_nested.get(env_idx, {}).get(agent_idx, 0.0))

                        # Apply reward shaping for dense learning signals
                        shaped_reward = self._shape_reward(base_reward, obs_dict[flat_id], flat_id)
                        reward_dict[flat_id] = shaped_reward

                        # Reward debug logging (uncomment for debugging)
                        # print(f"[REWARD DEBUG] Agent {flat_id}: base_reward={base_reward:.2f}, shaped={shaped_reward:.2f}")

                        # v7.7: Accumulate episode rewards
                        if flat_id not in self._episode_rewards:
                            self._episode_rewards[flat_id] = 0.0
                        self._episode_rewards[flat_id] += shaped_reward

                        # Mid-episode: never set terminated/truncated for live agents
                        terminated_dict[flat_id] = False
                        truncated_dict[flat_id] = False

                        # v8.0: Action mask placeholder (continuous actions don't use masking)
                        action_mask = self._get_action_mask(obs_dict[flat_id])

                        info_dict[flat_id] = info_nested.get(env_idx, {}).get(agent_idx, {})
                        info_dict[flat_id]["action_mask"] = action_mask

                # Episode Termination Logic (v8.0 - Fixed team elimination detection)
                self.episode_steps += 1
                episode_duration = current_time - self._episode_start_time

                # DIAGNOSTIC: Log alive/dead agents on first few steps
                if self.episode_steps <= 3:
                    print(f"[DIAGNOSTIC] Step {self.episode_steps}: Alive={len(self._alive_agents)}, Dead={len(self._dead_agents)}")

                # Condition 1: Timeout
                timeout_reached = episode_duration >= MAX_EPISODE_DURATION

                # Condition 2: Max step safeguard (fallback)
                max_steps_reached = self.episode_steps >= self.max_episode_steps

                # Condition 3: Team elimination (all agents dead)
                # CRITICAL FIX: Without this check, poll() blocks forever when UE5 ends episode
                team_eliminated = (len(self._alive_agents) == 0 and len(self._dead_agents) > 0)

                if team_eliminated:
                    # Natural termination: all agents died
                    # Use terminated=True (not truncated) since episode ended naturally
                    for aid in self._agent_ids:
                        terminated_dict[aid] = True
                        truncated_dict[aid] = False
                    terminated_dict["__all__"] = True
                    truncated_dict["__all__"] = False
                    print(f"[EPISODE END] Team eliminated at step {self.episode_steps}, duration {episode_duration:.1f}s")
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

                # v8.0: Build 50-dim obs (46 base zeros + 4 strategy one-hot) for each agent
                fallback_obs = {}
                for aid in self._agent_ids:
                    base_obs = np.zeros(46, dtype=np.float32)
                    strategy_idx = self._strategy_assignments.get(aid, 0)
                    strategy_onehot = np.zeros(4, dtype=np.float32)
                    strategy_onehot[strategy_idx] = 1.0
                    fallback_obs[aid] = np.concatenate([base_obs, strategy_onehot])

                return (
                    fallback_obs,
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
            info_dict = {}

            for flat_id in self._agent_ids:
                env_idx, agent_idx = self.agent_map[flat_id]
                agent_obs_data = obs_nested.get(env_idx, {}).get(agent_idx, None)

                # v8.0: Get base observation (46 features from UE5)
                if agent_obs_data is not None:
                    if isinstance(agent_obs_data, dict):
                        obs_val = list(agent_obs_data.values())[0]
                    else:
                        obs_val = agent_obs_data
                    base_obs = np.array(obs_val, dtype=np.float32).flatten()
                else:
                    base_obs = np.zeros(46, dtype=np.float32)  # v8.0: 46 base features

                # v8.0: Append strategy one-hot (4 features) for curriculum learning
                # Strategy indices: 0=Assault, 1=Defend, 2=Support, 3=Retreat
                strategy_idx = self._strategy_assignments.get(flat_id, 0)  # Default: Assault
                strategy_onehot = np.zeros(4, dtype=np.float32)
                strategy_onehot[strategy_idx] = 1.0

                # Concatenate: [46 base features] + [4 strategy one-hot] = 50 total
                obs_dict[flat_id] = np.concatenate([base_obs, strategy_onehot])

                # v8.0: Action mask placeholder for RLlib info dict
                action_mask = self._get_action_mask(obs_dict[flat_id])
                info_dict[flat_id] = {"action_mask": action_mask}

            return obs_dict, info_dict

        def render(self):
            return self.schola_env.render() if hasattr(self.schola_env, 'render') else None

        def close(self):
            if hasattr(self.schola_env, 'close'):
                self.schola_env.close()


# Fallback dummy environment for testing without Schola
class SBDAPMEnv:
    """
    Dummy single-agent environment for testing without Schola.
    Returns random observations and zero rewards.
    v8.0: Updated to use 50-dim observation (46 base + 4 strategy one-hot)
    """
    def __init__(self, **kwargs):
        print("[SBDAPMEnv] WARNING: Using dummy environment (Schola not available)")
        self._obs_space = spaces.Box(low=-np.inf, high=np.inf, shape=(RLConfig.OBSERVATION_SIZE,), dtype=np.float32)
        # v8.0: Combined action space (4 tactical + 1 combat priority = 5 continuous)
        self._action_space = spaces.Box(
            low=-np.inf,
            high=np.inf,
            shape=(RLConfig.NUM_TOTAL_OUTPUTS,),
            dtype=np.float32
        )
        self.max_episode_steps = kwargs.get("max_episode_steps", 1000)
        self.episode_steps = 0

    @property
    def observation_space(self):
        return self._obs_space

    @property
    def action_space(self):
        return self._action_space

    def reset(self, *, seed=None, options=None):
        self.episode_steps = 0
        # v8.0: Generate random 46-dim base obs + assault strategy one-hot [1,0,0,0]
        base_obs = np.random.randn(46).astype(np.float32)
        strategy_onehot = np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32)  # Assault
        obs = np.concatenate([base_obs, strategy_onehot])
        info = {"action_mask": np.ones(4, dtype=np.int8)}
        return obs, info

    def step(self, action):
        self.episode_steps += 1
        # v8.0: Generate random 46-dim base obs + assault strategy one-hot
        base_obs = np.random.randn(46).astype(np.float32)
        strategy_onehot = np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32)  # Assault
        obs = np.concatenate([base_obs, strategy_onehot])
        reward = 0.0
        terminated = False
        truncated = self.episode_steps >= self.max_episode_steps
        info = {"action_mask": np.ones(4, dtype=np.int8)}
        return obs, reward, terminated, truncated, info

    def render(self):
        return None

    def close(self):
        pass