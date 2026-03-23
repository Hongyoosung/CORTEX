"""
DE Entity-Centric Policy Training (v10.2+).

Modes:
  rllib     — RLlib PPO, single shared EntityCentricPolicy across all agents
  validate  — unit tests (policy shapes, ONNX, reward config)
  eval      — load checkpoint, print EQS weight profile

Usage:
  python train.py --mode rllib --iterations 100
  python train.py --mode validate
  python train.py --mode eval --checkpoint de_policy.pt
  python train.py --mode eval --checkpoint <rllib_checkpoint_dir>
"""

import sys
import os
import time
import numpy as np
import torch
import torch.nn as nn
from torch.utils.tensorboard import SummaryWriter
from typing import Dict
from collections import defaultdict
import json

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from policy import (
    EntityCentricPolicy, EntityCentricPolicy_NoSelfAttn,
    CentralizedCritic,
    PPOTrainer, ReplayBuffer, Transition,
    collate_fn, OBS_DIM, EQS_DIM, EQS_LABELS,
    GLOBAL_STATE_DIM, MAPPO_OBS_DIM,
)

# Ablation toggle: set USE_SELF_ATTN=0 to train without Self-Attention
USE_SELF_ATTN = os.environ.get('USE_SELF_ATTN', '1') == '1'

def _create_policy_network(hidden=64, heads=4):
    """Create policy based on USE_SELF_ATTN toggle."""
    if USE_SELF_ATTN:
        return EntityCentricPolicy(hidden=hidden, heads=heads)
    else:
        print("[ABLATION] Self-Attention DISABLED — using CrossAttention-only variant")
        return EntityCentricPolicy_NoSelfAttn(hidden=hidden, heads=heads)

try:
    from ray.rllib.env.multi_agent_env import MultiAgentEnv
    RLLIB_AVAILABLE = True
except ImportError:
    RLLIB_AVAILABLE = False
    print("Warning: RLlib not available. Install with: pip install ray[rllib]")


# ── Reward configuration ──────────────────────────────────────────────────────
# These values mirror the C++ DERewardData cooperative defaults (plan §3).
# Python-side reward is already computed by C++; this config is for reference
# and for any Python-side shaping/scaling applied in process_reward().

REWARD_CONFIG = {
    # Cooperative base occupation (applied by DERewardSubsystem C++)
    "BaseOccupationReward":    2.0,   # +2.0/step: sole ally within 2000cm of uncontrolled base
    "CoOccupationPenalty":    -0.5,   # -0.5/step: 2+ allies stacking same base
    "BaseCaptureCreditReward": 5.0,   # +5.0 sparse: agent that flipped base ownership
    "AssignedBaseReachReward": 1.0,   # +1.0 sparse: first time reaching assigned base

    # Python-side scaling applied to the received step reward before PPO update
    "reward_scale":  0.01,
    "reward_clip":   5.0,
}


def process_reward(raw_reward: float) -> float:
    """
    Apply Python-side reward scaling and clipping to the raw UE5 step reward.
    The cooperative base rewards are computed C++-side; this function only
    normalises the magnitude for PPO stability.
    """
    r = raw_reward * REWARD_CONFIG["reward_scale"]
    r = float(np.clip(r, -REWARD_CONFIG["reward_clip"], REWARD_CONFIG["reward_clip"]))
    return r


# ── Curriculum Scheduler ──────────────────────────────────────────────────────

class CurriculumScheduler:
    """
    Automatically promotes ScriptedAI difficulty when RL performance is
    sustained above a per-tier reward threshold.

    Tier promotion flow:
        0 (Passive) → 1 (Basic) → 2 (Standard) → 3 (Aggressive)

    A tier is promoted once `window` consecutive reward measurements all
    exceed `promotion_thresholds[current_tier]`.  Thresholds are expressed
    in the *scaled* episode reward space (after process_reward scaling).

    On promotion the new tier is written to `config_path` as JSON so that
    UE5's LoadTierFromConfig() picks it up at the next episode reset.
    """

    # Win-rate thresholds: fraction of early-terminated episodes won by RL team.
    # Timeout episodes are excluded (no clear winner).
    PROMOTION_THRESHOLDS = {
        0: 0.50,  # Passive  → Basic:      RL wins >50% of decided episodes
        1: 0.55,  # Basic    → Standard:   RL wins >55% of decided episodes
        2: 0.65,  # Standard → Aggressive: RL wins >65% of decided episodes
        3: 1.01,  # Aggressive — no further promotion (unreachable sentinel)
    }

    def __init__(
        self,
        config_path: str,
        initial_tier: int = 1,
        window: int = 5,
    ):
        self.config_path  = config_path
        self.current_tier = initial_tier
        self.window       = window
        self._win_rate_buf: list = []

        # Write initial tier so UE5 always finds a valid config
        self._write_config()
        print(f"[Curriculum] Initialized at tier {self.current_tier}  "
              f"(config → {self.config_path})")

    # ── Public interface ──────────────────────────────────────────────────

    def update(self, win_rate: float) -> bool:
        """
        Call once per training iteration with the RL win rate for that iteration.
        win_rate is the fraction of early-terminated (non-timeout) episodes won
        by the RL team. Returns True if the tier was promoted this call.
        """
        if self.current_tier >= 3:
            return False  # already at max

        self._win_rate_buf.append(win_rate)
        if len(self._win_rate_buf) > self.window:
            self._win_rate_buf.pop(0)

        if len(self._win_rate_buf) < self.window:
            return False  # not enough history yet

        threshold = self.PROMOTION_THRESHOLDS[self.current_tier]
        if all(r >= threshold for r in self._win_rate_buf):
            self.current_tier += 1
            self._win_rate_buf.clear()
            self._write_config()
            print(f"[Curriculum] *** TIER PROMOTED → {self.current_tier} ***  "
                  f"(win_rate≥{threshold:.0%} sustained over {self.window} iters)")
            return True

        return False

    def get_tier(self) -> int:
        return self.current_tier

    # ── Internal ──────────────────────────────────────────────────────────

    def _write_config(self):
        """Write current tier to JSON so UE5 reads it on next episode reset."""
        os.makedirs(os.path.dirname(self.config_path) or ".", exist_ok=True)
        payload = json.dumps({"difficulty_tier": self.current_tier}, indent=2)
        with open(self.config_path, "w") as f:
            f.write(payload)


