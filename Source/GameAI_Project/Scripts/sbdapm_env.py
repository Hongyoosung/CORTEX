"""
SBDAPM Environment Wrapper for Schola/RLlib Training (v3.1 Multi-Agent)

Multi-agent environment for 4 follower agents with shared PPO policy.

Observation: 78 features per agent (71 FObservationElement + 7 current objective embedding)
Action: 8-dimensional Box per agent (continuous, flattened)
  - [0-1]: MoveDirection (continuous): [-1, 1] x [-1, 1]
  - [2]:   MoveSpeed (continuous): [0, 1]
  - [3-4]: LookDirection (continuous): [-1, 1] x [-1, 1]
  - [5]:   Fire (continuous [0,1], interpreted as binary >= 0.5)
  - [6]:   Crouch (continuous [0,1], interpreted as binary >= 0.5)
  - [7]:   UseAbility (continuous [0,1], interpreted as binary >= 0.5)
"""

from gymnasium import spaces
import numpy as np

# RLlib multi-agent support
try:
    from ray.rllib.env.multi_agent_env import MultiAgentEnv
    RLLIB_AVAILABLE = True
except ImportError:
    RLLIB_AVAILABLE = False
    print("Warning: ray[rllib] not installed")
    # Fallback for non-RLlib usage
    class MultiAgentEnv:
        pass

try:
    from schola.gym.env import GymEnv as UnrealEnv
    from schola.gym.env import GymVectorEnv as UnrealVectorEnv
    SCHOLA_AVAILABLE = True
except ImportError:
    SCHOLA_AVAILABLE = False
    print("Warning: schola not installed. Install with: pip install schola[rllib]")


class SBDAPMEnv:
    """
    SBDAPM atomic action environment (v3.0).

    Connects to Unreal Engine via Schola gRPC and exposes:
    - Observation: 78 float features (71 FObservationElement + 7 objective embedding)
    - Action: 8-dimensional Box (continuous, flattened)
      - [0-1]: move_direction [-1, 1]
      - [2]:   move_speed [0, 1]
      - [3-4]: look_direction [-1, 1]
      - [5-7]: fire, crouch, use_ability [0, 1] (interpreted as binary)
    - Reward: Hierarchical (individual + coordination + strategic)
    """

    def __init__(self, host="localhost", port=50051, **kwargs):
        """
        Initialize environment.

        Args:
            host: gRPC server host (UE Schola plugin)
            port: gRPC server port
        """
        self.host = host
        self.port = port

        # Define spaces
        self.observation_space = spaces.Box(
            low=-np.inf,
            high=np.inf,
            shape=(78,),  # 71 observation + 7 objective embedding
            dtype=np.float32
        )
        # Flattened 8D continuous action space
        # [0-1]: move_x, move_y in [-1, 1]
        # [2]:   speed in [0, 1]
        # [3-4]: look_x, look_y in [-1, 1]
        # [5-7]: fire, crouch, ability in [0, 1] (binary interpreted)
        self.action_space = spaces.Box(
            low=np.array([-1.0, -1.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.0], dtype=np.float32),
            high=np.array([1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0], dtype=np.float32),
            shape=(8,),
            dtype=np.float32
        )

        # Episode tracking
        self.episode_steps = 0
        self.max_episode_steps = 100000  # Very high limit - let UE control episode ending (2min or team elimination)
        self.total_reward = 0.0

        # Internal connection (will be set by Schola)
        self._connected = False

    def reset(self, *, seed=None, options=None):
        """Reset environment for all agents."""
        self.episode_steps = 0
        
        try:
            # Get observations from Schola
            obs_vec, info_vec = self.schola_env.reset(seed=seed, options=options)
            
            if isinstance(obs_vec, dict):
                raw_agent_ids = list(obs_vec.keys())
                print(f"[SBDAPMMultiAgentEnv] Raw agents detected: {raw_agent_ids}")
                
                # ===== 수정: CDO 및 숫자 없는 컴포넌트 필터링 =====
                filtered_agent_ids = []
                
                for aid in raw_agent_ids:
                    # 1. CDO 필터링 (정확한 매칭)
                    if aid == "ScholaAgentComponent":
                        print(f"[SBDAPMMultiAgentEnv] Filtering CDO: {aid}")
                        continue
                    
                    # 2. Default/Archetype 키워드 필터링
                    if "Default" in aid or "Archetype" in aid:
                        print(f"[SBDAPMMultiAgentEnv] Filtering default/archetype: {aid}")
                        continue
                    
                    # 3. 숫자가 없는 컴포넌트 필터링 (인스턴스가 아님)
                    if not any(char.isdigit() for char in aid):
                        print(f"[SBDAPMMultiAgentEnv] Filtering non-instance: {aid}")
                        continue
                    
                    filtered_agent_ids.append(aid)
                
                # 정렬하여 일관성 보장
                filtered_agent_ids = sorted(filtered_agent_ids)
                
                print(f"[SBDAPMMultiAgentEnv] Filtered agents: {filtered_agent_ids}")
                print(f"[SBDAPMMultiAgentEnv] Agent count: {len(filtered_agent_ids)}")
                
                # 예상 개수 검증 (4개)
                EXPECTED_AGENT_COUNT = 4
                if len(filtered_agent_ids) != EXPECTED_AGENT_COUNT:
                    print(f"[SBDAPMMultiAgentEnv] WARNING: Expected {EXPECTED_AGENT_COUNT} agents, "
                        f"got {len(filtered_agent_ids)}")
                
                self._agent_ids = set(filtered_agent_ids)
                self._agent_id_list = filtered_agent_ids
                
                # Extract observations
                obs_dict = {}
                for agent_id in self._agent_ids:
                    if agent_id not in obs_vec:
                        print(f"[SBDAPMMultiAgentEnv] Error: Missing obs for {agent_id}")
                        obs_dict[agent_id] = np.zeros(78, dtype=np.float32)
                        continue
                    
                    obs = obs_vec[agent_id]
                    
                    # Handle batched obs
                    if hasattr(obs, 'shape') and len(obs.shape) == 2:
                        obs = obs[0]
                    
                    obs_dict[agent_id] = obs.astype(np.float32)
                
                # Extract info
                info_dict = {}
                if isinstance(info_vec, dict):
                    info_dict = {aid: info_vec.get(aid, {}) for aid in self._agent_ids}
                else:
                    info_dict = {aid: {} for aid in self._agent_ids}
                
                return obs_dict, info_dict
                
            else:
                print(f"[SBDAPMMultiAgentEnv] ERROR: Expected dict, got {type(obs_vec)}")
                return {}, {}
                
        except Exception as e:
            print(f"[SBDAPMMultiAgentEnv] Error during reset: {e}")
            import traceback
            traceback.print_exc()
            return {}, {}

    def step(self, action):
        """
        Execute action and return result.

        Args:
            action: (8,) numpy array in Box space:
                [0-1]: move_direction (x, y)
                [2]:   move_speed
                [3-4]: look_direction (x, y)
                [5]:   fire (0-1, binary interpreted)
                [6]:   crouch (0-1, binary interpreted)
                [7]:   use_ability (0-1, binary interpreted)

        Returns:
            observation, reward, terminated, truncated, info
        """
        self.episode_steps += 1

        # Default values (will be overwritten by Schola callbacks)
        observation = np.zeros(78, dtype=np.float32)
        reward = 0.0
        terminated = False
        truncated = self.episode_steps >= self.max_episode_steps

        self.total_reward += reward

        info = {
            "episode_steps": self.episode_steps,
            "action": {
                "move": [float(action[0]), float(action[1])],
                "speed": float(action[2]),
                "look": [float(action[3]), float(action[4])],
                "fire": action[5] >= 0.5,
                "crouch": action[6] >= 0.5,
                "ability": action[7] >= 0.5
            },
            "total_reward": self.total_reward
        }

        return observation, reward, terminated, truncated, info

    def render(self):
        """Rendering is handled by UE."""
        pass

    def close(self):
        """Close connection."""
        self._connected = False


