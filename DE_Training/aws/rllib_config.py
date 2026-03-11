"""
DE v10.2 — RLlib Configuration with W&B Monitoring
=====================================================
Drop this into your train_rllib.py / AlgorithmConfig builder.

Matches the v10.2 Commander-Executor architecture:
  - 3 independent policies: assault_policy, defend_policy, support_policy
  - Obs: 54-dim  |  Action: Box([-1,1]^6)  |  Reward: composite win-rate

W&B metrics logged:
  - episode_reward_mean / min / max
  - win_rate (custom)
  - policy_reward_mean per strategy
  - training iteration wall-time
"""

from __future__ import annotations

import os
import json
import time
from pathlib import Path
from typing import Any, Dict, Optional

import numpy as np

import ray
from ray import tune
from ray.rllib.algorithms.ppo import PPOConfig
from ray.rllib.utils.annotations import override
from ray.rllib.algorithms.callbacks import DefaultCallbacks
from ray.rllib.env import BaseEnv
from ray.rllib.evaluation import Episode, RolloutWorker
from ray.rllib.policy import Policy
from ray.tune.logger import DEFAULT_LOGGERS
from ray.air.integrations.wandb import WandbLoggerCallback

###############################################################################
# S3 Metrics Reporter — writes latest.json so launch_training.py can poll it
###############################################################################


class S3MetricsReporter:
    """Writes a small JSON metrics blob to S3 after every training iteration."""

    def __init__(self, s3_bucket: str, key: str = "metrics/latest.json"):
        self.s3_bucket = s3_bucket
        self.key = key
        try:
            import boto3
            self._s3 = boto3.client("s3")
            self._enabled = True
        except ImportError:
            print("[S3MetricsReporter] boto3 not installed — S3 reporting disabled")
            self._enabled = False

    def report(self, result: dict, reward_threshold: float, max_steps: int) -> None:
        if not self._enabled:
            return

        win_rate      = result.get("custom_metrics", {}).get("win_rate_mean", 0.0)
        total_steps   = result.get("num_env_steps_sampled_lifetime", 0)
        training_done = (
            win_rate >= reward_threshold or total_steps >= max_steps
        )

        payload = {
            "iteration":       result.get("training_iteration", 0),
            "total_steps":     total_steps,
            "win_rate":        win_rate,
            "episode_reward":  result.get("episode_reward_mean", 0.0),
            "training_complete": training_done,
            "timestamp":       time.time(),
        }

        try:
            self._s3.put_object(
                Bucket=self.s3_bucket,
                Key=self.key,
                Body=json.dumps(payload),
                ContentType="application/json",
            )
        except Exception as exc:  # noqa: BLE001
            print(f"[S3MetricsReporter] Warning: {exc}")


###############################################################################
# Custom Callbacks — win-rate tracking + checkpoint sync to S3
###############################################################################


class DETrainingCallbacks(DefaultCallbacks):
    """
    Tracks per-episode win/loss, computes rolling win-rate, and syncs
    checkpoints to S3 when a new best reward is achieved.
    """

    def on_episode_end(
        self,
        *,
        worker: RolloutWorker,
        base_env: BaseEnv,
        policies: Dict[str, Policy],
        episode: Episode,
        **kwargs: Any,
    ) -> None:
        # UE5 env sets 'winner' in info: 1.0 = win, 0.0 = loss / draw
        win = episode.last_info_for().get("winner", 0.0)
        episode.custom_metrics["win"] = float(win)

    def on_train_result(
        self,
        *,
        algorithm,
        result: dict,
        **kwargs: Any,
    ) -> None:
        # Aggregate win rate from custom_metrics collected across workers
        wins = result.get("custom_metrics", {}).get("win_mean", 0.0)
        result["custom_metrics"]["win_rate"] = wins  # expose as top-level custom metric

        # Sync checkpoint to S3 if a new best is detected
        s3_bucket = os.environ.get("S3_BUCKET", "")
        if s3_bucket and result.get("is_new_best", False):
            checkpoint = algorithm.save()
            _sync_to_s3(checkpoint, s3_bucket, prefix="checkpoints/best/")