# ── Training configuration ────────────────────────────────────────────────────

class DETrainingConfig:
    HOST    = "localhost"
    PORT    = 50051
    NUM_UE5_ENVIRONMENTS = int(os.environ.get('NUM_SCHOLA_ENVS', 4))
    NUM_WORKERS          = int(os.environ.get('NUM_WORKERS', 0))
    NUM_ITERATIONS       = int(os.environ.get('NUM_ITERATIONS', 100))

    # PPO hyperparameters
    LEARNING_RATE    = 3e-4
    TRAIN_BATCH_SIZE = 8000
    MINIBATCH_SIZE   = 512
    NUM_SGD_ITER     = 6     # reverted: 8 epochs × VF_CLIP_PARAM=20 caused critic oscillation
    GAMMA            = 0.99
    GAE_LAMBDA       = 0.95
    CLIP_PARAM       = 0.2
    ENTROPY_COEFF    = 0.01
    VF_LOSS_COEFF    = 0.5   # reverted: 1.0 + VF_CLIP=20 + 8 epochs stacked too aggressively
    GRAD_CLIP        = 1.0   # loosened: 0.5 was too tight when critic needs to close a large value gap
    VF_CLIP_PARAM    = 10.0  # reverted: 20.0 caused critic oscillation on Support's bimodal reward

    LR_SCHEDULE = [
        [0,          3e-4],
        [1_000_000,  1e-4],  # was 2M — shift earlier; Vanguard expl_var dropped at ~1M steps
        [3_000_000,  5e-5],
    ]
    ENTROPY_SCHEDULE = [
        [0,          0.01],
        [300_000,    0.005],
        [800_000,    0.003],
        [1_500_000,  0.002],  # was 0.001 — keep higher to prevent Strike entropy collapse
        [2_500_000,  0.001],  # floor: Strike needs exploration budget past 1M steps
        [5_000_000,  0.001],  # hold floor; do not decay further
    ]

    OUTPUT_DIR = "/app/training_results"


# ── RLlib helpers ─────────────────────────────────────────────────────────────

STRATEGY_POLICY_NAMES = {0: "strike_policy", 1: "vanguard_policy", 2: "support_policy"}

