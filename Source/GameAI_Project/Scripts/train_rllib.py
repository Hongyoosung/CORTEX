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
    SGD_MINIBATCH_SIZE = 128
    NUM_SGD_ITER = 10
    GAMMA = 0.99
    GAE_LAMBDA = 0.95
    CLIP_PARAM = 0.2
    ENTROPY_COEFF = 0.01
    VF_LOSS_COEFF = 0.5

    # Training
    NUM_WORKERS = 4  # 4 parallel UE instances (ports 50051-50054)
    NUM_ENVS_PER_WORKER = 1
    NUM_ITERATIONS = 100
    CHECKPOINT_FREQ = 10

    # Paths
    OUTPUT_DIR = "training_results"
    MODEL_NAME = "tactical_policy"


def create_env_config():
    """Create environment configuration for Schola."""
    return {
        "host": SBDAPMConfig.HOST,
        "base_port": SBDAPMConfig.PORT,  # Base port, each worker adds worker_index
        "max_episode_steps": SBDAPMConfig.MAX_EPISODE_STEPS,
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
        "fcnet_hiddens": SBDAPMConfig.HIDDEN_LAYERS,
        "fcnet_activation": "relu",
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
            return SBDAPMMultiAgentEnv(**config)
    else:
        # Fallback to dummy env for testing
        def env_creator(config):
            from sbdapm_env import SBDAPMEnv
            return SBDAPMEnv(**config)

    register_env("sbdapm_env", env_creator)



def export_onnx(algo, output_dir):
    """Export trained policy with dual heads (actor + critic) to single ONNX model."""
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
        # Export Dual-Head Network (Actor + Critic)
        # ========================================
        class DualHeadWrapper(nn.Module):
            """Wrapper for PPO actor-critic network with dual outputs.

            Outputs:
                - action_logits: 8-dim atomic actions (continuous + discrete)
                - state_value: 1-dim state value estimate for MCTS
            """
            def __init__(self, model):
                super().__init__()
                self.model = model

            def forward(self, obs):
                # RLlib models expect dict input
                model_out, _ = self.model({"obs": obs})

                # Get value estimate from critic head
                value = self.model.value_function()

                # Return raw logits (no softmax - atomic actions are mixed continuous/discrete)
                # C++ will apply appropriate activations per action dimension
                return model_out, value

        dual_head_wrapper = DualHeadWrapper(model)
        dual_head_wrapper.eval()

        # Dummy input: 78 features (71 observation + 7 objective embedding)
        dummy_input = torch.randn(1, 78)

        # Export unified model with 2 outputs
        model_path = output_dir / "rl_policy_network.onnx"
        torch.onnx.export(
            dual_head_wrapper,
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

        print(f"[SUCCESS] Dual-head PPO model exported to: {model_path}")
        print(f"\nModel structure:")
        print(f"  - Input: 78 dims (71 obs + 7 objective)")
        print(f"  - Output 1 (Actor): 8 dims (atomic actions)")
        print(f"  - Output 2 (Critic): 1 dim (state value)")
        print(f"\nReady for UE5:")
        print(f"  - Copy to: Content/Models/rl_policy_network.onnx")
        print(f"  - RL Policy uses actor head for actions")
        print(f"  - MCTS uses critic head for value estimation")

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

    # Initialize Ray
    # include_dashboard=False is often required on Windows to prevent timeouts
    ray.init(ignore_reinit_error=True, include_dashboard=False)

    # Register environment
    register_env()

    # Create output directory
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = os.path.join(SBDAPMConfig.OUTPUT_DIR, timestamp)
    os.makedirs(output_dir, exist_ok=True)

    print(f"\nOutput directory: {output_dir}")
    print(f"TensorBoard logs: {output_dir}")
    print(f"Training for {args.iterations} iterations\n")

    # Create config and algorithm with TensorBoard logging
    config = create_ppo_config()

    # Enable TensorBoard by setting local_dir for checkpoints
    # RLlib automatically writes TensorBoard event files to the trial directory
    algo = config.build(logger_creator=lambda config: ray.tune.logger.UnifiedLogger(
        config, output_dir, loggers=None
    ))



    # Training loop
    best_reward = float("-inf")
    total_ue_episodes = 0  # Track UE episodes for sync logging

    for i in range(args.iterations):
        result = algo.train()

        # Extract metrics (multi-agent aware)
        episode_reward_mean = result.get("episode_reward_mean", 0)
        episode_len_mean = result.get("episode_len_mean", 0)
        episodes_this_iter = result.get("episodes_this_iter", 0)
        
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
