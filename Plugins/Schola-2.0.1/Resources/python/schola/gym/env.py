# Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.

"""
Implementation of gym.vector.VectorEnv backed by a Schola Environment.
"""

from collections import defaultdict
from math import inf
from typing import Any, Dict, List, Optional, Tuple, TypeVar, Union

from ray import init
from schola.core.protocols.base import BaseRLProtocol
from schola.core.simulators.base import BaseSimulator, UnsupportedProtocolException
from schola.core.protocols.base import AutoResetType
from schola.core.error_manager import EnvironmentException, NoAgentsException, NoEnvironmentsException
import numpy as np
import gymnasium as gym
from schola.core.utils.id_manager import nested_get, IdManager
from gymnasium.vector.utils import batch_space
from gymnasium.vector.vector_env import AutoresetMode, VectorEnv
import logging

T = TypeVar("T")


class GymEnv(gym.Env):
    """
    A Gym Environment that wraps a Schola Environment.

    Parameters
    ----------
    unreal_connection : UnrealConnection
        The connection to the Unreal Engine.
    verbosity : int, default=0
        The verbosity level for the environment.
    """

    def __init__(self, simulator: BaseSimulator, protocol : BaseRLProtocol, verbosity: int = 0):

        self.protocol = protocol
        self.simulator = simulator
        if (not isinstance(self.protocol, self.simulator.supported_protocols)):
            raise UnsupportedProtocolException(f"Protocol {self.protocol} is not supported by the simulator {self.simulator}.")
        
        self.protocol.start()
        self.simulator.start(self.protocol.properties)

        self.protocol.send_startup_msg(auto_reset_type=AutoresetMode.DISABLED)

        self._agent_id, self.action_space, self.observation_space = self._define_environment()
        super().__init__()

    def _define_environment(self):

        ids, self.agent_types, obs_defns, action_defns = self.protocol.get_definition()

        id_manager = IdManager(ids)
        env_id, agent_id = id_manager[0]

        action_space = action_defns[env_id][agent_id]
        obs_space = obs_defns[env_id][agent_id]

        try:
           assert(id_manager.num_ids == 1), "GymEnv is designed for single-agent non-vectorized environments only. Please use GymVectorEnv for multi-agent or vectorized environments."
        except Exception as e:
            self.protocol.close()
            self.simulator.stop()
            raise e
        
        return agent_id, action_space, obs_space

    def close(self, **kwargs):
        self.protocol.close()
        self.simulator.stop()

    def reset(
        self, seed: Optional[int] = None, options: Optional[Dict[str, str]] = None
    ) -> Tuple[Dict[str, np.ndarray], Dict[int, Dict[str, str]]]:
        super().reset(seed=seed, options=options)
        # wrap options in a list if provided
        options = [options] if options is not None else None
        seeds = [seed] if seed is not None else None
        obs, nested_infos = self.protocol.send_reset_msg(seeds=seeds, options=options)
        return obs[0][self._agent_id], nested_infos[0][self._agent_id]

    def step(
        self, action: Any
    ) -> Tuple[Dict[str, np.ndarray], float, bool, bool, Dict[str, str]]:
        """
        Execute one step in the environment.

        Parameters
        ----------
        action : Any
            The action to execute in the environment.

        Returns
        -------
        observations : Dict[str, np.ndarray]
            The observations from the environment after executing the action.
        reward : float
            The reward received after executing the action.
        terminated : bool
            Whether the episode has terminated (reached a terminal state).
        truncated : bool
            Whether the episode was truncated (e.g., time limit reached).
        info : Dict[str, str]
            Additional information about the step.
        """
        # reset mode is disabled so we can ignore the observations/infos coresponding to the first step of a new episode
        observations, rewards, terminateds, truncateds, nested_infos, _ , _ = self.protocol.send_action_msg({0:{self._agent_id:action}}, {self._agent_id:self.action_space})
        observations, rewards, terminated, truncated, infos = (
            observations[0][self._agent_id],
            rewards[0][self._agent_id],
            terminateds[0][self._agent_id],
            truncateds[0][self._agent_id],
            nested_infos[0][self._agent_id],
        )
        return observations, rewards, terminated, truncated, infos


