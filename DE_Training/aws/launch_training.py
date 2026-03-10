"""
DE v10.2 — AWS Training Automation Script
==========================================
Launches a Ray cluster, submits the training job, and tears the cluster down
automatically when either:
  (a) the win-rate reward threshold is reached, OR
  (b) the maximum number of training steps is exceeded.

Usage:
  python aws/launch_training.py [options]

Requirements:
  pip install ray boto3 wandb

Environment variables:
  WANDB_API_KEY        — your W&B API key
  AWS_DEFAULT_REGION   — defaults to us-east-1
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

import boto3

###############################################################################
# Configuration
###############################################################################

# Paths (relative to repo root)
CLUSTER_YAML = "aws/cluster.yaml"
TRAIN_SCRIPT  = "train_rllib.py"

# Termination criteria
DEFAULT_REWARD_THRESHOLD = 0.40   # >40% win rate — from v10.2 target spec
DEFAULT_MAX_STEPS        = 100_000

# Polling interval (seconds) while waiting for training to finish
POLL_INTERVAL_SECONDS = 30

###############################################################################
# Helpers
###############################################################################


def run(cmd: list[str], check: bool = True, capture: bool = False) -> subprocess.CompletedProcess:
    """Run a shell command, streaming output to stdout unless capture=True."""
    print(f"\n[launch] $ {' '.join(cmd)}", flush=True)
    return subprocess.run(
        cmd,
        check=check,
        capture_output=capture,
        text=True,
    )


def cluster_up(cluster_yaml: str, yes: bool = True) -> None:
    cmd = ["ray", "up", cluster_yaml, "--no-config-cache"]
    if yes:
        cmd.append("-y")
    run(cmd)


def cluster_down(cluster_yaml: str, yes: bool = True) -> None:
    cmd = ["ray", "down", cluster_yaml]
    if yes:
        cmd.append("-y")
    run(cmd, check=False)  # Don't raise on failure — we always want to attempt teardown


def get_head_ip(cluster_yaml: str) -> str:
    result = run(["ray", "get-head-ip", cluster_yaml], capture=True)
    return result.stdout.strip()


def submit_training(
    cluster_yaml: str,
    train_script: str,
    extra_args: list[str],
    reward_threshold: float,
    max_steps: int,
    s3_bucket: str,
    wandb_project: str,
) -> subprocess.Popen:
    """
    Submit the training script via `ray submit` and return the Popen handle.
    The script is expected to write a JSON status file to S3 / local disk.
    """
    script_args = [
        "--reward-threshold", str(reward_threshold),
        "--max-steps",        str(max_steps),
        "--s3-bucket",        s3_bucket,
        "--wandb-project",    wandb_project,
    ] + extra_args

    cmd = [
        "ray", "submit", cluster_yaml, train_script,
        "--",
        *script_args,
    ]

    print(f"\n[launch] Submitting training job…\n  {' '.join(cmd)}", flush=True)
    # Use Popen so we can poll the process while monitoring S3 metrics
    return subprocess.Popen(cmd)


###############################################################################
# S3 metric polling
###############################################################################


def fetch_latest_metrics(s3_bucket: str, metrics_key: str = "metrics/latest.json") -> Optional[dict]:
    """
    Pull the latest training metrics JSON written by the training script to S3.
    Returns None if the key does not exist yet.
    """
    s3 = boto3.client("s3")
    try:
        obj = s3.get_object(Bucket=s3_bucket, Key=metrics_key)
        return json.loads(obj["Body"].read())
    except s3.exceptions.NoSuchKey:
        return None
    except Exception as exc:  # noqa: BLE001
        print(f"[launch] Warning: could not fetch metrics from S3: {exc}")
        return None


def should_terminate(
    metrics: Optional[dict],
    reward_threshold: float,
    max_steps: int,
) -> tuple[bool, str]:
    """
    Returns (terminate, reason) based on the latest metrics dict.
    """
    if metrics is None:
        return False, ""

    total_steps   = metrics.get("total_steps", 0)
    win_rate      = metrics.get("win_rate", 0.0)
    training_done = metrics.get("training_complete", False)

    if training_done:
        return True, "training script signalled completion"
    if win_rate >= reward_threshold:
        return True, f"win_rate {win_rate:.3f} ≥ threshold {reward_threshold:.3f}"
    if total_steps >= max_steps:
        return True, f"total_steps {total_steps} ≥ max_steps {max_steps}"

    return False, ""


###############################################################################
# Main
###############################################################################


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="DE AWS training launcher")

    p.add_argument("--cluster-yaml",       default=CLUSTER_YAML,  help="Ray cluster YAML")
    p.add_argument("--train-script",       default=TRAIN_SCRIPT,  help="Training entry-point script")
    p.add_argument("--reward-threshold",   type=float, default=DEFAULT_REWARD_THRESHOLD,
                   help="Win-rate at which training is considered successful")
    p.add_argument("--max-steps",          type=int,   default=DEFAULT_MAX_STEPS,
                   help="Hard cap on total environment steps")
    p.add_argument("--s3-bucket",          required=True, help="S3 bucket name for logs/checkpoints")
    p.add_argument("--wandb-project",      default="de-v10-2", help="W&B project name")
    p.add_argument("--poll-interval",      type=int, default=POLL_INTERVAL_SECONDS,
                   help="Seconds between S3 metric polls")
    p.add_argument("--skip-cluster-up",    action="store_true",
                   help="Assume cluster is already running, skip `ray up`")
    p.add_argument("--skip-cluster-down",  action="store_true",
                   help="Do NOT tear down cluster after training (useful for debugging)")
    p.add_argument("--yes", "-y",          action="store_true",
                   help="Auto-confirm all ray up / ray down prompts")
    # Any extra args are forwarded to the training script
    p.add_argument("extra", nargs=argparse.REMAINDER)

    return p.parse_args()


def main() -> int:
    args = parse_args()

    # ── 1. Cluster up ────────────────────────────────────────────────────────
    if not args.skip_cluster_up:
        print("\n[launch] === Bringing up Ray cluster ===")
        cluster_up(args.cluster_yaml, yes=args.yes)
    else:
        print("[launch] Skipping cluster-up (--skip-cluster-up set)")

    # ── 2. Submit training job ────────────────────────────────────────────────
    print("\n[launch] === Submitting training job ===")
    proc = submit_training(
        cluster_yaml     = args.cluster_yaml,
        train_script     = args.train_script,
        extra_args       = args.extra,
        reward_threshold = args.reward_threshold,
        max_steps        = args.max_steps,
        s3_bucket        = args.s3_bucket,
        wandb_project    = args.wandb_project,
    )

    # ── 3. Monitor until done ────────────────────────────────────────────────
    terminate_reason = "training process exited"
    try:
        print(f"\n[launch] Monitoring S3 metrics every {args.poll_interval}s …")
        print(f"         Threshold: win_rate ≥ {args.reward_threshold}  |  "
              f"max_steps = {args.max_steps:,}\n")

        while True:
            # Check if `ray submit` itself has finished
            retcode = proc.poll()
            if retcode is not None:
                terminate_reason = f"ray submit process exited (code={retcode})"
                print(f"\n[launch] {terminate_reason}")
                break

            # Poll S3 for latest metrics
            metrics = fetch_latest_metrics(args.s3_bucket)
            if metrics:
                steps    = metrics.get("total_steps", 0)
                win_rate = metrics.get("win_rate", 0.0)
                print(
                    f"[monitor] steps={steps:>8,}  win_rate={win_rate:.3f}  "
                    f"threshold={args.reward_threshold:.3f}",
                    flush=True,
                )

            terminate, reason = should_terminate(metrics, args.reward_threshold, args.max_steps)
            if terminate:
                terminate_reason = reason
                print(f"\n[launch] Termination condition met: {reason}")
                proc.terminate()
                try:
                    proc.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    proc.kill()
                break

            time.sleep(args.poll_interval)

    except KeyboardInterrupt:
        terminate_reason = "interrupted by user (Ctrl+C)"
        print(f"\n[launch] {terminate_reason}")
        proc.terminate()

    finally:
        # ── 4. Sync final artefacts to S3 ────────────────────────────────────
        print("\n[launch] === Syncing final artefacts to S3 ===")
        run(
            ["aws", "s3", "sync", "training_results_v10_2/",
             f"s3://{args.s3_bucket}/results/", "--exclude", "*.pyc"],
            check=False,
        )

        # ── 5. Tear down cluster ──────────────────────────────────────────────
        if not args.skip_cluster_down:
            print(f"\n[launch] === Tearing down cluster (reason: {terminate_reason}) ===")
            cluster_down(args.cluster_yaml, yes=args.yes)
        else:
            print("[launch] Skipping cluster-down (--skip-cluster-down set)")

    print(f"\n[launch] Done. Termination reason: {terminate_reason}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
