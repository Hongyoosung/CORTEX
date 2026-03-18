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
    EntityCentricPolicy, PPOTrainer, ReplayBuffer, Transition,
    collate_fn, OBS_DIM, EQS_DIM, EQS_LABELS,
)

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
    "UndefendedBasePenalty":  -1.0,   # -1.0/step: friendly base with no nearby ally (shared)
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
    NUM_SGD_ITER     = 6
    GAMMA            = 0.99
    GAE_LAMBDA       = 0.95
    CLIP_PARAM       = 0.2
    ENTROPY_COEFF    = 0.01
    VF_LOSS_COEFF    = 0.5
    GRAD_CLIP        = 0.5
    VF_CLIP_PARAM = float('inf')

    LR_SCHEDULE = [
        [0, 3e-4],
        [2_000_000, 1e-4],
        [4_000_000, 5e-5],
    ]
    ENTROPY_SCHEDULE = [
        [0,          0.01],
        [300_000,    0.005],
        [800_000,    0.003],
        [1_500_000,  0.001],
        [2_500_000,  0.0005],
    ]

    OUTPUT_DIR = "/app/training_results"


# ── RLlib helpers ─────────────────────────────────────────────────────────────

STRATEGY_POLICY_NAMES = {0: "assault_policy", 1: "defend_policy", 2: "support_policy"}

