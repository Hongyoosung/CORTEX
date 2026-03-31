# Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.
"""
Base Class for Unreal Connections
"""
from typing import Any, Dict, List, Optional, Tuple
import socket
from .base import BaseProtocol, BaseProtocolMixin

class SocketProtocolMixin(BaseProtocolMixin):

    def __init__(self, url: str, port: int = None):
        self.url = url
        self.port = 0 if port is None else port
        self.tcp_socket = None
        
    def on_close(self) -> None:
        """
        Close the Unreal Connection. Method must be safe to call multiple times.
        """
        if self.has_socket:
            self.tcp_socket.close()

    def on_start(self) -> None:
        """
        Bind the tcp_socket
        """
        if not self.has_socket:
            if socket.has_ipv6:
                self.tcp_socket = socket.socket(socket.AF_INET6)
            else:
                self.tcp_socket = socket.socket(socket.AF_INET)
            if self.port == 0:
                self.tcp_socket.bind((self.url, 0))
                self.port = self.tcp_socket.getsockname()[1]
 
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

    @property
    def has_socket(self) -> bool:
        """
        Returns whether the connection is active or not

        Returns
        -------
        bool
            Whether the connection is active or not
        """
        return self.tcp_socket is not None and self.tcp_socket.fileno() != -1

    
    @property
    def mixin_properties(self) -> Dict[str, Any]:
        return {"Port" : self.port}