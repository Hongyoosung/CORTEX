# Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.
"""
Base Class for Connections that use the gRPC server
"""
from typing import Any, Dict, List, Optional, Tuple
import grpc
from schola.generated.Definitions_pb2 import TrainingDefinition
from schola.core.protocols.base import BaseImitationProtocol, BaseRLProtocol
from schola.core.protocols.protobuf.deserialize import from_proto
from schola.core.protocols.protobuf.serialize import to_proto, fill_generic
import schola.generated.ImitationConnector_pb2_grpc as imitation_grpc
import schola.generated.ImitationConnector_pb2 as imitation_messages
import schola.generated.ImitationState_pb2 as imitation_state_messages
from schola.core.protocols.socket import SocketProtocolMixin
import gymnasium as gym
import sys
import logging

class gRPCImitationProtocol(BaseImitationProtocol, SocketProtocolMixin):
    
    def __init__(self, 
                 url: str, 
                 port: int = None,
                 protocol_start_timeout: int = 45):
        super().__init__(url, port)
        self.channel: Optional[grpc.Channel] = None
        self.stub: imitation_grpc.ImitationConnectorServiceStub = None
        self.protocol_start_timeout = protocol_start_timeout
        
    def close(self) -> None:
        """
        Close the Unreal Connection. Method must be safe to call multiple times.
        """
        logging.info("... Close invoked")
        SocketProtocolMixin.on_close(self)
            
        if self.channel_connected:
            self.channel.close()
            self.channel = None
        else:
            logging.info("... gRPC channel already closed?")

    def start(self) -> None:
        """
        Open the Connection to Unreal Engine.
        """
        SocketProtocolMixin.on_start(self)

        self.channel = grpc.secure_channel(
            self.address, grpc.local_channel_credentials()
        ).__enter__()
        self.stub = imitation_grpc.ImitationConnectorServiceStub(self.channel)

    def send_startup_msg(self, seeds: List = None, options: List = None):
        start_msg = imitation_messages.ImitationConnectorStartRequest()
        
        if not seeds is None or not options is None:
            
            if seeds is None:
                seeds = [None] * len(options)
            if options is None:
                options = [{} for _ in range(len(seeds))]

            # environments is a map, so we populate it like a dictionary
            for env_id, (seed, option_dict) in enumerate(zip(seeds, options)):
                env_settings = start_msg.environments[env_id]
                if seed is not None:
                    env_settings.seed = seed
                if option_dict:
                    for key, value in option_dict.items():
                        env_settings.options = str(value)
        
        self.stub.StartImitationConnector(
            start_msg, timeout=self.protocol_start_timeout, wait_for_ready=True
        )


    def get_definition(self) -> Tuple[List[List[str]], Dict[int, Dict[str, str]], Dict[int,Dict[str,gym.Space]], Dict[int,Dict[str,gym.Space]]]:
        definition: TrainingDefinition = (
            self.stub.RequestTrainingDefinition(
                imitation_messages.ImitationDefinitionRequest()
            )
        )
        return from_proto(definition)
        
    def get_data(self) -> List[Any]:
        data_request = imitation_messages.ImitationStateRequest()
        data: imitation_state_messages.ImitationState = self.stub.RequestState(data_request)
        return from_proto(data)
        
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