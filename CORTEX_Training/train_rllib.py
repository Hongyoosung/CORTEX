"""
RLlib Training Script for CORTEX (v8.0 - Multi-Head Tactical Parameters)

Trains PPO agents via Schola gRPC connection to Unreal Engine.

v8.0 Architecture (Current - 2026-01-14):
    - MCTS assigns strategies → RL outputs tactical parameters + combat priority
    - Multi-head network: 68 input → [256, 256, 128] → 4 strategy heads (4 params each) + combat head (2 choices)
    - Hybrid action space: 4 continuous tactical params + 2 discrete combat logits
    - Curriculum learning (3 phases: Single → Mixed → Dynamic)
    - Exports to: cortex_policy_v8.onnx

v6.0 Architecture (Deprecated 2026-01-14):
    - MCTS assigns Missions → RL selects strategies (4-action space)
    - Single-head network: 68 input → [128, 128, 64] → 4 policy logits + 1 value
    - Exports to: cortex_policy_v6.onnx (archived to Content/AI/Models/v7.0-archive/)

Usage:
    1. Start UE with Schola plugin (game mode)
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

# Windows DLL fix: Disable CUDA if not using GPU (prevents c10.dll conflicts)
# Uncomment the line below if training on CPU only:
# os.environ["CUDA_VISIBLE_DEVICES"] = ""

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
# MULTI-HEAD TACTICAL POLICY (v8.0)
# ==============================================================================

class MultiHeadTacticalPolicy(TorchModelV2, nn.Module):
    """
    Multi-Head PPO Network for Tactical Parameters + Combat Control (v8.0)

    Architecture:
        - Input: 68 features (64 base + 4 strategy one-hot from MCTS)
        - Shared Feature Extractor: [128 → 128 → 64] ReLU
        - 4 Strategy-Specific Heads: Assault, Defend, Support, Retreat
          - Each head: FC(64→32→4) → Sigmoid → [Aggression, CoverPref, Spread, Risk]
        - 1 Combat Head: FC(64→2) → Softmax → [Closest, LowestHP]
        - 1 Shared Critic Head: FC(64→1) → Linear → State Value

    v8.0 Key Changes:
        - MCTS assigns strategies (part of observation, not action)
        - RL outputs tactical parameters (continuous) + combat choices (discrete)
        - Separate policy heads guarantee strategy differentiation
        - Combat control: target priority (2 discrete choices)

    Outputs:
        - Tactical parameters: 4 continuous values [0,1] (Aggression, CoverPref, Spread, Risk)
        - Combat priority: 2-way discrete (Closest, LowestHP)
        - State value: 1-dim value estimate
    """

    def __init__(self, obs_space, action_space, num_outputs, model_config, name):
        TorchModelV2.__init__(self, obs_space, action_space, num_outputs, model_config, name)
        nn.Module.__init__(self)

        # v8.0: Observation size from RLConfig (synced from C++)
        obs_dim = RLConfig.OBSERVATION_SIZE  # 68 features (64 base + 4 strategy one-hot)

        # Get hidden layers from config (default: [128, 128, 64])
        hidden_layers = model_config.get("custom_model_config", {}).get("hidden_layers", [128, 128, 64])

        # Shared feature extractor: learns common features (perception, tactical context)
        layers = []
        prev_size = obs_dim
        for hidden_size in hidden_layers:
            layers.append(SlimFC(prev_size, hidden_size, activation_fn=nn.ReLU))
            prev_size = hidden_size

        self.shared_trunk = nn.Sequential(*layers)
        final_hidden_size = hidden_layers[-1]  # Last layer size (64)

        # === Strategy-Specific Policy Heads (4 heads) ===
        # Each strategy has dedicated output layers for tactical parameters
        # This architecturally guarantees behavioral differentiation
        self.assault_head = nn.Sequential(
            SlimFC(final_hidden_size, 32, activation_fn=nn.ReLU),
            SlimFC(32, 4, activation_fn=nn.Sigmoid)  # [Aggression, CoverPref, Spread, Risk]
        )
        self.defend_head = nn.Sequential(
            SlimFC(final_hidden_size, 32, activation_fn=nn.ReLU),
            SlimFC(32, 4, activation_fn=nn.Sigmoid)
        )
        self.support_head = nn.Sequential(
            SlimFC(final_hidden_size, 32, activation_fn=nn.ReLU),
            SlimFC(32, 4, activation_fn=nn.Sigmoid)
        )
        self.retreat_head = nn.Sequential(
            SlimFC(final_hidden_size, 32, activation_fn=nn.ReLU),
            SlimFC(32, 4, activation_fn=nn.Sigmoid)
        )

        # === Combat Head (Target Priority) ===
        # 2-way softmax: [Closest, LowestHP]
        self.combat_head = SlimFC(final_hidden_size, 2, activation_fn=None)  # Logits for softmax

        # === Shared Value Head ===
        # Value estimate independent of strategy assignment
        self.value_head = SlimFC(final_hidden_size, 1, activation_fn=None)

        # Store last features for value function
        self._last_features = None
        self._last_strategy_index = None

    @override(TorchModelV2)
    def forward(self, input_dict, state, seq_lens):
        """
        Forward pass (v8.0 - Multi-Head)

        Args:
            input_dict: Contains 'obs' with shape [batch, 68]
                - obs[:, :64]: base observation
                - obs[:, 64:68]: strategy one-hot [Assault, Defend, Support, Retreat]

        Returns:
            (action_logits, state): 6-dim output (4 tactical params + 2 combat logits), unchanged state
        """
        obs = input_dict["obs"]
        batch_size = obs.shape[0]

        # Extract strategy one-hot (last 4 features)
        strategy_onehot = obs[:, 64:68]  # [batch, 4]

        # Run shared trunk
        features = self.shared_trunk(obs)  # [batch, 64]
        self._last_features = features

        # Route to appropriate strategy head based on one-hot encoding
        # For each sample in batch, select the appropriate head
        tactical_params = torch.zeros(batch_size, 4, device=obs.device)  # [batch, 4]

        for i in range(batch_size):
            strategy_idx = torch.argmax(strategy_onehot[i]).item()
            self._last_strategy_index = strategy_idx  # Store for debugging

            if strategy_idx == 0:  # Assault
                tactical_params[i] = self.assault_head(features[i:i+1]).squeeze(0)
            elif strategy_idx == 1:  # Defend
                tactical_params[i] = self.defend_head(features[i:i+1]).squeeze(0)
            elif strategy_idx == 2:  # Support
                tactical_params[i] = self.support_head(features[i:i+1]).squeeze(0)
            elif strategy_idx == 3:  # Retreat
                tactical_params[i] = self.retreat_head(features[i:i+1]).squeeze(0)

        # Combat head (shared across all strategies)
        combat_logits = self.combat_head(features)  # [batch, 2]

        # Concatenate outputs: [4 tactical params, 2 combat logits] = 6 dims
        # RLlib will treat this as a multi-discrete action space
        output = torch.cat([tactical_params, combat_logits], dim=-1)  # [batch, 6]

        return output, state

    @override(TorchModelV2)
    def value_function(self):
        """Return value estimate from value head (used by PPO)"""
        if self._last_features is None:
            raise ValueError("Must call forward() before value_function()")
        return self.value_head(self._last_features).squeeze(-1)


# ==============================================================================
# SINGLE-HEAD STRATEGY POLICY (v6.0 - DEPRECATED)
# ==============================================================================

class SingleHeadStrategyPolicy(TorchModelV2, nn.Module):
    """
    Single-Head PPO Network for Strategy Selection (v6.0)

    Architecture:
        - Input: 68 features (64 base + 4 Mission context)
        - Shared Trunk: [128 → 128 → 64] ReLU (learns common features)
        - Policy Head: 4 strategy logits (Assault, Defend, Support, Retreat)
        - Value Head: 1-dim value estimate (for MCTS + PPO)

    v6.0 Changes:
        - MCTS assigns Missions, RL selects strategies (not micro-actions)
        - Simplified from 13-output multi-head to 4-output single-head
        - Mission context included in observation (replaces strategy index)

    Outputs:
        - Policy logits: 4-dim [Assault, Defend, Support, Retreat]
        - State value: 1-dim value estimate
    """

    def __init__(self, obs_space, action_space, num_outputs, model_config, name):
        TorchModelV2.__init__(self, obs_space, action_space, num_outputs, model_config, name)
        nn.Module.__init__(self)

        # v6.0: Observation size from RLConfig (synced from C++)
        obs_dim = RLConfig.OBSERVATION_SIZE  # 68 features (64 base + 4 Mission context)

        # Get hidden layers from config (default: [256, 256, 128])
        hidden_layers = model_config.get("custom_model_config", {}).get("hidden_layers", [256, 256, 128])

        # Shared trunk: learns common features (perception, tactical context)
        layers = []
        prev_size = obs_dim
        for hidden_size in hidden_layers:
            layers.append(SlimFC(prev_size, hidden_size, activation_fn=nn.ReLU))
            prev_size = hidden_size

        self.shared_trunk = nn.Sequential(*layers)
        final_hidden_size = hidden_layers[-1]  # Last layer size

        # Single policy head: 4 strategy logits (v6.0)
        # Outputs probability distribution over [Assault, Defend, Support, Retreat]
        self.policy_head = SlimFC(final_hidden_size, num_outputs, activation_fn=None)

        # Value head: state value estimate (used by MCTS + PPO)
        self.value_head = SlimFC(final_hidden_size, 1, activation_fn=None)

        # Store last features for value function
        self._last_features = None

    @override(TorchModelV2)
    def forward(self, input_dict, state, seq_lens):
        """
        Forward pass (v6.0 - Single Head)

        Args:
            input_dict: Contains 'obs' with shape [batch, 68] (64 base + 4 Mission context)

        Returns:
            (policy_logits, state): 4-dim strategy logits, unchanged state
        """
        obs = input_dict["obs"]

        # Run shared trunk (learns from full observation including Mission context)
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

    # Network architecture (increased capacity for value learning)
    HIDDEN_LAYERS = [256, 256, 128]  # Increased from [128, 128, 64]

    # PPO hyperparameters (v8.0: Tuned for hybrid continuous + discrete action space)
    LEARNING_RATE = 5e-5  # Reduced from 1e-4 for continuous action stability
    TRAIN_BATCH_SIZE = 8000  # Increased from 4000 for better continuous gradient estimates
    SGD_MINIBATCH_SIZE = 512  # Doubled from 256 for more stable updates
    NUM_SGD_ITER = 15  # Increased from 10 for better continuous action learning
    GAMMA = 0.99
    GAE_LAMBDA = 0.95
    CLIP_PARAM = 0.2
    ENTROPY_COEFF = 0.01  # Reduced from 0.5 (continuous actions have higher base entropy)
    VF_LOSS_COEFF = 1.5  # Unchanged - critical for value learning

    # Training
    NUM_WORKERS = 0  # Windows fix: 0 = single process (no DLL conflicts)
    NUM_ENVS_PER_WORKER = 1
    NUM_ITERATIONS = 100
    CHECKPOINT_FREQ = 10

    # Paths
    OUTPUT_DIR = "training_results"
    MODEL_NAME = "tactical_policy"


# ==============================================================================
# CURRICULUM LEARNING (v8.0)
# ==============================================================================

class CurriculumScheduler:
    """
    Three-phase curriculum learning for v8.0 tactical parameter training.

    Phase 1 (0-1000 episodes): Single Strategy
        - All agents assigned Assault strategy
        - Learn: Basic movement, objective advancement, combat engagement
        - Goal: Establish baseline tactical parameter values

    Phase 2 (1000-3000 episodes): Mixed Strategies
        - 2 agents Assault, 2 agents Defend
        - Learn: Strategy-specific parameter differentiation
        - Goal: Separate policy heads learn distinct profiles

    Phase 3 (3000+ episodes): Dynamic Assignment
        - MCTS controls strategy assignments
        - Learn: Adaptive tactics, strategy coordination
        - Goal: Robust policy across all strategy combinations
    """

    def __init__(self, phase1_episodes=1000, phase2_episodes=3000):
        self.phase1_threshold = phase1_episodes
        self.phase2_threshold = phase2_episodes
        self.current_phase = 1
        self.episode_count = 0
        self.last_log_episode = 0

    def get_strategy_assignments(self, num_agents=4):
        """
        Get strategy assignments for current curriculum phase.

        Returns:
            dict {agent_id: strategy_index} or None (MCTS control in Phase 3)
        """
        if self.episode_count < self.phase1_threshold:
            # Phase 1: All Assault
            return {f"agent_{i}": 0 for i in range(num_agents)}

        elif self.episode_count < self.phase2_threshold:
            # Phase 2: 2 Assault, 2 Defend
            assignments = {}
            for i in range(num_agents):
                assignments[f"agent_{i}"] = 0 if i < 2 else 1
            return assignments

        else:
            # Phase 3: MCTS controls
            return None

    def update(self, episodes_this_iter):
        """Update episode count and log phase transitions."""
        prev_phase = self.current_phase
        self.episode_count += episodes_this_iter

        # Determine current phase
        if self.episode_count >= self.phase2_threshold:
            self.current_phase = 3
        elif self.episode_count >= self.phase1_threshold:
            self.current_phase = 2
        else:
            self.current_phase = 1

        # Log phase transition
        if self.current_phase != prev_phase:
            self._log_phase_transition()

        # Periodic status log (every 100 episodes)
        if self.episode_count - self.last_log_episode >= 100:
            self._log_status()
            self.last_log_episode = self.episode_count

    def _log_phase_transition(self):
        """Log curriculum phase transition."""
        print(f"\n{'='*70}")
        print(f"CURRICULUM PHASE TRANSITION: Phase {self.current_phase} ACTIVATED")
        print(f"{'='*70}")
        print(f"Episode Count: {self.episode_count}")

        if self.current_phase == 1:
            print(f"Strategy Assignment: All Assault (single strategy learning)")
            print(f"Focus: Basic movement, combat, objective advancement")
        elif self.current_phase == 2:
            print(f"Strategy Assignment: 2 Assault, 2 Defend (mixed strategies)")
            print(f"Focus: Strategy differentiation, parameter profile separation")
        elif self.current_phase == 3:
            print(f"Strategy Assignment: MCTS-controlled (dynamic)")
            print(f"Focus: Adaptive tactics, full strategy coordination")

        print(f"{'='*70}\n")

    def _log_status(self):
        """Log periodic curriculum status."""
        assignments = self.get_strategy_assignments()
        assignment_str = "MCTS-controlled" if assignments is None else str(assignments)
        print(f"[CURRICULUM] Phase {self.current_phase} | Episodes: {self.episode_count} | Assignments: {assignment_str}")


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
        # v8.0: Multi-head tactical policy (4 strategy heads + combat head)
        "custom_model": "multi_head_tactical_policy",
        "custom_model_config": {
            "obs_dim": RLConfig.OBSERVATION_SIZE,  # v8.0: 68 features (64 base + 4 strategy one-hot)
            "hidden_layers": SBDAPMConfig.HIDDEN_LAYERS,  # [128, 128, 64]
            # v8.0: Hybrid action space (4 continuous tactical + 2 combat logits)
            "num_outputs": RLConfig.NUM_TACTICAL_PARAMS + RLConfig.NUM_COMBAT_CHOICES,  # 4 + 2 = 6
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
    """Register multi-head tactical policy with RLlib (v8.0)"""
    from ray.rllib.models import ModelCatalog

    if TORCH_AVAILABLE:
        # v8.0: Multi-head tactical parameters + combat control
        ModelCatalog.register_custom_model("multi_head_tactical_policy", MultiHeadTacticalPolicy)
        print("[v8.0] Multi-head tactical policy registered with RLlib")

        # v6.0: Keep legacy single-head for backwards compatibility
        ModelCatalog.register_custom_model("single_head_strategy_policy", SingleHeadStrategyPolicy)
        print("[v6.0] Legacy single-head strategy policy registered")
    else:
        print("[WARNING] PyTorch not available - cannot register custom model")



def export_onnx_v8(algo, output_dir):
    """
    Export trained multi-head policy to ONNX format (v8.0)

    Exports ONNX model with separate strategy heads for C++ inference:
    - cortex_policy_v8.onnx

    Model structure:
        Input: 68 features (64 base + 4 strategy one-hot from MCTS)
        Output 1-4: Tactical parameters for each strategy head [4 continuous values each]
        Output 5: Combat priority logits [2-way: Closest, LowestHP]
        Output 6: State value estimate
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
        # Export Multi-Head Network (v8.0)
        # Separate heads for each strategy + combat + value
        # ========================================

        class MultiHeadPolicyWrapper(nn.Module):
            """
            Wrapper for multi-head policy export (v8.0)

            Exports all strategy-specific heads separately so C++ can:
            1. Route to correct head based on MCTS-assigned strategy
            2. Sample combat priority from combat head
            3. Use value head for RL training

            Outputs:
                - assault_tactical: [batch, 4] tactical params for Assault
                - defend_tactical: [batch, 4] tactical params for Defend
                - support_tactical: [batch, 4] tactical params for Support
                - retreat_tactical: [batch, 4] tactical params for Retreat
                - combat_logits: [batch, 2] target priority logits [Closest, LowestHP]
                - value: [batch, 1] state value estimate
            """
            def __init__(self, model):
                super().__init__()
                self.model = model

            def forward(self, obs):
                """
                Args:
                    obs: [batch, 68] (64 base + 4 strategy one-hot)

                Returns:
                    Tuple of (assault_tactical, defend_tactical, support_tactical,
                             retreat_tactical, combat_logits, value)
                """
                # Run shared trunk
                features = self.model.shared_trunk(obs)  # [batch, 64]

                # Run all strategy heads
                assault_tactical = self.model.assault_head(features)  # [batch, 4]
                defend_tactical = self.model.defend_head(features)    # [batch, 4]
                support_tactical = self.model.support_head(features)  # [batch, 4]
                retreat_tactical = self.model.retreat_head(features)  # [batch, 4]

                # Run combat head
                combat_logits = self.model.combat_head(features)  # [batch, 2]

                # Run value head
                value = self.model.value_head(features)  # [batch, 1]

                return (assault_tactical, defend_tactical, support_tactical,
                       retreat_tactical, combat_logits, value)

        wrapper = MultiHeadPolicyWrapper(model)
        wrapper.eval()

        # Dummy input: observation size from RLConfig (synced from C++)
        dummy_input = torch.randn(1, RLConfig.OBSERVATION_SIZE)

        # Export multi-head model
        model_path = output_dir / "cortex_policy_v8.onnx"
        torch.onnx.export(
            wrapper,
            dummy_input,
            str(model_path),
            input_names=["observation"],
            output_names=[
                "assault_tactical",   # Output 0: [batch, 4]
                "defend_tactical",    # Output 1: [batch, 4]
                "support_tactical",   # Output 2: [batch, 4]
                "retreat_tactical",   # Output 3: [batch, 4]
                "combat_logits",      # Output 4: [batch, 2]
                "value"               # Output 5: [batch, 1]
            ],
            dynamic_axes={
                "observation": {0: "batch_size"},
                "assault_tactical": {0: "batch_size"},
                "defend_tactical": {0: "batch_size"},
                "support_tactical": {0: "batch_size"},
                "retreat_tactical": {0: "batch_size"},
                "combat_logits": {0: "batch_size"},
                "value": {0: "batch_size"}
            },
            opset_version=11
        )

        print(f"\n[v8.0 EXPORT COMPLETE]")
        print(f"[SUCCESS] Multi-head policy exported to: {model_path}")
        print(f"\nModel structure:")
        print(f"  - Input: {RLConfig.OBSERVATION_SIZE} dims (64 base + 4 strategy one-hot)")
        print(f"  - Output 0 (Assault): 4 tactical params [Aggression, CoverPref, Spread, Risk]")
        print(f"  - Output 1 (Defend): 4 tactical params")
        print(f"  - Output 2 (Support): 4 tactical params")
        print(f"  - Output 3 (Retreat): 4 tactical params")
        print(f"  - Output 4 (Combat): 2 logits [Closest, LowestHP]")
        print(f"  - Output 5 (Value): 1 dim state value estimate")
        print(f"\nReady for UE5:")
        print(f"  - Copy cortex_policy_v8.onnx to: Content/AI/Models/")
        print(f"  - RLPolicyNetwork routes to appropriate strategy head based on MCTS assignment")
        print(f"  - Combat head provides target priority selection")
        print(f"  - Value head used for PPO training")
        return True

    except Exception as e:
        print(f"ONNX export failed: {e}")
        import traceback
        traceback.print_exc()
        print("Saving checkpoint instead...")
        return False


