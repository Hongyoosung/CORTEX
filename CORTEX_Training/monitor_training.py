"""
Training Progress Monitor for CORTEX RL Training

Usage:
    python monitor_training.py [training_dir]

    If training_dir not provided, monitors the most recent training run.

Features:
    - Real-time metric tracking
    - Health checks for learning progress
    - Recommendations based on observed metrics
"""

import os
import sys
import json
import time
from pathlib import Path
from datetime import datetime


def find_latest_training_dir():
    """Find the most recent training directory."""
    results_dir = Path("training_results")
    if not results_dir.exists():
        return None

    subdirs = [d for d in results_dir.iterdir() if d.is_dir()]
    if not subdirs:
        return None

    # Sort by modification time
    latest = max(subdirs, key=lambda d: d.stat().st_mtime)
    return latest


def parse_result_json(json_path):
    """Parse RLlib result.json for metrics."""
    metrics = []

    if not json_path.exists():
        return metrics

    with open(json_path, 'r') as f:
        for line in f:
            try:
                result = json.loads(line.strip())

                # Extract key metrics
                iteration = result.get("training_iteration", 0)

                env_runners = result.get("env_runners", {})
                episode_reward_mean = env_runners.get("episode_reward_mean", 0)
                episode_len_mean = env_runners.get("episode_len_mean", 0)

                # Extract learner stats
                info = result.get("info", {})
                learner = info.get("learner", {})
                # Try shared_policy first (multi-agent), fallback to default_policy
                policy_stats = learner.get("shared_policy", {}) or learner.get("default_policy", {})
                learner_stats = policy_stats.get("learner_stats", {})

                vf_explained_var = learner_stats.get("vf_explained_var", 0.0)
                entropy = learner_stats.get("entropy", 0.0)

                metrics.append({
                    "iteration": iteration,
                    "reward": episode_reward_mean,
                    "length": episode_len_mean,
                    "vf_var": vf_explained_var,
                    "entropy": entropy
                })
            except json.JSONDecodeError:
                continue

    return metrics


def analyze_training_health(metrics):
    """Analyze training progress and provide health assessment."""
    if not metrics:
        return "NO DATA", []

    latest = metrics[-1]
    iteration = latest["iteration"]

    issues = []
    recommendations = []

    # Check 1: Value function learning
    if iteration > 30:
        if latest["vf_var"] < 0.1:
            issues.append("❌ Value function not learning (vf_var < 0.1)")
            recommendations.append("→ Increase VF_LOSS_COEFF to 2.0 or higher")
        elif latest["vf_var"] < 0.3:
            issues.append("⚠️  Value function learning slowly (vf_var < 0.3)")
            recommendations.append("→ Consider increasing VF_LOSS_COEFF to 2.0")
        else:
            issues.append(f"✅ Value function learning well (vf_var = {latest['vf_var']:.3f})")

    # Check 2: Policy convergence
    if iteration > 50:
        if latest["entropy"] > 1.2:
            issues.append("❌ Policy not converging (entropy > 1.2)")
            recommendations.append("→ Check entropy coefficient decay schedule")
        elif latest["entropy"] > 0.8:
            issues.append("⚠️  Policy converging slowly (entropy > 0.8)")
        else:
            issues.append(f"✅ Policy converging (entropy = {latest['entropy']:.3f})")

    # Check 3: Reward improvement
    if len(metrics) > 10:
        early_reward = sum(m["reward"] for m in metrics[:10]) / 10
        recent_reward = sum(m["reward"] for m in metrics[-10:]) / 10
        improvement = recent_reward - early_reward

        if improvement < 100:
            issues.append(f"❌ Minimal reward improvement (+{improvement:.1f})")
            recommendations.append("→ Check reward shaping weights")
            recommendations.append("→ Verify UE5 reward structure")
        elif improvement < 500:
            issues.append(f"⚠️  Slow reward improvement (+{improvement:.1f})")
        else:
            issues.append(f"✅ Good reward improvement (+{improvement:.1f})")

    # Check 4: Episode length
    if latest["length"] > 1800:
        issues.append("⚠️  Episodes timing out (length > 1800)")
        recommendations.append("→ Consider reducing MAX_EPISODE_STEPS for early training")
    elif latest["length"] < 1500:
        issues.append(f"✅ Episodes completing faster (length = {latest['length']:.0f})")

    # Overall health
    critical_issues = [i for i in issues if i.startswith("❌")]
    if critical_issues:
        health = "CRITICAL"
    elif len([i for i in issues if i.startswith("⚠️")]) > 2:
        health = "NEEDS ATTENTION"
    else:
        health = "HEALTHY"

    return health, issues, recommendations


