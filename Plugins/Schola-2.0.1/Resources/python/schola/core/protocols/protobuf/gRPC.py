# Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.
"""
Base Class for Connections that use the gRPC server
"""
from typing import Any, Dict, List, Optional, Tuple
import grpc
from schola.core.protocols.base import AutoResetType, BaseRLProtocol
from schola.core.protocols.protobuf.deserialize import from_proto
from schola.core.protocols.protobuf.serialize import to_proto, fill_generic
import schola.generated.GymConnector_pb2_grpc as gym_grpc
import schola.generated.GymConnector_pb2 as util_messages
import schola.generated.Definitions_pb2 as env_definitions
import schola.generated.State_pb2 as state
import schola.generated.StateUpdates_pb2 as state_updates
from schola.core.protocols.socket import SocketProtocolMixin
import gymnasium as gym
from gymnasium.vector.vector_env import AutoresetMode
import sys
import logging

class gRPCProtocol(BaseRLProtocol, SocketProtocolMixin):
    
    def __init__(self, 
                 url: str, 
                 port: int = None,
                 environment_start_timeout: int = 45):
        super().__init__(url, port)
        self.channel: Optional[grpc.Channel] = None
        self.gym_stub: gym_grpc.GymServiceStub = None
        self.environment_start_timeout = environment_start_timeout
        self.update_future = None
        
    def close(self) -> None:
        """
        Close the Unreal Connection. Method must be safe to call multiple times.
        """
        logging.info("... Close invoked")
        SocketProtocolMixin.on_close(self)
            
        if self.channel_connected:
            try:
                state_update = state_updates.StateUpdate(status=state_updates.CommunicatorStatus.CLOSED)
                self.gym_stub.UpdateState(state_update)
            except grpc.RpcError as e:
                # Unreal may have already closed the connection, which is fine
                logging.debug(f"... gRPC close message failed (connection may already be closed): {e}")
            finally:
                self.channel.close()
                self.channel = None
        else:
            logging.info("... gRPC channel already closed?")

    def start(self) -> None:
        """
        Open the Connection to Unreal Engine.
        """
        SocketProtocolMixin.on_start(self)

        # Set max message sizes to 100MB to handle large messages
        options = [
            ('grpc.max_send_message_length', 100 * 1024 * 1024),
            ('grpc.max_receive_message_length', 100 * 1024 * 1024),
        ]
        
        self.channel = grpc.secure_channel(
            self.address, grpc.local_channel_credentials(), options=options
        ).__enter__()
        self.gym_stub = gym_grpc.GymServiceStub(self.channel)

    def send_startup_msg(self, auto_reset_type: AutoresetMode = AutoresetMode.SAME_STEP):
        start_msg = util_messages.GymConnectorStartRequest()
        
        if auto_reset_type == AutoresetMode.DISABLED:
            start_msg.autoreset_type = util_messages.AutoResetType.DISABLED
        elif auto_reset_type == AutoresetMode.SAME_STEP:
            start_msg.autoreset_type = util_messages.AutoResetType.SAME_STEP
        elif auto_reset_type == AutoresetMode.NEXT_STEP:
            start_msg.autoreset_type = util_messages.AutoResetType.NEXT_STEP
        
        self.gym_stub.StartGymConnector(
            start_msg, timeout=self.environment_start_timeout, wait_for_ready=True
        )

    def get_definition(self) -> Tuple[List[List[str]], List[Dict[int, str]], Dict[int, Dict[str, gym.Space]], Dict[int, Dict[str, gym.Space]]]:
        training_defn: env_definitions.TrainingDefinition = (
            self.gym_stub.RequestTrainingDefinition(
                util_messages.TrainingDefinitionRequest()
            )
        )

        # just a nested list of all the environments and their active agents
        uids: List[List[int]] = [
            [agent_id for agent_id in env_defn.agent_definitions]
            for env_defn in training_defn.environment_definitions
        ]

        agent_types = [
            {
                agent_uid: env_defn.agent_definitions[agent_uid].agent_type
                for agent_uid in uids[i]
            }
            for i, env_defn in enumerate(training_defn.environment_definitions)
        ]

        uids, agent_types, obs_spaces, act_spaces = from_proto(training_defn)

        return uids, agent_types, obs_spaces, act_spaces

    def send_reset_msg(self, seeds : List = None, options: List = None):

        # abort any inprogress stuff
        state_update = state_updates.StateUpdate(reset=state_updates.Reset())
        reset_msg : state_updates.Reset = state_update.reset

        if seeds is not None:
            for env_id, seed in enumerate(seeds):
                if seed is not None:
                    reset_msg.environments[env_id].seed = seed

        if options is not None:
            for env_id, option_dict in enumerate(options):
                for key, option in option_dict.items():
                    reset_msg.environments[env_id].options[key] = option
        
        response : state.State  = self.gym_stub.UpdateState(state_update)
        obs, info = from_proto(response.initial_state)
        # Convert from Dict[Dict[envID, Dict[agentID, Any]]] to list[Dict[agentID, Any]]
        observations = [obs[env_id] for env_id in range(len(obs))]
        infos = [info[env_id] for env_id in range(len(info))]
        return observations, infos
    
    def send_action_msg(self, actions : Dict[int,Dict[str,Any]], action_space: Dict[str, gym.Space]):
        state_update = state_updates.StateUpdate(step=state_updates.Step())
        state_update.status = state_updates.CommunicatorStatus.GOOD

        for env_id in actions:
            env_update = state_update.step.environments.add()
            for agent_id, action in actions[env_id].items():
                fill_generic(to_proto(action_space[agent_id], action), env_update.updates[agent_id])
                
        training_state : state.State = self.gym_stub.UpdateState(state_update)
        observations, rewards, terminateds, truncateds, infos = from_proto(training_state.training_state)

        if(training_state.HasField("initial_state")):
            initial_obs, initial_info = from_proto(training_state.initial_state)
        else:
            initial_obs, initial_info = {}, {}
        
        return observations, rewards, terminateds, truncateds, infos, initial_obs, initial_info


    def connect_stubs(self, *stubs: List["grpc.Stub"]) -> List["grpc.Stub"]:
        """
        Connects the gRPC stubs to the Unreal Engine channel

        Parameters
        ----------
        *stubs : List["grpc.Stub"]
            The gRPC stubs to connect to the Unreal Engine channel
        """
        if not self.channel_connected:
            raise RuntimeError("Connection has not been started, please call start() before connecting gRPC stubs")
        return [stub(self.channel) for stub in stubs]

    @property
    def channel_connected(self) -> bool:
        """
        Returns whether the connection is active or not

        Returns
        -------
        bool
            Whether the connection is active or not
        """
        return self.channel != None

    def __bool__(self) -> bool:
        """
        Returns whether the connection is active or not

        Returns
        -------
        bool
            True iff the connection is active
        """
        return self.has_socket and self.channel_connected

    @property
    def properties(self) -> Dict[str,Any]:
        return self.mixin_properties