def _sync_to_s3(local_path: str, bucket: str, prefix: str = "checkpoints/") -> None:
    import subprocess
    dest = f"s3://{bucket}/{prefix}"
    subprocess.run(
        ["aws", "s3", "sync", local_path, dest, "--exclude", "*.pyc"],
        check=False,
    )
    print(f"[S3Sync] {local_path} → {dest}")


###############################################################################
# Policy / Obs / Action Specs (must match policy_training.py)
###############################################################################

OBS_DIM    = 54   # 51 agent obs + 3 strategy one-hot
ACTION_DIM = 6    # 6-dim EQS weights

import gymnasium as gym

obs_space    = gym.spaces.Box(low=-1.0, high=1.0, shape=(OBS_DIM,),    dtype=np.float32)
action_space = gym.spaces.Box(low=-1.0, high=1.0, shape=(ACTION_DIM,), dtype=np.float32)

POLICY_MAPPING = {
    "assault": "assault_policy",
    "defend":  "defend_policy",
    "support": "support_policy",
}


def policy_mapping_fn(agent_id: str, episode, worker, **kwargs) -> str:
    """Map agent_id (e.g. 'assault_0') → policy name."""
    for prefix, policy in POLICY_MAPPING.items():
        if agent_id.startswith(prefix):
            return policy
    return "assault_policy"  # fallback


###############################################################################
# Build RLlib PPO Config
###############################################################################


def build_config(
    s3_bucket: str             = "",
    reward_threshold: float    = 0.40,
    max_steps: int             = 100_000,
    wandb_project: str         = "de-v10-2",
    wandb_api_key: str         = "",
    num_rollout_workers: int   = 4,
    train_batch_size: int      = 4096,
    sgd_minibatch_size: int    = 512,
    num_sgd_iter: int          = 10,
    lr: float                  = 3e-4,
    gamma: float               = 0.99,
    lambda_: float             = 0.95,
    clip_param: float          = 0.2,
    entropy_coeff: float       = 0.01,
    vf_loss_coeff: float       = 0.5,
) -> PPOConfig:

    reporter = S3MetricsReporter(s3_bucket) if s3_bucket else None

    config = (
        PPOConfig()
        .environment(
            env="DECombatEnv-v0",   # registered in train_rllib.py
            env_config={
                "host":            os.environ.get("HOST", "localhost"),
                "port":            int(os.environ.get("PORT", 50051)),
                "reward_threshold": reward_threshold,
                "max_steps":       max_steps,
                "s3_bucket":       s3_bucket,
            },
        )
        .framework("torch")
        .rollouts(
            num_rollout_workers    = num_rollout_workers,
            num_envs_per_worker   = 1,
            rollout_fragment_length = "auto",
        )
        .training(
            train_batch_size  = train_batch_size,
            sgd_minibatch_size = sgd_minibatch_size,
            num_sgd_iter      = num_sgd_iter,
            lr                = lr,
            gamma             = gamma,
            lambda_           = lambda_,
            clip_param        = clip_param,
            entropy_coeff     = entropy_coeff,
            vf_loss_coeff     = vf_loss_coeff,
            # Network: 2-layer 256-unit MLP (matches v10.2 spec)
            model={
                "fcnet_hiddens":     [256, 256],
                "fcnet_activation":  "relu",
                "vf_share_layers":   False,
            },
        )
        .multi_agent(
            policies={
                "assault_policy": (None, obs_space, action_space, {}),
                "defend_policy":  (None, obs_space, action_space, {}),
                "support_policy": (None, obs_space, action_space, {}),
            },
            policy_mapping_fn=policy_mapping_fn,
            policies_to_train=["assault_policy", "defend_policy", "support_policy"],
        )
        .callbacks(DETrainingCallbacks)
        .resources(
            num_gpus            = int(os.environ.get("NUM_GPUS", 0)),
            num_cpus_per_worker = 1,
        )
        .debugging(log_level="WARN")
    )

    return config


###############################################################################
# W&B Logger Callback (passed to tune.run / algo.train loop)
###############################################################################


