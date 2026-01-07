"""
RLlib Training Script for SBDAPM (v6.0 - Single-Head Strategy Selection)

Trains PPO agents via Schola gRPC connection to Unreal Engine.

v6.0 Architecture:
    - MCTS assigns objectives → RL selects strategies (4-action space)
    - Single-head network: 68 input → [128, 128, 64] → 4 policy logits + 1 value
    - Exports to: cortex_policy_v6.onnx

Usage:
    1. Start UE with Schola plugin (game mode)
    2. Run: python train_rllib.py
    3. Model exports to cortex_policy_v6.onnx

Requirements:
    pip install schola[rllib] ray[rllib] torch
"""

import os
import sys
import warnings
from pathlib import Path

# Suppress Ray deprecation warnings
os.environ["PYTHONWARNINGS"] = "ignore::DeprecationWarning"
warnings.filterwarnings("ignore", category=DeprecationWarning)

# v6.0: Import RLConfig from auto-generated config (synced from C++)
try:
    from training_env.config import RLConfig
except ImportError:
    print("Warning: training_env/config.py not found. Run: python tools/sync_config_from_cpp.py")
    # Fallback values if config not available
    class RLConfig:
        OBSERVATION_SIZE = 68
        NUM_STRATEGIES = 4

import torch  # Fix for Windows DLL error (must be imported before ray)
import argparse
from datetime import datetime

# Check for required packages
try:
    import ray
    from ray.rllib.algorithms.ppo import PPOConfig
    from ray.rllib.policy.policy import Policy
    RLLIB_AVAILABLE = True
except ImportError:
    RLLIB_AVAILABLE = False
    print("Error: ray[rllib] not installed. Run: pip install ray[rllib]")

try:
    from schola.gym.env import GymEnv as UnrealEnv
    SCHOLA_AVAILABLE = True
except ImportError as e:
    SCHOLA_AVAILABLE = False
    print("Warning: schola not installed. Run: pip install schola[rllib]")
    print(f"\n[치명적 오류] Schola import 실패 원인:\n{e}")

import numpy as np
from gymnasium import spaces

# Import PyTorch and RLlib model classes for custom multi-head network
try:
    import torch.nn as nn
    from ray.rllib.models.torch.torch_modelv2 import TorchModelV2
    from ray.rllib.models.torch.misc import SlimFC
    from ray.rllib.utils.annotations import override
    TORCH_AVAILABLE = True
except ImportError:
    TORCH_AVAILABLE = False
    print("Warning: PyTorch or RLlib model classes not available")


# ==============================================================================
# SINGLE-HEAD STRATEGY POLICY (v6.0)
# ==============================================================================

class SingleHeadStrategyPolicy(TorchModelV2, nn.Module):
    """
    Single-Head PPO Network for Strategy Selection (v6.0)

    Architecture:
        - Input: 68 features (64 base + 4 objective context)
        - Shared Trunk: [128 → 128 → 64] ReLU (learns common features)
        - Policy Head: 4 strategy logits (Assault, Defend, Support, Retreat)
        - Value Head: 1-dim value estimate (for MCTS + PPO)

    v6.0 Changes:
        - MCTS assigns objectives, RL selects strategies (not micro-actions)
        - Simplified from 13-output multi-head to 4-output single-head
        - Objective context included in observation (replaces strategy index)

    Outputs:
        - Policy logits: 4-dim [Assault, Defend, Support, Retreat]
        - State value: 1-dim value estimate
    """

    def __init__(self, obs_space, action_space, num_outputs, model_config, name):
        TorchModelV2.__init__(self, obs_space, action_space, num_outputs, model_config, name)
        nn.Module.__init__(self)

        # v6.0: Observation size from RLConfig (synced from C++)
        obs_dim = RLConfig.OBSERVATION_SIZE  # 68 features (64 base + 4 objective context)

        # Shared trunk: learns common features (perception, tactical context)
        self.shared_trunk = nn.Sequential(
            SlimFC(obs_dim, 128, activation_fn=nn.ReLU),
            SlimFC(128, 128, activation_fn=nn.ReLU),
            SlimFC(128, 64, activation_fn=nn.ReLU)
        )

        # Single policy head: 4 strategy logits (v6.0)
        # Outputs probability distribution over [Assault, Defend, Support, Retreat]
        self.policy_head = SlimFC(64, num_outputs, activation_fn=None)

        # Value head: state value estimate (used by MCTS + PPO)
        self.value_head = SlimFC(64, 1, activation_fn=None)

        # Store last features for value function
        self._last_features = None

    @override(TorchModelV2)
    def forward(self, input_dict, state, seq_lens):
        """
        Forward pass (v6.0 - Single Head)

        Args:
            input_dict: Contains 'obs' with shape [batch, 68] (64 base + 4 objective context)

        Returns:
            (policy_logits, state): 4-dim strategy logits, unchanged state
        """
        obs = input_dict["obs"]

        # Run shared trunk (learns from full observation including objective context)
        features = self.shared_trunk(obs)  # [batch, 64]
        self._last_features = features

        # Single policy head: outputs strategy logits
        logits = self.policy_head(features)  # [batch, 4]

        return logits, state

    @override(TorchModelV2)
    def value_function(self):
        """Return value estimate from value head (used by MCTS + PPO)"""
        if self._last_features is None:
            raise ValueError("Must call forward() before value_function()")
        return self.value_head(self._last_features).squeeze(-1)