if RLLIB_AVAILABLE:
    from ray.rllib.models.torch.torch_modelv2 import TorchModelV2
    from ray.rllib.utils.annotations import override

    class EntityCentricRLlibModel(TorchModelV2, nn.Module):
        """
        RLlib TorchModelV2 wrapper for EntityCentricPolicy with MAPPO dual critics.

        Three separate instances are registered — one per role (strike / vanguard / support).
        Each only receives observations for its assigned role.

        Observation layout (297-dim):
            [0:226]   agent obs  → actor input + local critic input
            [226:297]  global state (FDETeamWorldState, 71-dim) → centralized critic input

        Value estimation (dual):
            V = α · V_local(agent_obs) + (1-α) · V_central(global_state)
            α is a learnable mixing coefficient (initialized to 0.5).

        forward() returns (B, 14) = [means(7), log_stds(7)].
        """

        # Shared centralized critic across all three role policies (within same worker).
        # RLlib instantiates all three policies in the same process for num_workers=0.
        # Each policy gets its own EntityCentricRLlibModel instance but they share
        # the centralized critic via this class-level reference. For multi-worker
        # setups, each worker gets its own copy (gradients are synced by RLlib).
        _shared_centralized_critic = None

        def __init__(self, obs_space, action_space, num_outputs, model_config, name):
            TorchModelV2.__init__(self, obs_space, action_space, num_outputs, model_config, name)
            nn.Module.__init__(self)
            cfg = model_config.get("custom_model_config", {})
            hidden = cfg.get("hidden", 64)
            heads  = cfg.get("heads", 4)

            # Per-role actor + local critic (unchanged)
            self.policy = _create_policy_network(hidden=hidden, heads=heads)

            # Shared centralized critic (MAPPO)
            if EntityCentricRLlibModel._shared_centralized_critic is None:
                EntityCentricRLlibModel._shared_centralized_critic = CentralizedCritic(
                    state_dim=GLOBAL_STATE_DIM, hidden=256,
                )
            self.centralized_critic = EntityCentricRLlibModel._shared_centralized_critic

            # Learnable mixing coefficient for dual value estimation
            # sigmoid(0.0) = 0.5 → equal weight initially
            self._value_mix_logit = nn.Parameter(torch.tensor(0.0))

            self._last_agent_obs = None
            self._last_global_state = None

        @override(TorchModelV2)
        def forward(self, input_dict, state, seq_lens):
            obs = input_dict["obs"].float()                       # (B, 297)

            # Slice: actor uses agent obs [0:226], critic uses global state [226:297]
            self._last_agent_obs    = obs[:, :OBS_DIM]                     # (B, 226)
            self._last_global_state = obs[:, OBS_DIM:]                     # (B, 71)

            means = self.policy(self._last_agent_obs)             # (B, 7)
            log_stds = torch.clamp(
                self.policy.log_std,
                EntityCentricPolicy.LOG_STD_MIN,
                EntityCentricPolicy.LOG_STD_MAX,
            ).unsqueeze(0).expand_as(means)                       # (B, 7)
            return torch.cat([means, log_stds], dim=-1), state    # (B, 14)

        @override(TorchModelV2)
        def value_function(self):
            """
            Dual value estimation: α · V_local + (1-α) · V_central

            V_local:   entity-centric attention critic on 226-dim agent obs
            V_central: MLP critic on 71-dim global team state
            α:         learned mixing coefficient (sigmoid of _value_mix_logit)
            """
            alpha = torch.sigmoid(self._value_mix_logit)
            v_local   = self.policy.get_value(self._last_agent_obs)           # (B,)
            v_central = self.centralized_critic(self._last_global_state)      # (B,)
            return alpha * v_local + (1.0 - alpha) * v_central


    def create_ppo_config():
        """Build RLlib PPO config — three separate per-role EntityCentricPolicy instances."""
        from ray.rllib.algorithms.ppo import PPOConfig
        from ray.rllib.policy.policy import PolicySpec

        try:
            from env_wrapper import AGENT_STRATEGY_REGISTRY
        except ImportError:
            from training.env_wrapper import AGENT_STRATEGY_REGISTRY

        def _policy_mapping_fn(agent_id, episode, worker, **kwargs):
            strategy_idx = AGENT_STRATEGY_REGISTRY.get(agent_id)
            if strategy_idx is None:
                # Agent not yet registered — happens before first reset.
                # Log a warning so we can detect if UE5 is not sending strategy info.
                print(f"[POLICY MAP] WARNING: {agent_id} not in registry, defaulting to strike")
                strategy_idx = 0
            return STRATEGY_POLICY_NAMES[strategy_idx]

        model_cfg = {"custom_model": "entity_centric_model",
                     "custom_model_config": {"hidden": 64, "heads": 4},
                     "max_seq_len": 20}

        config = PPOConfig()
        config = config.environment(
            env="de_entity_centric",
            env_config={
                "host":     DETrainingConfig.HOST,
                "base_port": DETrainingConfig.PORT,
                "num_envs": DETrainingConfig.NUM_UE5_ENVIRONMENTS,
            },
            disable_env_checking=True,
        )
        config = config.framework("torch")
        config = config.api_stack(
            enable_rl_module_and_learner=False,
            enable_env_runner_and_connector_v2=False,
        )
        config = config.env_runners(
            num_env_runners=DETrainingConfig.NUM_WORKERS,
            num_envs_per_env_runner=1,
            rollout_fragment_length="auto",
            batch_mode="complete_episodes",
        )
        # Three separate policies — one per role
        config = config.multi_agent(
            policies={
                name: PolicySpec(config={"model": model_cfg})
                for name in STRATEGY_POLICY_NAMES.values()
            },
            policy_mapping_fn=_policy_mapping_fn,
            count_steps_by="agent_steps",
        )
        config = config.debugging(log_level="WARN")
        config = config.reporting(
            metrics_num_episodes_for_smoothing=10,
            min_sample_timesteps_per_iteration=DETrainingConfig.TRAIN_BATCH_SIZE,
        )
        config = config.training(
            lr=DETrainingConfig.LEARNING_RATE,
            lr_schedule=DETrainingConfig.LR_SCHEDULE,
            gamma=DETrainingConfig.GAMMA,
            entropy_coeff_schedule=DETrainingConfig.ENTROPY_SCHEDULE,
            train_batch_size=DETrainingConfig.TRAIN_BATCH_SIZE,
            minibatch_size=DETrainingConfig.MINIBATCH_SIZE,
            num_epochs=DETrainingConfig.NUM_SGD_ITER,
            lambda_=DETrainingConfig.GAE_LAMBDA,
            clip_param=DETrainingConfig.CLIP_PARAM,
            vf_clip_param=DETrainingConfig.VF_CLIP_PARAM,
            entropy_coeff=DETrainingConfig.ENTROPY_COEFF,
            vf_loss_coeff=DETrainingConfig.VF_LOSS_COEFF,
            grad_clip=DETrainingConfig.GRAD_CLIP,
            use_gae=True,
            use_critic=True,
            use_kl_loss=True,
            kl_coeff=0.2,
            kl_target=0.01,
        )
        return config


# ── Training functions ────────────────────────────────────────────────────────

