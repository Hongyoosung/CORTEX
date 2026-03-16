# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

from concurrent import futures
from typing import Callable, List
import grpc

import pytest
from functools import cache
from typing import Dict, List, Optional, Any, Tuple
import pytest

from schola.gym import env

from .envs.gym_server import GymToGymServiceServicer, VecGymToGymServiceServicer
from .envs.imitation_server import GymToImitationServiceServicer
from .envs.pettingzoo_server import PettingZooToGymServiceServicer, VecPettingZooToGymServiceServicer
import schola.generated.GymConnector_pb2_grpc as gym_connector_grpc
import schola.generated.ImitationConnector_pb2_grpc as imitation_connector_grpc
import minigrid
from minigrid.wrappers import DictObservationSpaceWrapper
import gymnasium as gym

@pytest.fixture(scope="function")
def make_env_server():
    
    servers = []
    def _make_env_server(env_name : str | Callable[..., gym.Env], wrappers: list = None, port:int = 0):
        servicer = GymToGymServiceServicer(env_name, wrappers)
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=1))
        gym_connector_grpc.add_GymServiceServicer_to_server(servicer, server)
        port = server.add_insecure_port(f"[::]:{port}")
        server.start()
        servers.append(server)
        return port
    
    yield _make_env_server

    for server in servers:
        server.stop(0)

    for server in servers:
        server.wait_for_termination()

@pytest.fixture(scope="function")
def make_vec_env_server():
    
    servers = []
    def _make_env_server(env_name : List[Callable[..., gym.Env]], wrappers: list = None, port:int = 0):
        servicer = VecGymToGymServiceServicer(env_name, wrappers)
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=1))
        gym_connector_grpc.add_GymServiceServicer_to_server(servicer, server)
        port = server.add_insecure_port(f"[::]:{port}")
        server.start()
        servers.append(server)
        return port
    
    yield _make_env_server

    for server in servers:
        server.stop(0)

    for server in servers:
        server.wait_for_termination()



@pytest.fixture(scope="function")
def make_pettingzoo_env_server():
    """Create a PettingZoo environment server (single environment)."""
    servers = []
    def _make_env_server(env_name: str | Callable, wrappers: list = None, port: int = 0):
        servicer = PettingZooToGymServiceServicer(env_name, wrappers)
        # Set max message sizes to 100MB to handle large environment definitions
        options = [
            ('grpc.max_send_message_length', 100 * 1024 * 1024),
            ('grpc.max_receive_message_length', 100 * 1024 * 1024),
        ]
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=1), options=options)
        gym_connector_grpc.add_GymServiceServicer_to_server(servicer, server)
        port = server.add_insecure_port(f"[::]:{port}")
        server.start()
        servers.append(server)
        return port
    
    yield _make_env_server

    for server in servers:
        server.stop(0)

    for server in servers:
        server.wait_for_termination()


@pytest.fixture(scope="function")
def make_vec_pettingzoo_env_server():
    """Create a vectorized PettingZoo environment server (multiple environments)."""
    servers = []
    def _make_env_server(env_funcs: List[Callable], wrappers: list = None, port: int = 0):
        servicer = VecPettingZooToGymServiceServicer(env_funcs, wrappers)
        # Set max message sizes to 100MB to handle large messages
        options = [
            ('grpc.max_send_message_length', 100 * 1024 * 1024),
            ('grpc.max_receive_message_length', 100 * 1024 * 1024),
        ]
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=1), options=options)
        gym_connector_grpc.add_GymServiceServicer_to_server(servicer, server)
        port = server.add_insecure_port(f"[::]:{port}")
        server.start()
        servers.append(server)
        return port
    
    yield _make_env_server

    for server in servers:
        server.stop(0)

    for server in servers:
        server.wait_for_termination()


@pytest.fixture(scope="function", params=[("CartPole-v1",None), ("MountainCar-v0",None), ("MiniGrid-DoorKey-5x5-v0", [DictObservationSpaceWrapper])], ids=lambda x: x[0])
def gym_id_and_wrappers(request):
    env_id, wrappers = request.param
    return env_id, wrappers