if RLLIB_AVAILABLE:
    from ray.rllib.models.torch.torch_modelv2 import TorchModelV2
    from ray.rllib.utils.annotations import override

    class EntityCentricRLlibModel(TorchModelV2, nn.Module):
        """
        RLlib TorchModelV2 wrapper for EntityCentricPolicy.

        Three separate instances are registered — one per role (assault / defend / support).
        Each only receives observations for its assigned role.
        forward() returns (B, 14) = [means(7), log_stds(7)].
        """

        def __init__(self, obs_space, action_space, num_outputs, model_config, name):
            TorchModelV2.__init__(self, obs_space, action_space, num_outputs, model_config, name)
            nn.Module.__init__(self)
            cfg = model_config.get("custom_model_config", {})
            hidden = cfg.get("hidden", 64)
            heads  = cfg.get("heads", 4)
            self.policy = EntityCentricPolicy(hidden=hidden, heads=heads)
            self._last_features = None

        @override(TorchModelV2)
        def forward(self, input_dict, state, seq_lens):
            obs = input_dict["obs"].float()            # (B, 170)
            self._last_features = obs
            means = self.policy(obs)                   # (B, 7)
            log_stds = torch.clamp(
                self.policy.log_std,
                EntityCentricPolicy.LOG_STD_MIN,
                EntityCentricPolicy.LOG_STD_MAX,
            ).unsqueeze(0).expand_as(means)            # (B, 7)
            return torch.cat([means, log_stds], dim=-1), state   # (B, 14)

        @override(TorchModelV2)
        def value_function(self):
            return self.policy.get_value(self._last_features)


    def create_ppo_config():
        """Build RLlib PPO config — three separate per-role EntityCentricPolicy instances."""
        from ray.rllib.algorithms.ppo import PPOConfig
        from ray.rllib.policy.policy import PolicySpec

        try:
            from env_wrapper import AGENT_STRATEGY_REGISTRY
        except ImportError:
            from training.env_wrapper import AGENT_STRATEGY_REGISTRY

        def _policy_mapping_fn(agent_id, episode, worker, **kwargs):
            strategy_idx = AGENT_STRATEGY_REGISTRY.get(agent_id, 0)
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

    best_reward    = float("-inf")
    cumul_episodes = 0
    cumul_steps    = 0

    print(f"{'Iter':<6} {'Reward':>10} {'EpLen':>8} {'Steps':>12} {'Time':>8}")
    print("-" * 50)

    for i in range(args.iterations):
        t0     = time.time()
        result = algo.train()
        dt     = time.time() - t0

        env_r  = result.get("env_runners", {})
        
        # 1. 기존 지표 추출
        reward = env_r.get("episode_reward_mean") or 0.0
        ep_len = env_r.get("episode_len_mean")    or 0.0
        eps    = env_r.get("episodes_this_iter",  0)
        
        # [수정됨] RLlib은 이미 누적된 스텝 수를 반환하므로, 기존 누적 변수에 더하지 않고 덮어씁니다.
        cumul_steps = result.get("num_agent_steps_sampled", 0) 
        
        # 이번 이터레이션에서 샘플링된 스텝 수 (초당 처리량 계산용)
        steps_this_iter = result.get("num_agent_steps_sampled_this_iter", 0)
        
        # 2. 신규 지표 추가 추출 (최대/최소 보상)
        reward_max = env_r.get("episode_reward_max")
        reward_min = env_r.get("episode_reward_min")

        reward = 0.0 if reward is None or (isinstance(reward, float) and np.isnan(reward)) else reward
        ep_len = 0.0 if ep_len is None or (isinstance(ep_len, float) and np.isnan(ep_len)) else ep_len

        cumul_episodes += eps

        # ---------------------------------------------------------------------
        # 텐서보드 기록
        # ---------------------------------------------------------------------
        
        # 에피소드 보상 (평균, 최대, 최소)
        tb.add_scalar("reward/episode_reward_mean", reward, cumul_steps)
        
        if reward_max is not None and not np.isnan(float(reward_max)):
            tb.add_scalar("reward/episode_reward_max", float(reward_max), cumul_steps)
        if reward_min is not None and not np.isnan(float(reward_min)):
            tb.add_scalar("reward/episode_reward_min", float(reward_min), cumul_steps)

        # 에피소드 길이 및 횟수
        tb.add_scalar("env/episode_len_mean", ep_len, cumul_steps)
        tb.add_scalar("env/episodes_completed", cumul_episodes, cumul_steps)
        
        # 시스템 퍼포먼스 (초당 스텝 처리량)
        # 누적 스텝(cumul_steps)이 아닌 이번 이터레이션 스텝(steps_this_iter)으로 계산해야 정확합니다.
        steps_per_sec = steps_this_iter / max(dt, 1e-6)
        tb.add_scalar("performance/steps_per_sec", steps_per_sec, cumul_steps)

        # Custom env metrics
        custom = env_r.get("custom_metrics", {})
        for strat in ("assault", "defend", "support"):
            key_mean = f"reward_strategy_{strat}_mean"
            key_sum  = f"reward_strategy_{strat}_sum"
            if key_mean in custom:
                tb.add_scalar(f"reward/strategy_{strat}_mean", float(custom[key_mean]), cumul_steps)
            if key_sum in custom:
                tb.add_scalar(f"reward/strategy_{strat}_sum",  float(custom[key_sum]),  cumul_steps)

        REWARD_COMPONENTS = [
            "BaseOccupationReward", "CoOccupationPenalty",
            "BaseCaptureCreditReward", "UndefendedBasePenalty",
            "AssignedBaseReachReward",
        ]
        for comp in REWARD_COMPONENTS:
            ckey = f"reward_component_{comp}_mean"
            if ckey in custom:
                tb.add_scalar(f"reward/component_{comp}", float(custom[ckey]), cumul_steps)

        # Learner metrics — logged separately per role policy
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
            role = policy_name.replace("_policy", "")
            raw = learner_info.get(policy_name, {})
            stats = raw.get("learner_stats", raw)
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

        tb.flush()

        print(f"{i+1:>3}/{args.iterations:<3}  {reward:>10.2f}  "
              f"{ep_len:>8.1f}  {cumul_steps:>12}  {dt:>7.1f}s")

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
        role_name = policy_name.replace("_policy", "")  # "assault" / "defend" / "support"
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

    # -- Test 6: obs dimension = 170 --
    print("[Test 6] OBS_DIM == 170")
    check("OBS_DIM == 170", OBS_DIM == 170, f"got {OBS_DIM}")
    expected = 7 + 8 * 5 + 8 * 5 + 8 * 7 + 8 + 8 + 8 + 3
    check("Layout arithmetic == 170", expected == 170, f"got {expected}")

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
    check("UndefendedBasePenalty == -1.0",  REWARD_CONFIG["UndefendedBasePenalty"]   == -1.0)
    check("AssignedBaseReachReward == 1.0", REWARD_CONFIG["AssignedBaseReachReward"] == 1.0)

    # -- Test 10: collate_fn --
    print("[Test 10] collate_fn")
    obs_list = [np.random.randn(OBS_DIM).astype(np.float32) for _ in range(4)]
    batch    = collate_fn(obs_list)
    check("collate_fn shape (4, 170)", batch.shape == (4, OBS_DIM), f"got {batch.shape}")

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
    parser.add_argument("--resume",         type=str,
                        default=os.environ.get("RESUME_CHECKPOINT") or None,
                        help="RLlib checkpoint to resume from")
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