def print_metrics_table(metrics, last_n=10):
    """Print formatted table of recent metrics."""
    if not metrics:
        print("No metrics available yet.")
        return

    recent = metrics[-last_n:]

    print("\n" + "="*90)
    print(f"{'Iter':<6} {'Reward':>10} {'Length':>8} {'VF Var':>10} {'Entropy':>10}")
    print("="*90)

    for m in recent:
        print(f"{m['iteration']:<6} {m['reward']:>10.2f} {m['length']:>8.1f} "
              f"{m['vf_var']:>10.4f} {m['entropy']:>10.3f}")

    print("="*90)


def monitor_training(training_dir, refresh_interval=30):
    """Monitor training progress in real-time."""
    training_path = Path(training_dir)
    result_json = training_path / "result.json"

    print(f"Monitoring training: {training_path}")
    print(f"Refresh interval: {refresh_interval}s")
    print("\nPress Ctrl+C to stop monitoring\n")

    last_iteration = 0

    try:
        while True:
            # Parse latest metrics
            metrics = parse_result_json(result_json)

            if not metrics:
                print(f"[{datetime.now().strftime('%H:%M:%S')}] Waiting for training to start...")
                time.sleep(refresh_interval)
                continue

            latest = metrics[-1]

            # Only update if new iteration
            if latest["iteration"] > last_iteration:
                last_iteration = latest["iteration"]

                # Clear screen (Windows compatible)
                os.system('cls' if os.name == 'nt' else 'clear')

                print("="*90)
                print(f"CORTEX Training Monitor - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
                print(f"Training Directory: {training_path}")
                print("="*90)

                # Show recent metrics
                print_metrics_table(metrics, last_n=10)

                # Health check
                health, issues, recommendations = analyze_training_health(metrics)

                print(f"\n🏥 Training Health: {health}")
                print("-"*90)

                for issue in issues:
                    print(f"  {issue}")

                if recommendations:
                    print("\n💡 Recommendations:")
                    for rec in recommendations:
                        print(f"  {rec}")

                print("\n" + "="*90)
                print(f"Total iterations: {latest['iteration']} | "
                      f"Last update: {datetime.now().strftime('%H:%M:%S')} | "
                      f"Refreshing every {refresh_interval}s")
                print("="*90)

            time.sleep(refresh_interval)

    except KeyboardInterrupt:
        print("\n\nMonitoring stopped.")

        if metrics:
            print("\nFinal Summary:")
            print_metrics_table(metrics, last_n=20)

            health, issues, recommendations = analyze_training_health(metrics)
            print(f"\nFinal Health: {health}")


def main():
    if len(sys.argv) > 1:
        training_dir = sys.argv[1]
    else:
        training_dir = find_latest_training_dir()
        if not training_dir:
            print("Error: No training results found in training_results/")
            print("\nUsage: python monitor_training.py [training_dir]")
            sys.exit(1)

        print(f"Auto-detected latest training: {training_dir}")

    # Check if directory exists
    if not Path(training_dir).exists():
        print(f"Error: Directory not found: {training_dir}")
        sys.exit(1)

    # Start monitoring
    monitor_training(training_dir, refresh_interval=30)


if __name__ == "__main__":
    main()