class GymVectorEnv(VectorEnv):
    """
    A Gym Vector Environment that wraps a Schola Environment.

    Parameters
    ----------
    unreal_connection : UnrealConnection
        The connection to the Unreal Engine.
    verbosity : int, default=0
        The verbosity level for the environment.

    Attributes
    ----------
    reset_infos : List[Dict[str,str]]
        The information returned from the last reset.
    """

    def __init__(self, simulator: BaseSimulator, protocol : BaseRLProtocol, verbosity: int = 0, autoreset_mode: str | AutoresetMode = AutoresetMode.SAME_STEP):
        
        self.protocol = protocol
        self.simulator = simulator
        if (not isinstance(self.protocol, self.simulator.supported_protocols)):
            raise UnsupportedProtocolException(f"Protocol {self.protocol} is not supported by the simulator {self.simulator}.")
        
        self.protocol.start()
        self.simulator.start(self.protocol.properties)

        self.autoreset_mode = (
            autoreset_mode
            if isinstance(autoreset_mode, AutoresetMode)
            else AutoresetMode(autoreset_mode)
        )
        self.metadata["autoreset_mode"] = self.autoreset_mode

        self.protocol.send_startup_msg(auto_reset_type=self.autoreset_mode)
        
        # one env per agent for
        self._observation_space: gym.Space | None = None
        self._action_space: gym.Space | None = None
        self._single_observation_space: gym.Space | None = None
        self._single_action_space: gym.Space | None = None
        
        self._define_environment()

        self.reset_infos: List[Dict[str, str]] = []

        super().__init__()


    def _define_environment(self):

        ids, agent_types, obs_defns, action_defns = self.protocol.get_definition()
        
        self.id_manager = IdManager(ids)
        self.num_envs = self.id_manager.num_ids

        self._action_space = gym.vector.utils.batch_differing_spaces(self.id_manager.flatten_dict_of_dicts(action_defns))

        self._observation_space = gym.vector.utils.batch_differing_spaces(self.id_manager.flatten_dict_of_dicts(obs_defns))

        first_env_id, first_agent_id = self.id_manager[0]

        self._single_action_space = action_defns[first_env_id][first_agent_id]
        self._single_observation_space = obs_defns[first_env_id][first_agent_id]
        try:
            if len(ids) == 0:
                raise NoEnvironmentsException()

            for env_id, agent_id_list in enumerate(self.id_manager.ids):
                if len(agent_id_list) == 0:
                    raise NoAgentsException(env_id)

            for env_id, agent_id in self.id_manager.id_list:
                assert (
                    action_defns[env_id][agent_id] == self.single_action_space
                ), f"Action Space Mismatch on Agent:{agent_id} in Env {env_id}.\nGot: {action_defns[env_id][agent_id]}\nExpected:{self.single_action_space}"
                assert (
                    obs_defns[env_id][agent_id] == self.single_observation_space
                ), f"Observation Space Mismatch on Agent:{agent_id} in Env {env_id}.\nGot: {obs_defns[env_id][agent_id]}\nExpected:{self.single_observation_space}"
        except Exception as e:
            self.protocol.close()
            self.simulator.stop()
            raise e
        
            
    def close(self, **kwargs):
        self.protocol.close()
        self.simulator.stop()

    def get_attr(self, name: str) -> List[None]:
        """
        Get an attribute from the environment.

        Parameters
        ----------
        name: str
            The name of the attribute to get.

        Notes
        -----
        This method is not implemented and will always return a list of None values, as sub-environments are not individually accessible.
        """
        return [None for x in range(0, self.num_envs)]

    def reset(
        self,
        seed: Union[None, List[int], int] = None,
        options: Union[Dict[str, List[str]], None] = None,
    ) -> Tuple[Dict[str, np.ndarray], Dict[int, Dict[str, str]]]:
        
        if seed is not None:
            if isinstance(seed, int):
                self.seed_sequence = np.random.SeedSequence(entropy=seed)
                self._np_random = np.random.default_rng(self.seed_sequence.spawn(1)[0])
                self._np_random_seed = seed
                seed = [
                    np.int32(x.generate_state(1)).item()
                    for x in self.seed_sequence.spawn(self.num_envs)
                ]
            if isinstance(seed, list):
                assert (
                    len(seed) == self.num_envs
                ), "Number of seeds must match number of environments, if passed as list"
        
        processed_options = None
        if options is not None:
            processed_options = [{} for _ in range(self.num_envs)]
            if "reset_mask" in options:
                raise NotImplementedError("reset_mask option is not currently supported in Schola Vector Environments.")
            for key, value in options.items():
                processed_options[env_id][f"{agent_id}_{key}"] = value

        obs, nested_infos = self.protocol.send_reset_msg(seeds=seed, options=processed_options)

        infos = {}
        for env_id, info_dict in enumerate(nested_infos):
            for agent_id in info_dict:
                uid = self.id_manager[env_id, agent_id]
                infos = self._add_info(infos, nested_infos[env_id][agent_id], uid)

        # flatten the observations, converting from dict to list using key as indices
        batched_observations = gym.vector.utils.create_empty_array(
            self.single_observation_space, n=self.num_envs
        )
        flattened_observations = self.id_manager.flatten_list_of_dicts(obs)
        gym.vector.utils.concatenate(
            self.single_observation_space, flattened_observations, batched_observations
        )

        return batched_observations, infos



    def unbatch_actions(
        self, actions: Dict[int, np.ndarray]
    ) -> Dict[int, Dict[int, Dict[str, np.ndarray]]]:
        """
        Unbatch actions from Dict[ObsID,Batched] to a nested Dict[EnvId,Dict[AgentId,Dict[ObsId,Value]]], effectively moving the env, and agent dimensions into Dictionaries.

        Parameters
        ----------
        actions : Dict[int,np.ndarray]
            The batched actions to unbatch.

        Returns
        -------
        Dict[int,Dict[int,Dict[str,np.ndarray]]]
            The unbatched actions.
        """
        # To prevent issues with non-iterable spaces we use the regular action space if num_envs ==1
        it = gym.vector.utils.iterate(self.action_space, actions)
        return self.id_manager.nest_list_to_dict_of_dicts([value for value in it])

    def step(
        self,
        actions: Dict[int, np.ndarray]
    ) -> Tuple[
        Dict[str, np.ndarray],
        np.ndarray,
        np.ndarray,
        np.ndarray,
        Dict[int, Dict[str, str]],
    ]:
        actions = self.unbatch_actions(actions)
        observations, rewards, terminateds, truncateds, nested_infos, initial_obs, initial_infos = self.protocol.send_action_msg(actions, defaultdict(lambda : self.single_action_space))

        array_rewards = np.asarray(self.id_manager.flatten_list_of_dicts(rewards))
        array_terminateds = np.asarray(self.id_manager.flatten_list_of_dicts(terminateds))
        array_truncateds = np.asarray(self.id_manager.flatten_list_of_dicts(truncateds))

        
        infos = {}

        for env_id, agent_id_list in enumerate(self.id_manager.ids):
            any_done = False
            all_done = True
            for agent_id in agent_id_list:
                uid = self.id_manager[env_id, agent_id]
                is_done = (terminateds[env_id][agent_id] or truncateds[env_id][agent_id])
                any_done = any_done or is_done
                all_done = all_done and is_done

            # We don't handle the case where 1 agent ends early currently.
            if any_done and not all_done:
                raise EnvironmentException(
                    f"SB3 with multi-agent environments does not support agents completing at different steps. Env {env_id} had agents in different completion states."
                )
        
        # if we get these then it means we are in self reset mode

        for env_id, agent_id_list in enumerate(self.id_manager.ids):
            for agent_id in agent_id_list:
                uid = self.id_manager[env_id, agent_id]
                if env_id in initial_obs:
                    infos = self._add_info(infos,{"final_info":nested_infos[env_id][agent_id],
                                                  "final_obs":observations[env_id][agent_id], 
                                                  **initial_infos[env_id][agent_id]}, uid)
                    observations[env_id][agent_id] = initial_obs[env_id][agent_id]
                else:
                    infos = self._add_info(infos,nested_infos[env_id][agent_id], uid)

        # flatten the observations, converting to one bit numpy ndarray

        array_observations = gym.vector.utils.create_empty_array(
            self.single_observation_space, n=self.num_envs
        )
        flattened_observations = self.id_manager.flatten_list_of_dicts(observations)
        gym.vector.utils.concatenate(
            self.single_observation_space, flattened_observations, array_observations
        )

        return (
            array_observations,
            array_rewards,
            array_terminateds,
            array_truncateds,
            infos,
        )

    @property
    def observation_space(self) -> gym.Space:
        return self._observation_space

    @property
    def action_space(self) -> gym.Space:
        return self._action_space

    @property
    def single_observation_space(self) -> gym.Space:
        return self._single_observation_space

    @property
    def single_action_space(self) -> gym.Space:
        return self._single_action_space