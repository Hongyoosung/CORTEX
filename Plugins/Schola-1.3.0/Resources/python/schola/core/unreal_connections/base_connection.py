# Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.
"""
Base Class for Unreal Connections
"""
from typing import List, Optional, Tuple
import grpc
import os
import socket


class BaseUnrealConnection:
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

    def __init__(self, url: str, port: int):
        self.channel: Optional[grpc.Channel] = None
        self.url = url
        self.port = port

    def close(self) -> None:
        """
        Close the Unreal Connection. Method must be safe to call multiple times.
        """
        if hasattr(self, "channel") and self.channel:
            self.channel.close()
            self.channel = None

    def __del__(self) -> None:
        """
        Destructor for Unreal Connections. Close the connection.
        """
        self.close()

    def start(self) -> None:
        """
        Open the Connection to Unreal Engine.
        """
        # Keepalive options to prevent channel death during long idle periods
        # (e.g., between data collection and RL policy training updates)
        channel_options = [
            ('grpc.keepalive_time_ms', 10000),           # Send keepalive ping every 10s
            ('grpc.keepalive_timeout_ms', 5000),          # Wait 5s for keepalive ack
            ('grpc.keepalive_permit_without_calls', True), # Send pings even when no RPCs
            ('grpc.http2.max_pings_without_data', 0),     # No limit on pings without data
        ]

        # UE5 Schola server uses InsecureServerCredentials().
        # In Docker: must use insecure_channel (cross-network to host).
        # Locally on Windows: local_channel_credentials() also works but
        # insecure is correct since server is insecure.
        if os.environ.get('IS_DOCKER', '').lower() in ('true', '1'):
            self.channel = grpc.insecure_channel(
                self.address, options=channel_options
            ).__enter__()
        else:
            self.channel = grpc.secure_channel(
                self.address, grpc.local_channel_credentials(), options=channel_options
            ).__enter__()

    @property
    def address(self) -> str:
        """
        Returns the address of the connection

        Returns
        -------
        str
            The address of the connection
        """
        return self.url + ":" + str(self.port)

    def connect_stubs(self, *stubs: List["grpc.Stub"]) -> List["grpc.Stub"]:
        """
        Connects the gRPC stubs to the Unreal Engine channel

        Parameters
        ----------
        *stubs : List["grpc.Stub"]
            The gRPC stubs to connect to the Unreal Engine channel
        """

        assert (
            self.channel != None
        ), "Connection has not been started, please create your channel before connecting gRPC stubs"
        return [stub(self.channel) for stub in stubs]

    @property
    def is_active(self) -> bool:
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
        return self.is_active

    def get_open_port(self, url: str) -> Tuple[socket.socket, int]:
        """
        Get an open port on the given URL

        Parameters
        ----------
        url : str
            The URL to get an open port on

        Returns
        -------
        socket.socket
            A socket object bound to the open port
        int
            The open port
        """
        if socket.has_ipv6:
            tcp_socket = socket.socket(socket.AF_INET6)
        else:
            tcp_socket = socket.socket(socket.AF_INET)
        tcp_socket.bind((url, 0))
        port = tcp_socket.getsockname()[1]
        return tcp_socket, port
