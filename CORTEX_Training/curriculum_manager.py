"""
Curriculum Manager for SBDAPM Training

Automatically switches levels based on convergence criteria.
Monitors TensorBoard logs to detect when to advance curriculum.

Usage:
    python curriculum_manager.py --tensorboard-dir training_results
"""

import argparse
import time
import json
from pathlib import Path
from tensorboard.backend.event_processing import event_accumulator

class CurriculumManager:
    """Manages curriculum progression based on training metrics."""

    CURRICULUM = [
        {
            "name": "T1_BasicCombat_2v2",
            "map": "Training_BasicCombat_2v2_v01",
            "min_episodes": 100,
            "success_criteria": {
                "win_rate_min": 0.48,
                "win_rate_max": 0.52,  # Balanced self-play
            }
        },
        {
            "name": "T2_CoverUsage_2v2",
            "map": "Training_CoverUsage_2v2_v01",
            "min_episodes": 150,
            "success_criteria": {
                "cover_usage_rate": 0.60,  # 60% time in cover
                "damage_reduction": 0.30,  # 30% less damage vs T1
            }
        },
        {
            "name": "T3_Positioning_3v3",
            "map": "Training_Positioning_3v3_v01",
            "min_episodes": 200,
            "success_criteria": {
                "formation_coherence": 0.60,
                "coordination_rate": 0.10,
            }
        },
        # Add T4-T10 following LevelDesignTemplate.md specs
    ]

    def __init__(self, tensorboard_dir: str):
        self.tensorboard_dir = Path(tensorboard_dir)
        self.current_level_idx = 0
        self.episode_count = 0

    def check_progression(self):
        """Check if current level meets advancement criteria."""
        current_level = self.CURRICULUM[self.current_level_idx]

        # Load latest metrics from TensorBoard
        metrics = self._load_latest_metrics()

        # Check minimum episodes
        if self.episode_count < current_level["min_episodes"]:
            print(f"[Curriculum] {current_level['name']}: {self.episode_count}/{current_level['min_episodes']} episodes")
            return False

        # Check success criteria
        criteria = current_level["success_criteria"]
        for metric_name, threshold in criteria.items():
            if metric_name not in metrics:
                print(f"[Curriculum] Waiting for metric: {metric_name}")
                return False

            if metrics[metric_name] < threshold:
                print(f"[Curriculum] {metric_name} = {metrics[metric_name]:.3f} < {threshold:.3f}")
                return False

        print(f"[Curriculum] ✅ {current_level['name']} COMPLETE! Advancing to next level...")
        return True

    def advance_level(self):
        """Advance to next curriculum level."""
        if self.current_level_idx >= len(self.CURRICULUM) - 1:
            print("[Curriculum] 🎉 All curriculum levels completed!")
            return None

        self.current_level_idx += 1
        next_level = self.CURRICULUM[self.current_level_idx]
        self.episode_count = 0

        print(f"[Curriculum] Loading: {next_level['map']}")

        # Send level switch command to UE5 via Schola (requires custom RPC)
        # Alternatively: Write to file that UE5 Blueprint polls
        command_file = Path("curriculum_command.json")
        command_file.write_text(json.dumps({
            "command": "load_level",
            "map_name": next_level["map"],
            "timestamp": time.time()
        }))

        return next_level["map"]

    def _load_latest_metrics(self):
        """Load latest metrics from TensorBoard logs."""
        # Find most recent event file
        event_files = sorted(self.tensorboard_dir.glob("**/events.out.tfevents.*"))
        if not event_files:
            return {}

        ea = event_accumulator.EventAccumulator(str(event_files[-1]))
        ea.Reload()

        metrics = {}

        # Extract relevant scalars
        if "episode_reward_mean" in ea.Tags()["scalars"]:
            metrics["reward"] = ea.Scalars("episode_reward_mean")[-1].value

        # Add custom metrics (from Schola logging)
        # e.g., win_rate, cover_usage, formation_coherence

        self.episode_count = len(ea.Scalars("episode_reward_mean")) if "episode_reward_mean" in ea.Tags()["scalars"] else 0

        return metrics

    def run(self, check_interval=60):
        """Run curriculum manager loop."""
        print(f"[Curriculum] Starting at level: {self.CURRICULUM[0]['name']}")

        while True:
            time.sleep(check_interval)

            if self.check_progression():
                next_map = self.advance_level()
                if next_map is None:
                    break

                # Wait for level to load
                print(f"[Curriculum] Waiting 30s for level to load...")
                time.sleep(30)


def main():
    parser = argparse.ArgumentParser(description="Curriculum Manager for SBDAPM")
    parser.add_argument("--tensorboard-dir", type=str, default="training_results",
                        help="TensorBoard log directory")
    parser.add_argument("--check-interval", type=int, default=60,
                        help="Check progression every N seconds")

    args = parser.parse_args()

    manager = CurriculumManager(args.tensorboard_dir)
    manager.run(args.check_interval)


if __name__ == "__main__":
    main()
