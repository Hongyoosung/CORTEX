# Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.
"""
Main Schola Environment
"""

from schola.core.unreal_connections import BaseUnrealConnection
from schola.core.error_manager import NoAgentsException, NoEnvironmentsException
import schola.generated.GymConnector_pb2 as gym_communication
import schola.generated.GymConnector_pb2_grpc as gym_grpc
import schola.generated.Definitions_pb2 as env_definitions
import schola.generated.State_pb2 as state
from schola.core.spaces import DictSpace
import grpc
import logging
import numpy as np
import atexit
from typing import Any, List, Dict, Optional, Tuple, Union, TypeVar


T = TypeVar("T")
import sys

if sys.version_info >= (3, 11):
    from enum import StrEnum
else:
    from backports.strenum import StrEnum


class AutoResetType(StrEnum):
    """
    Enum for Auto Reset Types.
    """

    DISABLED = "Disabled"
    SAME_STEP = "SameStep"
    NEXT_STEP = "NextStep"


# A Dictionary, with EnvIds as keys and a Dictionary of AgentIds to some TypeVar as Value.
EnvAgentIdDict = Dict[int, Dict[int, T]]


class ScholaEnv:
    """
    A Gym-Like Environment that wraps a connection to the Unreal Engine, running the Schola Plugin for Unreal.

    Parameters
    ----------
    unreal_connection : BaseUnrealConnection
        The connection to the Unreal Engine.
    verbosity : int, default=0
        The verbosity level for the environment.
    environment_start_timeout : int, default=45
        The time to wait for the environment to start in seconds.
    auto_reset_type : AutoResetType, default=AutoResetType.SAME_STEP
        The type of auto-reset for the environment. See Gymnasium for more details on the different modes. Only Disabled, and SameStep are currently supported.

    Attributes
    ----------
    unreal_connection : BaseUnrealConnection
        The connection to the Unreal Engine.
    gym_stub : gym_grpc.GymServiceStub
        The gRPC stub for the Gym Service.
    ids : List[List[int]]
        A nested list of all the environments and their active agents.
    agent_display_names : List[Dict[int,str]]
        A list of mappings from the id to the display names for each agent in each environment.
    obs_defns : Dict[int,Dict[int,DictSpace]]
        The observation space definitions for each agent in each environment.
    action_defns : Dict[int,Dict[int,DictSpace]]
        The action space definitions for each agent in each environment.
    steps : int
        The number of steps taken in the current episode of the environment.
    next_action : Dict[int,Dict[int,Any]], optional
        The next action to be taken by each agent in each environment.

    Raises
    ------
    NoEnvironmentsException
        If there are no environment definitions.
    NoAgentsException
        If there are no agents defined for any environment.
    """

    def __init__(
        self,
        unreal_connection: BaseUnrealConnection,
        verbosity: int = 0,
        environment_start_timeout: int = 45,
        auto_reset_type: AutoResetType = AutoResetType.SAME_STEP,
    ):

        log_level = logging.WARNING
        log_level = logging.INFO if verbosity == 1 else log_level
        log_level = logging.DEBUG if verbosity >= 2 else log_level

        logging.basicConfig(
            format="%(asctime)s:%(levelname)s:%(message)s", level=log_level
        )

        logging.info("creating channel")
        self.unreal_connection = unreal_connection
        self.unreal_connection.start()

        atexit.register(self.close)
        self.gym_stub: gym_grpc.GymServiceStub = self.unreal_connection.connect_stubs(
            gym_grpc.GymServiceStub
        )[0]

        # Server might be booting up if we have a standalone connection, so we wait for 45 to verify
        start_msg = gym_communication.GymConnectorStartRequest()
        if auto_reset_type == AutoResetType.DISABLED:
            start_msg.autoreset_type = gym_communication.DISABLED
        elif auto_reset_type == AutoResetType.SAME_STEP:
            start_msg.autoreset_type = gym_communication.SAMESTEP
        elif auto_reset_type == AutoResetType.NEXT_STEP:
            start_msg.autoreset_type = gym_communication.NEXTSTEP
        self.gym_stub.StartGymConnector(
            start_msg, timeout=environment_start_timeout, wait_for_ready=True
        )

        logging.info("requesting environment definition")
        self.ids: List[List[int]] = []
        self.agent_display_names: List[Dict[int, str]] = []
        # ids is set here
        self._define_environment()
        self.steps: int = 0
        self.next_action: Optional[Dict[int, Dict[int, Any]]] = None

    def _create_space_definitions(
        self, defn_map: Dict[int, Dict[int, env_definitions.AgentDefinition]]
    ) -> None:
        """
        Create space definitions for observation and action spaces.

        Parameters
        ----------
        defn_map : Dict[int, Dict[int, env_definitions.AgentDefinition]]
            A dictionary containing environment and agent definitions.
        """
        self.obs_defns: Dict[int, Dict[int, DictSpace]] = {}
        self.action_defns: Dict[int, Dict[int, DictSpace]] = {}

        for env_id, env_defn in enumerate(defn_map):
            for agent_id, agent_defn in env_defn.agent_definitions.items():
                obs_space = DictSpace.from_proto(agent_defn.obs_space)
                if agent_defn.normalize_obs:
                    obs_space = obs_space.to_normalized()

                self.obs_defns.setdefault(env_id, {}).setdefault(agent_id, obs_space)
                self.action_defns.setdefault(env_id, {}).setdefault(
                    agent_id, DictSpace.from_proto(agent_defn.action_space)
                )

    def get_obs_space(self, env_id: int, agent_id: int) -> DictSpace:
        """
        Get the observation space for a specific environment and agent.

        Parameters
        ----------
        env_id : int
            The ID of the environment.
        agent_id : int
            The ID of the agent.

        Returns
        -------
        DictSpace
            The observation space for the specified environment and agent.
        """

        return self.obs_defns[env_id][agent_id]

    def get_action_space(self, env_id: int, agent_id: int) -> DictSpace:
        """
        Get the action space for a specific environment and agent.

        Parameters
        ----------
        env_id : int
            The ID of the environment.
        agent_id : int
            The ID of the agent.

        Returns
        -------
        DictSpace
            The action space for the specified environment and agent.
        """

        return self.action_defns[env_id][agent_id]

    def _define_environment(self) -> None:
        """
        Define the environment.
        This method retrieves the training definition from the gym stub and defines the environment based on the retrieved data.
        It populates the `ids` attribute with a nested list of all the environments and their active agents.
        It also populates the `agent_display_names` attribute with a nested dict mapping the id to the display names for each agent in each environment.
        Finally, it calls the `_create_space_definitions` method to create space definitions for the environment.

        Raises
        ------
        NoEnvironmentsException
            If there are no environment definitions.

        NoAgentsException
            If there are no agents defined for any environment.
        """

        training_defn: env_definitions.TrainingDefinition = (
            self.gym_stub.RequestTrainingDefinition(
                gym_communication.TrainingDefinitionRequest()
            )
        )

        # just a nested list of all the environments and their active agents
        self.ids: List[List[int]] = [
            [agent_id for agent_id in env_defn.agent_definitions]
            for env_defn in training_defn.environment_definitions
        ]

        if len(self.ids) == 0:
            raise NoEnvironmentsException()

        for env_id, agent_id_list in enumerate(self.ids):
            if len(agent_id_list) == 0:
                raise NoAgentsException(env_id)

        self.agent_display_names = [
            {
                agent_id: env_defn.agent_definitions[agent_id].name
                for agent_id in self.ids[i]
            }
            for i, env_defn in enumerate(training_defn.environment_definitions)
        ]

        self._create_space_definitions(training_defn.environment_definitions)

    def poll(
        self,
    ) -> Tuple[
        EnvAgentIdDict[Dict[str, Any]],
        EnvAgentIdDict[float],
        EnvAgentIdDict[bool],
        EnvAgentIdDict[bool],
        EnvAgentIdDict[Dict[str, str]],
    ]:
        """
        Polls the environment for the current state.

        Returns
        -------
        observations : EnvAgentIdDict[Dict[str,Any]]
            A dictionary, keyed by the environment and agent Id, containing the observations for each agent.
        rewards : EnvAgentIdDict[float]
            A dictionary, keyed by the environment and agent Id, containing the reward for each agent.
        terminateds : EnvAgentIdDict[bool]
            A dictionary, keyed by the environment and agent Id, containing the termination flag for each agent.
        truncateds : EnvAgentIdDict[bool]
            A dictionary, keyed by the environment and agent Id, containing the truncation flag for each agent.
        infos : EnvAgentIdDict[Dict[str,str]]]:
            A dictionary, keyed by the environment and agent Id, containing the information dictionary for each agent.
        """

        if self.steps == 0:
            logging.info("Starting Epoch")
        # convert action into Proto class
        state_update = gym_communication.TrainingStateUpdate()
        for env_id in self.next_action:
            env_update = state_update.updates[env_id].step
            for agent_id in self.next_action[env_id]:
                agent_update = env_update.updates[agent_id]
                self.action_defns[env_id][agent_id].fill_proto(
                    agent_update.actions, self.next_action[env_id][agent_id]
                )
        state_update.status = gym_communication.CommunicatorStatus.GOOD
        logging.debug(state_update)
        # send it to Unreal (with timeout to prevent infinite blocking after idle periods)
        try:
            training_state = self.gym_stub.UpdateState(state_update, timeout=120)
        except grpc.RpcError as e:
            logging.error(f"UpdateState failed (step {self.steps}): {e.code()} - {e.details()}")
            raise
        # convert proto to observations, reward, terminated, truncated and other info
        self.steps += 1
        logging.debug(training_state)
        observations, rewards, terminateds, truncateds, infos = (
            self._convert_state_to_tuple(training_state)
        )
        logging.debug(observations)
        # welp let's see if this goes
        if len(observations.keys()) < 1:
            return self.poll()

        return observations, rewards, terminateds, truncateds, infos

    def send_actions(self, action: EnvAgentIdDict[Dict[str, Any]]) -> None:
        """
        Send Actions to all agents and environments.

        Parameters
        ----------
        action : EnvAgentIdDict[Dict[str,Any]]
            A dictionary, keyed by the environment and agent Id, containing the actions for all active environments and agents.

        Notes
        -----
        The actions are not sent to Unreal until Poll is called.

        See Also
        --------
        poll : Where the actions are actually sent to unreal
        """
        self.next_action = action

    def hard_reset(
        self,
        env_ids: Optional[List[int]] = None,
        seeds: Union[None, List[int], int] = None,
        options: Union[List[Dict[str, str]], Dict[str, str], None] = None,
    ) -> Tuple[EnvAgentIdDict[Dict[str, Any]], EnvAgentIdDict[Dict[str, str]]]:
        """
        Perform a hard reset on the environment.

        Parameters
        ----------
        env_ids : Optional[List[int]]
            A list of environment IDs to reset. If None, all environments will be reset. Default is None.
        seeds : Union[None, List[int], int]
            The seeds to use for random number generation. If an int is provided, it will be used as the seed for all environments. If a list of ints is provided, each environment will be assigned a seed from the list. Default is None.
        options : Union[List[Dict[str,str]], Dict[str,str], None]
            The options to set for each environment. If a list of dictionaries is provided, each environment will be assigned the corresponding dictionary of options. If a single dictionary is provided, all environments will be assigned the same options. Default is None.

        Returns
        -------
        observations : EnvAgentIdDict[Dict[str,Any]]
            A dictionary, keyed by the environment and agent Id, containing the observations of the agents in the environments immediately following a reset
        infos : EnvAgentIdDict[Dict[str,str]]
            A dictionary, keyed by the environment and agent Id, containing the infos of the agents in the environment

        Raises
        ------
        AssertionError
            If the number of seeds provided, is not zero or one, and does not match the number of environments.

        AssertionError
            If the number of options dictionaries provided, is not zero or one, does not match the number of environments.

        Notes
        -----
        - If seeds are provided, the environment will be seeded with the specified values.
        - If options are provided, the environment will be configured with the specified options.

        See Also
        --------
        gymnasium.Env.reset : The equivalent operation in gymnasium
        """

        if seeds is not None and isinstance(seeds, int):
            self.seed_sequence = np.random.SeedSequence(entropy=seeds)
            self.np_random = np.random.default_rng(self.seed_sequence.spawn(1)[0])

        target_env_ids = env_ids if env_ids else list(range(self.num_envs))
        # abort any inprogress stuff
        state_update = gym_communication.TrainingStateUpdate()
        # generate seeds out here
        if seeds is not None:
            if isinstance(seeds, list):
                assert (
                    len(seeds) == self.num_envs
                ), "Number of seeds must match number of environments, if passed as list"
                self.seeds = seeds
            else:
                # Note this converts the uint32 to a python int
                self.seeds = [
                    np.int32(x.generate_state(1)).item()
                    for x in self.seed_sequence.spawn(self.num_envs)
                ]

        for env_id in target_env_ids:
            reset_msg = state_update.updates[env_id].reset

            if seeds is not None:
                reset_msg.seed = self.seeds[env_id]

            if options is not None:
                if isinstance(options, list):
                    assert (
                        len(options) == self.num_envs
                    ), "Number of options dictionaries must match number of environments, if passed as list"
                    env_options = options[env_id]
                else:
                    env_options = options
                # convert to string
                for key in env_options:
                    reset_msg.options[key] = str(env_options[key])
        # send the message without caring about the response
        self.gym_stub.UpdateState.future(state_update)
        # reset everyone

        return self.soft_reset(target_env_ids)

    def soft_reset(
        self, ids: Optional[List[int]] = None
    ) -> Tuple[EnvAgentIdDict[Dict[str, Any]], EnvAgentIdDict[Dict[str, str]]]:
        """
        Soft reset the environment, by waiting for Unreal to reset and send a Post Reset State to python.

        Parameters
        ----------
        ids : List[int], optional
            A list of environment IDs to reset. If not provided or set to None, all environment IDs will be reset.

        Returns
        -------
        observations : EnvAgentIdDict[Dict[str,Any]]
            A dictionary, keyed by the environment and agent Id, containing the observations of the agents in the environments immediately following a reset
        infos : EnvAgentIdDict[Dict[str,str]]
            A dictionary, keyed by the environment and agent Id, containing the infos of the agents in the environment
        """

        if ids == None or len(ids) == 0:
            ids = list(range(len(self.ids)))
        self.steps = 0

        # send an empty request for an update
        logging.info("requesting environment state post reset")
        logging.info(
            f"Waiting for environment(s) {','.join([str(x) for x in ids])} to reset"
        )

        state_request = gym_communication.InitialTrainingStateRequest()
        env_state: state.TrainingState = self.gym_stub.RequestInitialTrainingState(
            state_request
        )
        logging.debug(env_state)
        logging.info("initial environment state received")
        # Note: Removed other portions for Gym compatibility instead of gymnasium
        return self._convert_reset_state_to_tuple(env_state)

    @property
    def num_agents(self) -> int:
        """
        Return the total number of agents in the environment.

        Returns
        -------
        int
            The total number of agents.
        """

        return sum([len(x) for x in self.ids])

    @property
    def num_envs(self) -> int:
        """
        Return the number of environments.
        Returns
        -------
        int
            The number of environments.
        """

        return len(self.ids)

    def close(self) -> None:
        """
        Closes the connection to the Unreal Engine and cleans up any resources. It is safe to call this method multiple times.

        See Also
        --------
        gymnasium.Env.close : The equivalent operation in gymnasium
        """

        # if the connection is active
        if self.unreal_connection.is_active:
            state_update = gym_communication.TrainingStateUpdate()
            state_update.status = gym_communication.CommunicatorStatus.CLOSED
            self.gym_stub.UpdateState.future(state_update)
            logging.info("Sending closed msg to Unreal")
            # this closes the event loop as well
        # this method is safe to call multiple times
        self.unreal_connection.close()

    def _convert_reset_state_to_tuple(
        self, reset_state: state.TrainingState
    ) -> Tuple[EnvAgentIdDict[Dict[str, Any]], EnvAgentIdDict[Dict[str, str]]]:
        """
        Convert the reset state, from a protobuf message to a tuple of observations and info.

        Parameters
        ----------
        reset_state : state.TrainingState
            The reset state object.

        Returns
        -------
        observations : EnvAgentIdDict[Dict[str,Any]]
            A dictionary, keyed by the environment and agent Id, containing the observations of the agents in the environments immediately following a reset
        infos : EnvAgentIdDict[Dict[str,str]]
            A dictionary, keyed by the environment and agent Id, containing the infos of the agents in the environment
        """

        observations = {}
        info = {}
        for env_id, env_state in reset_state.environment_states.items():
            for agent_id, agent_state in env_state.agent_states.items():
                proc_obs = self.get_obs_space(env_id, agent_id).process_data(
                    agent_state.observations
                )

                observations.setdefault(env_id, {})[agent_id] = proc_obs

                info.setdefault(env_id, {})[agent_id] = dict(agent_state.info)
        return observations, info

    def _convert_state_to_tuple(self, training_state: state.TrainingState) -> Tuple[
        EnvAgentIdDict[Dict[str, Any]],
        EnvAgentIdDict[float],
        EnvAgentIdDict[bool],
        EnvAgentIdDict[bool],
        EnvAgentIdDict[Dict[str, str]],
    ]:
        """
        Convert a training state, from a protobuf message to a tuple of observations, rewards, terminateds, truncateds and infos.

        Parameters
        ----------
        training_state : state.TrainingState
            The training state object.

        Returns
        -------
        observations : EnvAgentIdDict[Dict[str,Any]]
            A dictionary, keyed by the environment and agent Id, containing the observations for each agent.
        rewards : EnvAgentIdDict[float]
            A dictionary, keyed by the environment and agent Id, containing the reward for each agent.
        terminateds : EnvAgentIdDict[bool]
            A dictionary, keyed by the environment and agent Id, containing the termination flag for each agent.
        truncateds : EnvAgentIdDict[bool]
            A dictionary, keyed by the environment and agent Id, containing the truncation flag for each agent.
        infos : EnvAgentIdDict[Dict[str,str]]]:
            A dictionary, keyed by the environment and agent Id, containing the information dictionary for each agent.
        """

        observations = {}
        rewards = {}
        completeds = {}
        truncateds = {}
        info = {}
        for env_id, env_state in enumerate(training_state.environment_states):
            for agent_id, agent_state in env_state.agent_states.items():
                proc_obs = self.get_obs_space(env_id, agent_id).process_data(
                    agent_state.observations
                )

                observations.setdefault(env_id, {})[agent_id] = proc_obs

                rewards.setdefault(env_id, {})[agent_id] = agent_state.reward

                completeds.setdefault(env_id, {})[agent_id] = (
                    agent_state.status == state.Status.COMPLETED
                )
                truncateds.setdefault(env_id, {})[agent_id] = (
                    agent_state.status == state.Status.TRUNCATED
                )

                info.setdefault(env_id, {})[agent_id] = dict(agent_state.info)

        return observations, rewards, completeds, truncateds, info