def build_wandb_callback(
    wandb_project: str = "de-v10-2",
    wandb_api_key: str = "",
) -> WandbLoggerCallback:
    """
    WandbLoggerCallback integrates with Ray Tune to log all RLlib metrics.

    Logged metrics include (but are not limited to):
      - episode_reward_mean / min / max
      - custom_metrics/win_rate
      - custom_metrics/win_mean
      - policy_reward_mean/{assault,defend,support}_policy
      - num_env_steps_sampled_lifetime
      - training_iteration
      - time_total_s

    Usage (Tune):
        tune.run(
            "PPO",
            config=build_config(...).to_dict(),
            callbacks=[build_wandb_callback()],
            ...
        )

    Usage (manual loop):
        import wandb
        wandb.init(project="de-v10-2")
        for result in algo.train():
            wandb.log(result)
    """
    api_key = wandb_api_key or os.environ.get("WANDB_API_KEY", "")

    return WandbLoggerCallback(
        project      = wandb_project,
        api_key      = api_key,
        log_config   = True,   # Log full RLlib config to W&B
        # Only upload these keys to keep the W&B run clean
        excludes=[
            "config",          # Already captured via log_config
            "hist_stats",      # Large histogram data — skip
        ],
    )


###############################################################################
# Tune stop criteria — mirrors launch_training.py termination logic
###############################################################################


def build_stop_criteria(
    reward_threshold: float = 0.40,
    max_steps: int          = 100_000,
) -> dict:
    return {
        "custom_metrics/win_rate_mean": reward_threshold,
        "num_env_steps_sampled_lifetime": max_steps,
    }


###############################################################################
# Convenience: full Tune run
###############################################################################


def run_tune(
    s3_bucket:        str   = "",
    reward_threshold: float = 0.40,
    max_steps:        int   = 100_000,
    wandb_project:    str   = "de-v10-2",
    wandb_api_key:    str   = "",
    num_workers:      int   = 4,
    local_dir:        str   = "./training_results_",
    restore:          Optional[str] = None,
) -> tune.ExperimentAnalysis:
    """Single-call entry point used by launch_training.py."""

    ray.init(ignore_reinit_error=True, address="auto")

    config   = build_config(
        s3_bucket        = s3_bucket,
        reward_threshold = reward_threshold,
        max_steps        = max_steps,
        wandb_project    = wandb_project,
        wandb_api_key    = wandb_api_key,
        num_rollout_workers = num_workers,
    )

    analysis = tune.run(
        "PPO",
        name             = "de",
        config           = config.to_dict(),
        stop             = build_stop_criteria(reward_threshold, max_steps),
        checkpoint_freq  = 10,
        checkpoint_at_end= True,
        keep_checkpoints_num = 3,
        local_dir        = local_dir,
        restore          = restore,
        callbacks        = [build_wandb_callback(wandb_project, wandb_api_key)],
        verbose          = 1,
    )

    return analysis


###############################################################################
# CLI entry point
###############################################################################

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="DE RLlib training with W&B")
    parser.add_argument("--s3-bucket",        default=os.environ.get("S3_BUCKET", ""))
    parser.add_argument("--reward-threshold", type=float, default=0.40)
    parser.add_argument("--max-steps",        type=int,   default=100_000)
    parser.add_argument("--wandb-project",    default="de-v10-2")
    parser.add_argument("--wandb-api-key",    default=os.environ.get("WANDB_API_KEY", ""))
    parser.add_argument("--num-workers",      type=int, default=4)
    parser.add_argument("--local-dir",        default="./training_results_")
    parser.add_argument("--restore",          default=None)
    args = parser.parse_args()

    analysis = run_tune(
        s3_bucket        = args.s3_bucket,
        reward_threshold = args.reward_threshold,
        max_steps        = args.max_steps,
        wandb_project    = args.wandb_project,
        wandb_api_key    = args.wandb_api_key,
        num_workers      = args.num_workers,
        local_dir        = args.local_dir,
        restore          = args.restore,
    )

    best = analysis.get_best_trial("custom_metrics/win_rate_mean", "max")
    print(f"\nBest trial: {best.trial_id}")
    print(f"  Win rate:  {best.last_result['custom_metrics']['win_rate_mean']:.3f}")
    print(f"  Steps:     {best.last_result['num_env_steps_sampled_lifetime']:,}")
    print(f"  Checkpoint:{best.checkpoint}")
