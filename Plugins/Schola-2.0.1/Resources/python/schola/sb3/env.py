# Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.

"""
Implementation of stable_baselines3.common.vec_env.VecEnv backed by a Schola Environment.
"""

from collections import OrderedDict, defaultdict
from typing import Any, Dict, List, Optional, Tuple, TypeVar, Union
from schola.core.protocols.base import BaseRLProtocol
from schola.core.simulators.base import BaseSimulator, UnsupportedProtocolException
from stable_baselines3.common.vec_env import VecEnv as Sb3VecEnv
from stable_baselines3.common.vec_env.subproc_vec_env import _stack_obs

import numpy as np
from schola.core.error_manager import EnvironmentException, NoAgentsException, NoEnvironmentsException
from schola.core.utils.id_manager import nested_get, IdManager
import logging

from schola.generated.GymConnector_pb2 import AutoResetType

T = TypeVar("T")


class VecEnv(Sb3VecEnv):
    """
    Stable-Baselines3 vectorized environment implementation for Schola.

    This class wraps Schola environments to be compatible with Stable-Baselines3's
    VecEnv interface, enabling use with SB3 algorithms.

    Parameters
    ----------
    simulator : BaseSimulator
        The simulator instance managing the simulator lifecycle.
    protocol : BaseRLProtocol
        The protocol instance for communication with the Simulator.
    verbosity : int, default=0
        The verbosity level for logging.
    """
    def __init__(self, 
                 simulator: BaseSimulator,
                 protocol: BaseRLProtocol,
                 verbosity: int = 0):
        self.simulator = simulator
        self.protocol = protocol

        
        self.ids: List[List[str]] = []
        self.agent_types: List[Dict[str, str]] = []
       
        self.steps: int = 0
        self.next_action: Optional[Dict[int, Dict[str, Any]]] = None
        
        if (not isinstance(self.protocol, self.simulator.supported_protocols)):
            raise UnsupportedProtocolException(f"Protocol {self.protocol} is not supported by the simulator {self.simulator}.")
        
        logging.info("...Starting Protocol and Simulator")
        self.protocol.start()
        self.simulator.start(self.protocol.properties)

        logging.info("...Sending Startup Message")
        self.protocol.send_startup_msg(auto_reset_type=AutoResetType.SAME_STEP)
        
        logging.info("...Requesting environment definition")
        self.id_manager, self.agent_types, obs_space, action_space = self._define_environment()

        super().__init__(self.id_manager.num_ids, obs_space, action_space)

    def _define_environment(self):
        """
        Define and validate the environment structure from Unreal Engine.

        This method retrieves the environment definition from Unreal Engine,
        validates that all environments and agents are properly configured,
        and ensures all agents have matching observation and action spaces.

        Returns
        -------
        Tuple[IdManager, List[Dict[str, str]], gym.Space, gym.Space]
            A tuple containing:
            - IdManager for mapping between nested and flat agent IDs
            - Agent groups for each environment
            - The observation space (must be the same for all agents)
            - The action space (must be the same for all agents)

        Raises
        ------
        NoEnvironmentsException
            If no environments are found.
        NoAgentsException
            If any environment has no agents.
        AssertionError
            If agents have mismatched observation or action spaces.
        """

        ids, agent_types, obs_defns, action_defns = self.protocol.get_definition()
        id_manager = IdManager(ids)
        first_env_id, first_agent_id = id_manager[0]
        action_space = action_defns[first_env_id][first_agent_id]
        obs_space = obs_defns[first_env_id][first_agent_id]
        try:
            if len(ids) == 0:
                raise NoEnvironmentsException()

            for env_id, agent_id_list in enumerate(id_manager.ids):
                if len(agent_id_list) == 0:
                    raise NoAgentsException(env_id)

            for env_id, agent_id in id_manager.id_list:
                assert (
                    action_defns[env_id][agent_id] == action_space
                ), f"Action Space Mismatch on Agent:{agent_id} in Env {env_id}.\nGot: {action_defns[env_id][agent_id]}\nExpected:{action_space}"
                assert (
                    obs_defns[env_id][agent_id] == obs_space
                ), f"Observation Space Mismatch on Agent:{agent_id} in Env {env_id}.\nGot: {obs_defns[env_id][agent_id]}\nExpected:{obs_space}"
        except Exception as e:
            self.protocol.close()
            self.simulator.stop()
            raise e
        
        return id_manager, agent_types, obs_space, action_space


    def close(self) -> None:
        logging.info("... closing environment")
        self.protocol.close()
        self.simulator.stop()

    def env_method(method_name, *method_args, indices=None, **method_kwargs):
        """
        Call a method on the underlying environments.

        Parameters
        ----------
        method_name : str
            The name of the method to call.
        *method_args
            Positional arguments for the method.
        indices : list, optional
            Indices of environments to call the method on.
        **method_kwargs
            Keyword arguments for the method.

        Notes
        -----
        This method is not fully implemented as sub-environments are not
        individually accessible in the Schola implementation.
        """
        raise NotImplementedError("env_method is not implemented for Schola environments, as sub-environments are not individually accessible.")

    def get_attr(self, attr_name, indices=None):
        """
        Get an attribute from the underlying environments.

        Parameters
        ----------
        attr_name : str
            The name of the attribute to retrieve.
        indices : list, optional
            Indices of environments to get the attribute from.

        Returns
        -------
        list
            A list of None values, as sub-environments are not individually accessible.

        Notes
        -----
        This method always returns None values because Schola environments
        do not expose individual sub-environment attributes.
        """
        return [None for x in range(0, self.id_manager.num_ids)]

    def reset(self) -> Dict[str, np.ndarray]:
        obs, nested_infos = self.protocol.send_reset_msg(seeds=self._seeds, options=self._options)
        
        self._reset_seeds()
        self._reset_options()
        

        for env_id, info_dict in enumerate(nested_infos):
            for agent_id in nested_infos[env_id]:
                uid = self.id_manager[env_id, agent_id]
                self.reset_infos[uid] = nested_infos[env_id][agent_id]
        # flatten the observations, converting from dict to list using key as indices
        obs = self.id_manager.flatten_list_of_dicts(obs)
        # flatten even more, for sb3 compatibility
        obs = _stack_obs(obs, self.observation_space)
        return obs

    def env_is_wrapped(self, wrapper_class, indices=None) -> bool:
        if indices is None:
            indices = (x for x in range(self.id_manager.num_ids))
        return [False for x in indices]

    def set_attr(self, attr_name, value, indices=None):
        """
        Set an attribute on the underlying environments.

        Parameters
        ----------
        attr_name : str
            The name of the attribute to set.
        value : Any
            The value to set the attribute to.
        indices : list, optional
            Indices of environments to set the attribute on.

        Notes
        -----
        This method is not fully implemented as sub-environments are not
        individually accessible in the Schola implementation.
        """
        raise NotImplementedError("set_attr is not implemented for Schola environments, as sub-environments are not individually accessible.")

    def step_async(self, actions: np.ndarray) -> None:
        #convert into a dictionary
        self.next_actions = self.id_manager.nest_list_to_dict_of_dicts(actions)
        

    def step_wait(
        self,
    ) -> Tuple[Dict[str, np.ndarray], np.ndarray, np.ndarray, List[Dict[str, str]]]:
        
        #we are in self reset mode so grab initial_obs and initial_infos
        observations, rewards, terminateds, truncateds, nested_infos, initial_obs, initial_infos = self.protocol.send_action_msg(self.next_actions, defaultdict(lambda : self.action_space))

        array_dones = np.empty((self.id_manager.num_ids,), dtype=np.bool_)

        array_rewards = np.asarray(self.id_manager.flatten_list_of_dicts(rewards))

        array_observations = self.id_manager.flatten_list_of_dicts(observations)

        infos = [{} for _ in range(self.num_envs)]
        for env_id, single_env_info in enumerate(nested_infos):
            for agent_id in single_env_info:
                uid = self.id_manager[env_id, agent_id]
                # safe because we are iterating over nested_infos
                infos[uid] = single_env_info[agent_id]

        
        for env_id, agent_id_list in enumerate(self.id_manager.ids):
            any_done = False
            all_done = True
            for agent_id in agent_id_list:
                uid = self.id_manager[env_id, agent_id]
                array_dones[uid] = terminateds[env_id].get(agent_id, False) or truncateds[env_id].get(agent_id, False)
                any_done = any_done or array_dones[uid]
                all_done = all_done and array_dones[uid]

            # We don't handle the case where 1 agent ends early currently.
            if any_done and not all_done:
                raise EnvironmentException(
                    f"SB3 with multi-agent environments does not support agents completing at different steps. Env {env_id} had agents in different completion states."
                )

        # following the sb3 vec env guideline we self reset
        
        for env_id in initial_infos:
            for agent_id in initial_infos[env_id]:
                uid = self.id_manager[env_id, agent_id]
                # safe because we are iterating over nested_infos
                self.reset_infos[uid] = initial_infos[env_id][agent_id]

        for env_id in initial_obs:
            for agent_id in self.id_manager.partial_get(env_id):
                uid = self.id_manager[env_id, agent_id]
                # Observations of the last step of the episode go into the info section
                infos[uid]["terminal_observation"] = observations[env_id][agent_id]
                infos[uid]["TimeLimit.truncated"] = (
                    truncateds[env_id][agent_id]
                    and not terminateds[env_id][agent_id]
                )

                # put the new observations from the start of the new episode into the returned obs
                array_observations[uid] = initial_obs[env_id][agent_id]
        
        return (
            _stack_obs(array_observations, self.observation_space),
            array_rewards,
            array_dones,
            infos,
        )
