"""
RLlib Training Script for CORTEX (v8.0 - Simplified)

Trains PPO agents via Schola gRPC connection to Unreal Engine.

v8.0 Changes:
    - REMOVED: Curriculum learning (MCTS in UE5 already controls strategy assignments)
    - SIMPLIFIED: Training loop with minimal logging
    - FIXED: Episode boundary synchronization (UE5 is single source of truth)

v8.0 Architecture:
    - MCTS assigns strategies → RL outputs tactical parameters + combat priority
    - Multi-head network: 50 input (46 base + 4 strategy one-hot) → [256, 256, 128]
    - Action space: 5 continuous outputs (4 tactical params + 1 combat priority)
    - Exports to: cortex_policy_v8.onnx

Usage:
    1. Start UE5 with Schola plugin (MaxEpisodeDuration=60s)
    2. Run: python train_rllib.py --iterations 50
    3. Model exports to cortex_policy_v8.onnx

Requirements:
    pip install schola[rllib] ray[rllib] torch onnx onnxruntime
"""

import os
import sys
import warnings
from pathlib import Path

# Suppress Ray deprecation warnings
os.environ["PYTHONWARNINGS"] = "ignore::DeprecationWarning"
warnings.filterwarnings("ignore", category=DeprecationWarning)

# Import RLConfig from auto-generated config (synced from C++)
try:
    from training_env.config import RLConfig
except ImportError:
    print("Warning: training_env/config.py not found. Using defaults.")
    class RLConfig:
        OBSERVATION_SIZE = 50  # 46 base + 4 strategy one-hot
        NUM_STRATEGIES = 4

import torch  # Must be imported before ray on Windows

import argparse
import time
from datetime import datetime

# Check for required packages
try:
    import ray
    from ray.rllib.algorithms.ppo import PPOConfig
    RLLIB_AVAILABLE = True
except ImportError:
    RLLIB_AVAILABLE = False
    print("Error: ray[rllib] not installed. Run: pip install ray[rllib]")

try:
    from schola.gym.env import GymEnv as UnrealEnv
    SCHOLA_AVAILABLE = True
except ImportError:
    SCHOLA_AVAILABLE = False
    print("Warning: schola not installed. Run: pip install schola[rllib]")

import numpy as np
from gymnasium import spaces

# Import PyTorch and RLlib model classes
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
# MULTI-HEAD TACTICAL POLICY (v8.0)
# ==============================================================================