def train_with_rllib(args):
    """Main RLlib training loop."""
    import ray
    import shutil
    from datetime import datetime
    from ray.tune.registry import register_env
    from ray.rllib.models import ModelCatalog

    try:
        from env_wrapper import DEEntityCentricEnv
    except ImportError:
        from training.env_wrapper import DEEntityCentricEnv

    print("=" * 70)
    print("DE Entity-Centric Policy Training (RLlib)")
    print(f"  host={DETrainingConfig.HOST}:{DETrainingConfig.PORT}")
    print(f"  workers={DETrainingConfig.NUM_WORKERS}  "
          f"ue5_envs={DETrainingConfig.NUM_UE5_ENVIRONMENTS}")
    print(f"  iterations={args.iterations}")
    print("=" * 70)

    try:
        ray.shutdown()
    except Exception:
        pass

    import tempfile
    ray_tmp = os.path.join(tempfile.gettempdir(), "ray_de_ec")
    os.makedirs(ray_tmp, exist_ok=True)
    ray.init(
        ignore_reinit_error=True,
        include_dashboard=False,
        _temp_dir=ray_tmp,
        num_cpus=4,
        object_store_memory=1 * 1024 ** 3,
        logging_level="ERROR",
    )

    register_env("de_entity_centric", lambda cfg: DEEntityCentricEnv(**cfg))
    # Single model class registered; all three per-role policies reference it.
    ModelCatalog.register_custom_model("entity_centric_model", EntityCentricRLlibModel)

    timestamp  = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = os.path.join(DETrainingConfig.OUTPUT_DIR, timestamp)
    latest_dir = os.path.join(output_dir, "latest")
    os.makedirs(output_dir, exist_ok=True)
    print(f"Output: {output_dir}")

    tb = SummaryWriter(log_dir=os.path.join(output_dir, "tb"))

    config = create_ppo_config()
    print("\nConnecting to UE5...")
    try:
        algo = config.build()
        print("Connected.\n")
    except Exception as e:
        print(f"[ERROR] Build failed: {e}")
        ray.shutdown()
        return

    if args.resume:
        print(f"Resuming from {args.resume}")
        algo.restore(args.resume)

    # Curriculum scheduler — writes <output_dir>/scripted_ai_config.json which
    # UE5's LoadTierFromConfig() reads at episode reset to update ScriptedAI tier.
    scripted_ai_config = os.path.join(output_dir, "scripted_ai_config.json")
    curriculum = CurriculumScheduler(
        config_path=scripted_ai_config,
        initial_tier=args.scripted_ai_tier,
        window=args.curriculum_window,
    )

    best_reward    = float("-inf")
    cumul_episodes = 0
    cumul_steps    = 0

    print(f"{'Iter':<6} {'Reward':>10} {'EpLen':>8} {'Steps':>12} {'Time':>8} {'Tier':>5}")
    print("-" * 58)

    for i in range(args.iterations):
        t0     = time.time()
        result = algo.train()
        dt     = time.time() - t0

        env_r  = result.get("env_runners", {})

        # Old RLlib API stack puts episode metrics at the top-level result dict;
        # new API stack nests them under "env_runners". Fall back accordingly.
        def _get(key, default=None):
            v = env_r.get(key)
            if v is None:
                v = result.get(key)
            return v if v is not None else default

        # 1. 기존 지표 추출
        reward = _get("episode_reward_mean", 0.0)
        ep_len = _get("episode_len_mean",    0.0)
        eps    = _get("episodes_this_iter",  0)

        # [수정됨] RLlib은 이미 누적된 스텝 수를 반환하므로, 기존 누적 변수에 더하지 않고 덮어씁니다.
        cumul_steps = result.get("num_agent_steps_sampled", 0)

        # 이번 이터레이션에서 샘플링된 스텝 수 (초당 처리량 계산용)
        steps_this_iter = result.get("num_agent_steps_sampled_this_iter", 0)

        # 2. 신규 지표 추가 추출 (최대/최소 보상)
        reward_max = _get("episode_reward_max")
        reward_min = _get("episode_reward_min")

        reward = 0.0 if reward is None or (isinstance(reward, float) and np.isnan(reward)) else reward
        ep_len = 0.0 if ep_len is None or (isinstance(ep_len, float) and np.isnan(ep_len)) else ep_len

        cumul_episodes += eps

        # ── Global metrics (all agents combined) ──────────────────────────────
        tb.add_scalar("global/reward/episode_mean", reward, cumul_steps)
        if reward_max is not None and not np.isnan(float(reward_max)):
            tb.add_scalar("global/reward/episode_max", float(reward_max), cumul_steps)
        if reward_min is not None and not np.isnan(float(reward_min)):
            tb.add_scalar("global/reward/episode_min", float(reward_min), cumul_steps)
        tb.add_scalar("global/env/episode_len_mean", ep_len, cumul_steps)
        tb.add_scalar("global/env/episodes_completed", cumul_episodes, cumul_steps)
        steps_per_sec = steps_this_iter / max(dt, 1e-6)
        tb.add_scalar("global/performance/steps_per_sec", steps_per_sec, cumul_steps)

        # ── Per-role reward & component metrics ───────────────────────────────
        custom = _get("custom_metrics", {})
        
        # 1. RLlib Native Policy Rewards (가장 확실한 전략별 보상 지표)
        policy_reward_mean = _get("policy_reward_mean", {})
        policy_reward_min  = _get("policy_reward_min", {})
        policy_reward_max  = _get("policy_reward_max", {})

        # 디버깅: 처음 3번의 이터레이션 동안 실제 RLlib이 뱉어내는 키들을 출력하여 구조 확인
        if i < 3:
            print(f"[DEBUG iter={i}] policy_reward_mean: {policy_reward_mean}")
            print(f"[DEBUG iter={i}] custom_metrics keys: {list(custom.keys())[:15]}")
            # 만약 result 최상위 키가 궁금하다면 아래 주석을 해제하세요.
            # print(f"[DEBUG iter={i}] result keys: {list(result.keys())}")

        # 2. RLlib 기본 제공 Policy별 보상 텐서보드 기록
        for strat, policy_name in zip(("strike", "vanguard", "support"), STRATEGY_POLICY_NAMES.values()):
            if policy_name in policy_reward_mean:
                tb.add_scalar(f"{strat}/reward/episode_mean", float(policy_reward_mean[policy_name]), cumul_steps)
            if policy_name in policy_reward_max:
                tb.add_scalar(f"{strat}/reward/episode_max", float(policy_reward_max[policy_name]), cumul_steps)
            if policy_name in policy_reward_min:
                tb.add_scalar(f"{strat}/reward/episode_min", float(policy_reward_min[policy_name]), cumul_steps)

            # 3. Custom Metrics (기존 로직 유지 - 환경 래퍼에서 따로 커스텀 기록을 하는 경우)
            key_mean = f"reward_strategy_{strat}_mean"
            key_sum  = f"reward_strategy_{strat}_sum_mean"
            if key_mean in custom:
                tb.add_scalar(f"{strat}/custom_reward/step_mean", float(custom[key_mean]), cumul_steps)
            if key_sum in custom:
                tb.add_scalar(f"{strat}/custom_reward/step_sum",  float(custom[key_sum]),  cumul_steps)

        # 4. 세부 보상 컴포넌트 기록 (기존 유지)
        REWARD_COMPONENTS = [
            "BaseOccupationReward", "CoOccupationPenalty",
            "BaseCaptureCreditReward",
            "AssignedBaseReachReward",
        ]
        for comp in REWARD_COMPONENTS:
            ckey = f"reward_component_{comp}_mean"
            if ckey in custom:
                tb.add_scalar(f"global/reward_components/{comp}", float(custom[ckey]), cumul_steps)

        # ── Per-role PPO learner metrics ──────────────────────────────────────
        # TB hierarchy: {role}/{metric_group}/{metric}
        # e.g. strike/losses/policy_loss, vanguard/kl/value, support/entropy/value
        TRAIN_METRICS = {
            "losses/policy_loss":  ["policy_loss", "mean_policy_loss"],
            "losses/vf_loss":      ["vf_loss", "mean_vf_loss"],
            "losses/total_loss":   ["total_loss", "mean_total_loss"],
            "kl/value":            ["kl", "mean_kl_loss", "kl_loss", "KL"],
            "kl/coeff":            ["cur_kl_coeff", "kl_coeff"],
            "entropy/value":       ["entropy", "mean_entropy", "entropy_coeff"],
            "lr/value":            ["cur_lr", "lr"],
            "vf/explained_var":    ["vf_explained_var", "explained_variance"],
        }
        learner_info = result.get("info", {}).get("learner", {})
        for policy_name in STRATEGY_POLICY_NAMES.values():
            role  = policy_name.replace("_policy", "")   # "strike" / "vanguard" / "support"
            raw   = learner_info.get(policy_name, {})
            stats = raw.get("learner_stats", raw)
            if not stats:
                continue
            for tag, candidates in TRAIN_METRICS.items():
                for k in candidates:
                    if k in stats:
                        try:
                            val = float(stats[k])
                            if not np.isnan(val):
                                tb.add_scalar(f"{role}/{tag}", val, cumul_steps)
                                break
                        except (ValueError, TypeError):
                            pass

        # ── MAPPO dual-critic metrics ──────────────────────────────────────────
        # Log the learned mixing coefficient α = sigmoid(logit) for each role
        for policy_name in STRATEGY_POLICY_NAMES.values():
            role = policy_name.replace("_policy", "")
            rllib_policy = algo.get_policy(policy_name)
            if rllib_policy and hasattr(rllib_policy.model, '_value_mix_logit'):
                alpha = torch.sigmoid(rllib_policy.model._value_mix_logit).item()
                tb.add_scalar(f"{role}/mappo/value_mix_alpha", alpha, cumul_steps)

        # ── Curriculum tier update ────────────────────────────────────────────
        rl_win_rate = custom.get("rl_win_rate_mean", custom.get("rl_win_rate"))
        if rl_win_rate is not None and not np.isnan(float(rl_win_rate)):
            rl_win_rate = float(rl_win_rate)
            tb.add_scalar("curriculum/rl_win_rate", rl_win_rate, cumul_steps)
            tier_promoted = curriculum.update(rl_win_rate)
        else:
            # No decided episodes this iteration — skip promotion check
            tier_promoted = False
        tb.add_scalar("curriculum/scripted_ai_tier", curriculum.get_tier(), cumul_steps)
        if tier_promoted:
            tb.add_scalar("curriculum/tier_promotion_step", cumul_steps, curriculum.get_tier())

        tb.flush()

        print(f"{i+1:>3}/{args.iterations:<3}  {reward:>10.2f}  "
              f"{ep_len:>8.1f}  {cumul_steps:>12}  {dt:>7.1f}s  {curriculum.get_tier():>5}")

        if (i + 1) % args.checkpoint_freq == 0:
            algo.save(output_dir)
            print(f"  >> Checkpoint @ iter {i+1}")

        if (i + 1) % args.latest_freq == 0:
            if os.path.exists(latest_dir):
                shutil.rmtree(latest_dir)
            algo.save(latest_dir)

        if reward > best_reward and not np.isnan(reward):
            best_reward = reward
            algo.save(os.path.join(output_dir, "best"))
            print(f"  >> NEW BEST: {best_reward:.2f}")

    print("\n" + "=" * 70)
    print("TRAINING COMPLETE")
    print(f"  total_episodes={cumul_episodes}  total_steps={cumul_steps}")
    print(f"  best_reward={best_reward:.2f}")
    print(f"  output={output_dir}")
    print("=" * 70)

    algo.save(output_dir)

    # Export one ONNX per role from best checkpoint
    best_dir = os.path.join(output_dir, "best")
    if os.path.exists(best_dir):
        algo.restore(os.path.abspath(best_dir))
    for strategy_idx, policy_name in STRATEGY_POLICY_NAMES.items():
        role_name = policy_name.replace("_policy", "")  # "strike" / "vanguard" / "support"
        rllib_policy = algo.get_policy(policy_name)
        if rllib_policy:
            model = rllib_policy.model.policy
            onnx_path = os.path.join(output_dir, f"de_policy_{role_name}.onnx")
            model.export_onnx(onnx_path)

    tb.close()
    algo.stop()
    ray.shutdown()
    return output_dir


