# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
"""Tests for the rllib cli"""


import pytest
from pathlib import Path
from schola.scripts.common import UnrealExecutableSimulatorArgs
from schola.scripts.rllib.train import app as train_app
from schola.scripts.rllib.settings import (
    RLlibScriptArgs,
    PPOSettings,
    SACSettings,
    IMPALASettings,
    TrainingSettings,
)
from schola.scripts.common import (
    UnrealEditorSimulatorArgs,
    ActivationFunctionEnum,
)


@pytest.fixture
def mock_main(mocker):
    """Mock the main training function to prevent actual training."""
    return mocker.patch("schola.scripts.rllib.train.main")

def test_ppo_default_arguments(mock_main):
    """Test PPO command with default arguments."""
    # Call with empty list to avoid reading from sys.argv
    train_app(["ppo"], result_action="return_value")
    
    # Verify main was called once
    mock_main.assert_called_once()
    
    # Extract the arguments passed to main
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify it's the correct dataclass
    assert isinstance(args, RLlibScriptArgs)
    
    # Verify algorithm settings
    assert isinstance(args.algorithm_settings, PPOSettings)
    assert args.algorithm_settings.gae_lambda == 0.95
    assert args.algorithm_settings.clip_param == 0.2
    assert args.algorithm_settings.use_gae is True
    
    # Verify default training settings
    assert isinstance(args.training_settings, TrainingSettings)
    assert args.training_settings.timesteps == 3000
    assert args.training_settings.learning_rate == 0.0003
    assert args.training_settings.gamma == 0.99
    
    # Verify default simulator is editor
    assert isinstance(args.environment_settings.simulator, UnrealEditorSimulatorArgs)


