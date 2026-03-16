# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
"""Tests for exporting RLlib policies to ONNX"""

import functools
from typing import Literal, Optional
import pytest
import gymnasium as gym
import numpy as np
import onnx
import os
from ray import air, tune
from ray.rllib.algorithms.algorithm import Algorithm
from ray.rllib.connectors.env_to_module import FlattenObservations
from schola.rllib.utils import export_onnx_from_policy
from ray.rllib.algorithms.ppo import PPO, PPOConfig
from ray.rllib.algorithms.sac import SAC, SACConfig
from ray.rllib.policy.policy import PolicySpec
from ray.tune.registry import register_env
from ray.rllib.env.multi_agent_env import MultiAgentEnv


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

@pytest.fixture
def algo_config(request):
    algo_name = request.param
    if algo_name == "ppo":
        config = PPOConfig()
        # Set batch sizes directly on the config object
        config.train_batch_size = 512
        config.sgd_minibatch_size = 128
        return config
    elif algo_name == "sac":
        return SACConfig()
    else:
        raise ValueError(f"Unknown algorithm: {algo_name}")

@pytest.fixture
def old_stack_algo_config(request):
    algo_name = request.param
    if algo_name == "ppo":
        return PPOConfig()
    elif algo_name == "sac":
        return SACConfig().training(
            replay_buffer_config={
                "type": "MultiAgentPrioritizedReplayBuffer",
                "capacity": 10000,
            }
        )
    else:
        raise ValueError(f"Unknown algorithm: {algo_name}")


def check_onnx_model(model_path, observation_space, action_space):
    """Check that the ONNX model exists and has the correct input and output names."""
    assert model_path.exists(), f"ONNX file not created at {model_path}"

    model = onnx.load(model_path)

    input_names = [input.name for input in model.graph.input]
    output_names = [output.name for output in model.graph.output]

    if isinstance(observation_space, gym.spaces.Dict):
        assert set(input_names) == set(observation_space.spaces.keys()) | {"state_in"}, "Input names should be the keys of the observation space or 'state_in'"
    else:
        assert input_names == ["obs", "state_in"], f"Model inputs should be ['obs', 'state_in'], if observation space is not a dict. Got {input_names}"

    if isinstance(action_space, gym.spaces.Dict):
        assert set(output_names) == set(action_space.spaces.keys()) | {"state_out"}, "Output names should be the keys of the action space or 'state_out'"
    else:
        assert output_names == ["action", "state_out"], f"Model outputs should be ['action', 'state_out'], if action space is not a dict. Got {output_names}"


# Define test spaces
default_box_space = gym.spaces.Box(low=-1, high=1, shape=(4,), dtype=np.float32)
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
        # Check different action spaces with dict observation space
        ((default_dict_space, default_box_space), alg_name),
        ((default_dict_space, default_discrete_space), alg_name),
        # Skip MultiBinary - not supported by RLlib
        ((default_dict_space, default_multi_discrete_space), alg_name),
        # Check different observation spaces with box action space
        ((default_box_space, default_box_space), alg_name),
        ((default_discrete_space, default_box_space), alg_name),
        # Skip MultiBinary - not supported by RLlib
        ((default_multi_discrete_space, default_box_space), alg_name),
    ]


# Test exporting RLlib policies to ONNX
@pytest.mark.parametrize(
    "env_class,old_stack_algo_config",
    [
        # PPO - supports all action space types
        ((default_box_space, default_box_space), "ppo"),
        ((default_discrete_space, default_box_space), "ppo"),
        # Skip MultiBinary - not supported by RLlib
        ((default_multi_discrete_space, default_box_space), "ppo"),
        # SAC - only supports Box action spaces (no Dict wrapping)
        ((default_box_space, default_box_space), "sac"),
    ],
    indirect=True,
    ids=lambda val: f"{type(val[0]).__name__}-{type(val[1]).__name__}" if isinstance(val, tuple) else val,
)
def test_export_rllib_policy_to_onnx(tmp_path, env_class, old_stack_algo_config):
    """Test exporting RLlib policies to ONNX format."""
    
    # Register the environment
    env_name = f"test_env_{id(env_class)}"
    register_env(env_name, lambda config: env_class())
    
    # Configure the algorithm
    config = (
        old_stack_algo_config
        .environment(env=env_name)
        .framework("torch")
        .env_runners(
            num_env_runners=0,  # Use local runner only for testing
        )
        .multi_agent(
            policies={"default_policy": PolicySpec()},
            policy_mapping_fn=lambda agent_id, *args, **kwargs: "default_policy",
        )
        .api_stack(
            enable_rl_module_and_learner=False,  # Disable RLModule for ONNX export
            enable_env_runner_and_connector_v2=False,
        )
    )
    
    # Build and train the algorithm briefly
    algo = config.build()
    
    try:
        # Train for just one iteration to initialize the policy
        algo.train()
        
        # Get the policy
        policy = algo.get_policy("default_policy")
        onnx_file = tmp_path / "test_policy" / "model.onnx"

        # Export to ONNX
        export_onnx_from_policy(policy, onnx_file)
        
        # Verify the ONNX file was created
        check_onnx_model(onnx_file, env_class().observation_space, env_class().action_space)
        
    finally:
        # Clean up
        algo.stop() 