# ── Validation ────────────────────────────────────────────────────────────────

def run_validation() -> bool:
    """Unit tests for EntityCentricPolicy, ONNX export, and reward config."""
    import tempfile

    print("\n" + "=" * 70)
    print("DE Entity-Centric Validation Suite")
    print("=" * 70 + "\n")

    passed = 0
    failed = 0

    def check(name, cond, detail=""):
        nonlocal passed, failed
        if cond:
            passed += 1
            print(f"  PASS: {name}")
        else:
            failed += 1
            print(f"  FAIL: {name}  {detail}")

    policy = EntityCentricPolicy()
    B      = 8
    obs    = torch.zeros(B, OBS_DIM)

    # -- Test 1: forward shape --
    print("[Test 1] forward() output shape")
    with torch.no_grad():
        out = policy(obs)
    check("output shape (B, 7)", out.shape == (B, EQS_DIM), f"got {out.shape}")
    check("output range [-1, 1]",
          (out >= -1.0).all() and (out <= 1.0).all(),
          f"range=[{out.min():.3f},{out.max():.3f}]")

    # -- Test 2: value function --
    print("[Test 2] get_value() shape")
    with torch.no_grad():
        val = policy.get_value(obs)
    check("value shape (B,)", val.shape == (B,), f"got {val.shape}")

    # -- Test 3: sample_action --
    print("[Test 3] sample_action()")
    acts, lps = policy.sample_action(obs)
    check("actions shape (B, 7)", acts.shape == (B, EQS_DIM), f"got {acts.shape}")
    check("actions in [-1, 1]",
          (acts >= -1).all() and (acts <= 1).all(),
          f"range=[{acts.min():.3f},{acts.max():.3f}]")
    check("log_probs shape (B,)", lps.shape == (B,), f"got {lps.shape}")

    # -- Test 4: compute_log_prob --
    print("[Test 4] compute_log_prob()")
    lp2 = policy.compute_log_prob(obs, acts)
    check("log_prob shape (B,)", lp2.shape == (B,), f"got {lp2.shape}")
    check("log_prob finite",     torch.isfinite(lp2).all().item())

    # -- Test 5: mask suppression --
    print("[Test 5] Padding mask suppression")
    from policy import (ALLY_MASK_START, ENEMY_MASK_START, BASE_MASK_START,
                        MAX_ALLIES, MAX_ENEMIES, MAX_BASES)
    obs_all_pad = obs.clone()
    obs_all_pad[:, ALLY_MASK_START:ENEMY_MASK_START] = 1.0   # all ally slots masked
    with torch.no_grad():
        out_pad = policy(obs_all_pad)
    check("forward works with all-padding ally mask", out_pad.shape == (B, EQS_DIM))

    # -- Test 6: obs dimension = 226 --
    print("[Test 6] OBS_DIM == 226")
    check("OBS_DIM == 226", OBS_DIM == 226, f"got {OBS_DIM}")
    expected = 7 + 8 * 9 + 8 * 8 + 8 * 7 + 8 + 8 + 8 + 3
    check("Layout arithmetic == 226", expected == 226, f"got {expected}")

    # -- Test 7: ONNX export + reload --
    print("[Test 7] ONNX export and ORT reload")
    try:
        import onnxruntime as ort
        with tempfile.NamedTemporaryFile(suffix=".onnx", delete=False) as f:
            p = f.name
        policy.export_onnx(p)
        sess = ort.InferenceSession(p)
        test_obs = torch.randn(1, OBS_DIM)
        ort_out  = sess.run(None, {"observation": test_obs.numpy()})[0]
        with torch.no_grad():
            pt_out = policy(test_obs).numpy()
        err = np.abs(ort_out - pt_out).max()
        check("ONNX matches PyTorch (err < 1e-4)", err < 1e-4, f"max_err={err:.6f}")
        os.unlink(p)
    except ImportError:
        print("  SKIP: onnxruntime not installed")

    # -- Test 8: PPOTrainer update --
    print("[Test 8] PPOTrainer.update() runs without error")
    trainer = PPOTrainer(EntityCentricPolicy(), learning_rate=1e-3)
    transitions = [
        Transition(
            state=np.random.randn(OBS_DIM).astype(np.float32),
            action=np.random.uniform(-1, 1, EQS_DIM).astype(np.float32),
            reward=float(np.random.randn()),
            next_state=np.random.randn(OBS_DIM).astype(np.float32),
            done=False,
            log_prob=float(np.random.randn()),
        )
        for _ in range(64)
    ]
    try:
        metrics = trainer.update(transitions, epochs=2)
        check("update() returns metrics dict", isinstance(metrics, dict))
        check("policy_loss in metrics", "policy_loss" in metrics)
    except Exception as e:
        check("PPOTrainer.update() no exception", False, str(e))

    # -- Test 9: reward config values match plan §3 --
    print("[Test 9] Reward config defaults match plan §3")
    check("BaseOccupationReward == 2.0",    REWARD_CONFIG["BaseOccupationReward"]    == 2.0)
    check("CoOccupationPenalty == -0.5",    REWARD_CONFIG["CoOccupationPenalty"]     == -0.5)
    check("BaseCaptureCreditReward == 5.0", REWARD_CONFIG["BaseCaptureCreditReward"] == 5.0)
    check("AssignedBaseReachReward == 1.0", REWARD_CONFIG["AssignedBaseReachReward"] == 1.0)

    # -- Test 10: collate_fn --
    print("[Test 10] collate_fn")
    obs_list = [np.random.randn(OBS_DIM).astype(np.float32) for _ in range(4)]
    batch    = collate_fn(obs_list)
    check("collate_fn shape (4, 226)", batch.shape == (4, OBS_DIM), f"got {batch.shape}")

    # -- Test 11: CentralizedCritic --
    print("[Test 11] CentralizedCritic shape and output")
    cc = CentralizedCritic(state_dim=GLOBAL_STATE_DIM)
    dummy_gs = torch.randn(B, GLOBAL_STATE_DIM)
    with torch.no_grad():
        cc_val = cc(dummy_gs)
    check("CentralizedCritic output shape (B,)", cc_val.shape == (B,), f"got {cc_val.shape}")
    check("CentralizedCritic output finite", torch.isfinite(cc_val).all().item())

    # -- Test 12: MAPPO obs slicing (297 = 226 + 71) --
    print("[Test 12] MAPPO obs dimension arithmetic")
    check("MAPPO_OBS_DIM == 297", MAPPO_OBS_DIM == 297, f"got {MAPPO_OBS_DIM}")
    check("GLOBAL_STATE_DIM == 71", GLOBAL_STATE_DIM == 71, f"got {GLOBAL_STATE_DIM}")
    check("OBS_DIM + GLOBAL_STATE_DIM == MAPPO_OBS_DIM",
          OBS_DIM + GLOBAL_STATE_DIM == MAPPO_OBS_DIM,
          f"{OBS_DIM} + {GLOBAL_STATE_DIM} != {MAPPO_OBS_DIM}")

    # -- Test 13: Dual value estimation (EntityCentricRLlibModel) --
    print("[Test 13] Dual value estimation")
    if RLLIB_AVAILABLE:
        from gymnasium import spaces as gym_spaces
        mappo_obs_space = gym_spaces.Box(-np.inf, np.inf, shape=(MAPPO_OBS_DIM,), dtype=np.float32)
        mappo_act_space = gym_spaces.Box(-1.0, 1.0, shape=(EQS_DIM,), dtype=np.float32)
        model_cfg = {"custom_model_config": {"hidden": 64, "heads": 4}}
        # Reset shared critic for clean test
        EntityCentricRLlibModel._shared_centralized_critic = None
        rllib_model = EntityCentricRLlibModel(
            mappo_obs_space, mappo_act_space, 14, model_cfg, "test_model"
        )
        dummy_mappo = torch.randn(B, MAPPO_OBS_DIM)
        with torch.no_grad():
            out, _ = rllib_model.forward({"obs": dummy_mappo}, [], None)
            val = rllib_model.value_function()
        check("RLlib model output shape (B, 14)", out.shape == (B, 14), f"got {out.shape}")
        check("Dual value output shape (B,)", val.shape == (B,), f"got {val.shape}")
        check("Dual value finite", torch.isfinite(val).all().item())

        # Verify shared critic across instances
        EntityCentricRLlibModel._shared_centralized_critic = None
        m1 = EntityCentricRLlibModel(mappo_obs_space, mappo_act_space, 14, model_cfg, "m1")
        m2 = EntityCentricRLlibModel(mappo_obs_space, mappo_act_space, 14, model_cfg, "m2")
        check("Shared centralized critic (same object)",
              m1.centralized_critic is m2.centralized_critic)
        # Clean up
        EntityCentricRLlibModel._shared_centralized_critic = None
    else:
        print("  SKIP: RLlib not available")

    print("\n" + "=" * 70)
    print(f"Results: {passed} passed, {failed} failed / {passed+failed} total")
    print("=" * 70 + "\n")
    return failed == 0