# ==============================================================================
# CONFIGURATION
# ==============================================================================

class SBDAPMConfig:
    """Training configuration."""

    # Environment
    HOST = "localhost"
    PORT = 50051
    MAX_EPISODE_STEPS = 2000  # MUST match UE5's MaxStepsPerEpisode (SimulationManagerGameMode.h:514)

    # Network architecture (matches train_tactical_policy.py)
    HIDDEN_LAYERS = [128, 128, 64]

    # PPO hyperparameters
    LEARNING_RATE = 3e-4
    TRAIN_BATCH_SIZE = 4000  
    SGD_MINIBATCH_SIZE = 256  # Doubled to match batch size increase
    NUM_SGD_ITER = 10
    GAMMA = 0.99
    GAE_LAMBDA = 0.95
    CLIP_PARAM = 0.2
    ENTROPY_COEFF = 0.5  # Increased from 0.1 to encourage exploration (large action space)
    VF_LOSS_COEFF = 0.5

    # Training
    NUM_WORKERS = 0  # 4 parallel UE instances (ports 50051-50054)
    NUM_ENVS_PER_WORKER = 1
    NUM_ITERATIONS = 100
    CHECKPOINT_FREQ = 10

    # Paths
    OUTPUT_DIR = "training_results"
    MODEL_NAME = "tactical_policy"


def create_env_config():
    """Create environment configuration for Schola."""

    # AWS distributed mode: Get worker IPs from environment variable
    worker_ips_str = os.getenv("WORKER_IPS", None)
    if worker_ips_str:
        worker_ips = worker_ips_str.strip().split()
        print(f"AWS Distributed Mode: {len(worker_ips)} worker IPs configured")
        print(f"Worker IPs: {worker_ips}")
    else:
        worker_ips = None
        print("Local Mode: Using localhost with port offsets")

    return {
        "host": SBDAPMConfig.HOST,
        "base_port": SBDAPMConfig.PORT,  # Base port, each worker adds worker_index
        "max_episode_steps": SBDAPMConfig.MAX_EPISODE_STEPS,
        "worker_ips": worker_ips,  # Pass to env creator for AWS mode
        # v4.0: Rate limiting handled UE-side (ScholaAgentComponent::DecisionInterval = 1.0s)
        # Python responds immediately to avoid blocking gRPC - UE5 controls decision frequency
    }