class MultiHeadTacticalPolicy(TorchModelV2, nn.Module):
    """
    Multi-Head PPO Network for Tactical Parameters + Combat Control.

    Architecture:
        - Input: 50 features (46 base + 4 strategy one-hot)
        - Shared Feature Extractor: [256 → 256 → 128] ReLU
        - 4 Strategy-Specific Mean Heads: Assault, Defend, Support, Retreat
        - 1 Combat Mean Head: FC(128→1)
        - 1 Shared Log-Std Head: FC(128→5)
        - 1 Shared Critic Head: FC(128→1)

    Outputs:
        - 10 values: 5 means + 5 log_stds for DiagGaussian distribution
    """

    def __init__(self, obs_space, action_space, num_outputs, model_config, name):
        TorchModelV2.__init__(self, obs_space, action_space, num_outputs, model_config, name)
        nn.Module.__init__(self)

        obs_dim = RLConfig.OBSERVATION_SIZE  # 50
        self.num_outputs = num_outputs
        self.action_dim = num_outputs // 2  # 5

        hidden_layers = model_config.get("custom_model_config", {}).get("hidden_layers", [256, 256, 128])

        # Shared feature extractor
        layers = []
        prev_size = obs_dim
        for hidden_size in hidden_layers:
            layers.append(SlimFC(prev_size, hidden_size, activation_fn=nn.ReLU))
            prev_size = hidden_size

        self.shared_trunk = nn.Sequential(*layers)
        final_hidden_size = hidden_layers[-1]

        # Strategy-Specific Mean Heads
        self.assault_mean_head = nn.Sequential(
            SlimFC(final_hidden_size, 32, activation_fn=nn.ReLU),
            SlimFC(32, 4, activation_fn=None)
        )
        self.defend_mean_head = nn.Sequential(
            SlimFC(final_hidden_size, 32, activation_fn=nn.ReLU),
            SlimFC(32, 4, activation_fn=None)
        )
        self.support_mean_head = nn.Sequential(
            SlimFC(final_hidden_size, 32, activation_fn=nn.ReLU),
            SlimFC(32, 4, activation_fn=None)
        )
        self.retreat_mean_head = nn.Sequential(
            SlimFC(final_hidden_size, 32, activation_fn=nn.ReLU),
            SlimFC(32, 4, activation_fn=None)
        )

        # Combat Mean Head
        self.combat_mean_head = SlimFC(final_hidden_size, 1, activation_fn=None)

        # Shared Log-Std Head
        self.log_std_head = SlimFC(final_hidden_size, self.action_dim, activation_fn=None)

        # Shared Value Head
        self.value_head = SlimFC(final_hidden_size, 1, activation_fn=None)

        self._last_features = None

    @override(TorchModelV2)
    def forward(self, input_dict, state, seq_lens):
        obs = input_dict["obs"]
        batch_size = obs.shape[0]

        # Extract strategy one-hot (last 4 features)
        strategy_onehot = obs[:, 46:50]

        # Run shared trunk
        features = self.shared_trunk(obs)
        self._last_features = features

        # Route to appropriate strategy head
        tactical_means = torch.zeros(batch_size, 4, device=obs.device)

        for i in range(batch_size):
            strategy_idx = torch.argmax(strategy_onehot[i]).item()

            if strategy_idx == 0:
                tactical_means[i] = self.assault_mean_head(features[i:i+1]).squeeze(0)
            elif strategy_idx == 1:
                tactical_means[i] = self.defend_mean_head(features[i:i+1]).squeeze(0)
            elif strategy_idx == 2:
                tactical_means[i] = self.support_mean_head(features[i:i+1]).squeeze(0)
            else:
                tactical_means[i] = self.retreat_mean_head(features[i:i+1]).squeeze(0)

        combat_mean = self.combat_mean_head(features)
        means = torch.cat([tactical_means, combat_mean], dim=-1)
        log_stds = self.log_std_head(features)
        output = torch.cat([means, log_stds], dim=-1)

        return output, state

    @override(TorchModelV2)
    def value_function(self):
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

    # Network architecture
    HIDDEN_LAYERS = [256, 256, 128]

    # PPO hyperparameters
    LEARNING_RATE = 5e-5
    TRAIN_BATCH_SIZE = 8000
    SGD_MINIBATCH_SIZE = 512
    NUM_SGD_ITER = 15
    GAMMA = 0.99
    GAE_LAMBDA = 0.95
    CLIP_PARAM = 0.2
    ENTROPY_COEFF = 0.01
    VF_LOSS_COEFF = 1.5

    # Training
    NUM_WORKERS = 0  # Windows: single process
    NUM_ENVS_PER_WORKER = 1
    NUM_ITERATIONS = 100
    CHECKPOINT_FREQ = 10

    # Paths
    OUTPUT_DIR = "training_results"


def create_env_config():
    """Create environment configuration."""
    import os
    is_docker = os.environ.get("IS_DOCKER", "false").lower() == "true"

    config = {
        "host": SBDAPMConfig.HOST,
        "base_port": SBDAPMConfig.PORT,
    }

    # Add Docker-specific settings
    if is_docker:
        config["is_docker"] = True
        config["timeout"] = 60  # Extended timeout for Docker networking (increased from 30s)

    return config


def create_ppo_config():
    """Create RLlib PPO configuration."""
    config = (
        PPOConfig()
        .environment(
            env="sbdapm_env",
            env_config=create_env_config(),
            disable_env_checking=True,
        )
        .framework("torch")
        .env_runners(
            num_env_runners=SBDAPMConfig.NUM_WORKERS,
            num_envs_per_env_runner=SBDAPMConfig.NUM_ENVS_PER_WORKER,
        )
        .multi_agent(
            policies={"shared_policy"},
            policy_mapping_fn=lambda agent_id, episode, worker, **kwargs: "shared_policy",
            count_steps_by="agent_steps",
        )
        .debugging(log_level="WARN")
        .reporting(
            metrics_num_episodes_for_smoothing=10,
            min_sample_timesteps_per_iteration=100,
        )
    )

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
        "custom_model": "multi_head_tactical_policy",
        "custom_model_config": {
            "obs_dim": RLConfig.OBSERVATION_SIZE,
            "hidden_layers": SBDAPMConfig.HIDDEN_LAYERS,
        },
        "max_seq_len": 20,
    }

    return config


def register_env():
    """Register custom environment with Ray."""
    from ray.tune.registry import register_env

    if SCHOLA_AVAILABLE:
        def env_creator(config):
            from sbdapm_env import SBDAPMMultiAgentEnv
            return SBDAPMMultiAgentEnv(**config)
    else:
        def env_creator(config):
            from sbdapm_env import SBDAPMEnv
            return SBDAPMEnv(**config)

    register_env("sbdapm_env", env_creator)


def register_custom_model():
    """Register multi-head tactical policy with RLlib."""
    from ray.rllib.models import ModelCatalog

    if TORCH_AVAILABLE:
        ModelCatalog.register_custom_model("multi_head_tactical_policy", MultiHeadTacticalPolicy)
        print("[v8.0] Multi-head tactical policy registered")