# ── Checkpoint evaluation ─────────────────────────────────────────────────────

def evaluate_checkpoint(checkpoint_path: str):
    """Load checkpoint and print EQS weight profile."""
    print("\n" + "=" * 70)
    print("DE Entity-Centric — Checkpoint Evaluation")
    print("=" * 70 + "\n")

    N = 200
    obs = torch.randn(N, OBS_DIM)

    if checkpoint_path.endswith(".pt") or checkpoint_path.endswith(".pth"):
        policy = EntityCentricPolicy()
        policy.load_state_dict(torch.load(checkpoint_path, map_location="cpu"))
        policy.eval()
        with torch.no_grad():
            out = policy(obs)
        means = out.mean(dim=0).numpy()
        stds  = out.std(dim=0).numpy()
        learned_std = policy.get_std().detach().numpy()

        print("EQS Weight Profile (random obs sample):")
        for i, label in enumerate(EQS_LABELS):
            bar = "#" * int((means[i] + 1) / 2 * 20) + "." * int((1 - means[i]) / 2 * 20)
            print(f"  {label:<28} {means[i]:>6.3f} ± {stds[i]:.3f}  [{bar}]")
        print(f"\nLearned σ: {np.round(learned_std, 3)}")

    else:
        # RLlib checkpoint directory
        import ray
        from ray.tune.registry import register_env
        from ray.rllib.models import ModelCatalog
        try:
            from env_wrapper import DEEntityCentricEnv
        except ImportError:
            from training.env_wrapper import DEEntityCentricEnv

        ray.init(ignore_reinit_error=True, include_dashboard=False, logging_level="ERROR")
        register_env("de_entity_centric", lambda cfg: DEEntityCentricEnv(**cfg))
        ModelCatalog.register_custom_model("entity_centric_model", EntityCentricRLlibModel)
        algo = create_ppo_config().build()
        algo.restore(checkpoint_path)

        for policy_name in STRATEGY_POLICY_NAMES.values():
            role = policy_name.replace("_policy", "")
            rllib_policy = algo.get_policy(policy_name)
            if not rllib_policy:
                continue
            model  = rllib_policy.model.policy
            model.eval()
            with torch.no_grad():
                out = model(obs)
            means = out.mean(dim=0).numpy()
            stds  = out.std(dim=0).numpy()
            learned_std = model.get_std().detach().numpy()

            print(f"\nEQS Weight Profile — {role.upper()}:")
            for i, label in enumerate(EQS_LABELS):
                bar = "#" * int((means[i] + 1) / 2 * 20) + "." * int((1 - means[i]) / 2 * 20)
                print(f"  {label:<28} {means[i]:>6.3f} ± {stds[i]:.3f}  [{bar}]")
            print(f"  Learned σ: {np.round(learned_std, 3)}")

        algo.stop()
        ray.shutdown()

    print("\n" + "=" * 70 + "\n")