def create_ppo_config():
    """Create RLlib PPO configuration for multi-agent training."""
    config = (
        PPOConfig()
        .environment(
            env="sbdapm_env",
            env_config=create_env_config(),
            disable_env_checking=True,  # Disable env wrapper inspection
        )
        .framework("torch")
        .env_runners(
            num_env_runners=SBDAPMConfig.NUM_WORKERS,
            num_envs_per_env_runner=SBDAPMConfig.NUM_ENVS_PER_WORKER,
        )
        .multi_agent(
            # All agents share one policy (parameter sharing)
            policies={"shared_policy"},
            policy_mapping_fn=lambda agent_id, episode, worker, **kwargs: "shared_policy",
            # Count rewards per agent
            count_steps_by="agent_steps",
        )
        .debugging(log_level="INFO")
        .reporting(
            metrics_num_episodes_for_smoothing=10,
            min_sample_timesteps_per_iteration=100,
        )
    )

    # Set training parameters directly on config object (Ray 2.6+ API)
    config.lr = SBDAPMConfig.LEARNING_RATE
    config.train_batch_size = SBDAPMConfig.TRAIN_BATCH_SIZE
    config.sgd_minibatch_size = SBDAPMConfig.SGD_MINIBATCH_SIZE
    config.num_sgd_iter = SBDAPMConfig.NUM_SGD_ITER
    config.gamma = SBDAPMConfig.GAMMA
    config.lambda_ = SBDAPMConfig.GAE_LAMBDA
    config.clip_param = SBDAPMConfig.CLIP_PARAM
    config.entropy_coeff = SBDAPMConfig.ENTROPY_COEFF
    config.vf_loss_coeff = SBDAPMConfig.VF_LOSS_COEFF
    config.model = {
        "custom_model": "single_head_strategy_policy",  # v6.0: Single-head network
        "custom_model_config": {
            "obs_dim": RLConfig.OBSERVATION_SIZE,  # v6.0: Synced from C++ RLConfig
            "hidden_layers": SBDAPMConfig.HIDDEN_LAYERS,  # [128, 128, 64]
            "num_outputs": RLConfig.NUM_STRATEGIES,  # v6.0: 4 strategy logits (synced from C++)
        },
        "max_seq_len": 20,  # Required by RLlib (not used for feedforward nets)
    }

    return config





def register_env():
    """Register custom environment with Ray."""
    from ray.tune.registry import register_env

    if SCHOLA_AVAILABLE:
        # Use multi-agent Schola environment (v3.1)
        def env_creator(config):
            from sbdapm_env import SBDAPMMultiAgentEnv

            # AWS distributed mode: Use specific worker IP
            worker_ips = config.get("worker_ips")
            if worker_ips:
                worker_index = config.get("worker_index", 0)
                host = worker_ips[worker_index % len(worker_ips)]
                port = config.get("base_port", 50051)
                print(f"[Worker {worker_index}] Connecting to {host}:{port}")

                return SBDAPMMultiAgentEnv(
                    host=host,
                    port=port,
                    max_episode_steps=config.get("max_episode_steps", 1000)
                )
            else:
                # Local mode: Use base_port + worker_index
                return SBDAPMMultiAgentEnv(**config)
    else:
        # Fallback to dummy env for testing
        def env_creator(config):
            from sbdapm_env import SBDAPMEnv
            return SBDAPMEnv(**config)

    register_env("sbdapm_env", env_creator)


def register_custom_model():
    """Register single-head strategy policy with RLlib (v6.0)"""
    from ray.rllib.models import ModelCatalog

    if TORCH_AVAILABLE:
        ModelCatalog.register_custom_model("single_head_strategy_policy", SingleHeadStrategyPolicy)
        print("[v6.0] Single-head strategy policy registered with RLlib")
    else:
        print("[WARNING] PyTorch not available - cannot register custom model")



