"""
Debug Training Script - Enhanced Logging for Zero Reward Issue

Run this instead of train_rllib.py to get detailed diagnostics.
"""

import os
import sys
import warnings

os.environ["PYTHONWARNINGS"] = "ignore::DeprecationWarning"
warnings.filterwarnings("ignore", category=DeprecationWarning)

import torch
import argparse
from datetime import datetime

try:
    import ray
    from ray.rllib.algorithms.ppo import PPOConfig
    RLLIB_AVAILABLE = True
except ImportError:
    RLLIB_AVAILABLE = False
    print("Error: ray[rllib] not installed")
    sys.exit(1)

try:
    from schola.gym.env import GymEnv as UnrealEnv
    SCHOLA_AVAILABLE = True
except ImportError:
    SCHOLA_AVAILABLE = False
    print("Error: schola not installed")
    sys.exit(1)

import numpy as np
from gymnasium import spaces

# Import config from train_rllib
sys.path.insert(0, os.path.dirname(__file__))
from train_rllib import SBDAPMConfig, create_env_config, create_ppo_config, register_env


def debug_train(args):
    """Training loop with enhanced debugging."""
    print("=" * 60)
    print("SBDAPM DEBUG Training")
    print("=" * 60)

    ray.init(ignore_reinit_error=True, include_dashboard=False)
    register_env()

    config = create_ppo_config()
    algo = config.build()

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = os.path.join(SBDAPMConfig.OUTPUT_DIR, f"debug_{timestamp}")
    os.makedirs(output_dir, exist_ok=True)

    print(f"\nOutput directory: {output_dir}")
    print(f"Training for {args.iterations} iterations\n")

    best_reward = float("-inf")

    for i in range(args.iterations):
        print(f"\n{'='*60}")
        print(f"ITERATION {i+1}/{args.iterations}")
        print(f"{'='*60}")

        # Train one iteration
        result = algo.train()

        # Extract ALL available metrics
        episode_reward_mean = result.get("episode_reward_mean", 0)
        episode_len_mean = result.get("episode_len_mean", 0)
        episodes_this_iter = result.get("episodes_this_iter", 0)
        timesteps_total = result.get("timesteps_total", 0)
        timesteps_this_iter = result.get("num_env_steps_sampled", 0)

        # Print detailed metrics
        print(f"\n[METRICS]")
        print(f"  Episode Reward Mean:  {episode_reward_mean:8.2f}")
        print(f"  Episode Length Mean:  {episode_len_mean:6.1f}")
        print(f"  Episodes This Iter:   {episodes_this_iter}")
        print(f"  Timesteps This Iter:  {timesteps_this_iter}")
        print(f"  Timesteps Total:      {timesteps_total}")

        # Check for zero-episode issue
        if episodes_this_iter == 0:
            print(f"\n⚠️  WARNING: No episodes completed this iteration!")
            print(f"  This suggests the environment is not stepping properly.")
        
        if episode_len_mean == 0.0 and episodes_this_iter > 0:
            print(f"\n⚠️  WARNING: Episodes completing with 0 steps!")
            print(f"  Check UE logs for immediate agent deaths or termination.")

        # Print additional debug info
        if "info" in result:
            print(f"\n[INFO DICT]")
            for key, value in list(result["info"].items())[:5]:
                print(f"  {key}: {value}")

        # Print sampler results if available
        if "sampler_results" in result:
            sampler = result["sampler_results"]
            print(f"\n[SAMPLER]")
            print(f"  Episode Reward Max:   {sampler.get('episode_reward_max', 'N/A')}")
            print(f"  Episode Reward Min:   {sampler.get('episode_reward_min', 'N/A')}")
            print(f"  Episode Length Max:   {sampler.get('episode_len_max', 'N/A')}")

        # Save checkpoint
        if (i + 1) % args.checkpoint_freq == 0:
            checkpoint_path = algo.save(output_dir)
            print(f"\n[CHECKPOINT] {checkpoint_path}")

        # Track best
        if episode_reward_mean > best_reward:
            best_reward = episode_reward_mean
            best_checkpoint = algo.save(os.path.join(output_dir, "best"))
            print(f"\n🎉 NEW BEST! reward={best_reward:.2f}")

    # Final save
    print("\n" + "=" * 60)
    print("Training Complete!")
    print("=" * 60)

    final_checkpoint = algo.save(output_dir)
    print(f"Final checkpoint: {final_checkpoint}")

    algo.stop()
    ray.shutdown()

    return output_dir


def main():
    parser = argparse.ArgumentParser(description="Debug SBDAPM training")
    parser.add_argument("--iterations", type=int, default=10,
                        help="Number of training iterations")
    parser.add_argument("--checkpoint-freq", type=int, default=5,
                        help="Save checkpoint every N iterations")
    parser.add_argument("--host", type=str, default="localhost",
                        help="Schola gRPC server host")
    parser.add_argument("--port", type=int, default=50051,
                        help="Schola gRPC server port")

    args = parser.parse_args()

    SBDAPMConfig.HOST = args.host
    SBDAPMConfig.PORT = args.port

    debug_train(args)


if __name__ == "__main__":
    main()
