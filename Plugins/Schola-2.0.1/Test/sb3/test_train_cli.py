# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
"""Tests for the SB3 CLI"""

import pytest
import gymnasium as gym
from unittest.mock import Mock, patch, MagicMock
from dataclasses import replace
from schola.scripts.sb3.train import app, ppo, sac, main
from schola.scripts.sb3.settings import SB3ScriptArgs, PPOSettings, SACSettings
from schola.scripts.common import gRPCProtocolArgs, UnrealEditorSimulatorArgs, UnrealExecutableSimulatorArgs, ActivationFunctionEnum, EnvironmentArgs


@pytest.mark.parametrize("gym_id", ["CartPole-v1", "MountainCar-v0"])
def test_launch_script(make_vec_env_server, gym_id):
    """Test that the PPO script can be launched with a gRPC server."""
    N_ENVS=3
    env_server_port = make_vec_env_server([gym.make(gym_id) for _ in range(N_ENVS)])

    # Create SB3ScriptArgs with protocol nested under environment_settings
    args = SB3ScriptArgs(
        environment_settings=EnvironmentArgs(
            protocol=gRPCProtocolArgs(port=env_server_port, url="localhost")
        )
    )
    ppo(args, simulator_type="editor")


# CLI Mocking Tests - verify CLI argument parsing creates correct settings classes

@patch('schola.scripts.sb3.train.main')
def test_ppo_cli_default_args(mock_main):
    """Test PPO command with default arguments creates correct settings."""
    # Parse CLI args with Cyclopts (returns tuple)
    command, bound, _ = app.parse_args(['ppo'], exit_on_error=False)
    
    # Execute the parsed command (which calls the mocked main)
    command(*bound.args, **bound.kwargs)
    
    # Verify main was called once
    assert mock_main.call_count == 1
    
    # Get the SB3ScriptArgs that was passed to main
    args = mock_main.call_args[0][0]
    
    # Verify it's the correct type
    assert isinstance(args, SB3ScriptArgs)
    
    # Verify algorithm settings is PPO
    assert isinstance(args.algorithm_settings, PPOSettings)
    
    # Verify default PPO parameters
    assert args.algorithm_settings.learning_rate == 0.0003
    assert args.algorithm_settings.n_steps == 2048
    assert args.algorithm_settings.batch_size == 64
    assert args.algorithm_settings.n_epochs == 10
    assert args.algorithm_settings.gamma == 0.99
    assert args.algorithm_settings.gae_lambda == 0.95
    assert args.algorithm_settings.clip_range == 0.2
    assert args.algorithm_settings.normalize_advantage == True
    assert args.algorithm_settings.ent_coef == 0.0
    assert args.algorithm_settings.vf_coef == 0.5


@patch('schola.scripts.sb3.train.main')
def test_ppo_cli_custom_args(mock_main):
    """Test PPO command with custom arguments."""
    # Parse CLI args with custom PPO parameters
    command, bound, _ = app.parse_args([
        'ppo',
        '--learning-rate', '0.001',
        '--n-steps', '1024',
        '--batch-size', '128',
        '--n-epochs', '5',
        '--gamma', '0.95',
        '--gae-lambda', '0.9',
        '--clip-range', '0.3',
        '--ent-coef', '0.01',
        '--vf-coef', '0.25',
        '--timesteps', '10000',
    ], exit_on_error=False)
    
    command(*bound.args, **bound.kwargs)
    
    assert mock_main.call_count == 1
    args = mock_main.call_args[0][0]
    
    # Verify custom PPO parameters
    assert args.algorithm_settings.learning_rate == 0.001
    assert args.algorithm_settings.n_steps == 1024
    assert args.algorithm_settings.batch_size == 128
    assert args.algorithm_settings.n_epochs == 5
    assert args.algorithm_settings.gamma == 0.95
    assert args.algorithm_settings.gae_lambda == 0.9
    assert args.algorithm_settings.clip_range == 0.3
    assert args.algorithm_settings.ent_coef == 0.01
    assert args.algorithm_settings.vf_coef == 0.25
    
    # Verify top-level args
    assert args.timesteps == 10000


