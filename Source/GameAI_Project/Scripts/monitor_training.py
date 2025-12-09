"""
Real-time training monitor for SBDAPM

Displays live metrics from TensorBoard event files during training.

Usage:
    python monitor_training.py --logdir training_results/[timestamp]
"""

import argparse
import time
import os
from pathlib import Path


def find_latest_training_dir(base_dir="training_results"):
    """Find most recent training directory."""
    if not os.path.exists(base_dir):
        return None

    subdirs = [d for d in Path(base_dir).iterdir() if d.is_dir()]
    if not subdirs:
        return None

    return max(subdirs, key=lambda d: d.stat().st_mtime)


def monitor_training(logdir):
    """Monitor training progress by parsing TensorBoard events."""
    try:
        from tensorboard.backend.event_processing import event_accumulator

        print(f"Monitoring training at: {logdir}")
        print("=" * 60)
        print(f"{'Iter':<6} {'Reward':>10} {'Reward Δ':>10} {'Ep Len':>10} {'Policy Loss':>12}")
        print("=" * 60)

        ea = event_accumulator.EventAccumulator(str(logdir))
        ea.Reload()

        prev_reward = None
        iteration = 0

        while True:
            ea.Reload()

            # Get latest metrics
            try:
                reward_events = ea.Scalars('episode_reward_mean')
                len_events = ea.Scalars('episode_len_mean')
                policy_loss_events = ea.Scalars('policy_loss')

                if reward_events:
                    latest_reward = reward_events[-1].value
                    latest_len = len_events[-1].value if len_events else 0
                    latest_policy_loss = policy_loss_events[-1].value if policy_loss_events else 0

                    reward_delta = latest_reward - prev_reward if prev_reward is not None else 0

                    print(f"{iteration:<6} {latest_reward:>10.2f} {reward_delta:>+10.2f} "
                          f"{latest_len:>10.1f} {latest_policy_loss:>12.4f}")

                    prev_reward = latest_reward
                    iteration += 1

            except KeyError:
                pass  # Metric not available yet

            time.sleep(5)  # Check every 5 seconds

    except ImportError:
        print("Error: tensorboard not installed. Run: pip install tensorboard")
        print("\nFalling back to simple log monitoring...")

        # Simple fallback: watch console output
        print("\nWatching for training updates...")
        print("(Start train_rllib.py in another terminal)")

    except KeyboardInterrupt:
        print("\nMonitoring stopped.")


def main():
    parser = argparse.ArgumentParser(description="Monitor SBDAPM training progress")
    parser.add_argument("--logdir", type=str, default=None,
                        help="Training log directory (auto-detects latest if not specified)")

    args = parser.parse_args()

    logdir = args.logdir
    if not logdir:
        logdir = find_latest_training_dir()
        if not logdir:
            print("Error: No training directories found in training_results/")
            print("Start training first with: python train_rllib.py")
            return

    monitor_training(logdir)


if __name__ == "__main__":
    main()