def export_onnx(algo, output_dir):
    """Export trained policy to ONNX format."""
    try:
        import torch
        import torch.nn as nn

        policy = algo.get_policy("shared_policy")
        if not policy:
            print("ERROR: Could not get 'shared_policy'")
            return False

        model = policy.model

        class MultiHeadPolicyWrapper(nn.Module):
            def __init__(self, model):
                super().__init__()
                self.model = model

            def forward(self, obs):
                features = self.model.shared_trunk(obs)
                assault_tactical = torch.sigmoid(self.model.assault_mean_head(features))
                defend_tactical = torch.sigmoid(self.model.defend_mean_head(features))
                support_tactical = torch.sigmoid(self.model.support_mean_head(features))
                retreat_tactical = torch.sigmoid(self.model.retreat_mean_head(features))
                combat_priority = torch.sigmoid(self.model.combat_mean_head(features))
                value = self.model.value_head(features)
                return (assault_tactical, defend_tactical, support_tactical,
                       retreat_tactical, combat_priority, value)

        wrapper = MultiHeadPolicyWrapper(model)
        wrapper.eval()

        dummy_input = torch.randn(1, RLConfig.OBSERVATION_SIZE)
        model_path = output_dir / "cortex_policy_v8.onnx"

        torch.onnx.export(
            wrapper, dummy_input, str(model_path),
            input_names=["observation"],
            output_names=["assault_tactical", "defend_tactical", "support_tactical",
                         "retreat_tactical", "combat_priority", "value"],
            dynamic_axes={
                "observation": {0: "batch_size"},
                "assault_tactical": {0: "batch_size"},
                "defend_tactical": {0: "batch_size"},
                "support_tactical": {0: "batch_size"},
                "retreat_tactical": {0: "batch_size"},
                "combat_priority": {0: "batch_size"},
                "value": {0: "batch_size"}
            },
            opset_version=11
        )

        print(f"[EXPORT] Model saved to: {model_path}")
        return True

    except Exception as e:
        print(f"ONNX export failed: {e}")
        return False


