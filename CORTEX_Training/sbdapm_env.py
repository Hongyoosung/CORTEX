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
                    auto_reset_type=AutoResetType.SAME_STEP
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

            # v8.5 VECTORIZED TRAINING: Per-environment episode tracking
            self._env_episode_steps = {i: 0 for i in range(self.num_envs)}
            self._env_episode_start_time = {i: None for i in range(self.num_envs)}
            self._env_episodes_completed = {i: 0 for i in range(self.num_envs)}
            self._env_done_flags = {i: False for i in range(self.num_envs)}

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
            reset_start = time.time()
            
            # 에피소드 통계 출력
            if self._env_episode_start_time and any(v > 0 for v in self._env_episode_steps.values()):
                print("=" * 80)
                print("EPISODE COMPLETION SUMMARY")
                for env_idx in range(self.num_envs):
                    if self._env_episode_start_time[env_idx]:
                        duration = time.time() - self._env_episode_start_time[env_idx]
                        print(f"  Env {env_idx}: Episode {self._env_episodes_completed[env_idx]}, "
                            f"Steps={self._env_episode_steps[env_idx]}, Duration={duration:.1f}s")
                        self._env_episodes_completed[env_idx] += 1
                print("=" * 80)
            
            # ✅ 첫 번째 리셋만 hard_reset 호출
            is_first = not self._first_reset_done
            
            if is_first:
                print("=" * 80)
                print(f"RESET: First reset - calling hard_reset() at Time={time.time():.2f}")
                
                current_time = time.time()
                for env_idx in range(self.num_envs):
                    self._env_episode_steps[env_idx] = 0
                    self._env_episode_start_time[env_idx] = current_time
                    self._env_done_flags[env_idx] = False
                
                try:
                    rawobs = self.schola_env.hard_reset()
                    print(f"RESET: hard_reset returned successfully. Duration={time.time()-reset_start:.2f}s")
                    self._first_reset_done = True
                    self._env_episode_start_time = current_time

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
                # ✅ 이후 리셋: Schola의 AutoReset이 자동 처리하므로
                # 다음 poll()에서 새 에피소드 관측값을 받음
                print(f"RESET: AutoReset mode - returning last observations (Time={time.time():.2f})")
                
                # 환경별 카운터 리셋
                current_time = time.time()
                for env_idx in range(self.num_envs):
                    self._env_episode_steps[env_idx] = 0
                    self._env_episode_start_time[env_idx] = current_time
                    self._env_done_flags[env_idx] = False

                # ✅ 마지막 관측값 반환 (Schola가 자동 리셋 처리)
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
                self.schola_env.send(formattedactions)
                step_result = self.schola_env.poll()
                
                # Parse result
                if len(step_result) == 5:
                    obs_nested, rew_nested, term_nested, trunc_nested, info_nested = step_result
                elif len(step_result) == 4:
                    obs_nested, rew_nested, term_nested, info_nested = step_result
                    trunc_nested = term_nested
                else:
                    raise ValueError(f"Unexpected poll result: {len(step_result)} items")
                
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
                    
                    # 🔥 에피소드 완료 시 카운터 증가 (로깅용)
                    if (is_term or is_trunc) and self._env_episode_start_time[env_idx]:
                        elapsed = time.time() - self._env_episode_start_time[env_idx]
                        print(f"[ENV {env_idx} DONE] Episode {self._env_episodes_completed[env_idx]} completed")
                        print(f"  Duration: {elapsed:.1f}s, Steps: {self._env_episode_steps[env_idx]}")
                        self._env_episodes_completed[env_idx] += 1
                        self._env_episode_steps[env_idx] = 0
                        self._env_episode_start_time[env_idx] = time.time()
                
                # 🔥 진행 중인 환경의 스텝 카운터 증가
                for env_idx in range(self.num_envs):
                    # terminated/truncated 체크하여 진행 중인 환경만 카운트
                    env_agents = [f"agent_{env_idx}_{i}" for i in range(8)]
                    if not all(terminated_dict.get(aid, False) or truncated_dict.get(aid, False) 
                            for aid in env_agents if aid in self._agent_ids):
                        self._env_episode_steps[env_idx] += 1

                # 🔥 주기적 진행 상황 로깅 (100 스텝마다)
                if self._env_episode_steps[env_idx] % 100 == 0:
                    elapsed = time.time() - self._env_episode_start_time[env_idx] if self._env_episode_start_time[env_idx] else 0
                    total_reward = sum(reward_dict.values())
                    avg_reward = total_reward / len(self._agent_ids) if self._agent_ids else 0
                    print("="*80)
                    print(f"[PROGRESS] Step={self._env_episode_steps[env_idx]}, Elapsed={elapsed:.1f}s")
                    print(f"  Total Reward={total_reward:.2f}, Avg Reward={avg_reward:.2f}")
                    for env_idx in range(self.num_envs):
                        env_elapsed = time.time() - self._env_episode_start_time[env_idx] if self._env_episode_start_time[env_idx] else 0
                        status = "⚡" if env_elapsed < 60 else "🔥"
                        print(f"  {status} Env {env_idx}: Episode {self._env_episodes_completed[env_idx]}, "
                            f"Steps={self._env_episode_steps[env_idx]}, Time={env_elapsed:.1f}s")
                    print("="*80)
                
                return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict
                
            except Exception as e:
                print(f"[STEP ERROR] {e}")
                import traceback
                traceback.print_exc()
                # Return terminal state
                fallback_obs = {flat_id: self._build_observation(np.zeros(46, dtype=np.float32))
                            for flat_id in self._agent_ids}
                return (fallback_obs,
                        {flat_id: 0.0 for flat_id in self._agent_ids},
                        {flat_id: True for flat_id in self._agent_ids},
                        {flat_id: False for flat_id in self._agent_ids},
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