# ── Entry point ───────────────────────────────────────────────────────────────

if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(
        description="DE Entity-Centric Policy Training"
    )
    parser.add_argument("--mode",
                        choices=["rllib", "validate", "eval"],
                        default="rllib")
    parser.add_argument("--iterations",     type=int,  default=DETrainingConfig.NUM_ITERATIONS)
    parser.add_argument("--checkpoint-freq",type=int,  default=10)
    parser.add_argument("--latest-freq",    type=int,  default=1)
    parser.add_argument("--host",           type=str,  default=DETrainingConfig.HOST)
    parser.add_argument("--port",           type=int,  default=DETrainingConfig.PORT)
    parser.add_argument("--checkpoint",     type=str,  default=None,
                        help="Checkpoint path for eval mode")
    parser.add_argument("--resume",              type=str,
                        default=os.environ.get("RESUME_CHECKPOINT") or None,
                        help="RLlib checkpoint to resume from")
    parser.add_argument("--scripted-ai-tier",    type=int, default=0,
                        help="Initial ScriptedAI difficulty tier (0=Passive … 3=Aggressive)")
    parser.add_argument("--curriculum-window",   type=int, default=5,
                        help="Iterations of sustained reward required for tier promotion")
    args = parser.parse_args()

    if args.mode == "validate":
        ok = run_validation()
        sys.exit(0 if ok else 1)

    elif args.mode == "eval":
        if not args.checkpoint:
            print("Error: --checkpoint required for eval mode")
            sys.exit(1)
        evaluate_checkpoint(args.checkpoint)

    elif args.mode == "rllib":
        if not RLLIB_AVAILABLE:
            print("Error: RLlib not available. Install with: pip install ray[rllib]")
            sys.exit(1)
        DETrainingConfig.HOST = args.host
        DETrainingConfig.PORT = args.port
        train_with_rllib(args)