# Test exporting RLlib policies to ONNX
@pytest.mark.parametrize(
    "env_class,algo_config",
    [
        # PPO - supports all action space types
        ((default_box_space, default_box_space), "ppo"),
        ((default_discrete_space, default_box_space), "ppo"),
        # Skip MultiBinary - not supported by RLlib
        ((default_multi_discrete_space, default_box_space), "ppo"),
        # SAC - only supports Box action spaces (no Dict wrapping)
        ((default_box_space, default_box_space), "sac"),
    ],
    indirect=True,
    ids=lambda val: f"{type(val[0]).__name__}-{type(val[1]).__name__}" if isinstance(val, tuple) else val,
)
def test_export_rllib_rl_module_to_onnx(tmp_path, env_class, algo_config):
    """Test exporting RLlib policies to ONNX format."""
    
    # Register the environment
    env_name = f"test_env_{id(env_class)}"
    register_env(env_name, lambda config: env_class())
    
    # Configure the algorithm
    config = (
        algo_config
        .environment(env=env_name)
        .framework("torch")
        .env_runners(
            num_env_runners=0,  # Use local runner only for testing
            env_to_module_connector=lambda env: FlattenObservations(
                input_observation_space=env.single_observation_space,
                input_action_space=env.single_action_space,
                multi_agent=False,
            ),
        )
        .multi_agent(
            policies={"default_policy": PolicySpec()},
            policy_mapping_fn=lambda agent_id, *args, **kwargs: "default_policy",
        )
        .api_stack(
            enable_rl_module_and_learner=True,  # Enable RLModule for ONNX export
            enable_env_runner_and_connector_v2=True,
        )
    )
    
    # Build and train the algorithm briefly
    algo = config.build()
    
    try:
        # Train for just one iteration to initialize the policy
        algo.train()
        
        onnx_file = tmp_path / "model.onnx"
        # Export to ONNX
        export_onnx_from_policy(algo.get_module(), onnx_file)
        
        # Verify the ONNX file was created
        check_onnx_model(onnx_file, env_class().observation_space, env_class().action_space)
    finally:
        # Clean up
        algo.stop()


# Test exporting RLlib RL modules with state dimension to ONNX
@pytest.mark.parametrize(
    "env_class,algo_config",
    [
        # PPO with Box observation and action spaces
        ((default_box_space, default_box_space), "ppo"),
    ],
    indirect=True,
    ids=lambda val: f"{type(val[0]).__name__}-{type(val[1]).__name__}" if isinstance(val, tuple) else val,
)
def test_export_rllib_rl_module_with_state_to_onnx(tmp_path, env_class, algo_config):
    """Test exporting RLlib RL modules with state dimension to ONNX format."""
    
    # Register the environment
    env_name = f"test_env_{id(env_class)}"
    register_env(env_name, lambda config: env_class())
    
    # Configure the algorithm with LSTM to enable state
    config = (
        algo_config
        .environment(env=env_name)
        .framework("torch")
        .env_runners(
            num_env_runners=0,  # Use local runner only for testing
            env_to_module_connector=lambda env: FlattenObservations(
                input_observation_space=env.single_observation_space,
                input_action_space=env.single_action_space,
                multi_agent=False,
            ),
        )
        .multi_agent(
            policies={"default_policy": PolicySpec()},
            policy_mapping_fn=lambda agent_id, *args, **kwargs: "default_policy",
        )
        .api_stack(
            enable_rl_module_and_learner=True,  # Enable RLModule for ONNX export
            enable_env_runner_and_connector_v2=True,
        )
        .rl_module(
             model_config={
                    "use_attention": True,
                    "attention_dim": 64,
            },
        )
    )
    
    # Build and train the algorithm briefly
    algo = config.build()
    
    try:
        # Train for just one iteration to initialize the policy
        algo.train()
        
        onnx_file = tmp_path / "model_with_state.onnx"
        # Export to ONNX
        export_onnx_from_policy(algo.get_module(), onnx_file)
        
        # Verify the ONNX file was created
        assert onnx_file.exists(), f"ONNX file not created at {onnx_file}"
        
        # Verify the ONNX model has state inputs/outputs
        check_onnx_model(onnx_file, env_class().observation_space, env_class().action_space)
        
    finally:
        # Clean up
        algo.stop()