@patch('schola.scripts.sb3.train.main')
def test_sac_cli_default_args(mock_main):
    """Test SAC command with default arguments creates correct settings."""
    command, bound, _ = app.parse_args(['sac'], exit_on_error=False)
    command(*bound.args, **bound.kwargs)
    
    assert mock_main.call_count == 1
    args = mock_main.call_args[0][0]
    
    # Verify it's the correct type
    assert isinstance(args, SB3ScriptArgs)
    
    # Verify algorithm settings is SAC
    assert isinstance(args.algorithm_settings, SACSettings)
    
    # Verify default SAC parameters
    assert args.algorithm_settings.learning_rate == 0.0003
    assert args.algorithm_settings.buffer_size == 1000000
    assert args.algorithm_settings.learning_starts == 100
    assert args.algorithm_settings.batch_size == 256
    assert args.algorithm_settings.tau == 0.005
    assert args.algorithm_settings.gamma == 0.99
    assert args.algorithm_settings.train_freq == 1
    assert args.algorithm_settings.gradient_steps == 1


@patch('schola.scripts.sb3.train.main')
def test_sac_cli_custom_args(mock_main):
    """Test SAC command with custom arguments."""
    command, bound, _ = app.parse_args([
        'sac',
        '--learning-rate', '0.0005',
        '--buffer-size', '500000',
        '--learning-starts', '200',
        '--batch-size', '512',
        '--tau', '0.01',
        '--gamma', '0.98',
        '--train-freq', '2',
        '--gradient-steps', '2',
        '--timesteps', '50000',
    ], exit_on_error=False)
    
    command(*bound.args, **bound.kwargs)
    
    assert mock_main.call_count == 1
    args = mock_main.call_args[0][0]
    
    # Verify custom SAC parameters
    assert args.algorithm_settings.learning_rate == 0.0005
    assert args.algorithm_settings.buffer_size == 500000
    assert args.algorithm_settings.learning_starts == 200
    assert args.algorithm_settings.batch_size == 512
    assert args.algorithm_settings.tau == 0.01
    assert args.algorithm_settings.gamma == 0.98
    assert args.algorithm_settings.train_freq == 2
    assert args.algorithm_settings.gradient_steps == 2
    
    assert args.timesteps == 50000


@patch('schola.scripts.sb3.train.main')
def test_ppo_network_architecture_args(mock_main):
    """Test that network architecture arguments are correctly parsed."""
    command, bound, _ = app.parse_args([
        'ppo',
        '--policy-parameters', '128', '128', '64',
        '--critic-parameters', '256', '128',
        '--activation', 'TanH',
    ], exit_on_error=False)
    
    command(*bound.args, **bound.kwargs)
    
    args = mock_main.call_args[0][0]
    
    # Verify network architecture settings
    assert args.network_architecture_settings.policy_parameters == [128, 128, 64]
    assert args.network_architecture_settings.critic_parameters == [256, 128]
    assert args.network_architecture_settings.activation == ActivationFunctionEnum.TanH


@patch('schola.scripts.sb3.train.main')
def test_ppo_logging_args(mock_main):
    """Test that logging arguments are correctly parsed."""
    command, bound, _ = app.parse_args([
        'ppo',
        '--enable-tensorboard',
        '--log-dir', './test_logs',
        '--log-freq', '100',
        '--callback-verbosity', '2',
        '--schola-verbosity', '1',
        '--sb3-verbosity', '2',
    ], exit_on_error=False)
    
    command(*bound.args, **bound.kwargs)
    
    args = mock_main.call_args[0][0]
    
    # Verify logging settings
    assert args.logging_settings.enable_tensorboard == True
    assert str(args.logging_settings.log_dir) == 'test_logs'
    assert args.logging_settings.log_freq == 100
    assert args.logging_settings.callback_verbosity == 2
    assert args.logging_settings.schola_verbosity == 1
    assert args.logging_settings.sb3_verbosity == 2


@patch('schola.scripts.sb3.train.main')
def test_ppo_checkpoint_args(mock_main):
    """Test that checkpoint arguments are correctly parsed."""
    command, bound, _ = app.parse_args([
        'ppo',
        '--save-freq', '5000',
        '--save-replay-buffer',
        '--save-vecnormalize',
    ], exit_on_error=False)
    
    command(*bound.args, **bound.kwargs)
    
    args = mock_main.call_args[0][0]
    
    # Verify checkpoint settings
    assert args.checkpoint_settings.save_freq == 5000
    assert args.checkpoint_settings.save_replay_buffer == True
    assert args.checkpoint_settings.save_vecnormalize == True