def export_onnx(algo, output_dir):
    """
    Export trained single-head policy to ONNX format (v6.0)

    Exports single ONNX model for C++ inference:
    - cortex_policy_v6.onnx

    Model structure:
        Input: 68 features (64 base + 4 objective context)
        Output 1: 4-dim policy logits [Assault, Defend, Support, Retreat]
        Output 2: 1-dim state value (for MCTS value estimation)
    """
    try:
        import torch
        import torch.nn as nn

        # Get policy (must specify policy name for multi-agent training)
        policy = algo.get_policy("shared_policy")
        if not policy:
            print("ERROR: Could not get 'shared_policy'. Available policies:", algo.workers.local_worker().policy_map.keys())
            return False

        model = policy.model

        # ========================================
        # Export Single-Head Network (v6.0)
        # Unified model for all strategies
        # ========================================

        class PolicyWrapper(nn.Module):
            """
            Wrapper for policy + value export

            Exports:
                - Shared trunk + policy head (4 strategy logits)
                - Value head (state value for MCTS)
            """
            def __init__(self, model):
                super().__init__()
                self.model = model

            def forward(self, obs):
                """
                Args:
                    obs: [batch, 68] (64 base + 4 objective context)

                Returns:
                    policy_logits: [batch, 4] strategy probabilities
                    state_value: [batch, 1] value estimate
                """
                # Run model forward pass
                policy_logits, _ = self.model({"obs": obs}, [], None)

                # Get value estimate
                value = self.model.value_function()

                return policy_logits, value.unsqueeze(-1)  # Make value [batch, 1]

        wrapper = PolicyWrapper(model)
        wrapper.eval()

        # Dummy input: observation size from RLConfig (synced from C++)
        dummy_input = torch.randn(1, RLConfig.OBSERVATION_SIZE)

        # Export single unified model
        model_path = output_dir / "cortex_policy_v6.onnx"
        torch.onnx.export(
            wrapper,
            dummy_input,
            str(model_path),
            input_names=["observation"],
            output_names=["policy_logits", "value"],
            dynamic_axes={
                "observation": {0: "batch_size"},
                "policy_logits": {0: "batch_size"},
                "value": {0: "batch_size"}
            },
            opset_version=11
        )

        print(f"\n[v6.0 EXPORT COMPLETE]")
        print(f"[SUCCESS] Policy exported to: {model_path}")
        print(f"\nModel structure:")
        print(f"  - Input: {RLConfig.OBSERVATION_SIZE} dims (64 base + 4 objective context)")
        print(f"  - Output 1 (Policy): {RLConfig.NUM_STRATEGIES} dims [Assault, Defend, Support, Retreat]")
        print(f"  - Output 2 (Value): 1 dim (state value for MCTS)")
        print(f"\nReady for UE5:")
        print(f"  - Copy cortex_policy_v6.onnx to: Content/Models/")
        print(f"  - RLPolicyNetwork loads single model for all strategies")
        print(f"  - MCTS uses value head for assignment evaluation")
        return True

    except Exception as e:
        print(f"ONNX export failed: {e}")
        import traceback
        traceback.print_exc()
        print("Saving checkpoint instead...")
        return False