@pytest.mark.parametrize(
    "env_class,algo_config",
    [
        # PPO - supports all action space types
        *make_all_combinations("ppo"),
        # SAC - only supports Box action spaces (no Dict wrapping)
        ((default_box_space, default_box_space), "sac"),
        ((default_dict_space, default_box_space), "sac"),
    ],
    indirect=True,
    ids=lambda val: f"{type(val[0]).__name__}-{type(val[1]).__name__}" if isinstance(val, tuple) else val,
)
def test_export_rllib_algorithm_to_onnx(tmp_path, env_class, algo_config):
    """Test exporting RLlib algorithms to ONNX format from checkpoint using tune.run()."""
    
    # Use shorter environment name to avoid Windows path length issues
    env_name = "test_env"
    register_env(env_name, lambda config: env_class())
    
    # Configure the algorithm
    config = (
        algo_config
        .environment(env=env_name)
        .framework("torch")
        .env_runners(
            num_env_runners=0,  # Use local runner only for testing
            env_to_module_connector=lambda env: FlattenObservations(
                input_observation_space=env.single_observation_space,
                input_action_space=env.single_action_space,
                multi_agent=False,
            ),
        )
        .multi_agent(
            policies={"default_policy": PolicySpec()},
            policy_mapping_fn=lambda agent_id, *args, **kwargs: "default_policy",
        )
        .api_stack(
            enable_rl_module_and_learner=True,  # Enable RLModule for ONNX export
            enable_env_runner_and_connector_v2=True,
        )
    )
    
    # Determine which algorithm class to use based on the config type
    algo_class = PPO if isinstance(algo_config, PPOConfig) else SAC
    
    # Disable TensorBoard to avoid Windows path issues in tests
    # Set environment variable before running
    old_disable_tb = os.environ.get('TUNE_DISABLE_AUTO_CALLBACK_LOGGERS')
    os.environ['TUNE_DISABLE_AUTO_CALLBACK_LOGGERS'] = '1'
    
    try:
        # Mirror train.py approach with tune.run()
        # Use short experiment name and custom trial name to avoid Windows path length limits
        results = tune.run(
            algo_class,
            name="exp",  # Short experiment name
            config=config,
            stop={"num_env_steps_sampled_lifetime": 1000},
            verbose=0,
            storage_path=str(tmp_path / "out"),  # Shorter storage path
            checkpoint_config=air.CheckpointConfig(
                checkpoint_at_end=True,
            ),
            trial_dirname_creator=lambda trial: "trial",  # Very short trial name
        )
    finally:
        # Restore environment variable
        if old_disable_tb is None:
            os.environ.pop('TUNE_DISABLE_AUTO_CALLBACK_LOGGERS', None)
        else:
            os.environ['TUNE_DISABLE_AUTO_CALLBACK_LOGGERS'] = old_disable_tb
    last_checkpoint = results.get_last_checkpoint()
    
    # Export to ONNX from checkpoint (mirroring train.py)
    algo_from_checkpoint = Algorithm.from_checkpoint(last_checkpoint)
    onnx_file = tmp_path / "model.onnx"
    # Export the RLModule directly since the connector is not restored from checkpoint
    export_onnx_from_policy(algo_from_checkpoint, onnx_file)
    
    # Verify the ONNX file was created
    check_onnx_model(onnx_file, env_class().observation_space, env_class().action_space)
    