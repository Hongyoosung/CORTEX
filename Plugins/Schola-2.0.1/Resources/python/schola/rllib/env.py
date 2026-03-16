# Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.

"""
RLlib Environment Implementations for Schola/Unreal Engine.

This module provides two environment classes for interfacing Unreal Engine with RLlib:

1. RayEnv: Single-environment implementation (inherits from BaseRayEnv, MultiAgentEnv)
   - Automatically selected when num_envs == 1
   - Returns MultiAgentDict format
   - Compatible with gymnasium wrappers
   - Validates that only one environment is created

2. RayVecEnv: Vectorized multi-environment implementation (inherits from BaseRayEnv, VectorMultiAgentEnv)
   - Automatically selected when num_envs > 1
   - Returns List[MultiAgentDict] format
   - NOT compatible with gymnasium wrappers
   - Supports multiple parallel environments

Both classes inherit from BaseRayEnv, which provides shared functionality for
protocol/simulator management, space initialization, and common properties.

Use make_ray_env() factory function to automatically select the appropriate class
based on the number of environments from the protocol.
"""

from abc import ABC, abstractmethod
from collections import defaultdict
from copy import deepcopy
from typing import Any, Iterable, List, Optional, Tuple, Dict, Union
import logging

import gymnasium as gym
import numpy as np
from schola.core.error_manager import EnvironmentException, NoAgentsException, NoEnvironmentsException
from schola.core.protocols.base import BaseRLProtocol
from schola.core.simulators.base import BaseSimulator, UnsupportedProtocolException
from gymnasium.vector.vector_env import AutoresetMode
from ray.rllib.env.multi_agent_env import MultiAgentEnv
from ray.rllib.utils.annotations import PublicAPI
from schola.core.utils.id_manager import IdManager

logger = logging.getLogger(__name__)


def sorted_multi_agent_space(multi_agent_space: Dict[int, gym.spaces.Dict]) -> gym.spaces.Dict:
    """
    Sorts the spaces in a multi-agent space alphabetically by agent ID.

    Parameters
    ----------
    multi_agent_space : Dict[int,gym.spaces.Dict]
        The multi-agent space to sort.

    Returns
    -------
    gym.spaces.Dict
        The sorted multi-agent space.
    """
    output_space = gym.spaces.Dict()
    for agent_id, original_space in multi_agent_space.items():
        sorted_space = gym.spaces.Dict()
        for key in sorted(original_space):
            sorted_space[key] = original_space[key]
        output_space[agent_id] = sorted_space
    return output_space

from ray.rllib.env.vector.vector_multi_agent_env import VectorMultiAgentEnv