if SCHOLA_AVAILABLE:
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
    import gymnasium as gym

    class SafeUnrealVectorEnv(UnrealVectorEnv):
        """
        Wrapper around UnrealVectorEnv to handle CDO (Class Default Object) mismatch.

        Problem: Schola creates action_space with 5 keys (1 CDO + 4 real agents)
                 but id_manager only has 4 entries (real agents).

        Solution: Store parent's original spaces, create filtered versions for RLlib.
                  Intercept step()/reset() to translate between 4-key and 5-key formats.
        """

        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)

            # Store parent's original spaces (with CDO)
            self._parent_action_space = self.action_space
            self._parent_observation_space = self.observation_space
            self._parent_single_action_space = self.single_action_space
            self._parent_single_observation_space = self.single_observation_space

            # Identify CDO keys
            self._cdo_keys = set()
            self._valid_keys = []

            if isinstance(self.single_action_space, spaces.Dict):
                original_keys = list(self.single_action_space.keys())
                id_count = len(self.id_manager.id_list) if hasattr(self, 'id_manager') else 0

                print(f"[SafeUnrealVectorEnv] Original action_space: {len(original_keys)} keys, id_manager: {id_count} entries")

                # Filter CDO keys
                for key in original_keys:
                    if key == "ScholaAgentComponent" or "Default" in key or "Archetype" in key:
                        self._cdo_keys.add(key)
                    elif not any(char.isdigit() for char in key):
                        self._cdo_keys.add(key)
                    else:
                        self._valid_keys.append(key)

                self._valid_keys = sorted(self._valid_keys)

                print(f"[SafeUnrealVectorEnv] CDO keys (excluded): {sorted(self._cdo_keys)}")
                print(f"[SafeUnrealVectorEnv] Valid keys: {self._valid_keys}")

                # Create filtered spaces for RLlib (4 agents only)
                filtered_single_action = spaces.Dict({
                    key: self.single_action_space[key] for key in self._valid_keys
                })
                filtered_single_obs = spaces.Dict({
                    key: self.single_observation_space[key] for key in self._valid_keys
                })

                # Replace instance attributes with filtered versions
                from gymnasium.vector.utils import batch_space
                self.single_action_space = filtered_single_action
                self.single_observation_space = filtered_single_obs
                self.action_space = batch_space(filtered_single_action, n=self.num_envs)
                self.observation_space = batch_space(filtered_single_obs, n=self.num_envs)

                print(f"[SafeUnrealVectorEnv] Exposed filtered spaces to RLlib: {len(self._valid_keys)} agents")

                # CRITICAL FIX: Patch id_manager to avoid index out of range errors
                # Schola creates action_space with N keys but id_manager with N-1 entries (CDO excluded)
                # Add dummy entries to id_manager to match action_space length
                # if hasattr(self, 'id_manager'):
                #     expected_count = len(self._parent_single_action_space.keys())  # Original action_space (with CDO)
                #     actual_count = len(self.id_manager.id_list)
                #
                #     if expected_count > actual_count:
                #         print(f"[SafeUnrealVectorEnv] Patching id_manager: adding {expected_count - actual_count} dummy entries")
                #         # Add dummy (env_id=0, agent_id=-1) entries for CDO keys
                #         for i in range(expected_count - actual_count):
                #             self.id_manager.id_list.append((0, -1))  # Dummy entry for CDO
                #         print(f"[SafeUnrealVectorEnv] id_manager now has {len(self.id_manager.id_list)} entries")

        def reset(self, **kwargs):
            """Reset and filter CDO from observations."""
            # Simply call reset - we are using filtered spaces permanently now
            obs, infos = super().reset(**kwargs)

            if isinstance(obs, dict) and self._cdo_keys:
                obs = {k: v for k, v in obs.items() if k not in self._cdo_keys}
                infos = {k: v for k, v in infos.items() if k not in self._cdo_keys}
                print(f"[SafeUnrealVectorEnv.reset] Filtered to {len(obs)} agents (CDO excluded)")

            return obs, infos

        def step(self, actions):
            """Call parent with filtered actions, filter results."""
            # We no longer need to add dummy CDO actions because we are not restoring
            # the parent action space. The environment now thinks it only has 4 agents.
            
            # Safeguard: Ensure action_space matches id_manager to prevent IndexError
            if hasattr(self, 'id_manager') and hasattr(self, 'action_space') and isinstance(self.action_space, spaces.Dict):
                id_count = len(self.id_manager.id_list)
                action_keys = list(self.action_space.keys())
                if len(action_keys) != id_count:
                    print(f"[SafeUnrealVectorEnv.step] Mismatch detected! action_space={len(action_keys)}, id_manager={id_count}. Forcing sync.")
                    
                    # Force update action_space to match filtered single_action_space
                    from gymnasium.vector.utils import batch_space
                    # Ensure single_action_space is filtered (should be done in init)
                    if isinstance(self.single_action_space, spaces.Dict) and len(self.single_action_space.keys()) == id_count:
                         self.action_space = batch_space(self.single_action_space, n=self.num_envs)
                         print(f"[SafeUnrealVectorEnv.step] action_space corrected to {len(self.action_space.keys())} keys.")
                    else:
                        print(f"[SafeUnrealVectorEnv.step] CRITICAL: single_action_space also mismatched! {len(self.single_action_space.keys())} keys.")

            # Inject dummy actions for CDO keys (Schola expects them even if we filtered them out)
            if self._cdo_keys and isinstance(actions, dict):
                # Infer batch size from existing actions
                try:
                    first_key = next(iter(actions))
                    batch_size = actions[first_key].shape[0]
                except (StopIteration, AttributeError, IndexError):
                    batch_size = self.num_envs

                for cdo_key in self._cdo_keys:
                    if cdo_key not in actions:
                        # Create dummy zero action with correct batch size
                        # Assuming Box(8,) for simplicity as we know the space
                        dummy_action = np.zeros((batch_size, 8), dtype=np.float32)
                        actions[cdo_key] = dummy_action
                        # print(f"[SafeUnrealVectorEnv] Injected dummy action for {cdo_key}")

            # CRITICAL: Temporarily restore parent spaces so GymVectorEnv.step_async 
            # includes the CDO keys when splitting the actions.
            # If we don't do this, iterate() uses the filtered action_space and drops the CDO key.
            filtered_action_space = self.action_space
            filtered_obs_space = self.observation_space
            self.action_space = self._parent_action_space
            self.observation_space = self._parent_observation_space

            try:
                # Call parent with actions (now including dummy CDO actions)
                obs_result, reward_result, terminated_result, truncated_result, info_result = super().step(actions)
            finally:
                # Restore filtered spaces
                self.action_space = filtered_action_space
                self.observation_space = filtered_obs_space

            # Filter out CDO from results (just in case)
            if self._cdo_keys:
                obs_dict = {k: v for k, v in obs_result.items() if k not in self._cdo_keys}
            else:
                obs_dict = obs_result

            # Convert Array results to Dicts (GymVectorEnv returns arrays, we need dicts for MultiAgentEnv)
            # We assume the order of arrays corresponds to self._valid_keys (sorted)
            if isinstance(reward_result, (np.ndarray, list, tuple)):
                reward_dict = {self._valid_keys[i]: float(reward_result[i]) for i in range(len(self._valid_keys)) if i < len(reward_result)}
            else:
                reward_dict = reward_result

            if isinstance(terminated_result, (np.ndarray, list, tuple)):
                terminated_dict = {self._valid_keys[i]: bool(terminated_result[i]) for i in range(len(self._valid_keys)) if i < len(terminated_result)}
            else:
                terminated_dict = terminated_result

            if isinstance(truncated_result, (np.ndarray, list, tuple)):
                truncated_dict = {self._valid_keys[i]: bool(truncated_result[i]) for i in range(len(self._valid_keys)) if i < len(truncated_result)}
            else:
                truncated_dict = truncated_result

            # Handle Info (GymVectorEnv returns dict of arrays/lists)
            # We need {agent_id: {info_key: info_val}}
            if isinstance(info_result, dict) and not any(k in self._valid_keys for k in info_result.keys()):
                # It's a dict of columns, transpose it
                info_dict = {}
                for i, agent_id in enumerate(self._valid_keys):
                    if i < self.num_envs: # Safety check
                        agent_info = {}
                        for k, v in info_result.items():
                            # Only index if it's a sequence (list, tuple, array)
                            # Dictionaries (KeyError) and strings (split chars) should be treated as global or ignored
                            if isinstance(v, (list, tuple, np.ndarray)) and len(v) > i:
                                agent_info[k] = v[i]
                            # If it's a scalar or dict, maybe it's global? Assign to all agents?
                            # For now, let's just include it as is if it's not a sequence
                            elif not hasattr(v, '__len__') or isinstance(v, (str, dict)):
                                agent_info[k] = v
            else:
                # Already dict of agents or unknown format
                info_dict = info_result
                if self._cdo_keys and isinstance(info_dict, dict):
                     info_dict = {k: v for k, v in info_dict.items() if k not in self._cdo_keys}

            return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict

        
        def batch_obs(self, obs):
            """
            Batch a list of observations.
            """
            # 예제: None 값 처리 및 배칭
            if any(o is None for o in obs):
                valid_obs = next((o for o in obs if o is not None), None)
                if valid_obs is not None:
                    obs = [o if o is not None else valid_obs for o in obs]
                else:
                    print("[SBDAPMMultiAgentEnv] Warning: All observations are None! Using zeros.")
                    dummy = self.single_observation_space.sample()
                    if isinstance(dummy, np.ndarray):
                        dummy.fill(0)
                    obs = [dummy for _ in obs]
            try:
                return super().batch_obs(obs)
            except Exception as e:
                print("[SBDAPMMultiAgentEnv] Error in batch_obs:", e)
                try:
                    import gymnasium.experimental.vector.utils as utils
                    return utils.batch_space(self.single_observation_space, len(obs))
                except Exception:
                    print("[SBDAPMMultiAgentEnv] Critical: Failed to create fallback batch.")
                    raise e





    class SBDAPMScholaEnv(gym.Env):
        """
        Full Schola-integrated environment (v3.0).

        Direct single-agent environment that interfaces with Schola for RLlib training.
        Handles multi-agent Schola environment but exposes single-agent interface.
        """

        def __init__(self, **kwargs):
            # Extract configuration
            host = kwargs.get("host", "localhost")
            port = kwargs.get("port", 50051)
            self.max_episode_steps = kwargs.get("max_episode_steps", 100000)  # Very high - let UE control episode ending

            # Create Schola connection
            connection = UnrealEditorConnection(url=host, port=port)
            # Use SafeUnrealVectorEnv to handle None observations
            self.schola_env = SafeUnrealVectorEnv(unreal_connection=connection, verbosity=1)

            print(f"[DEBUG] Schola Env Num Envs: {self.schola_env.num_envs}")
            print(f"[DEBUG] Schola Env Observation Space Type: {type(self.schola_env.observation_space)}")
            print(f"[DEBUG] Schola Env Action Space Type: {type(self.schola_env.action_space)}")

            # Inspect action space structure
            if isinstance(self.schola_env.action_space, spaces.Dict):
                print(f"[DEBUG] Action space is Dict with keys: {list(self.schola_env.action_space.keys())}")
                first_agent = list(self.schola_env.action_space.keys())[0]
                print(f"[DEBUG] Action space for '{first_agent}': {self.schola_env.action_space[first_agent]}")

            # Check id_manager structure
            if hasattr(self.schola_env, 'id_manager'):
                print(f"[DEBUG] id_manager has {len(self.schola_env.id_manager.id_list)} entries")
                print(f"[DEBUG] id_manager entries: {self.schola_env.id_manager.id_list}")
                # Map flat_id to agent_id
                print(f"[DEBUG] Agent ID mapping:")
                for flat_id, (env_id, agent_id_int) in enumerate(self.schola_env.id_manager.id_list):
                    print(f"[DEBUG]   flat_id {flat_id} → env={env_id}, agent_int={agent_id_int}")

            # Define our single-agent spaces
            self.observation_space = spaces.Box(
                low=-np.inf,
                high=np.inf,
                shape=(78,),  # 71 observation + 7 objective embedding
                dtype=np.float32
            )
            self.action_space = spaces.Box(
                low=np.array([-1.0, -1.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.0], dtype=np.float32),
                high=np.array([1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0], dtype=np.float32),
                shape=(8,),
                dtype=np.float32
            )

            # Agent tracking
            self.agent_ids = None
            self.primary_agent_id = None
            self.action_space_structure = None
            self.episode_steps = 0

            print(f"[SBDAPMScholaEnv] Initialized with host={host}, port={port}")
            print(f"[SBDAPMScholaEnv] Type: {type(self).__name__} (Single-agent Schola wrapper)")

        def _format_action_for_schola(self, action):
            """
            Convert flat 8-dim action to Schola's expected format.

            Handles:
            1. Box((N, 8)) - Schola vector env, reshape (8,) to (N, 8) by tiling
            2. Dict - map flat action to dict keys
            3. Box((8,)) - return as-is
            """
            # Ensure action is numpy array
            if not isinstance(action, np.ndarray):
                action = np.array(action, dtype=np.float32)

            # Handle Box space
            if isinstance(self.action_space_structure, spaces.Box):
                expected_shape = self.action_space_structure.shape
                if len(expected_shape) == 2 and expected_shape[1] == 8:
                    # Vector env expecting (N, 8) matrix
                    # Reshape our (8,) action to (N, 8) by repeating rows
                    num_envs = expected_shape[0]
                    return np.tile(action.reshape(1, -1), (num_envs, 1)).astype(np.float32)
                elif expected_shape == (8,):
                    # Correct shape
                    return action.astype(np.float32)
                else:
                    print(f"[SBDAPMScholaEnv] WARNING: Unexpected action shape {expected_shape}, using as-is")
                    return action.astype(np.float32)

            # Handle nested Dict
            if isinstance(self.action_space_structure, spaces.Dict):
                # If nested Dict, we need to map flat action to dict structure
                # Common structure: {'move': Box(2), 'look': Box(2), 'actions': Box(3)}
                # Our action: [move_x, move_y, speed, look_x, look_y, fire, crouch, ability]
                action_dict = {}
                subspace_keys = list(self.action_space_structure.keys())

                # Try to infer mapping from subspace keys
                idx = 0
                for key in subspace_keys:
                    subspace = self.action_space_structure[key]
                    if isinstance(subspace, spaces.Box):
                        dim = subspace.shape[0] if len(subspace.shape) > 0 else 1
                        if dim == 1:
                            action_dict[key] = np.array([action[idx]], dtype=np.float32)
                            idx += 1
                        else:
                            action_dict[key] = action[idx:idx+dim].astype(np.float32)
                            idx += dim
                    else:
                        # Fallback: use sample
                        action_dict[key] = subspace.sample()

                return action_dict

            # Fallback - return as-is
            return action

        def reset(self, *, seed=None, options=None):
            """Reset environment for all agents."""
            self.episode_steps = 0
            
            try:
                obs_vec, info_vec = self.schola_env.reset(seed=seed, options=options)
                
                if isinstance(obs_vec, dict):
                    raw_agent_ids = list(obs_vec.keys())
                    print(f"[SBDAPMMultiAgentEnv] Raw agents detected: {raw_agent_ids}")
                    
                    # ===== CDO 필터링 (관찰용) =====
                    filtered_agent_ids = []
                    
                    for aid in raw_agent_ids:
                        # CDO 제외
                        if aid == "ScholaAgentComponent":
                            print(f"[SBDAPMMultiAgentEnv] Filtering CDO from observations: {aid}")
                            continue
                        
                        # Default/Archetype 제외
                        if "Default" in aid or "Archetype" in aid:
                            print(f"[SBDAPMMultiAgentEnv] Filtering archetype: {aid}")
                            continue
                        
                        # 숫자 없는 컴포넌트 제외
                        if not any(char.isdigit() for char in aid):
                            print(f"[SBDAPMMultiAgentEnv] Filtering non-instance: {aid}")
                            continue
                        
                        filtered_agent_ids.append(aid)
                    
                    # 정렬 및 개수 검증
                    filtered_agent_ids = sorted(filtered_agent_ids)
                    
                    print(f"[SBDAPMMultiAgentEnv] Filtered agents: {filtered_agent_ids}")
                    print(f"[SBDAPMMultiAgentEnv] Agent count: {len(filtered_agent_ids)}")
                    
                    # 예상 개수 검증
                    EXPECTED_AGENT_COUNT = 4
                    if len(filtered_agent_ids) != EXPECTED_AGENT_COUNT:
                        print(f"[SBDAPMMultiAgentEnv] WARNING: Expected {EXPECTED_AGENT_COUNT}, "
                            f"got {len(filtered_agent_ids)}")
                    
                    self._agent_ids = set(filtered_agent_ids)
                    self._agent_id_list = filtered_agent_ids
                    
                    # Extract observations (CDO 제외)
                    obs_dict = {}
                    for agent_id in self._agent_ids:
                        if agent_id not in obs_vec:
                            print(f"[SBDAPMMultiAgentEnv] Error: Missing obs for {agent_id}")
                            obs_dict[agent_id] = np.zeros(78, dtype=np.float32)
                            continue
                        
                        obs = obs_vec[agent_id]
                        
                        # Handle batched obs
                        if hasattr(obs, 'shape') and len(obs.shape) == 2:
                            obs = obs[0]
                        
                        obs_dict[agent_id] = obs.astype(np.float32)
                    
                    # Extract info (CDO 제외)
                    info_dict = {}
                    if isinstance(info_vec, dict):
                        info_dict = {aid: info_vec.get(aid, {}) for aid in self._agent_ids}
                    else:
                        info_dict = {aid: {} for aid in self._agent_ids}
                    
                    return obs_dict, info_dict
                    
                else:
                    print(f"[SBDAPMMultiAgentEnv] ERROR: Expected dict, got {type(obs_vec)}")
                    return {}, {}
                    
            except Exception as e:
                print(f"[SBDAPMMultiAgentEnv] Error during reset: {e}")
                import traceback
                traceback.print_exc()
                return {}, {}

        def step(self, action):
            """Execute action for first agent and return result."""
            try:
                # Ensure action is numpy array
                if not isinstance(action, np.ndarray):
                    action = np.array(action, dtype=np.float32)

                # Format action according to Schola's expected structure
                formatted_action = self._format_action_for_schola(action)

                # Handle Dict action space - create action dict with all agent IDs
                if isinstance(self.schola_env.action_space, spaces.Dict) and self.agent_ids:
                    # Create action dict with all agent IDs using formatted action
                    action_vec = {agent_id: formatted_action.copy() if isinstance(formatted_action, np.ndarray)
                                  else formatted_action for agent_id in self.agent_ids}
                else:
                    # Fallback: use formatted action directly
                    action_vec = formatted_action

                # Call vectorized step
                obs_vec, reward_vec, terminated_vec, truncated_vec, info_vec = self.schola_env.step(action_vec)

                # Extract first agent's data
                if obs_vec is None:
                    obs = np.zeros(78, dtype=np.float32)
                elif isinstance(obs_vec, dict):
                    # Dict space: extract using primary agent ID
                    if self.primary_agent_id and self.primary_agent_id in obs_vec:
                        obs = obs_vec[self.primary_agent_id]
                    else:
                        print(f"[SBDAPMScholaEnv] Warning: Agent ID {self.primary_agent_id} not found in obs_vec keys: {list(obs_vec.keys())}")
                        obs = np.zeros(78, dtype=np.float32)
                elif isinstance(obs_vec, (list, tuple, np.ndarray)) and len(obs_vec) > 0:
                    obs = obs_vec[0]
                else:
                    obs = np.zeros(78, dtype=np.float32)

                # Extract reward/terminated/truncated
                if isinstance(reward_vec, dict) and self.primary_agent_id:
                    reward = float(reward_vec.get(self.primary_agent_id, 0.0))
                elif reward_vec is not None and hasattr(reward_vec, '__len__') and len(reward_vec) > 0:
                    reward = float(reward_vec[0])
                else:
                    reward = 0.0

                if isinstance(terminated_vec, dict) and self.primary_agent_id:
                    terminated = bool(terminated_vec.get(self.primary_agent_id, False))
                elif terminated_vec is not None and hasattr(terminated_vec, '__len__') and len(terminated_vec) > 0:
                    terminated = bool(terminated_vec[0])
                else:
                    terminated = False

                if isinstance(truncated_vec, dict) and self.primary_agent_id:
                    truncated = bool(truncated_vec.get(self.primary_agent_id, False))
                elif truncated_vec is not None and hasattr(truncated_vec, '__len__') and len(truncated_vec) > 0:
                    truncated = bool(truncated_vec[0])
                else:
                    truncated = False

                # Extract first agent's info
                info = {}
                if info_vec is not None:
                    if isinstance(info_vec, dict):
                        # Try to extract for primary agent ID first
                        if self.primary_agent_id and self.primary_agent_id in info_vec:
                            info = info_vec[self.primary_agent_id]
                        else:
                            # Fallback: extract first entry
                            info = {k: v[0] if isinstance(v, (list, np.ndarray)) and len(v) > 0 else v
                                    for k, v in info_vec.items()}
                    elif isinstance(info_vec, (list, tuple)) and len(info_vec) > 0:
                        info = info_vec[0] if info_vec[0] is not None else {}

            except Exception as e:
                print(f"[SBDAPMScholaEnv] Error during step: {e}")
                import traceback
                traceback.print_exc()
                obs = np.zeros(78, dtype=np.float32)
                reward = 0.0
                terminated = True
                truncated = False
                info = {}

            self.episode_steps += 1

            # Enforce max episode length
            if self.episode_steps >= self.max_episode_steps:
                truncated = True

            # Validate observation shape
            if hasattr(obs, 'shape') and obs.shape != (78,):
                # print(f"[SBDAPMScholaEnv] Warning: Expected obs shape (78,), got {obs.shape}")
                # Handle 2D observations (batch dimension) - take first observation
                if len(obs.shape) == 2 and obs.shape[-1] == 78:
                    # print(f"[SBDAPMScholaEnv] Extracting first observation from batch")
                    obs = obs[0]
                elif obs.size == 78:
                    obs = obs.reshape(78)
                else:
                    print(f"[SBDAPMScholaEnv] Cannot reshape, using zeros. obs.size={obs.size}")
                    obs = np.zeros(78, dtype=np.float32)
            elif not hasattr(obs, 'shape'):
                 obs = np.zeros(78, dtype=np.float32)

            return obs, reward, terminated, truncated, info

        def render(self):
            """Rendering is handled by UE."""
            return self.schola_env.render() if hasattr(self.schola_env, 'render') else None

        def close(self):
            """Close Schola connection."""
            if hasattr(self.schola_env, 'close'):
                self.schola_env.close()


    class SBDAPMMultiAgentEnv(MultiAgentEnv):
        """
        Multi-Agent RLlib Environment for SBDAPM (v3.1)

        Proper multi-agent environment where each of the 4 follower agents:
        - Receives independent observations
        - Executes independent actions (from shared policy)
        - Collects independent rewards

        This fixes the bug where all agents were receiving identical actions.
        """

        def __init__(self, **kwargs):
            super().__init__()

            # Extract configuration
            host = kwargs.get("host", "localhost")

            # Handle dynamic port assignment for multi-worker setups
            base_port = kwargs.get("base_port", None)
            if base_port is not None:
                # Multi-worker mode: calculate port from worker_index
                try:
                    import ray
                    worker = ray.get_runtime_context()
                    worker_index = getattr(worker, 'worker_index', 0)
                    port = base_port + worker_index
                    print(f"[SBDAPMMultiAgentEnv] Worker {worker_index} connecting to port {port}")
                except:
                    # Fallback: use base_port as-is
                    port = base_port
                    print(f"[SBDAPMMultiAgentEnv] Using base_port {port} (no worker context)")
            else:
                # Single-worker mode: use explicit port
                port = kwargs.get("port", 50051)
                print(f"[SBDAPMMultiAgentEnv] Using explicit port {port}")

            self.max_episode_steps = kwargs.get("max_episode_steps", 100000)

            # Create Schola connection with CDO workaround
            from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
            connection = UnrealEditorConnection(url=host, port=port)
            self.schola_env = SafeUnrealVectorEnv(unreal_connection=connection, verbosity=1)

            # Define per-agent spaces
            self._obs_space = spaces.Box(
                low=-np.inf,
                high=np.inf,
                shape=(78,),
                dtype=np.float32
            )
            self._action_space = spaces.Box(
                low=np.array([-1.0, -1.0, 0.0, -1.0, -1.0, 0.0, 0.0, 0.0], dtype=np.float32),
                high=np.array([1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0], dtype=np.float32),
                shape=(8,),
                dtype=np.float32
            )

            # Agent tracking
            self._agent_ids = set()
            self._agent_id_list = []  # Ordered list for consistent action ordering
            self.episode_steps = 0

            print(f"[SBDAPMMultiAgentEnv] Initialized with UnrealEnv (non-vectorized)")
            print(f"[DEBUG] Schola action_space type: {type(self.schola_env.action_space)}")
            print(f"[DEBUG] Schola observation_space type: {type(self.schola_env.observation_space)}")



        @property
        def observation_space(self):
            """Return observation space for a single agent."""
            return self._obs_space

        @property
        def action_space(self):
            """Return action space for a single agent."""
            return self._action_space

        def reset(self, *, seed=None, options=None):
            """
            Reset environment for all agents.

            Returns:
                obs_dict: {agent_id: observation} for all agents
                info_dict: {agent_id: info} for all agents
            """
            self.episode_steps = 0

            try:
                # Get observations from Schola UnrealEnv (returns dict directly)
                obs_vec, info_vec = self.schola_env.reset(seed=seed, options=options)

                # Extract agent IDs from observations
                if isinstance(obs_vec, dict):
                    raw_agent_ids = list(obs_vec.keys())
                    print(f"[SBDAPMMultiAgentEnv] Raw agents detected: {raw_agent_ids}")

                    # Filter out CDO (Class Default Object) and invalid entries
                    filtered_agent_ids = []
                    for aid in raw_agent_ids:
                        if aid == "ScholaAgentComponent" or "Default" in aid or "Archetype" in aid:
                            print(f"[SBDAPMMultiAgentEnv] Filtering CDO/template: {aid}")
                            continue
                        if not any(char.isdigit() for char in aid):
                            print(f"[SBDAPMMultiAgentEnv] Filtering non-instance: {aid}")
                            continue
                        filtered_agent_ids.append(aid)

                    # Sort for consistency
                    filtered_agent_ids = sorted(filtered_agent_ids)
                    self._agent_ids = set(filtered_agent_ids)
                    self._agent_id_list = filtered_agent_ids

                    print(f"[SBDAPMMultiAgentEnv] Filtered agents: {self._agent_id_list}")
                    print(f"[SBDAPMMultiAgentEnv] Agent count: {len(self._agent_ids)}")

                    # Extract observations
                    obs_dict = {}
                    for agent_id in self._agent_ids:
                        if agent_id not in obs_vec:
                            print(f"[SBDAPMMultiAgentEnv] Error: Missing obs for {agent_id}")
                            obs_dict[agent_id] = np.zeros(78, dtype=np.float32)
                            continue

                        obs = obs_vec[agent_id]
                        # Handle batched obs (shouldn't happen with GymEnv, but be safe)
                        if hasattr(obs, 'shape') and len(obs.shape) == 2:
                            obs = obs[0]
                        obs_dict[agent_id] = obs.astype(np.float32)

                    # Extract info
                    info_dict = {}
                    if isinstance(info_vec, dict):
                        info_dict = {aid: info_vec.get(aid, {}) for aid in self._agent_ids}
                    else:
                        info_dict = {aid: {} for aid in self._agent_ids}

                    return obs_dict, info_dict

                else:
                    print(f"[SBDAPMMultiAgentEnv] ERROR: Expected dict, got {type(obs_vec)}")
                    return {}, {}

            except Exception as e:
                print(f"[SBDAPMMultiAgentEnv] Error during reset: {e}")
                import traceback
                traceback.print_exc()
                return {}, {}

        def step(self, action_dict):
            """
            Execute actions for all agents.
            
            Args:
                action_dict: {agent_id: action} where action is (8,) numpy array
            
            Returns:
                obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict
            """
            try:
                # Validate action_dict
                if not isinstance(action_dict, dict):
                    print(f"[SBDAPMMultiAgentEnv] ERROR: Expected action dict, got {type(action_dict)}")
                    action_dict = {agent_id: np.zeros(8, dtype=np.float32) for agent_id in self._agent_ids}
                
                received_agents = set(action_dict.keys())
                expected_agents = self._agent_ids
                missing_agents = expected_agents - received_agents
                extra_agents = received_agents - expected_agents
                
                if missing_agents:
                    print(f"[SBDAPMMultiAgentEnv] Warning: Missing actions for {len(missing_agents)} agents")
                if extra_agents:
                    print(f"[SBDAPMMultiAgentEnv] Warning: Extra actions for {len(extra_agents)} unknown agents")
                
                # Build action dict for valid agents only (CDO filtered in reset())
                formatted_actions = {}
                num_envs = self.schola_env.num_envs

                print(f"[SBDAPMMultiAgentEnv.step] num_envs={num_envs}, action_dict keys={list(action_dict.keys())}")
                print(f"[SBDAPMMultiAgentEnv.step] _agent_id_list (sorted)={self._agent_id_list}")

                # Build batched actions for VectorEnv (num_envs, 8)
                # CRITICAL: Only place action at the agent's own env_idx, use zeros elsewhere
                # VectorEnv will dispatch each row to the corresponding agent index
                for env_idx, agent_id in enumerate(self._agent_id_list):
                    if agent_id in action_dict:
                        action = action_dict[agent_id]
                        if not isinstance(action, np.ndarray):
                            action = np.array(action, dtype=np.float32)
                        if action.shape != (8,):
                            print(f"[SBDAPMMultiAgentEnv] Warning: Invalid shape for {agent_id}: {action.shape}")
                            action = np.zeros(8, dtype=np.float32)

                        # Create (num_envs, 8) batch - standard VectorEnv format
                        batched_action = np.zeros((num_envs, 8), dtype=np.float32)
                        batched_action[env_idx] = action
                        formatted_actions[agent_id] = batched_action

                        print(f"[SBDAPMMultiAgentEnv.step] Agent {agent_id} (env_idx={env_idx}): action={action[:3]}... placed at row {env_idx}")
                    else:
                        print(f"[SBDAPMMultiAgentEnv] Warning: Missing action for {agent_id}, using zeros")
                        formatted_actions[agent_id] = np.zeros((num_envs, 8), dtype=np.float32)

                # Call UnrealEnv step (dict in, dict out)
                obs_vec, reward_vec, terminated_vec, truncated_vec, info_vec = self.schola_env.step(formatted_actions)

                # Extract observations for valid agents (CDO already filtered)
                obs_dict = {}
                for agent_id in self._agent_ids:
                    if agent_id in obs_vec:
                        obs = obs_vec[agent_id]
                        if hasattr(obs, 'shape') and len(obs.shape) == 2:
                            obs = obs[0]
                        obs_dict[agent_id] = obs.astype(np.float32)
                    else:
                        obs_dict[agent_id] = np.zeros(78, dtype=np.float32)

                # Extract rewards
                reward_dict = {agent_id: float(reward_vec.get(agent_id, 0.0)) for agent_id in self._agent_ids}

                # Extract terminated
                terminated_dict = {agent_id: bool(terminated_vec.get(agent_id, False)) for agent_id in self._agent_ids}

                # Extract truncated
                truncated_dict = {agent_id: bool(truncated_vec.get(agent_id, False)) for agent_id in self._agent_ids}

                # Extract info
                info_dict = {agent_id: info_vec.get(agent_id, {}) for agent_id in self._agent_ids}
                
                self.episode_steps += 1
                
                # Enforce max episode length
                if self.episode_steps >= self.max_episode_steps:
                    truncated_dict = {agent_id: True for agent_id in self._agent_ids}
                
                # RLlib expects "__all__" key for global termination
                terminated_dict["__all__"] = all(terminated_dict.values())
                truncated_dict["__all__"] = all(truncated_dict.values())
                
                return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict
            
            except Exception as e:
                print(f"[SBDAPMMultiAgentEnv] Error during step: {e}")
                import traceback
                traceback.print_exc()
                
                # Return error state
                obs_dict = {agent_id: np.zeros(78, dtype=np.float32) for agent_id in self._agent_ids}
                reward_dict = {agent_id: 0.0 for agent_id in self._agent_ids}
                terminated_dict = {agent_id: True for agent_id in self._agent_ids}
                terminated_dict["__all__"] = True
                truncated_dict = {agent_id: False for agent_id in self._agent_ids}
                truncated_dict["__all__"] = False
                info_dict = {agent_id: {} for agent_id in self._agent_ids}
                
                return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict


        def render(self):
            """Rendering is handled by UE."""
            return self.schola_env.render() if hasattr(self.schola_env, 'render') else None

        def close(self):
            """Close Schola connection."""
            if hasattr(self.schola_env, 'close'):
                self.schola_env.close()