@patch('schola.scripts.sb3.train.main')
def test_sac_with_executable_simulator(mock_main):
    """Test SAC command with executable simulator type is correctly parsed."""
    command, bound, _ = app.parse_args([
        'sac',
        '--simulator-type', 'executable',
    ], exit_on_error=False)
    
    command(*bound.args, **bound.kwargs)
    
    args = mock_main.call_args[0][0]
    
    # Note: use_executable alone may not work due to mutually exclusive group defaults
    # This test documents the current behavior - may default to editor
    # For actual executable usage, both flags may need to be explicitly set
    assert isinstance(args.environment_settings.simulator, UnrealExecutableSimulatorArgs)


@patch('schola.scripts.sb3.train.main')
def test_ppo_with_editor_simulator(mock_main):
    """Test that editor simulator type is correctly parsed."""
    command, bound, _ = app.parse_args([
        'ppo',
        '--simulator-type', 'editor',
    ], exit_on_error=False)
    
    command(*bound.args, **bound.kwargs)
    
    args = mock_main.call_args[0][0]
    
    # Verify simulator is UnrealEditorSimulatorArgs
    assert isinstance(args.environment_settings.simulator, UnrealEditorSimulatorArgs)


@patch('schola.scripts.sb3.train.main')
@pytest.mark.parametrize("algorithm,settings_class", [
    ("ppo", PPOSettings),
    ("sac", SACSettings),
])
def test_algorithm_settings_type(mock_main, algorithm, settings_class):
    """Parametrized test to verify correct settings class for each algorithm."""
    command, bound, _ = app.parse_args([algorithm], exit_on_error=False)
    command(*bound.args, **bound.kwargs)
    
    args = mock_main.call_args[0][0]
    assert isinstance(args.algorithm_settings, settings_class)


@patch('schola.scripts.sb3.train.main')
def test_ppo_with_pbar(mock_main):
    """Test that progress bar flag is correctly parsed."""
    command, bound, _ = app.parse_args([
        'ppo',
        '--pbar',
    ], exit_on_error=False)
    
    command(*bound.args, **bound.kwargs)
    
    args = mock_main.call_args[0][0]
    assert args.pbar == True


@patch('schola.scripts.sb3.train.main')
def test_ppo_with_disable_eval(mock_main):
    """Test that disable-eval flag is correctly parsed."""
    command, bound, _ = app.parse_args([
        'ppo',
        '--disable-eval',
    ], exit_on_error=False)
    
    command(*bound.args, **bound.kwargs)
    
    args = mock_main.call_args[0][0]
    assert args.disable_eval == True


@patch('schola.scripts.sb3.train.main')
def test_sac_with_sde(mock_main):
    """Test SAC with state-dependent exploration arguments."""
    command, bound, _ = app.parse_args([
        'sac',
        '--use-sde',
        '--sde-sample-freq', '10',
    ], exit_on_error=False)
    
    command(*bound.args, **bound.kwargs)
    
    args = mock_main.call_args[0][0]
    assert args.algorithm_settings.use_sde == True
    assert args.algorithm_settings.sde_sample_freq == 10


@patch('schola.scripts.sb3.train.main')
def test_ppo_complex_scenario(mock_main):
    """Test PPO with a complex combination of arguments."""
    command, bound, _ = app.parse_args([
        'ppo',
        '--timesteps', '100000',
        '--learning-rate', '0.0001',
        '--n-steps', '512',
        '--batch-size', '128',
        '--n-epochs', '20',
        '--policy-parameters', '64', '64',
        '--critic-parameters', '64', '64',
        '--activation', 'Sigmoid',
        '--enable-tensorboard',
        '--log-dir', './complex_logs',
        '--save-freq', '10000',
        '--pbar',
    ], exit_on_error=False)
    
    command(*bound.args, **bound.kwargs)
    
    args = mock_main.call_args[0][0]
    
    # Verify multiple settings are correctly combined
    assert args.timesteps == 100000
    assert args.algorithm_settings.learning_rate == 0.0001
    assert args.algorithm_settings.n_steps == 512
    assert args.algorithm_settings.batch_size == 128
    assert args.network_architecture_settings.policy_parameters == [64, 64]
    assert args.network_architecture_settings.activation == ActivationFunctionEnum.Sigmoid
    assert args.logging_settings.enable_tensorboard == True
    assert args.checkpoint_settings.save_freq == 10000
    assert args.pbar == True