class BaseRayEnv(ABC):
    """
    Abstract base class for Schola RLlib environments.
    
    Provides shared functionality for protocol/simulator management, space
    initialization, and common properties. Subclasses (RayEnv, RayVecEnv)
    must implement reset(), step(), and _init_agent_tracking().
    
    This class does NOT inherit from any RLlib environment classes. Subclasses
    use multiple inheritance to combine BaseRayEnv with their specific RLlib
    parent (MultiAgentEnv or VectorMultiAgentEnv).
    
    Shared Attributes:
        protocol: Communication protocol with Unreal Engine
        simulator: Simulator instance managing Unreal processes
        id_manager: Manages environment and agent IDs
        possible_agents: All agents that can exist (static after init)
        num_envs: Number of parallel environments
        metadata: Environment metadata (autoreset_mode, etc.)
        _observation_space, _action_space: Gymnasium spaces
        _single_observation_space, _single_action_space: Per-agent spaces
        _single_observation_spaces, _single_action_spaces: Dict of agent spaces
    """
    
    def __init__(self, protocol: BaseRLProtocol, simulator: BaseSimulator, verbosity: int = 0):
        """Initialize protocol, simulator, and shared environment infrastructure."""
        # 1. Protocol and simulator setup
        self.protocol = protocol
        self.simulator = simulator
        if not isinstance(self.protocol, self.simulator.supported_protocols):
            raise UnsupportedProtocolException(
                f"Protocol {self.protocol} is not supported by the simulator {self.simulator}."
            )
        
        # Start protocol/simulator if not already started (handles factory function reuse case)
        # Protocol has __bool__() to check if started; simulator.start() should be idempotent
        if not self.protocol:
            self.protocol.start()
        # Always call simulator.start() - implementations should handle multiple calls gracefully
        # (checking simulator.__bool__() is unreliable as it may be abstract)
        self.simulator.start(self.protocol.properties)
        
        # 2. Initialize space attributes
        self._init_space_attributes()
        
        # 3. Send startup message with autoreset
        # Note: This may be called twice if protocol was already started (e.g., from factory function)
        # Protocol implementations should handle duplicate startup messages gracefully
        self.protocol.send_startup_msg(auto_reset_type=AutoresetMode.NEXT_STEP)
        
        # 4. Define environment (calls subclass _define_environment)
        self._define_environment()
        
        # 6. Initialize agent tracking (subclass-specific)
        self._init_agent_tracking()
    
    def _init_space_attributes(self):
        """Initialize observation and action space attributes to None."""
        self._observation_space: gym.Space | None = None
        self._action_space: gym.Space | None = None
        self._single_observation_space: gym.Space | None = None
        self._single_action_space: gym.Space | None = None
        self._single_observation_spaces: Dict[str, gym.Space] | None = None
        self._single_action_spaces: Dict[str, gym.Space] | None = None
    
    @abstractmethod
    def _init_agent_tracking(self):
        """
        Initialize agent tracking structures.
        
        Must be implemented by subclasses:
        - RayEnv: self._terminated_agents, self._truncated_agents (sets)
        - RayVecEnv: Creates self.envs list of _SingleEnvWrapper instances
        """
        pass
    
    @abstractmethod
    def _define_environment(self):
        """
        Define environment spaces and agent structure.
        
        Must be implemented by subclasses to:
        1. Get definition from protocol
        2. Create IdManager
        3. Initialize possible_agents
        4. Call _build_spaces()
        5. Call _validate_environments()
        6. Set num_envs
        """
        pass
    
    def _build_spaces(self, obs_defns: Dict, action_defns: Dict, first_env_id: int):
        """
        Build observation and action spaces from protocol definitions.
        
        Creates Dict spaces mapping agent IDs to their individual spaces.
        
        Args:
            obs_defns: Observation space definitions from protocol
            action_defns: Action space definitions from protocol
            first_env_id: ID of first environment to use for space extraction
        """
        # Build single observation/action spaces as dicts of agent_id -> space
        self._single_observation_spaces = {}
        self._single_action_spaces = {}
        
        for agent_id in obs_defns[first_env_id]:
            self._single_observation_spaces[agent_id] = obs_defns[first_env_id][agent_id]
            self._single_action_spaces[agent_id] = action_defns[first_env_id][agent_id]
        
        # Create the Dict spaces for RLlib
        self._single_observation_space = gym.spaces.Dict(self._single_observation_spaces)
        self._single_action_space = gym.spaces.Dict(self._single_action_spaces)
        
        # For backwards compatibility, set observation_space and action_space
        self._observation_space = self._single_observation_space
        self._action_space = self._single_action_space
    
    def _validate_environments(self, ids: List[List[str]]):
        """
        Validate that environments and agents are properly configured.
        
        Args:
            ids: List of agent ID lists (one per environment)
            
        Raises:
            NoEnvironmentsException: If no environments provided
            NoAgentsException: If any environment has no agents
        """
        try:
            if len(ids) == 0:
                raise NoEnvironmentsException()
            
            for env_id, agent_id_list in enumerate(ids):
                if len(agent_id_list) == 0:
                    raise NoAgentsException(env_id)
        
        except Exception as e:
            self.protocol.close()
            self.simulator.stop()
            raise e
    
    def close_extras(self, **kwargs):
        """Close protocol and stop simulator."""
        self.protocol.close()
        self.simulator.stop()
    
    # ===== Abstract Methods (must be implemented by subclasses) =====
    
    @abstractmethod
    def reset(self, **kwargs):
        """
        Reset environment(s).
        
        Signature differs by subclass:
        - RayEnv: reset(*, seed: Optional[int], ...) -> Tuple[Dict, Dict]
        - RayVecEnv: reset(*, seed: Optional[Union[int, List[int]]], ...) -> Tuple[List[Dict], List[Dict]]
        """
        pass
    
    @abstractmethod
    def step(self, actions):
        """
        Step environment(s) with actions.
        
        Signature differs by subclass:
        - RayEnv: step(actions: Dict) -> Tuple[Dict, Dict[str, float], ...]
        - RayVecEnv: step(actions: List[Dict]) -> Tuple[List[Dict], List[Dict[str, float]], ...]
        """
        pass
    
    # ===== Shared Properties (100% identical) =====
    
    @property
    def num_agents(self) -> int:
        """Total number of possible agents (ever seen)."""
        return len(self.possible_agents)
    
    @property
    def max_num_agents(self) -> int:
        """Maximum number of agents that can exist."""
        return len(self.possible_agents)
    
    @property
    def observation_space(self) -> Optional[gym.Space]:
        """Observation space (Dict of agent spaces)."""
        return self._observation_space
    
    @property
    def action_space(self) -> Optional[gym.Space]:
        """Action space (Dict of agent spaces)."""
        return self._action_space
    
    @property
    def single_observation_space(self) -> Optional[gym.Space]:
        """Single-agent observation space."""
        return self._single_observation_space
    
    @property
    def single_action_space(self) -> Optional[gym.Space]:
        """Single-agent action space."""
        return self._single_action_space
    
    @property
    def single_observation_spaces(self) -> Dict[str, gym.Space]:
        """Dict mapping agent IDs to observation spaces."""
        return self._single_observation_spaces
    
    @property
    def single_action_spaces(self) -> Dict[str, gym.Space]:
        """Dict mapping agent IDs to action spaces."""
        return self._single_action_spaces


