"""
RLlib Training Script for CORTEX (v8.0 - Simplified)

Trains PPO agents via Schola gRPC connection to Unreal Engine.

v8.0 Changes:
    - REMOVED: Curriculum learning (MCTS in UE5 already controls strategy assignments)
    - SIMPLIFIED: Training loop with minimal logging
    - FIXED: Episode boundary synchronization (UE5 is single source of truth)

v8.0 Architecture:
    - MCTS assigns strategies → RL outputs tactical parameters + combat priority
    - Multi-head network: 56 input (52 base + 4 strategy one-hot) → [256, 256, 128]
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
        OBSERVATION_SIZE = 56  # 52 base + 4 strategy one-hot
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
        - Input: 56 features (52 base + 4 strategy one-hot)
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

        obs_dim = RLConfig.OBSERVATION_SIZE  # 56
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

        # v8.10 FIX: Strategy-Specific Value Heads (Critical for VF collapse fix)
        # Each strategy sees different reward distributions:
        # - Assault: High combat spikes (kills, damage)
        # - Defend: High survival/cover (steady accumulation)
        # - Support: High coordination (formation, combined fire)
        # - Retreat: High survival + objective progress (escape rewards)
        # Single value head cannot learn all these modes → explained_var = 0
        self.assault_value_head = SlimFC(final_hidden_size, 1, activation_fn=None)
        self.defend_value_head = SlimFC(final_hidden_size, 1, activation_fn=None)
        self.support_value_head = SlimFC(final_hidden_size, 1, activation_fn=None)
        self.retreat_value_head = SlimFC(final_hidden_size, 1, activation_fn=None)

        # v8.8: Log-std clamping bounds (prevents entropy explosion)
        log_std_config = model_config.get("custom_model_config", {})
        self.log_std_min = log_std_config.get("log_std_min", -2.0)
        self.log_std_max = log_std_config.get("log_std_max", 0.5)

        self._last_features = None

    @override(TorchModelV2)
    def forward(self, input_dict, state, seq_lens):
        obs = input_dict["obs"]
        batch_size = obs.shape[0]

        # Extract strategy one-hot (last 4 features)
        strategy_onehot = obs[:, 52:56]

        # Run shared trunk
        features = self.shared_trunk(obs)
        self._last_features = features
        self._last_strategy_onehot = strategy_onehot  # Store for value_function()

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

        # v8.8: Clamp log_std to prevent entropy explosion
        log_stds_raw = self.log_std_head(features)
        log_stds = torch.clamp(log_stds_raw, self.log_std_min, self.log_std_max)

        output = torch.cat([means, log_stds], dim=-1)

        return output, state

    @override(TorchModelV2)
    def value_function(self):
        if self._last_features is None:
            raise ValueError("Must call forward() before value_function()")

        # v8.10 FIX: Strategy-specific value function
        # Route to appropriate value head based on strategy one-hot
        # This fixes the value function collapse issue where single head
        # couldn't learn multi-modal return distributions across strategies
        batch_size = self._last_features.shape[0]

        # Get last observation to extract strategy (stored during forward())
        if not hasattr(self, '_last_strategy_onehot'):
            # Fallback: use Assault head if strategy not available
            return self.assault_value_head(self._last_features).squeeze(-1)

        strategy_onehot = self._last_strategy_onehot
        values = torch.zeros(batch_size, device=self._last_features.device)

        for i in range(batch_size):
            strategy_idx = torch.argmax(strategy_onehot[i]).item()

            if strategy_idx == 0:  # Assault
                values[i] = self.assault_value_head(self._last_features[i:i+1]).squeeze()
            elif strategy_idx == 1:  # Defend
                values[i] = self.defend_value_head(self._last_features[i:i+1]).squeeze()
            elif strategy_idx == 2:  # Support
                values[i] = self.support_value_head(self._last_features[i:i+1]).squeeze()
            else:  # Retreat
                values[i] = self.retreat_value_head(self._last_features[i:i+1]).squeeze()

        return values


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

    # PPO hyperparameters (v8.10.2: Post-Diagnosis Fix)
    LEARNING_RATE = 5e-5  # REDUCED from 1e-4 (was too aggressive, causing instability)
    LEARNING_RATE_END = 1e-5  # Keep final LR the same
    TRAIN_BATCH_SIZE = 48000
    SGD_MINIBATCH_SIZE = 2048
    NUM_SGD_ITER = 3
    GAMMA = 0.99
    GAE_LAMBDA = 0.95
    CLIP_PARAM = 0.15

    # v9.0.2 ENTROPY FIX: Entropy still at 6.8 after 192k steps (NOT decreasing!)
    # ROOT CAUSE: Return normalization created microscopic rewards (fixed above)
    #             Entropy coefficient still needs to be stronger now that rewards are normal
    # FIX: Increase entropy coeff to 0.05 (5× original) to push entropy down
    ENTROPY_COEFF = 0.05  # INCREASED from 0.02 (entropy stuck at 6.8, needs stronger penalty)
    VF_LOSS_COEFF = 1.0   # INCREASED from 0.5 (VF needs stronger learning signal)
    GRAD_CLIP = 0.5
    VF_CLIP_PARAM = 10.0  # v9.0.3: INCREASED from 2.0 - Allow full TD error range for [-5, 5] rewards

    # v9.0.1: Log-std clamping to reduce entropy ceiling
    # Previous: LOG_STD_MAX=0.5 → max σ=1.65 → max entropy=9.59 (too high)
    # Fixed: LOG_STD_MAX=0.0 → max σ=1.0 → max entropy≈7.8 (better convergence)
    LOG_STD_MIN = -2.0  # Minimum log_std (σ_min ≈ 0.135)
    LOG_STD_MAX = 0.0   # FIXED: Reduced from 0.5 (max σ = 1.0 instead of 1.65)

    # Training
    NUM_WORKERS = 0  # Windows: single process
    NUM_ENVS_PER_WORKER = 1 
    NUM_UE5_ENVIRONMENTS = 4 
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
        "num_envs": SBDAPMConfig.NUM_UE5_ENVIRONMENTS,  # Pass environment count to Python env
        "normalize_returns": False,  # v9.0.2: DISABLED - rewards already normalized in C++
    }

    # Add Docker-specific settings
    if is_docker:
        config["is_docker"] = True
        config["timeout"] = 60  # Extended timeout for Docker networking (increased from 30s)

    return config


def create_ppo_config():
    """Create RLlib PPO configuration - Fixed based on ppo.py source."""
    from episode_logger_callback import EpisodeLoggerCallback

    # 1. Config 객체 생성
    config = PPOConfig()

    # 2. Environment 설정
    config = config.environment(
        env="cortex_env",
        env_config=create_env_config(),
        disable_env_checking=True,
    )

    # 3. Framework 및 Runner 설정
    config = config.framework("torch")
    config = config.env_runners(
        num_env_runners=SBDAPMConfig.NUM_WORKERS,
        num_envs_per_env_runner=SBDAPMConfig.NUM_ENVS_PER_WORKER,
        rollout_fragment_length=256,
        batch_mode="truncate_episodes", 
    )
    
    # 4. Multi-agent 설정
    config = config.multi_agent(
        policies={"shared_policy"},
        policy_mapping_fn=lambda agent_id, episode, worker, **kwargs: "shared_policy",
        count_steps_by="agent_steps",
    )
    

    # 5. Callbacks (Episode Logging only)
    # Removed PolicyUpdatePauseCallback - not needed in sync architecture
    config.callbacks(EpisodeLoggerCallback)

    # 6. Debugging & Reporting
    config = config.debugging(log_level="WARN")
    config = config.reporting(
        metrics_num_episodes_for_smoothing=10,
        min_sample_timesteps_per_iteration=32000,  # v8.6 ASYNC: Match TRAIN_BATCH_SIZE (update every 1000 env steps)
    )

    # 7. Training 설정 (ppo.py에 명시된 인자만 training() 메서드로 전달)
    config = config.training(
        lr=SBDAPMConfig.LEARNING_RATE,  
        lr_schedule=[  
            (0, 5e-5),       
            (1000000, 2e-5), 
            (2000000, 1e-5), 
        ],
        train_batch_size=SBDAPMConfig.TRAIN_BATCH_SIZE,
        lambda_=SBDAPMConfig.GAE_LAMBDA,
        clip_param=SBDAPMConfig.CLIP_PARAM, 
        vf_clip_param=SBDAPMConfig.VF_CLIP_PARAM,  
        entropy_coeff=SBDAPMConfig.ENTROPY_COEFF,
        entropy_coeff_schedule=[
            (0, 0.05),         # v9.0.2: Higher initial (entropy stuck at 6.8)
            (500000, 0.03),    # v9.0.2: Slower decay
            (1000000, 0.02),   # v9.0.2: Keep minimum higher
        ],
        vf_loss_coeff=SBDAPMConfig.VF_LOSS_COEFF,  
        grad_clip=SBDAPMConfig.GRAD_CLIP, 
        use_gae=True,
        use_critic=True,
        use_kl_loss=True,
        kl_coeff=0.2,
        kl_target=0.01,
    )

    # PPO 고유 설정 (ppo.py __init__에 정의됨)
    config.num_epochs = SBDAPMConfig.NUM_SGD_ITER
    config.minibatch_size = SBDAPMConfig.SGD_MINIBATCH_SIZE
    config.shuffle_batch_per_epoch = True
    
    # 9. Model configuration
    config.model = {
        "custom_model": "multi_head_tactical_policy",
        "custom_model_config": {
            "obs_dim": RLConfig.OBSERVATION_SIZE,
            "hidden_layers": SBDAPMConfig.HIDDEN_LAYERS,
            # v8.8: Log-std clamping bounds to prevent entropy explosion
            "log_std_min": SBDAPMConfig.LOG_STD_MIN,
            "log_std_max": SBDAPMConfig.LOG_STD_MAX,
        },
        "max_seq_len": 20,
    }
    
    return config






def register_env():
    """Register custom environment with Ray."""
    from ray.tune.registry import register_env

    if SCHOLA_AVAILABLE:
        def env_creator(config):
            from cortex_env import CORTEXSyncMultiAgentEnv
            return CORTEXSyncMultiAgentEnv(**config)
    else:
        def env_creator(config):
            from cortex_env import CORTEXSyncMultiAgentEnv
            return CORTEXSyncMultiAgentEnv(**config)

    register_env("cortex_env", env_creator)


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

                # v8.10: Export all 4 strategy-specific value heads
                assault_value = self.model.assault_value_head(features)
                defend_value = self.model.defend_value_head(features)
                support_value = self.model.support_value_head(features)
                retreat_value = self.model.retreat_value_head(features)

                return (assault_tactical, defend_tactical, support_tactical,
                       retreat_tactical, combat_priority,
                       assault_value, defend_value, support_value, retreat_value)

        wrapper = MultiHeadPolicyWrapper(model)
        wrapper.eval()

        dummy_input = torch.randn(1, RLConfig.OBSERVATION_SIZE)
        model_path = output_dir / "cortex_policy_v8.onnx"

        torch.onnx.export(
            wrapper, dummy_input, str(model_path),
            input_names=["observation"],
            output_names=["assault_tactical", "defend_tactical", "support_tactical",
                         "retreat_tactical", "combat_priority",
                         "assault_value", "defend_value", "support_value", "retreat_value"],
            dynamic_axes={
                "observation": {0: "batch_size"},
                "assault_tactical": {0: "batch_size"},
                "defend_tactical": {0: "batch_size"},
                "support_tactical": {0: "batch_size"},
                "retreat_tactical": {0: "batch_size"},
                "combat_priority": {0: "batch_size"},
                "assault_value": {0: "batch_size"},
                "defend_value": {0: "batch_size"},
                "support_value": {0: "batch_size"},
                "retreat_value": {0: "batch_size"}
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

    # Override NUM_UE5_ENVIRONMENTS from environment if set
    if "NUM_UE5_ENVIRONMENTS" in os.environ:
        num_envs = int(os.environ["NUM_UE5_ENVIRONMENTS"])
        SBDAPMConfig.NUM_UE5_ENVIRONMENTS = num_envs
        print(f"[Config] NUM_UE5_ENVIRONMENTS overridden to {num_envs}")

    # Check if resuming from checkpoint
    resume_checkpoint = None
    start_iteration = 0
    if args.resume:
        resume_checkpoint = os.path.abspath(args.resume)
        if not os.path.exists(resume_checkpoint):
            print(f"ERROR: Checkpoint directory not found: {resume_checkpoint}")
            sys.exit(1)

        # Try to detect last iteration from progress.csv
        progress_csv = os.path.join(resume_checkpoint, "progress.csv")
        if os.path.exists(progress_csv):
            try:
                import csv
                with open(progress_csv, 'r') as f:
                    reader = csv.DictReader(f)
                    rows = list(reader)
                    if rows:
                        last_row = rows[-1]
                        start_iteration = int(last_row.get('training_iteration', 0))
                        print(f"[Resume] Detected last completed iteration: {start_iteration}")
            except Exception as e:
                print(f"[Resume] Could not read iteration from progress.csv: {e}")
                print("[Resume] Will resume from iteration 0")

    print("=" * 60)
    print("CORTEX v8.10.2 - Post-Diagnosis Corrective Fix")
    if args.resume:
        print(f"RESUMING from checkpoint: {resume_checkpoint}")
    print("=" * 60)
    print(f"  Host: {SBDAPMConfig.HOST}:{SBDAPMConfig.PORT}")
    print(f"  Workers: {SBDAPMConfig.NUM_WORKERS}")
    print(f"  UE5 Environments: {SBDAPMConfig.NUM_UE5_ENVIRONMENTS}")
    print(f"  Total Agents: {SBDAPMConfig.NUM_UE5_ENVIRONMENTS * 8} ({SBDAPMConfig.NUM_UE5_ENVIRONMENTS} envs × 8 agents)")
    if args.resume:
        print(f"  Starting Iteration: {start_iteration + 1}")
        print(f"  Remaining Iterations: {args.iterations}")
        print(f"  Total Iterations: {start_iteration + args.iterations}")
    else:
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

    # Create or use existing output directory
    if args.resume:
        output_dir = resume_checkpoint
        print(f"Output: {output_dir} (resuming)")
    else:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_dir = os.path.join(SBDAPMConfig.OUTPUT_DIR, timestamp)
        os.makedirs(output_dir, exist_ok=True)
        print(f"Output: {output_dir}")

    # Create logger
    from ray.tune.logger import UnifiedLogger, TBXLogger, JsonLogger, CSVLogger

    def logger_creator(config_dict):
        return UnifiedLogger(config_dict, output_dir, loggers=[JsonLogger, CSVLogger, TBXLogger])

    # Build or restore algorithm
    if args.resume:
        print(f"\nRestoring from checkpoint: {resume_checkpoint}")
        try:
            config = create_ppo_config()
            algo = config.build(logger_creator=logger_creator)
            algo.restore(resume_checkpoint)
            print("Checkpoint restored successfully!\n")
        except Exception as e:
            print(f"[ERROR] Failed to restore checkpoint: {e}")
            ray.shutdown()
            sys.exit(1)
    else:
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

    # If resuming, try to load cumulative stats from checkpoint
    if args.resume:
        progress_csv = os.path.join(resume_checkpoint, "progress.csv")
        if os.path.exists(progress_csv):
            try:
                import csv
                with open(progress_csv, 'r') as f:
                    reader = csv.DictReader(f)
                    rows = list(reader)
                    if rows:
                        last_row = rows[-1]
                        # Try to get cumulative stats
                        cumulative_episodes = int(last_row.get('episodes_total', 0))
                        cumulative_steps = int(last_row.get('num_env_steps_sampled_lifetime', 0))

                        # Try to get best reward from all previous iterations
                        all_rewards = [float(row.get('env_runners/episode_reward_mean', float('-inf')))
                                      for row in rows if row.get('env_runners/episode_reward_mean')]
                        if all_rewards:
                            best_reward = max([r for r in all_rewards if not np.isnan(r)])

                        print(f"[Resume] Loaded stats: episodes={cumulative_episodes}, steps={cumulative_steps}, best_reward={best_reward:.2f}")
            except Exception as e:
                print(f"[Resume] Could not load cumulative stats: {e}")

    print("\n" + "="*80)
    if args.resume:
        print(f"RESUMING TRAINING FROM ITERATION {start_iteration + 1}")
    else:
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
        current_iter = start_iteration + i + 1
        total_iters = start_iteration + args.iterations
        status_indicator = "✓" if episodes_completed_this_iter else "→"
        print(f"{status_indicator} {current_iter:>3}/{total_iters:<3} {reward:>10.2f} {ep_len:>8.1f} "
              f"{episodes:>10} {agent_steps:>12} {iter_time:>7.1f}s {best_reward:>10.2f}")

        # Print detailed breakdown every 10 iterations or on first iteration
        if i == 0 or (i + 1) % 10 == 0:
            print(f"\n  [ITERATION {current_iter} DETAILS]")
            if episodes_completed_this_iter:
                print(f"    Episode Reward: mean={reward:.2f}, min={reward_min:.2f}, max={reward_max:.2f}")
                print(f"    Episode length: {ep_len:.1f} steps")
                print(f"    Episodes this iteration: {episodes}")

                # Show custom metrics from callback (per-agent and per-env rewards)
                custom_metrics = env_results.get("custom_metrics", {})
                if custom_metrics:
                    agent_reward_mean = custom_metrics.get("agent_reward_mean_mean", None)
                    agent_reward_std = custom_metrics.get("agent_reward_std_mean", None)
                    env_reward_mean = custom_metrics.get("env_reward_mean_mean", None)

                    if agent_reward_mean is not None:
                        print(f"    Per-Agent Reward: mean={agent_reward_mean:.2f}, std={agent_reward_std:.2f}")
                    if env_reward_mean is not None:
                        print(f"    Per-Env Reward: mean={env_reward_mean:.2f}")
            else:
                print(f"    No episodes completed this iteration (still collecting samples)")
            print(f"    Agent steps this iteration: {agent_steps}")
            print(f"    Cumulative: {cumulative_episodes} episodes, {cumulative_steps} steps")

            # Show learner stats (v8.10: Enhanced VF collapse monitoring)
            learner_info = result.get('info', {}).get('learner', {}).get('shared_policy', {}).get('learner_stats', {})
            if learner_info:
                total_loss = learner_info.get('total_loss', 'N/A')
                vf_loss = learner_info.get('vf_loss', 'N/A')
                policy_loss = learner_info.get('policy_loss', 'N/A')
                kl = learner_info.get('kl', 'N/A')
                entropy = learner_info.get('entropy', 'N/A')
                cur_kl_coeff = learner_info.get('cur_kl_coeff', 'N/A')
                vf_explained_var = learner_info.get('vf_explained_var', 'N/A')

                print(f"    Loss: total={total_loss:.4f}, policy={policy_loss:.4f}, vf={vf_loss:.4f}" if isinstance(total_loss, float) else f"    Loss: {total_loss}")

                # v9.0.1: Enhanced diagnostics for entropy and value function
                if isinstance(entropy, float):
                    entropy_coeff = learner_info.get('entropy_coeff', 'N/A')
                    print(f"    Entropy: {entropy:.2f} (coeff={entropy_coeff:.4f}, penalty={(entropy * entropy_coeff):.2f})")
                if isinstance(vf_explained_var, float):
                    print(f"    Value Function: explained_var={vf_explained_var:.4f}, loss={vf_loss:.4f}")
                if isinstance(kl, float):
                    print(f"    KL Divergence: {kl:.6f} (coeff={cur_kl_coeff:.4f})")
            print()  # Blank line for readability

        # Checkpoint
        if (i + 1) % args.checkpoint_freq == 0:
            algo.save(output_dir)
            print(f"  >> Checkpoint saved at iteration {current_iter}\n")

        # Track best
        if reward > best_reward and not np.isnan(reward):
            best_reward = reward
            algo.save(os.path.join(output_dir, "best"))
            print(f"  >> NEW BEST REWARD: {best_reward:.2f} (iteration {current_iter})\n")

    # Final save
    print("\n" + "="*80)
    print("TRAINING COMPLETE!")
    print("="*80)
    if args.resume:
        print(f"Resumed from iteration: {start_iteration}")
        print(f"Additional iterations: {args.iterations}")
        print(f"Total iterations: {start_iteration + args.iterations}")
    else:
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
    parser.add_argument("--resume", type=str, default=None,
                       help="Path to checkpoint directory to resume training from (e.g., training_results/20260131_100243)")

    args = parser.parse_args()

    SBDAPMConfig.HOST = args.host
    SBDAPMConfig.PORT = args.port

    if not RLLIB_AVAILABLE:
        print("Error: ray[rllib] required. pip install ray[rllib] torch")
        sys.exit(1)

    train(args)


if __name__ == "__main__":
    main()