def train(args):
    """Main training loop."""
    import os

    # Override NUM_WORKERS from environment if set (Docker mode)
    if "NUM_WORKERS" in os.environ:
        num_workers = int(os.environ["NUM_WORKERS"])
        SBDAPMConfig.NUM_WORKERS = num_workers
        print(f"[Docker] NUM_WORKERS overridden to {num_workers}")

    print("=" * 60)
    print("CORTEX v8.0 Training")
    print("=" * 60)
    print(f"  Host: {SBDAPMConfig.HOST}:{SBDAPMConfig.PORT}")
    print(f"  Workers: {SBDAPMConfig.NUM_WORKERS}")
    print(f"  Iterations: {args.iterations}")
    print()

    # Cleanup any existing Ray
    try:
        ray.shutdown()
    except:
        pass

    # Windows multiprocessing fix
    import multiprocessing
    try:
        multiprocessing.set_start_method('spawn', force=True)
    except RuntimeError:
        pass

    # Initialize Ray
    import tempfile
    ray_temp_dir = os.path.join(tempfile.gettempdir(), "ray_cortex")
    os.makedirs(ray_temp_dir, exist_ok=True)

    print("Initializing Ray...")
    try:
        ray.init(
            ignore_reinit_error=True,
            include_dashboard=False,
            _temp_dir=ray_temp_dir,
            num_cpus=4,
            object_store_memory=1 * 1024**3,  
            logging_level="ERROR",
        )
        print("Ray initialized successfully")
    except Exception as e:
        print(f"Ray init failed: {e}, using local mode")
        ray.init(local_mode=True, ignore_reinit_error=True)

    register_env()
    register_custom_model()

    # Create output directory
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = os.path.join(SBDAPMConfig.OUTPUT_DIR, timestamp)
    os.makedirs(output_dir, exist_ok=True)

    print(f"Output: {output_dir}")

    # Create logger
    from ray.tune.logger import UnifiedLogger, TBXLogger, JsonLogger, CSVLogger

    def logger_creator(config_dict):
        return UnifiedLogger(config_dict, output_dir, loggers=[JsonLogger, CSVLogger, TBXLogger])

    # Build algorithm
    print("\nConnecting to UE5...")
    config = create_ppo_config()

    try:
        algo = config.build(logger_creator=logger_creator)
        print("Connected!\n")
    except Exception as e:
        print(f"[ERROR] Failed to connect: {e}")
        ray.shutdown()
        sys.exit(1)

    # Training loop
    best_reward = float("-inf")
    cumulative_episodes = 0
    cumulative_steps = 0

    print("\n" + "="*80)
    print("TRAINING PROGRESS")
    print("="*80)
    print(f"{'Iter':<6} {'Reward':>10} {'EpLen':>8} {'Episodes':>10} {'Steps':>12} {'Time':>8} {'Best':>10}")
    print("-"*80)

    for i in range(args.iterations):
        iter_start = time.time()

        result = algo.train()

        iter_time = time.time() - iter_start

        # Extract metrics
        env_results = result.get("env_runners", {})
        reward = env_results.get("episode_reward_mean", None)
        reward_min = env_results.get("episode_reward_min", None)
        reward_max = env_results.get("episode_reward_max", None)
        ep_len = env_results.get("episode_len_mean", None)
        episodes = env_results.get("episodes_this_iter", 0)
        agent_steps = result.get("num_agent_steps_sampled", 0)

        # Handle nan/None values (happens when no episodes complete in this iteration)
        episodes_completed_this_iter = episodes > 0
        if reward is None or np.isnan(reward):
            reward = 0
        if reward_min is None or np.isnan(reward_min):
            reward_min = 0
        if reward_max is None or np.isnan(reward_max):
            reward_max = 0
        if ep_len is None or np.isnan(ep_len):
            ep_len = 0

        # Update cumulative counters
        cumulative_episodes += episodes
        cumulative_steps += agent_steps

        # Print concise progress line
        status_indicator = "✓" if episodes_completed_this_iter else "→"
        print(f"{status_indicator} {i+1:>3}/{args.iterations:<3} {reward:>10.2f} {ep_len:>8.1f} "
              f"{episodes:>10} {agent_steps:>12} {iter_time:>7.1f}s {best_reward:>10.2f}")

        # Print detailed breakdown every 10 iterations or on first iteration
        if i == 0 or (i + 1) % 10 == 0:
            print(f"\n  [ITERATION {i+1} DETAILS]")
            if episodes_completed_this_iter:
                print(f"    Reward: mean={reward:.2f}, min={reward_min:.2f}, max={reward_max:.2f}")
                print(f"    Episode length: {ep_len:.1f} steps")
                print(f"    Episodes this iteration: {episodes}")
            else:
                print(f"    No episodes completed this iteration (still collecting samples)")
            print(f"    Agent steps this iteration: {agent_steps}")
            print(f"    Cumulative: {cumulative_episodes} episodes, {cumulative_steps} steps")
            learner_info = result.get('info', {}).get('learner', {}).get('default_policy', {}).get('learner_stats', {})
            total_loss = learner_info.get('total_loss', 'N/A')
            print(f"    Policy loss: {total_loss}")
            print()

        # Checkpoint
        if (i + 1) % args.checkpoint_freq == 0:
            algo.save(output_dir)
            print(f"  >> Checkpoint saved at iteration {i+1}\n")

        # Track best
        if reward > best_reward and not np.isnan(reward):
            best_reward = reward
            algo.save(os.path.join(output_dir, "best"))
            print(f"  >> NEW BEST REWARD: {best_reward:.2f} (iteration {i+1})\n")

    # Final save
    print("\n" + "="*80)
    print("TRAINING COMPLETE!")
    print("="*80)
    print(f"Total iterations: {args.iterations}")
    print(f"Total episodes: {cumulative_episodes}")
    print(f"Total agent steps: {cumulative_steps}")
    print(f"Best reward: {best_reward:.2f}")
    print(f"Output directory: {output_dir}")
    print("="*80 + "\n")

    algo.save(output_dir)
    print(f"Final model saved to: {output_dir}")

    # Export ONNX
    best_dir = os.path.join(output_dir, "best")
    if os.path.exists(best_dir):
        algo.restore(os.path.abspath(best_dir))
    export_onnx(algo, Path(output_dir))

    algo.stop()
    ray.shutdown()

    return output_dir


def main():
    parser = argparse.ArgumentParser(description="Train CORTEX tactical policy")
    parser.add_argument("--iterations", type=int, default=SBDAPMConfig.NUM_ITERATIONS)
    parser.add_argument("--checkpoint-freq", type=int, default=SBDAPMConfig.CHECKPOINT_FREQ)
    parser.add_argument("--host", type=str, default=SBDAPMConfig.HOST)
    parser.add_argument("--port", type=int, default=SBDAPMConfig.PORT)

    args = parser.parse_args()

    SBDAPMConfig.HOST = args.host
    SBDAPMConfig.PORT = args.port

    if not RLLIB_AVAILABLE:
        print("Error: ray[rllib] required. pip install ray[rllib] torch")
        sys.exit(1)

    train(args)


if __name__ == "__main__":
    main()