def train(args):
    """Main training loop."""
    print("=" * 60)
    print("SBDAPM RLlib Training")
    print("=" * 60)
    print(f"\nConnection Configuration:")
    print(f"  Host: {SBDAPMConfig.HOST}")
    print(f"  Port: {SBDAPMConfig.PORT}")
    print(f"  Workers: {SBDAPMConfig.NUM_WORKERS}")
    print()

    # Initialize Ray
    # include_dashboard=False is often required on Windows to prevent timeouts
    ray.init(ignore_reinit_error=True, include_dashboard=False)

    # Register environment and custom model (v5.0)
    register_env()
    register_custom_model()

    # Create output directory
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = os.path.join(SBDAPMConfig.OUTPUT_DIR, timestamp)
    os.makedirs(output_dir, exist_ok=True)

    print(f"\nOutput directory: {output_dir}")
    print(f"TensorBoard logs: {output_dir}")
    print(f"Training for {args.iterations} iterations\n")

    # Create config and algorithm with TensorBoard logging
    config = create_ppo_config()

    # Enable TensorBoard - Use Ray's default UnifiedLogger which includes:
    # - CSVLogger (progress.csv)
    # - JsonLogger (result.json)
    # - TBXLogger (TensorBoard events)
    from ray.tune.logger import UnifiedLogger, TBXLogger, JsonLogger, CSVLogger

    def logger_creator(config_dict):
        """Create logger with TensorBoard support."""
        return UnifiedLogger(
            config_dict,
            output_dir,
            loggers=[JsonLogger, CSVLogger, TBXLogger]  # Explicitly include TensorBoard
        )

    algo = config.build(logger_creator=logger_creator)



    # Training loop
    best_reward = float("-inf")
    total_ue_episodes = 0  # Track UE episodes for sync logging

    for i in range(args.iterations):
        result = algo.train()

        # Extract metrics (multi-agent aware)
        # In RLlib's new API, episode metrics are nested under "env_runners"
        env_runner_results = result.get("env_runners", {})
        episode_reward_mean = env_runner_results.get("episode_reward_mean", 0)
        episode_len_mean = env_runner_results.get("episode_len_mean", 0)
        episodes_this_iter = env_runner_results.get("episodes_this_iter", 0)
        
        # Track UE episodes
        total_ue_episodes += episodes_this_iter

        # Multi-agent specific metrics
        num_agent_steps = result.get("num_agent_steps_sampled", 0)
        num_env_steps = result.get("num_env_steps_sampled", 0)

        print(f"Iteration {i+1:4d} (UE Episodes: ~{total_ue_episodes}): "
              f"reward={episode_reward_mean:8.2f}, "
              f"len={episode_len_mean:6.1f}, "
              f"agent_steps={num_agent_steps}, "
              f"env_steps={num_env_steps}")

        # Save checkpoint
        if (i + 1) % args.checkpoint_freq == 0:
            checkpoint_path = algo.save(output_dir)
            print(f"  Checkpoint: {checkpoint_path}")

        # Track best
        if episode_reward_mean > best_reward:
            best_reward = episode_reward_mean
            best_checkpoint = algo.save(os.path.join(output_dir, "best"))
            print(f"  New best! reward={best_reward:.2f}")


    # Final save
    print("\n" + "=" * 60)
    print("Training Complete!")
    print("=" * 60)

    final_checkpoint = algo.save(output_dir)
    print(f"Final checkpoint: {final_checkpoint}")

    # Export ONNX from best checkpoint (v6.0: single-head policy + value)

    # Try to export from best checkpoint first
    best_checkpoint_dir = os.path.join(output_dir, "best")
    if os.path.exists(best_checkpoint_dir):
        print(f"\nExporting best model from: {best_checkpoint_dir}")
        # Restore best checkpoint (use absolute path for PyArrow compatibility)
        algo.restore(os.path.abspath(best_checkpoint_dir))
        if export_onnx(algo, Path(output_dir)):
            print(f"\nBest model exported to: {output_dir}/cortex_policy_v6.onnx")
        else:
            print("\nBest model export failed, trying final checkpoint...")
            # Fallback to final checkpoint (use absolute path for PyArrow compatibility)
            algo.restore(os.path.abspath(output_dir))
            export_onnx(algo, Path(output_dir))
    else:
        # No best checkpoint, use final
        print(f"\nExporting final model...")
        if export_onnx(algo, Path(output_dir)):
            print(f"\nModel exported to: {output_dir}/cortex_policy_v6.onnx")

    print("\nTo use in Unreal Engine:")
    print("  1. Copy cortex_policy_v6.onnx to Content/Models/")
    print("  2. RLPolicyNetwork loads single model for strategy selection")
    print("  3. Policy head used for strategy selection, Value head used by MCTS")

    # Cleanup
    algo.stop()
    ray.shutdown()

    return output_dir


def main():
    parser = argparse.ArgumentParser(description="Train SBDAPM tactical policy with RLlib")
    parser.add_argument("--iterations", type=int, default=SBDAPMConfig.NUM_ITERATIONS,
                        help="Number of training iterations")
    parser.add_argument("--checkpoint-freq", type=int, default=SBDAPMConfig.CHECKPOINT_FREQ,
                        help="Save checkpoint every N iterations")
    parser.add_argument("--host", type=str, default=SBDAPMConfig.HOST,
                        help="Schola gRPC server host")
    parser.add_argument("--port", type=int, default=SBDAPMConfig.PORT,
                        help="Schola gRPC server port")

    args = parser.parse_args()

    # Update config
    SBDAPMConfig.HOST = args.host
    SBDAPMConfig.PORT = args.port

    if not RLLIB_AVAILABLE:
        print("\nError: ray[rllib] is required. Install with:")
        print("  pip install ray[rllib] torch")
        sys.exit(1)

    if not SCHOLA_AVAILABLE:
        print("\nWarning: schola not installed. Using dummy environment.")
        print("For real training, install with:")
        print("  pip install schola[rllib]")

    train(args)


if __name__ == "__main__":
    main()