def test_ppo_custom_training_parameters(mock_main):
    """Test PPO command with custom training parameters."""
    train_app([
        "ppo",
        "--training-settings.timesteps", "10000",
        "--training-settings.learning-rate", "0.001",
        "--training-settings.gamma", "0.95",
        "--training-settings.minibatch-size", "64",
        "--training-settings.train-batch-size-per-learner", "256",
        "--training-settings.num-sgd-iter", "10"
    ], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify custom training settings
    assert args.training_settings.timesteps == 10000
    assert args.training_settings.learning_rate == 0.001
    assert args.training_settings.gamma == 0.95
    assert args.training_settings.minibatch_size == 64
    assert args.training_settings.train_batch_size_per_learner == 256
    assert args.training_settings.num_sgd_iter == 10


def test_ppo_custom_algorithm_parameters(mock_main):
    """Test PPO command with custom PPO-specific parameters."""
    train_app([
        "ppo",
        "--gae-lambda", "0.90",
        "--clip-param", "0.3",
        "--no-use-gae"
    ], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify custom PPO settings
    assert args.algorithm_settings.gae_lambda == 0.90
    assert args.algorithm_settings.clip_param == 0.3
    assert args.algorithm_settings.use_gae is False


def test_sac_default_arguments(mock_main):
    """Test SAC command with default arguments."""
    train_app(["sac"], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify it's using SAC settings
    assert isinstance(args.algorithm_settings, SACSettings)
    assert args.algorithm_settings.tau == 0.005
    assert args.algorithm_settings.target_entropy == "auto"
    assert args.algorithm_settings.initial_alpha == 1.0
    assert args.algorithm_settings.n_step == 1
    assert args.algorithm_settings.twin_q is True


def test_sac_custom_parameters(mock_main):
    """Test SAC command with custom SAC-specific parameters."""
    train_app([
        "sac",
        "--tau", "0.01",
        "--initial-alpha", "0.5",
        "--n-step", "3",
        "--no-twin-q"
    ], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify custom SAC settings
    assert args.algorithm_settings.tau == 0.01
    assert args.algorithm_settings.initial_alpha == 0.5
    assert args.algorithm_settings.n_step == 3
    assert args.algorithm_settings.twin_q is False


def test_impala_default_arguments(mock_main):
    """Test IMPALA command with default arguments."""
    train_app(["impala"], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify it's using IMPALA settings
    assert isinstance(args.algorithm_settings, IMPALASettings)
    assert args.algorithm_settings.vtrace is True
    assert args.algorithm_settings.vtrace_clip_rho_threshold == 1.0
    assert args.algorithm_settings.vtrace_clip_pg_rho_threshold == 1.0


def test_impala_custom_parameters(mock_main):
    """Test IMPALA command with custom IMPALA-specific parameters."""
    train_app([
        "impala",
        "--no-vtrace",
        "--vtrace-clip-rho-threshold", "2.0",
        "--vtrace-clip-pg-rho-threshold", "1.5"
    ], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify custom IMPALA settings
    assert args.algorithm_settings.vtrace is False
    assert args.algorithm_settings.vtrace_clip_rho_threshold == 2.0
    assert args.algorithm_settings.vtrace_clip_pg_rho_threshold == 1.5


def test_resource_settings(mock_main):
    """Test resource allocation parameters."""
    train_app([
        "ppo",
        "--resource-settings.num-gpus", "2",
        "--resource-settings.num-cpus", "8",
        "--resource-settings.num-learners", "4",
        "--resource-settings.num-cpus-per-learner", "2",
        "--resource-settings.num-gpus-per-learner", "1"
    ], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify resource settings
    assert args.resource_settings.num_gpus == 2
    assert args.resource_settings.num_cpus == 8
    assert args.resource_settings.num_learners == 4
    assert args.resource_settings.num_cpus_per_learner == 2
    assert args.resource_settings.num_gpus_per_learner == 1


def test_network_architecture_settings(mock_main):
    """Test network architecture parameters."""
    train_app([
        "ppo",
        "--network-architecture-settings.activation", "TanH",
        "--network-architecture-settings.use-attention",
        "--network-architecture-settings.attention-dim", "128",
        "--network-architecture-settings.fcnet-hiddens", "256", "256"
    ], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify network architecture settings
    assert args.network_architecture_settings.activation == ActivationFunctionEnum.TanH, "Activation should be TanH"
    assert args.network_architecture_settings.use_attention is True, "Use attention should be True"
    assert args.network_architecture_settings.attention_dim == 128, "Attention dim should be 128"
    assert args.network_architecture_settings.fcnet_hiddens == [256, 256], "FCNet hiddens should be [256, 256]"


def test_logging_settings(mock_main):
    """Test logging verbosity parameters."""
    train_app([
        "ppo",
        "--logging-settings.schola-verbosity", "2",
        "--logging-settings.rllib-verbosity", "3"
    ], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify logging settings
    assert args.logging_settings.schola_verbosity == 2
    assert args.logging_settings.rllib_verbosity == 3


def test_checkpoint_settings(mock_main, tmp_path):
    """Test checkpoint configuration parameters."""
    checkpoint_dir = tmp_path / "checkpoints"
    checkpoint_dir.mkdir()
    
    train_app([
        "ppo",
        "--checkpoint-settings.checkpoint-dir", str(checkpoint_dir),
        "--checkpoint-settings.save-freq", "1000",
        "--checkpoint-settings.enable-checkpoints",
        "--checkpoint-settings.save-final-policy",
        "--checkpoint-settings.export-onnx"
    ], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify checkpoint settings
    assert args.checkpoint_settings.checkpoint_dir == checkpoint_dir
    assert args.checkpoint_settings.save_freq == 1000
    assert args.checkpoint_settings.enable_checkpoints is True
    assert args.checkpoint_settings.save_final_policy is True
    assert args.checkpoint_settings.export_onnx is True


def test_ppo_with_executable_simulator(mock_main, tmp_path):
    """Test executable simulator type is correctly parsed."""
    executable_path = tmp_path / "UnrealGame.exe"
    executable_path.touch()  # Create fake executable
    
    train_app([
        "ppo",
        "--executable-path", str(executable_path),
        "--simulator-type", "executable",
        "--headless"
    ], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify executable parameters are stored (even if editor is still used)
    # This tests that the executable parameters can be parsed correctly
    assert args.environment_settings.simulator.executable_path == executable_path
    assert isinstance(args.environment_settings.simulator, UnrealExecutableSimulatorArgs)


def test_protocol_settings(mock_main):
    """Test protocol configuration parameters."""
    train_app([
        "ppo",
        "--protocol.port", "12345"
    ], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify protocol settings
    assert args.environment_settings.protocol.port == 12345


def test_parse_args_without_execution():
    """Test parsing arguments without executing the command."""
    from schola.scripts.rllib.train import ppo
    command, bound, _ = train_app.parse_args(["ppo", "--training-settings.timesteps", "5000"])
    
    # Verify the command was parsed correctly
    assert command == ppo
    assert bound.arguments["args"].training_settings.timesteps == 5000


def test_multiple_algorithms_return_different_settings(mock_main):
    """Test that different algorithm commands create different settings."""
    # Test PPO
    train_app(["ppo"], result_action="return_value")
    ppo_args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    mock_main.reset_mock()
    
    # Test SAC
    train_app(["sac"], result_action="return_value")
    sac_args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    mock_main.reset_mock()
    
    # Test IMPALA
    train_app(["impala"], result_action="return_value")
    impala_args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify different algorithm types
    assert isinstance(ppo_args.algorithm_settings, PPOSettings)
    assert isinstance(sac_args.algorithm_settings, SACSettings)
    assert isinstance(impala_args.algorithm_settings, IMPALASettings)


def test_complex_configuration(mock_main, tmp_path):
    """Test a complex configuration with many parameters."""
    checkpoint_dir = tmp_path / "checkpoints"
    checkpoint_dir.mkdir()
    
    train_app([
        "ppo",
        # Training settings
        "--training-settings.timesteps", "50000",
        "--training-settings.learning-rate", "0.0005",
        "--training-settings.gamma", "0.98",
        "--training-settings.minibatch-size", "32",
        "--training-settings.train-batch-size-per-learner", "128",
        "--training-settings.num-sgd-iter", "8",
        # PPO settings
        "--gae-lambda", "0.92",
        "--clip-param", "0.25",
        # Resource settings
        "--resource-settings.num-gpus", "1",
        "--resource-settings.num-cpus", "4",
        # Network architecture
        "--network-architecture-settings.activation", "ReLU",
        # Logging
        "--logging-settings.schola-verbosity", "1",
        "--logging-settings.rllib-verbosity", "2",
        # Checkpoint settings
        "--checkpoint-settings.checkpoint-dir", str(checkpoint_dir),
        "--checkpoint-settings.save-freq", "5000",
        "--checkpoint-settings.enable-checkpoints",
        # Protocol settings
        "--protocol.port", "50051"
    ], result_action="return_value")
    
    mock_main.assert_called_once()
    args: RLlibScriptArgs = mock_main.call_args[0][0]
    
    # Verify all settings were applied correctly
    assert args.training_settings.timesteps == 50000
    assert args.training_settings.learning_rate == 0.0005
    assert args.training_settings.gamma == 0.98
    assert args.algorithm_settings.gae_lambda == 0.92
    assert args.algorithm_settings.clip_param == 0.25
    assert args.resource_settings.num_gpus == 1
    assert args.resource_settings.num_cpus == 4
    assert args.network_architecture_settings.activation == ActivationFunctionEnum.ReLU
    assert args.logging_settings.schola_verbosity == 1
    assert args.logging_settings.rllib_verbosity == 2
    assert args.checkpoint_settings.save_freq == 5000
    assert args.environment_settings.protocol.port == 50051


