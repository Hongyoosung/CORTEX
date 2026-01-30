"""
Diagnostic script to check value head initialization and VF health.

Usage:
    python diagnose_value_heads.py --checkpoint training_results/20260128_114130/checkpoint_000029
"""

import argparse
import os
import torch
import numpy as np
from ray.rllib.algorithms.ppo import PPO


def diagnose_value_heads(checkpoint_path):
    """
    Diagnose value head initialization and health.

    Args:
        checkpoint_path: Path to the checkpoint directory
    """
    checkpoint_path = os.path.abspath(checkpoint_path)

    if not os.path.exists(checkpoint_path):
        print(f"ERROR: Checkpoint not found: {checkpoint_path}")
        return False

    print(f"Loading checkpoint from: {checkpoint_path}")
    print("="*80)

    try:
        # Restore algorithm from checkpoint
        algo = PPO.from_checkpoint(checkpoint_path)

        # Get the trained policy
        policy = algo.get_policy("shared_policy")
        if not policy:
            print("ERROR: Could not get 'shared_policy'")
            return False

        model = policy.model

        print("\n🔍 VALUE HEAD INITIALIZATION CHECK")
        print("="*80)

        # Check each value head's weights and biases
        value_heads = [
            ("assault_value_head", model.assault_value_head),
            ("defend_value_head", model.defend_value_head),
            ("support_value_head", model.support_value_head),
            ("retreat_value_head", model.retreat_value_head),
        ]

        print("\nWeight Statistics:")
        print(f"{'Head':<25} {'Mean':<12} {'Std':<12} {'Min':<12} {'Max':<12}")
        print("-"*80)

        weight_stats = {}
        for name, head in value_heads:
            # Get the linear layer weights
            if hasattr(head, 'weight'):
                weights = head.weight.data
            elif hasattr(head, '_model') and hasattr(head._model[0], 'weight'):
                weights = head._model[0].weight.data
            else:
                print(f"Warning: Could not find weights for {name}")
                continue

            mean = weights.mean().item()
            std = weights.std().item()
            min_val = weights.min().item()
            max_val = weights.max().item()

            weight_stats[name] = {
                'mean': mean,
                'std': std,
                'min': min_val,
                'max': max_val,
                'weights': weights
            }

            print(f"{name:<25} {mean:<12.6f} {std:<12.6f} {min_val:<12.6f} {max_val:<12.6f}")

        # Check if all value heads have identical initialization
        print("\n\n🔍 VALUE HEAD SIMILARITY CHECK")
        print("="*80)

        if len(weight_stats) == 4:
            heads = list(weight_stats.keys())
            print("\nPairwise Weight Correlation:")
            print(f"{'Pair':<40} {'Correlation':<15} {'Status'}")
            print("-"*80)

            for i in range(len(heads)):
                for j in range(i+1, len(heads)):
                    head1, head2 = heads[i], heads[j]
                    weights1 = weight_stats[head1]['weights'].flatten()
                    weights2 = weight_stats[head2]['weights'].flatten()

                    # Calculate correlation
                    correlation = torch.corrcoef(torch.stack([weights1, weights2]))[0, 1].item()

                    status = "🚨 IDENTICAL!" if abs(correlation) > 0.99 else "✅ Different"
                    pair_name = f"{head1} vs {head2}"
                    print(f"{pair_name:<40} {correlation:<15.6f} {status}")

        # Test value function outputs with different strategies
        print("\n\n🔍 VALUE FUNCTION OUTPUT TEST")
        print("="*80)

        # Create dummy observations with different strategy one-hots
        obs_dim = model.shared_trunk[0].in_features
        dummy_obs_base = torch.randn(1, obs_dim - 4)  # Base observation without strategy

        strategies = ["Assault", "Defend", "Support", "Retreat"]
        print("\nValue Function Outputs (same obs, different strategies):")
        print(f"{'Strategy':<15} {'Value Output':<15} {'Status'}")
        print("-"*80)

        value_outputs = []
        for i, strategy in enumerate(strategies):
            # Create strategy one-hot
            strategy_onehot = torch.zeros(1, 4)
            strategy_onehot[0, i] = 1.0

            # Concatenate base obs with strategy one-hot
            obs = torch.cat([dummy_obs_base, strategy_onehot], dim=-1)

            # Get value output
            with torch.no_grad():
                features = model.shared_trunk(obs)
                if i == 0:
                    value = model.assault_value_head(features).item()
                elif i == 1:
                    value = model.defend_value_head(features).item()
                elif i == 2:
                    value = model.support_value_head(features).item()
                else:
                    value = model.retreat_value_head(features).item()

            value_outputs.append(value)

            # Check if value is near zero (not trained)
            status = "⚠️ Near zero" if abs(value) < 0.1 else "✅ Non-zero"
            print(f"{strategy:<15} {value:<15.6f} {status}")

        # Check variance in value outputs
        value_variance = np.var(value_outputs)
        print(f"\nValue Output Variance: {value_variance:.6f}")
        if value_variance < 0.01:
            print("🚨 WARNING: Very low variance - value heads may not be learning different distributions!")
        else:
            print("✅ Value heads show different outputs")

        # Get training stats if available
        print("\n\n🔍 TRAINING STATISTICS")
        print("="*80)

        # Try to get learner info from the last training iteration
        try:
            # This may not work if we just loaded from checkpoint
            learner_stats = algo.get_policy().get_metrics()
            if learner_stats:
                print("\nLearner Statistics:")
                for key, value in learner_stats.items():
                    if 'vf' in key.lower() or 'value' in key.lower():
                        print(f"  {key}: {value}")
        except:
            print("(Training statistics not available from checkpoint)")

        algo.stop()

        print("\n" + "="*80)
        print("✅ Diagnosis complete!")
        return True

    except Exception as e:
        print(f"❌ Diagnosis failed: {e}")
        import traceback
        traceback.print_exc()
        return False


def main():
    parser = argparse.ArgumentParser(description="Diagnose value head initialization")
    parser.add_argument(
        "--checkpoint",
        type=str,
        required=True,
        help="Path to checkpoint directory"
    )

    args = parser.parse_args()

    diagnose_value_heads(args.checkpoint)


if __name__ == "__main__":
    main()