class RayEnv(BaseRayEnv, MultiAgentEnv):
    """
    Schola's single-environment implementation of MultiAgentEnv for Unreal Engine.
    
    This class manages a single multi-agent environment communicating with Unreal Engine
    via a protocol/simulator architecture. It is compatible with gymnasium wrappers and
    always returns dict format (MultiAgentDict).
    
    Inherits from:
        BaseRayEnv: Shared protocol, simulator, and space management
        MultiAgentEnv: RLlib's single-environment multi-agent interface
    
    Use this class when:
    - Running with local runner (num_env_runners=0)
    - Only one parallel environment is needed
    - Gymnasium wrappers need to be applied
    
    Key Features:
    - Single Unreal environment (num_envs must equal 1)
    - Multi-agent support
    - Protocol-based communication with Unreal Engine
    - Compatible with gymnasium wrappers (inherits from MultiAgentEnv -> gym.Env)
    - Always returns MultiAgentDict format
    
    Note:
        This class will raise an error if num_envs > 1. For multiple parallel environments,
        use RayVecEnv instead.
    """

    def __init__(self, protocol: BaseRLProtocol, simulator: BaseSimulator, verbosity: int = 0):
        # Initialize shared base class functionality
        BaseRayEnv.__init__(self, protocol, simulator, verbosity)
        
        # Initialize MultiAgentEnv (required for gym.Env compatibility)
        MultiAgentEnv.__init__(self)
    
    def _init_agent_tracking(self):
        """Initialize single-environment agent tracking structures."""
        self._terminated_agents: set = set()
        self._truncated_agents: set = set()

    def _define_environment(self):
        """Define environment spaces and validate single environment constraint."""
        ids, agent_types, obs_defns, action_defns = self.protocol.get_definition()
        
        from itertools import chain
        self.id_manager = IdManager(ids)
        self._env_id, self._agent_id = self.id_manager[0]
        # Validate single environment constraint (RayEnv-specific)
        if self.id_manager.num_envs != 1:
            raise AssertionError(
                f"Expected Environment to be non-vectorized but found {self.id_manager.num_envs} environments. Use RayVecEnv for multiple environments."
            )
        
        self.num_envs = 1
        self.possible_agents = list(set(chain.from_iterable(ids)))  # All agents that can ever exist
        self._current_agents = self.possible_agents.copy()  # Agents currently alive
        # Initialize agents attribute (will be updated dynamically in reset/step)
        self.agents = []
        
        # Use base class methods for space building
        first_env_id, first_agent_id = self.id_manager[0]
        self._build_spaces(obs_defns, action_defns, first_env_id)
        self._validate_environments(ids)

    def reset(
        self, *, seed: Optional[int] = None, options: Optional[Dict[str, Any]] = None
    ) -> Tuple[Dict[str, Any], Dict[str, Any]]:
        """
        Reset the environment.
        
        Args:
            seed: Random seed (int)
            options: Optional reset options
            
        Returns:
            Tuple of (observations, infos) as MultiAgentDict format.
        """
        MultiAgentEnv.reset(self, seed=seed, options=options)
        seed_list = [seed] if seed is not None else None
        option_list = [options] if options is not None else None
        observations, infos = self.protocol.send_reset_msg(seeds=seed_list, options=option_list)

        # Update agent tracking based on what Unreal returned (env_id is always 0)
        
        agents_in_obs = set(observations[self._env_id].keys())
        self._current_agents = agents_in_obs
        # Update agents attribute to match current active agents
        self.agents = list(agents_in_obs)
        logger.debug(f"RayEnv reset with agents: {agents_in_obs}")
        
        # Reset terminated and truncated agent tracking
        self._terminated_agents = set()
        self._truncated_agents = set()

        # Return dict format (env_id is always 0 for single environment)
        logger.debug(f"RayEnv.reset() returning MultiAgentDict")
        return observations[self._env_id], infos[self._env_id]

    def step(
        self, actions: Dict[str, Any]
    ) -> Tuple[Dict[str, Any], Dict[str, float], Dict[str, bool], Dict[str, bool], Dict[str, Any]]:
        """
        Step the environment with the given actions.
        
        Args:
            actions: Action dict (MultiAgentDict {agent_id: action})
            
        Returns:
            Tuple of (observations, rewards, terminateds, truncateds, infos) as MultiAgentDict format.
        """
        # Convert actions to dict format expected by protocol (env_id: actions)
        action_dict = {self._env_id: actions}
        
        # Send action and get response with no autoreset support
        observations, rewards, terminateds, truncateds, infos, _, _ = \
            self.protocol.send_action_msg(action_dict, self._single_action_spaces)
     
        # Normal step - update agent tracking
        agents_in_terminateds = set(terminateds[self._env_id].keys())
        agents_in_truncateds = set(truncateds[self._env_id].keys())
        all_agents_this_step = agents_in_terminateds | agents_in_truncateds
        
        # Track terminated/truncated agents
        for agent_id in all_agents_this_step:
            if agent_id in terminateds[self._env_id] and terminateds[self._env_id][agent_id]:
                self._terminated_agents.add(agent_id)
            if agent_id in truncateds[self._env_id] and truncateds[self._env_id][agent_id]:
                self._truncated_agents.add(agent_id)
        
        # Update current agents (remove terminated/truncated)
        current_active_agents = set()
        for agent_id in all_agents_this_step:
            is_terminated = agent_id in terminateds[self._env_id] and terminateds[self._env_id][agent_id]
            is_truncated = agent_id in truncateds[self._env_id] and truncateds[self._env_id][agent_id]
            if not (is_terminated or is_truncated):
                current_active_agents.add(agent_id)
        
        self._current_agents = current_active_agents
        # Update agents attribute to match current active agents
        self.agents = list(current_active_agents) if current_active_agents else list(self.possible_agents)
        
        # Compute __all__ flag
        agents_in_this_env = self._current_agents | self._terminated_agents | self._truncated_agents
        num_done = len(self._terminated_agents | self._truncated_agents)
        num_total = len(agents_in_this_env)
        
        terminateds[self._env_id]["__all__"] = (num_done == num_total) if num_total > 0 else False
        truncateds[self._env_id]["__all__"] = (len(self._truncated_agents) == num_total) if num_total > 0 else False

        # Return dict format (env_id is always 0)
        logger.debug(f"RayEnv.step() returning MultiAgentDict")
        return observations[self._env_id], rewards[self._env_id], terminateds[self._env_id], truncateds[self._env_id], infos[self._env_id]
    