def export_onnx(algo, output_dir):
    """
    Export trained single-head policy to ONNX format (v6.0 - DEPRECATED)

    Exports single ONNX model for C++ inference:
    - cortex_policy_v6.onnx

    Model structure:
        Input: 68 features (64 base + 4 Mission context)
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
                    obs: [batch, 68] (64 base + 4 Mission context)

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
        print(f"  - Input: {RLConfig.OBSERVATION_SIZE} dims (64 base + 4 Mission context)")
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


def get_entropy_coeff(iteration):
    """
    Entropy coefficient decay schedule.

    Strategy:
    - Iterations 0-30: 0.5 (high exploration)
    - Iterations 31-80: 0.5 → 0.05 (linear decay)
    - Iterations 81+: 0.05 (low exploitation)

    This allows initial exploration, then gradual policy convergence.
    """
    if iteration <= 30:
        return 0.5
    elif iteration <= 80:
        # Linear decay from 0.5 to 0.05
        progress = (iteration - 30) / 50  # 0.0 to 1.0
        return 0.5 - (0.45 * progress)
    else:
        return 0.05


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

    # Windows fix: Ensure no stale Ray processes
    try:
        ray.shutdown()
        print("Cleaned up existing Ray processes")
    except:
        pass  # No existing Ray instance

    # Windows multiprocessing fix: Use 'spawn' to avoid DLL conflicts
    import multiprocessing
    try:
        multiprocessing.set_start_method('spawn', force=True)
    except RuntimeError:
        pass  # Already set

    # Windows Ray fix: Create temp directory and configure Ray
    import tempfile
    ray_temp_dir = os.path.join(tempfile.gettempdir(), "ray_cortex")
    os.makedirs(ray_temp_dir, exist_ok=True)

    print(f"Initializing Ray with temp directory: {ray_temp_dir}")
    print("This may take 10-30 seconds on first run...")

    # Initialize Ray with Windows-specific settings
    try:
        ray.init(
            ignore_reinit_error=True,
            include_dashboard=False,  # Disable dashboard to avoid GCS timeout
            _temp_dir=ray_temp_dir,   # Explicit temp directory
            num_cpus=4,               # Limit CPU detection issues on Windows
            object_store_memory=1 * 1024**3,  # 1GB object store (prevent memory issues)
            _system_config={
                "gcs_rpc_server_reconnect_timeout_s": 300,  # Increase GCS timeout
            },
            logging_level="ERROR",  # Reduce log spam
        )
        print("✅ Ray initialized successfully!")
    except Exception as e:
        print(f"⚠️  Standard Ray init failed: {e}")
        print("Falling back to Ray local mode (single machine, no GCS)...")
        ray.init(
            local_mode=True,  # Fallback: single-process mode (no GCS, no workers)
            ignore_reinit_error=True,
        )
        print("✅ Ray local mode initialized (NUM_WORKERS will be ignored)")
        if SBDAPMConfig.NUM_WORKERS > 0:
            print(f"⚠️  Note: NUM_WORKERS={SBDAPMConfig.NUM_WORKERS} ignored in local mode")
            SBDAPMConfig.NUM_WORKERS = 0  # Force single worker in local mode

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


    # Initial entropy coefficient (will be updated dynamically in training loop)
    config.entropy_coeff = SBDAPMConfig.ENTROPY_COEFF

    algo = config.build(logger_creator=logger_creator)

    # Training loop
    best_reward = float("-inf")
    total_ue_episodes = 0  # Track UE episodes for sync logging

    # v8.0: Initialize curriculum scheduler
    curriculum = CurriculumScheduler(phase1_episodes=1000, phase2_episodes=3000)
    print(f"\n{'='*70}")
    print(f"CURRICULUM LEARNING ENABLED (v8.0)")
    print(f"  Phase 1 (0-1000 episodes): All Assault")
    print(f"  Phase 2 (1000-3000 episodes): 2 Assault, 2 Defend")
    print(f"  Phase 3 (3000+ episodes): MCTS-controlled")
    print(f"{'='*70}\n")

    # v8.0: Track parameter distributions for monitoring differentiation
    from collections import defaultdict
    strategy_params_history = defaultdict(list)

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

        # v8.0: Update curriculum scheduler
        curriculum.update(episodes_this_iter)

        # Multi-agent specific metrics
        num_agent_steps = result.get("num_agent_steps_sampled", 0)
        num_env_steps = result.get("num_env_steps_sampled", 0)

        # Extract value function metrics
        vf_explained_var = result.get("info", {}).get("learner", {}).get("default_policy", {}).get("learner_stats", {}).get("vf_explained_var", 0.0)
        entropy = result.get("info", {}).get("learner", {}).get("default_policy", {}).get("learner_stats", {}).get("entropy", 0.0)

        # Calculate current entropy coefficient based on iteration (for display only)
        current_entropy_coeff = get_entropy_coeff(i)

        print(f"Iteration {i+1:4d} (UE Episodes: ~{total_ue_episodes}): "
              f"reward={episode_reward_mean:8.2f}, "
              f"len={episode_len_mean:6.1f}, "
              f"vf_var={vf_explained_var:.4f}, "
              f"entropy={entropy:.3f}, "
              f"agent_steps={num_agent_steps}")

        # v8.0: Monitor parameter profiles every 10 iterations
        if (i + 1) % 10 == 0:
            try:
                import torch
                policy = algo.get_policy("shared_policy")
                model = policy.model

                print(f"\n{'='*70}")
                print(f"TACTICAL PARAMETER PROFILES (Iteration {i+1}, Phase {curriculum.current_phase})")
                print(f"{'='*70}")

                for strategy_idx, strategy_name in enumerate(['Assault', 'Defend', 'Support', 'Retreat']):
                    # Create dummy observation with strategy one-hot
                    dummy_obs = torch.zeros(1, 68)
                    dummy_obs[0, 64 + strategy_idx] = 1.0

                    # Sample tactical parameters from strategy-specific head
                    with torch.no_grad():
                        output, _ = model({"obs": dummy_obs}, [], None)
                        tactical_params = output[0, :4].cpu().numpy()

                    strategy_params_history[strategy_name].append(tactical_params)

                    # Calculate recent mean
                    recent = np.array(strategy_params_history[strategy_name][-10:])
                    means = recent.mean(axis=0)
                    stds = recent.std(axis=0)

                    print(f"{strategy_name:8s}: Agg={means[0]:.3f}±{stds[0]:.3f}, "
                          f"Cover={means[1]:.3f}±{stds[1]:.3f}, "
                          f"Spread={means[2]:.3f}±{stds[2]:.3f}, "
                          f"Risk={means[3]:.3f}±{stds[3]:.3f}")

                # Calculate differentiation metric (Assault vs Defend)
                if len(strategy_params_history['Assault']) > 0 and len(strategy_params_history['Defend']) > 0:
                    assault_params = strategy_params_history['Assault'][-1]
                    defend_params = strategy_params_history['Defend'][-1]
                    diff = np.abs(assault_params - defend_params).mean()
                    print(f"\nDifferentiation (Assault vs Defend): {diff:.3f} (target: >0.3)")

                print(f"{'='*70}\n")

            except Exception as e:
                print(f"[WARNING] Parameter monitoring failed: {e}")

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

    # Try to export from best checkpoint first (v8.0: multi-head policy)
    best_checkpoint_dir = os.path.join(output_dir, "best")
    if os.path.exists(best_checkpoint_dir):
        print(f"\nExporting best model from: {best_checkpoint_dir}")
        # Restore best checkpoint (use absolute path for PyArrow compatibility)
        algo.restore(os.path.abspath(best_checkpoint_dir))
        if export_onnx_v8(algo, Path(output_dir)):
            print(f"\nBest model exported to: {output_dir}/cortex_policy_v8.onnx")
        else:
            print("\nBest model export failed, trying final checkpoint...")
            # Fallback to final checkpoint (use absolute path for PyArrow compatibility)
            algo.restore(os.path.abspath(output_dir))
            export_onnx_v8(algo, Path(output_dir))
    else:
        # No best checkpoint, use final
        print(f"\nExporting final model...")
        if export_onnx_v8(algo, Path(output_dir)):
            print(f"\nModel exported to: {output_dir}/cortex_policy_v8.onnx")

    print("\nTo use in Unreal Engine:")
    print("  1. Copy cortex_policy_v8.onnx to Content/AI/Models/")
    print("  2. RLPolicyNetwork routes to strategy-specific head based on MCTS assignment")
    print("  3. Combat head provides target priority, Value head used for PPO training")

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
