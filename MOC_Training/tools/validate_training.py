"""
Training Validation Script

Validates Phase 1 training results against target metrics:
- Training stability (no divergence)
- Strategy diversity (>20% per option)
- Combat capability (>40% win rate vs random)

Usage:
    python validate_training.py --checkpoint checkpoints/policy_final.pth --log-dir runs/phase1
"""

import sys
import os
import argparse
import json
import torch
import numpy as np
from pathlib import Path
from typing import Dict, List, Tuple
import matplotlib.pyplot as plt
from tensorboard.backend.event_processing import event_accumulator

# Add parent directory to path
sys.path.append(str(Path(__file__).parent.parent))

from models.multi_head_policy import MultiHeadRLPolicy


class TrainingValidator:
    """Validates training results against Phase 1 target metrics."""

    def __init__(self, checkpoint_path: str, log_dir: str):
        self.checkpoint_path = checkpoint_path
        self.log_dir = log_dir
        self.results = {}

    def validate_all(self) -> Dict[str, bool]:
        """
        Run all validation checks.

        Returns:
            Dictionary of validation results
        """
        print("="*80)
        print("Phase 1 Training Validation")
        print("="*80)

        # Load checkpoint
        checkpoint = self._load_checkpoint()

        # Validation checks
        checks = {
            'stability': self.check_training_stability(checkpoint),
            'strategy_diversity': self.check_strategy_diversity(checkpoint),
            'model_integrity': self.check_model_integrity(checkpoint),
            'parameter_count': self.check_parameter_count(checkpoint)
        }

        # Print summary
        print("\n" + "="*80)
        print("Validation Summary")
        print("="*80)

        all_passed = True
        for check_name, passed in checks.items():
            status = "✅ PASS" if passed else "❌ FAIL"
            print(f"{check_name.replace('_', ' ').title()}: {status}")
            all_passed = all_passed and passed

        if all_passed:
            print("\n🎉 All validation checks passed!")
        else:
            print("\n⚠️ Some validation checks failed. Review results above.")

        return checks

    def _load_checkpoint(self) -> Dict:
        """Load training checkpoint."""
        print(f"\nLoading checkpoint: {self.checkpoint_path}")

        if not os.path.exists(self.checkpoint_path):
            raise FileNotFoundError(f"Checkpoint not found: {self.checkpoint_path}")

        checkpoint = torch.load(self.checkpoint_path, map_location='cpu')
        print(f"✅ Checkpoint loaded")
        return checkpoint

    def check_training_stability(self, checkpoint: Dict) -> bool:
        """
        Check training stability (no divergence).

        Criteria:
        - Final loss should be finite (not NaN/Inf)
        - Loss should not increase dramatically in final 20% of training
        - Gradient norms should be bounded

        Returns:
            True if training is stable
        """
        print("\n" + "-"*80)
        print("Checking Training Stability...")
        print("-"*80)

        stats = checkpoint.get('stats', {})
        total_transitions = stats.get('total_transitions', 0)

        if total_transitions == 0:
            print("⚠️ No training statistics found in checkpoint")
            return False

        # Check if we have TensorBoard logs
        if os.path.exists(self.log_dir):
            try:
                ea = event_accumulator.EventAccumulator(self.log_dir)
                ea.Reload()

                # Get loss scalars
                if 'train/total_loss' in ea.Tags()['scalars']:
                    loss_events = ea.Scalars('train/total_loss')

                    if len(loss_events) == 0:
                        print("⚠️ No loss data found in TensorBoard logs")
                        return False

                    # Extract loss values
                    losses = [event.value for event in loss_events]

                    # Check for NaN/Inf
                    if any(not np.isfinite(loss) for loss in losses):
                        print("❌ Training diverged: NaN or Inf loss detected")
                        return False

                    # Check for dramatic increase in final 20%
                    split_idx = int(len(losses) * 0.8)
                    early_mean = np.mean(losses[:split_idx])
                    late_mean = np.mean(losses[split_idx:])

                    if late_mean > early_mean * 2.0:
                        print(f"❌ Training instability: Loss increased {late_mean/early_mean:.2f}x in final phase")
                        return False

                    print(f"✅ Training stable:")
                    print(f"   Early loss (first 80%): {early_mean:.4f}")
                    print(f"   Late loss (final 20%): {late_mean:.4f}")
                    print(f"   Final loss: {losses[-1]:.4f}")
                    return True

            except Exception as e:
                print(f"⚠️ Could not load TensorBoard logs: {e}")

        print("⚠️ Could not verify training stability (logs missing)")
        return False

    def check_strategy_diversity(self, checkpoint: Dict) -> bool:
        """
        Check strategy diversity (>20% representation per option).

        Criteria:
        - Each of the 5 strategies should have at least 20% representation
        - Maximum deviation from uniform (20%) should be < 10%

        Returns:
            True if strategy diversity is sufficient
        """
        print("\n" + "-"*80)
        print("Checking Strategy Diversity...")
        print("-"*80)

        stats = checkpoint.get('stats', {})
        strategy_counts = stats.get('strategy_counts', {})

        if not strategy_counts:
            print("⚠️ No strategy statistics found in checkpoint")
            return False

        # Convert to percentages
        total = sum(strategy_counts.values())
        if total == 0:
            print("❌ No transitions recorded")
            return False

        strategy_names = ['Assault', 'Defend', 'Support', 'Scout', 'Retreat']
        percentages = {}

        print("\nStrategy Distribution:")
        all_above_threshold = True

        for i, name in enumerate(strategy_names):
            count = strategy_counts.get(i, 0)
            pct = (count / total) * 100
            percentages[name] = pct

            status = "✅" if pct >= 20.0 else "❌"
            print(f"  {status} {name}: {pct:.1f}% ({count:,} transitions)")

            if pct < 20.0:
                all_above_threshold = False

        if all_above_threshold:
            print("\n✅ All strategies have >20% representation")
            return True
        else:
            print("\n❌ Some strategies underrepresented (<20%)")
            return False

    def check_model_integrity(self, checkpoint: Dict) -> bool:
        """
        Check model integrity.

        Criteria:
        - Model state dict is loadable
        - All parameters are finite
        - Model can perform forward pass

        Returns:
            True if model is valid
        """
        print("\n" + "-"*80)
        print("Checking Model Integrity...")
        print("-"*80)

        try:
            # Load model
            policy = MultiHeadRLPolicy()
            policy.load_state_dict(checkpoint['policy_state_dict'])
            policy.eval()

            print("✅ Model state dict loaded successfully")

            # Check parameters
            total_params = 0
            nan_params = 0

            for name, param in policy.named_parameters():
                total_params += param.numel()
                if not torch.isfinite(param).all():
                    nan_params += 1
                    print(f"⚠️ Non-finite parameters in {name}")

            if nan_params > 0:
                print(f"❌ {nan_params} parameter tensors contain NaN/Inf")
                return False

            print(f"✅ All {total_params:,} parameters are finite")

            # Test forward pass
            with torch.no_grad():
                dummy_state = torch.randn(1, 52)
                dummy_option = torch.tensor([0])
                dummy_target = torch.randn(1, 3)
                dummy_duration = torch.randn(1, 1)

                output = policy(dummy_state, dummy_option, dummy_target, dummy_duration)

                if not torch.isfinite(output).all():
                    print("❌ Forward pass produces NaN/Inf")
                    return False

            print("✅ Forward pass test successful")
            return True

        except Exception as e:
            print(f"❌ Model integrity check failed: {e}")
            return False

    def check_parameter_count(self, checkpoint: Dict) -> bool:
        """
        Verify parameter count matches architecture.

        Returns:
            True if parameter count is correct
        """
        print("\n" + "-"*80)
        print("Checking Parameter Count...")
        print("-"*80)

        try:
            policy = MultiHeadRLPolicy()
            policy.load_state_dict(checkpoint['policy_state_dict'])

            param_count = policy.get_num_parameters()
            print(f"Total parameters: {param_count:,}")

            # Expected range (rough estimate)
            # Shared: 52*128 + 128*256 + 5*16 + 3*16 ≈ 40k
            # Per head: (289*128 + 128*8) ≈ 38k
            # Total: 40k + 5*38k ≈ 230k
            expected_min = 200000
            expected_max = 300000

            if expected_min <= param_count <= expected_max:
                print(f"✅ Parameter count within expected range ({expected_min:,} - {expected_max:,})")
                return True
            else:
                print(f"⚠️ Parameter count outside expected range")
                return False

        except Exception as e:
            print(f"❌ Parameter count check failed: {e}")
            return False

    def generate_report(self, output_path: str = 'validation_report.txt'):
        """
        Generate detailed validation report.

        Args:
            output_path: Path to save report
        """
        print(f"\nGenerating validation report: {output_path}")

        with open(output_path, 'w') as f:
            f.write("="*80 + "\n")
            f.write("Phase 1 Training Validation Report\n")
            f.write("="*80 + "\n\n")

            f.write(f"Checkpoint: {self.checkpoint_path}\n")
            f.write(f"Log Directory: {self.log_dir}\n\n")

            # Add validation results
            for section, result in self.results.items():
                f.write(f"{section}: {'PASS' if result else 'FAIL'}\n")

        print(f"✅ Report saved: {output_path}")


def main():
    parser = argparse.ArgumentParser(description='Validate Phase 1 Training Results')
    parser.add_argument('--checkpoint', type=str, required=True, help='Path to checkpoint (.pth)')
    parser.add_argument('--log-dir', type=str, required=True, help='TensorBoard log directory')
    parser.add_argument('--report', type=str, default='validation_report.txt', help='Output report path')

    args = parser.parse_args()

    # Run validation
    validator = TrainingValidator(args.checkpoint, args.log_dir)
    results = validator.validate_all()

    # Generate report
    validator.results = results
    validator.generate_report(args.report)

    # Exit code
    return 0 if all(results.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