class _SingleEnvWrapper(MultiAgentEnv):
    """
    Internal wrapper that exposes a single environment ID as a MultiAgentEnv.
    
    This is used by RayVecEnv to create a list of MultiAgentEnv instances,
    matching RLlib's SyncVectorMultiAgentEnv pattern.
    """
    
    def __init__(self, env_id: int, protocol: BaseRLProtocol, simulator: BaseSimulator,
                 single_observation_spaces: Dict[str, gym.Space],
                 single_action_spaces: Dict[str, gym.Space],
                 possible_agents: List[str],
                 parent_vec_env: 'RayVecEnv'):
        # Initialize agent tracking BEFORE calling super().__init__()
        # because the parent class checks self.agents property which depends on _current_agents
        self._current_agents = set()
        self._terminated_agents = set()
        self._truncated_agents = set()
        
        self.env_id = env_id
        self.protocol = protocol
        self.simulator = simulator
        self._single_observation_spaces = single_observation_spaces
        self._single_action_spaces = single_action_spaces
        self.possible_agents = list(possible_agents)  # Convert set to list to match MultiAgentEnv type
        self.parent_vec_env = parent_vec_env
        # Set spaces
        self.observation_spaces = self._single_observation_spaces
        self.action_spaces = self._single_action_spaces
        self._single_observation_space = gym.spaces.Dict(self._single_observation_spaces)
        self._single_action_space = gym.spaces.Dict(self._single_action_spaces)
        self.observation_space = self._single_observation_space
        self.action_space = self._single_action_space
        self._reset_on_next_step = False
        
        super().__init__()
    
    def reset(self, *, seed: Optional[int] = None, options: Optional[Dict[str, Any]] = None):
        """Reset is handled by parent RayVecEnv - this shouldn't be called directly."""
        raise NotImplementedError("Single environment reset should be handled by RayVecEnv")

    def _reset(self, observations: Dict[str, Any]):
        """Inverse of reset To be called from RayVecEnv."""
        self._current_agents = set(observations.keys())
        self._terminated_agents = set()
        self._truncated_agents = set()
        self._reset_on_next_step = False

    def step(self, actions: Dict[str, Any]):
        """Step is handled by parent RayVecEnv - this shouldn't be called directly."""
        raise NotImplementedError("Single environment step should be handled by RayVecEnv")
    
    def _step(self, observations: Dict[str, Any], terminateds: Dict[str, bool], truncateds: Dict[str, bool]):
        """Inverse of step To be called from RayVecEnv."""
        if self._reset_on_next_step:
            self._reset(observations)
        else:
            observed_agents = set(observations.keys())
            self._terminated_agents = self._terminated_agents | set(filter(lambda x: terminateds[x], terminateds.keys()))
            self._truncated_agents = self._truncated_agents | set(filter(lambda x: truncateds[x], truncateds.keys()))
            self._current_agents = (self._current_agents | observed_agents) - (self._terminated_agents | self._truncated_agents)
    
    @property
    def agents(self) -> List[str]:
        return list(self._current_agents)
    
    @agents.setter
    def agents(self, value: List[str]):
        """Setter for agents to support parent class initialization."""
        self._current_agents = set(value)


