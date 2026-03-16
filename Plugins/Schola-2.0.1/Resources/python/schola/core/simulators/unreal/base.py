# Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.
"""
Base Class for Unreal Connections
"""
from typing import Any, Dict, List, Optional, Tuple
import grpc
import socket
from schola.core.protocols.base import BaseProtocol
from schola.core.protocols.protobuf.gRPC import gRPCProtocol
from schola.core.simulators.base import BaseSimulator 

class BaseUnrealSimulator(BaseSimulator):
    """
    Abstract Base Class for a gRPC based connection to Unreal Engine.

    Parameters
    ----------
    url : str
        The url to connect to

    port : int
        The port on that URL to connect to

    Attributes
    ----------
    url: str
        The URL to connect to
    port: int
        The port on the URL to connect to
    channel: grpc.Channel
        The channel connecting to Unreal Engine on the chosen address
    """

    def __init__(self):
        ...


    @property
    def supported_protocols(self) -> Tuple[BaseProtocol,...]:
        return (gRPCProtocol,)
