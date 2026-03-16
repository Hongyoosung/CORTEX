# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
"""Tests for exporting SB3 policies to ONNX"""

import functools
from typing import Literal, Optional
import pytest
import gymnasium as gym
import numpy as np
from schola.sb3.utils import VecMergeDictActionWrapper, save_model_as_onnx
import stable_baselines3 as sb3

import gymnasium as gym
from schola.sb3.env import VecEnv
from schola.core.protocols.protobuf.gRPC import gRPCProtocol
from schola.core.simulators.unreal.editor import UnrealEditor
from schola.scripts.common import gRPCProtocolArgs
from schola.scripts.sb3.train import ppo
from schola.scripts.sb3.settings import SB3ScriptArgs
import onnx


def check_onnx_model(model_path, observation_space, action_space):
    """Check that the ONNX model exists and has the correct input and output names."""
    assert model_path.exists(), f"ONNX file not created at {model_path}"

    model = onnx.load(model_path)

    input_names = [input.name for input in model.graph.input]
    output_names = [output.name for output in model.graph.output]

    # Check if input and output names are correct
    if isinstance(observation_space, gym.spaces.Dict):
        assert set(input_names) == set(observation_space.spaces.keys()) | {"state_in"}, "Input names should be the keys of the observation space or 'state_in'"
    else:
        assert input_names == ["obs", "state_in"], f"Model inputs should be ['obs','state_in'], if observation space is not a dict. Got {input_names}"

    if isinstance(action_space, gym.spaces.Dict):
        assert set(output_names) == set(action_space.spaces.keys()) | {"state_out"}, "Output names should be the keys of the action space or 'state_out'"
    else:
        assert output_names == ["action", "state_out"], f"Model outputs should be ['action', 'state_out'], if action space is not a dict. Got {output_names}"



# Test Exporting


@pytest.fixture
def env_class(request):
    obs_space, action_space = request.param

    # test env is a gym env with a dictionary observation space and dictionary action space
    class TestEnv(gym.Env):
        def __init__(self):
            self.observation_space = obs_space
            self.action_space = action_space
            
            super().__init__()

        def reset(self, seed=None, options=None):
            super().reset(seed=seed)
            observation = self.observation_space.sample()
            info = {}
            return observation, info

        def step(self, action):
            observation = self.observation_space.sample()
            reward = 0
            terminated = False
            truncated = False
            info = {}
            return observation, reward, terminated, truncated, info

    return TestEnv

from stable_baselines3 import PPO, SAC, TD3, DDPG, A2C, DQN

@pytest.fixture
def algo(request):
    buffer_size = 10000
    algo_name = request.param
    if algo_name == "ppo":
        return sb3.PPO
    elif algo_name == "sac":
        return lambda *args, **kwargs: sb3.SAC(*args, **kwargs, buffer_size=buffer_size)
    elif algo_name == "td3":
        return lambda *args, **kwargs: sb3.TD3(*args, **kwargs, buffer_size=buffer_size)
    elif algo_name == "ddpg":
        return lambda *args, **kwargs: sb3.DDPG(
            *args, **kwargs, buffer_size=buffer_size
        )
    elif algo_name == "a2c":
        return lambda *args, **kwargs: sb3.A2C(*args, **kwargs)
    elif algo_name == "dqn":
        return lambda *args, **kwargs: sb3.DQN(*args, **kwargs, buffer_size=buffer_size)
    else:
        raise ValueError(f"Unknown algorithm: {algo_name}")


from stable_baselines3.common.base_class import BaseAlgorithm
from stable_baselines3.common.env_util import make_vec_env

default_box_space = gym.spaces.Box(low=0, high=1, shape=(84, 84, 3))
default_discrete_space = gym.spaces.Discrete(2)
default_binary_space = gym.spaces.MultiBinary(2)
default_multi_discrete_space = gym.spaces.MultiDiscrete([2, 3])
default_dict_space = gym.spaces.Dict(
    {
        "box": default_box_space,
        "discrete": default_discrete_space,
        "binary": default_binary_space,
        "multi_discrete": default_multi_discrete_space,
    }
)

def make_all_combinations(alg_name):
    return [
        # Check different action spaces
        ((default_dict_space, default_box_space), alg_name),
        ((default_dict_space, default_discrete_space), alg_name),
        ((default_dict_space, default_binary_space), alg_name),
        ((default_dict_space, default_multi_discrete_space), alg_name),
         # Check different observation spaces
        ((default_box_space, default_box_space), alg_name),
        ((default_discrete_space, default_box_space), alg_name),
        ((default_binary_space, default_box_space), alg_name),
        ((default_multi_discrete_space, default_box_space), alg_name),
    ]

# Test exporting SB3 policies to ONNX, tests with every algorithm from SB3
@pytest.mark.parametrize(
    "env_class,algo",
    [
        # PPO
        *make_all_combinations("ppo"),
        # SAC
        # try all obs spaces but only Box action spaces are supported
        ((default_box_space, default_box_space), "sac"),
        ((default_discrete_space, default_box_space), "sac"),
        ((default_binary_space, default_box_space), "sac"),
        ((default_multi_discrete_space, default_box_space), "sac"),
        ((default_dict_space, default_box_space), "sac"),
        # TD3
        ((default_box_space, default_box_space), "td3"),
        ((default_discrete_space, default_box_space), "td3"),
        ((default_binary_space, default_box_space), "td3"),
        ((default_multi_discrete_space, default_box_space), "td3"),
        ((default_dict_space, default_box_space), "td3"),
        # DDPG
        ((default_box_space, default_box_space), "ddpg"),
        ((default_discrete_space, default_box_space), "ddpg"),
        ((default_binary_space, default_box_space), "ddpg"),
        ((default_multi_discrete_space, default_box_space), "ddpg"),
        ((default_dict_space, default_box_space), "ddpg"),
        # A2C
        *make_all_combinations("a2c"),
        # DQN
        ((default_box_space, default_discrete_space), "dqn"),
        #((default_discrete_space, default_discrete_space), "dqn"),
        ((default_binary_space, default_discrete_space), "dqn"),
        #((default_multi_discrete_space, default_discrete_space), "dqn"),
        #((default_dict_space, default_discrete_space), "dqn"),
    ],
    indirect=True,
    ids=lambda val: f"{type(val[0]).__name__}-{type(val[1]).__name__}" if isinstance(val, tuple) else val,
    )
def test_export_sb3_policy_to_onnx(tmp_path, env_class, algo):

    # Create a dummy environment
    env = make_vec_env(env_class, 2, seed=1)  # prevents an error with automatic seeding
    # Create a dummy model
    model: BaseAlgorithm = algo("MultiInputPolicy" if isinstance(env_class().observation_space, gym.spaces.Dict) else "MlpPolicy", env, verbose=1)
    # Train the model
    model.__original_action_space = env.unwrapped.action_space

    save_model_as_onnx(model, tmp_path / "test.onnx")

    check_onnx_model(tmp_path / "test.onnx", env_class().observation_space, env_class().action_space)