class RayVecEnv(BaseRayEnv, VectorMultiAgentEnv):
    """
    Schola's vectorized implementation of VectorMultiAgentEnv for Unreal Engine.
    
    This class manages multiple parallel multi-agent environments communicating with Unreal Engine
    via a protocol/simulator architecture. It follows RLlib's SyncVectorMultiAgentEnv pattern
    by maintaining a list of MultiAgentEnv instances in self.envs.
    
    Inherits from:
        BaseRayEnv: Shared protocol, simulator, and space management
        VectorMultiAgentEnv: RLlib's vectorized multi-agent interface
    
    Note: Does NOT inherit from MultiAgentEnv - only uses MultiAgentEnv
    instances via _SingleEnvWrapper in self.envs list.
    
    Use this class when:
    - Running with remote runners (num_env_runners >= 1)
    - Multiple parallel environments are needed
    - Maximum training throughput is desired
    
    Key Features:
    - Supports multiple parallel Unreal environments (num_envs >= 1)
    - Multi-agent support within each environment
    - Automatic episode reset (autoreset_mode="next_step")
    - Protocol-based communication with Unreal Engine
    - Always returns List[MultiAgentDict] format
    - Follows RLlib's VectorMultiAgentEnv pattern with self.envs list
    
    Note:
        This class cannot be wrapped with gymnasium wrappers (they require gymnasium.Env).
        For single environment with wrapper support, use RayEnv instead.
    """

    def __init__(self, protocol : BaseRLProtocol, simulator: BaseSimulator, verbosity: int = 0):
        # Initialize shared base class functionality
        BaseRayEnv.__init__(self, protocol, simulator, verbosity)
        
        # Setup metadata
        self.metadata = {}
        self.metadata["autoreset_mode"] = "next_step"

        # Initialize VectorMultiAgentEnv
        VectorMultiAgentEnv.__init__(self)
    
    def _init_agent_tracking(self):
        """Initialize vectorized agent tracking by creating wrapper instances."""
        self.render_mode = None
        
        # Create list of MultiAgentEnv instances matching RLlib's pattern
        self.envs = [
            _SingleEnvWrapper(
                env_id=i,
                protocol=self.protocol,
                simulator=self.simulator,
                single_observation_spaces=self._single_observation_spaces,
                single_action_spaces=self._single_action_spaces,
                possible_agents=self.possible_agents,
                parent_vec_env=self
            )
            for i in range(self.num_envs)
        ]


    def _define_environment(self):
        """Define environment spaces for multiple parallel environments."""
        ids, agent_types, obs_defns, action_defns = self.protocol.get_definition()
        
        from itertools import chain
        self.id_manager = IdManager(ids)
        self.possible_agents = list(set(chain.from_iterable(ids)))  # All agents that can ever exist in the envs.

        # Use base class methods for space building
        first_env_id, first_agent_id = self.id_manager[0]
        self._build_spaces(obs_defns, action_defns, first_env_id)
        self._validate_environments(ids)
        
        self.num_envs = self.id_manager.num_envs


    def reset(
        self, *, seed: Optional[Union[int, List[int]]] = None, options: Optional[Dict[str, Any]] = None
    ) -> Tuple[List[Dict[str, Any]], List[Dict[str, Any]]]:
        """
        Reset all sub-environments.
        
        Args:
            seed: Random seed (int or list of ints, one per environment)
            options: Optional reset options
            
        Returns:
            Tuple of (observations, infos) as List[MultiAgentDict] format.
        """
        if seed is not None:
            # Create a nicely distributed set of seeds for each sub-environment
            if isinstance(seed, int):
                self.seed_sequence = np.random.SeedSequence(entropy=seed)
                self._np_random = np.random.default_rng(self.seed_sequence.spawn(1)[0])
                self._np_random_seed = seed
                # Generate seeds and ensure they fit in int32 range
                seed = [
                    int(x.generate_state(1).item() & 0x7FFFFFFF)  # Mask to fit in signed int32
                    for x in self.seed_sequence.spawn(self.num_envs)
                ]
            elif isinstance(seed, list):
                assert (
                    len(seed) == self.num_envs
                ), "Number of seeds must match number of environments, if passed as list"
            else:
                raise EnvironmentException(
                    "Seed must be None, an int, or a list of ints with length equal to the number of environments"
                )

        observations, infos = self.protocol.send_reset_msg(seeds=seed, options=options)
        
        # Update agent tracking and wrapper states based on what Unreal returned
        for env_id in range(self.num_envs):
            wrapper : _SingleEnvWrapper = self.envs[env_id]
            wrapper._reset(observations[env_id])
            logger.debug(f"Env {env_id} reset with agents: {wrapper._current_agents}")
        
        # Always return list format for vectorized environments
        logger.debug(f"RayVecEnv.reset() returning list format: length={len(observations)}, num_envs={self.num_envs}")
        return observations, infos

    def step(self, actions: List[Dict[str, Any]]) -> Tuple[List[Dict[str, Any]], List[Dict[str, float]], List[Dict[str, bool]], List[Dict[str, bool]], List[Dict[str, Any]]]:
        """
        Step all sub-environments with the given actions.
        
        Args:
            actions: List of action dicts (List[MultiAgentDict])
            
        Returns:
            Tuple of (observations, rewards, terminateds, truncateds, infos) as List[MultiAgentDict] format.
        """
        # Convert actions list to dict format expected by protocol
        action_dict = {i: actions[i] for i in range(len(actions))}
        
        # We are in Next Step reset mode so ignore the initial_obs and initial_infos
        observations, rewards, terminateds, truncateds, infos, _, _ = self.protocol.send_action_msg(action_dict, self._single_action_spaces)

        # Handle agents dynamically based on what Unreal returns
        # Following RLlib spec: terminateds/truncateds dicts contain ALL agents (even inactive ones)
        # In turn-based/hierarchical scenarios, agents may not act every step but are still alive
        # and appear in terminateds/truncateds with False values
        for env_id in range(self.num_envs):
            env : _SingleEnvWrapper = self.envs[env_id]
            env._step(observations[env_id], terminateds[env_id], truncateds[env_id])
        
            agents_in_this_env = env._current_agents | env._terminated_agents | env._truncated_agents
            num_done = len(env._terminated_agents | env._truncated_agents)
            num_total = len(agents_in_this_env)
            
            terminateds[env_id]["__all__"] = (num_done == num_total) if num_total > 0 else False
            truncateds[env_id]["__all__"] = (len(env._truncated_agents) == num_total) if num_total > 0 else False
            if terminateds[env_id]["__all__"] or truncateds[env_id]["__all__"]:
                env._reset_on_next_step = True

        
        # Always return list format for vectorized environments
        logger.debug(f"RayVecEnv.step() returning list format: num_envs={self.num_envs}")
        return observations, rewards, terminateds, truncateds, infos

