"""
RLlib Training Script for SBDAPM

Trains PPO agents via Schola gRPC connection to Unreal Engine.

Usage:
    1. Start UE with Schola plugin (game mode)
    2. Run: python train_rllib.py
    3. Model exports to tactical_policy.onnx

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
# MULTI-HEAD TACTICAL POLICY (v5.0)
# ==============================================================================

class MultiHeadTacticalPolicy(TorchModelV2, nn.Module):
    """
    Multi-Head PPO Network for Strategy-Specific Tactical Decision Making (v5.0)

    Architecture:
        - Input: 64 features + 1 strategy index
        - Shared Trunk: [128 → 128 → 64] ReLU (learns common features)
        - 4 Strategy-Specific Heads: Assault, Defend, Support, Retreat
        - Shared Critic: Value estimate (for PPO training)

    Head Selection:
        Strategy index (0-3) gates which head is active during forward pass

    Outputs:
        - Action logits: [Position(4), Target(6), Fire(3)] = 13 total
        - State value: 1-dim value estimate
    """

    def __init__(self, obs_space, action_space, num_outputs, model_config, name):
        TorchModelV2.__init__(self, obs_space, action_space, num_outputs, model_config, name)
        nn.Module.__init__(self)

        # Observation: 64 features + 1 strategy index (int 0-3)
        # But RLlib passes flattened obs, so we need to handle the full 65-dim input
        obs_dim = 64  # Core observation features

        # Shared trunk: learns common features (aiming, cover, movement)
        self.shared_trunk = nn.Sequential(
            SlimFC(obs_dim, 128, activation_fn=nn.ReLU),
            SlimFC(128, 128, activation_fn=nn.ReLU),
            SlimFC(128, 64, activation_fn=nn.ReLU)
        )

        # Strategy-specific heads (Position[4] + Target[6] + Fire[3] = 13 outputs)
        # Each head learns specialized behavior for its strategy
        self.assault_head = SlimFC(64, num_outputs, activation_fn=None)
        self.defend_head = SlimFC(64, num_outputs, activation_fn=None)
        self.support_head = SlimFC(64, num_outputs, activation_fn=None)
        self.retreat_head = SlimFC(64, num_outputs, activation_fn=None)

        # Shared critic: value estimate (all strategies share same value function)
        self.value_head = SlimFC(64, 1, activation_fn=None)

        # Store last features for value function
        self._last_features = None

    @override(TorchModelV2)
    def forward(self, input_dict, state, seq_lens):
        """
        Forward pass with strategy-gated head selection

        Args:
            input_dict: Contains 'obs' with shape [batch, 65] (64 features + 1 strategy index)

        Returns:
            (action_logits, state): Logits from selected head, unchanged state
        """
        obs = input_dict["obs"]

        # Extract strategy index (last element) and core observation
        # obs[:, :-1] = 64 features, obs[:, -1] = strategy index (0-3)
        core_obs = obs[:, :-1]  # [batch, 64]
        strategy_idx = obs[:, -1].long()  # [batch] - convert to integer indices

        # Run shared trunk (learns common features across all strategies)
        features = self.shared_trunk(core_obs)  # [batch, 64]
        self._last_features = features

        # Select head based on strategy index (batch-wise gating)
        # For batch processing, we need to handle mixed strategies in same batch
        batch_size = features.shape[0]
        logits = torch.zeros(batch_size, self.num_outputs, device=features.device)

        # Apply each head to its corresponding batch elements
        for strategy in range(4):
            mask = (strategy_idx == strategy)
            if mask.any():
                if strategy == 0:  # Assault
                    logits[mask] = self.assault_head(features[mask])
                elif strategy == 1:  # Defend
                    logits[mask] = self.defend_head(features[mask])
                elif strategy == 2:  # Support
                    logits[mask] = self.support_head(features[mask])
                elif strategy == 3:  # Retreat
                    logits[mask] = self.retreat_head(features[mask])

        return logits, state

    @override(TorchModelV2)
    def value_function(self):
        """Return value estimate from shared critic"""
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
    MAX_EPISODE_STEPS = 1000

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
        "custom_model": "multi_head_tactical_policy",  # v5.0: Multi-head network
        "custom_model_config": {
            "obs_dim": 64,  # Core observation features (v5.0 streamlined)
            "strategy_dim": 1,  # Strategy index (0-3)
            "hidden_layers": SBDAPMConfig.HIDDEN_LAYERS,  # [128, 128, 64]
            "num_heads": 4,  # Assault, Defend, Support, Retreat
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
    """Register multi-head tactical policy with RLlib (v5.0)"""
    from ray.rllib.models import ModelCatalog

    if TORCH_AVAILABLE:
        ModelCatalog.register_custom_model("multi_head_tactical_policy", MultiHeadTacticalPolicy)
        print("[v5.0] Multi-head tactical policy registered with RLlib")
    else:
        print("[WARNING] PyTorch not available - cannot register custom model")



def export_onnx(algo, output_dir):
    """
    Export trained multi-head policy to ONNX format (v5.0)

    Exports 4 separate ONNX models (one per strategy head) for C++ inference:
    - assault_policy.onnx
    - defend_policy.onnx
    - support_policy.onnx
    - retreat_policy.onnx

    Each model:
        Input: 64 features (core observation)
        Output 1: 13-dim action logits [Position(4), Target(6), Fire(3)]
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
        # Export Multi-Head Network (v5.0)
        # Strategy-specific models for C++ inference
        # ========================================

        strategy_names = ["assault", "defend", "support", "retreat"]
        success_count = 0

        for strategy_idx, strategy_name in enumerate(strategy_names):
            class StrategyHeadWrapper(nn.Module):
                """
                Wrapper for single strategy head extraction

                Exports:
                    - Shared trunk + specific head
                    - Value function (shared critic)
                """
                def __init__(self, model, strategy_idx):
                    super().__init__()
                    self.model = model
                    self.strategy_idx = strategy_idx

                def forward(self, obs):
                    """
                    Args:
                        obs: [batch, 64] core observation features

                    Returns:
                        action_logits: [batch, 13] from selected head
                        state_value: [batch, 1] from shared critic
                    """
                    # Create full observation with strategy index
                    batch_size = obs.shape[0]
                    strategy_tensor = torch.full((batch_size, 1), self.strategy_idx, dtype=obs.dtype, device=obs.device)
                    full_obs = torch.cat([obs, strategy_tensor], dim=-1)  # [batch, 65]

                    # Run model forward pass (gates to selected head)
                    model_out, _ = self.model({"obs": full_obs}, [], None)

                    # Get value estimate from shared critic
                    value = self.model.value_function()

                    return model_out, value

            wrapper = StrategyHeadWrapper(model, strategy_idx)
            wrapper.eval()

            # Dummy input: 64 features (v5.0 streamlined observation)
            dummy_input = torch.randn(1, 64)

            # Export strategy-specific model
            model_path = output_dir / f"{strategy_name}_policy.onnx"
            torch.onnx.export(
                wrapper,
                dummy_input,
                str(model_path),
                input_names=["observation"],
                output_names=["action_logits", "state_value"],
                dynamic_axes={
                    "observation": {0: "batch_size"},
                    "action_logits": {0: "batch_size"},
                    "state_value": {0: "batch_size"}
                },
                opset_version=11
            )

            print(f"[SUCCESS] {strategy_name.capitalize()} policy exported to: {model_path}")
            success_count += 1

        if success_count == 4:
            print(f"\n[v5.0 EXPORT COMPLETE]")
            print(f"Model structure (per strategy):")
            print(f"  - Input: 64 dims (streamlined observation)")
            print(f"  - Output 1 (Actor): 13 dims [Position(4), Target(6), Fire(3)]")
            print(f"  - Output 2 (Critic): 1 dim (state value for MCTS)")
            print(f"\nReady for UE5:")
            print(f"  - Copy all 4 .onnx files to: Content/Models/")
            print(f"  - RLPolicyNetwork loads strategy-specific model based on EStrategyType")
            print(f"  - MCTS uses critic head for value estimation")
            return True
        else:
            print(f"[WARNING] Only {success_count}/4 models exported successfully")
            return False

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

    # Export ONNX from best checkpoint (dual-head actor-critic)

    # Try to export from best checkpoint first
    best_checkpoint_dir = os.path.join(output_dir, "best")
    if os.path.exists(best_checkpoint_dir):
        print(f"\nExporting best model from: {best_checkpoint_dir}")
        # Restore best checkpoint (pass result object directly)
        algo.restore(best_checkpoint)
        if export_onnx(algo, Path(output_dir)):
            print(f"\nBest model exported to: {output_dir}/rl_policy_network.onnx")
        else:
            print("\nBest model export failed, trying final checkpoint...")
            # Fallback to final checkpoint (pass result object directly)
            algo.restore(final_checkpoint)
            export_onnx(algo, Path(output_dir))
    else:
        # No best checkpoint, use final
        print(f"\nExporting final model...")
        if export_onnx(algo, Path(output_dir)):
            print(f"\nModel exported to: {output_dir}/rl_policy_network.onnx")

    print("\nTo use in Unreal Engine:")
    print("  1. Copy rl_policy_network.onnx to Content/Models/")
    print("  2. RLPolicyNetwork loads single model with dual heads")
    print("  3. Actor head used for actions, Critic head used by MCTS")

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
