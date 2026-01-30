"""
CORTEX v8.10 Issue Diagnosis Script

This script verifies the following hypotheses:
1. Reward normalization not applied
2. Strategy-specific value head routing error
3. VF_CLIP_PARAM too large
4. Entropy schedule vs VF training conflict
5. Multi-agent reward correlation

Usage:
    python diagnose_v8.10_issues.py --checkpoint training_results/20260128_164441
"""

import argparse
import os
import sys
import numpy as np
import torch
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))

try:
    from ray.rllib.algorithms.ppo import PPO
    from training_env.config import RLConfig
    RLLIB_AVAILABLE = True
except ImportError:
    RLLIB_AVAILABLE = False
    print("ERROR: ray[rllib] not installed")
    sys.exit(1)


def diagnose_reward_normalization(checkpoint_path):
    """
    Hypothesis 1: Verify reward normalization

    Check if rewards in checkpoint exceed [-10, 10] range.
    """
    print("\n" + "="*80)
    print("HYPOTHESIS 1: Reward Normalization Not Applied")
    print("="*80)

    # Load checkpoint
    try:
        algo = PPO.from_checkpoint(checkpoint_path)
        print(f"✅ Loaded checkpoint: {checkpoint_path}")
    except Exception as e:
        print(f"❌ Failed to load checkpoint: {e}")
        return

    # Check recent training stats
    result_json = Path(checkpoint_path).parent / "result.json"
    if result_json.exists():
        import json
        with open(result_json) as f:
            for line in f:
                try:
                    result = json.loads(line)
                    env_results = result.get("env_runners", {})
                    reward_max = env_results.get("episode_reward_max", None)
                    reward_min = env_results.get("episode_reward_min", None)

                    if reward_max is not None and reward_min is not None:
                        print(f"\n📊 Last Training Result:")
                        print(f"   Reward Range: [{reward_min:.2f}, {reward_max:.2f}]")

                        if abs(reward_max) > 10.0 or abs(reward_min) > 10.0:
                            print(f"   🚨 ISSUE DETECTED: Rewards exceed normalized range [-10, 10]")
                            print(f"   🔍 Root Cause: Either:")
                            print(f"      1. C++ RewardCalculator.cpp not compiled/loaded")
                            print(f"      2. Python env is NOT applying normalization")
                            print(f"      3. TensorBoard shows CUMULATIVE episode rewards (not per-step)")
                        else:
                            print(f"   ✅ Rewards within normalized range [-10, 10]")
                except:
                    continue

    algo.stop()


def diagnose_value_head_routing(checkpoint_path):
    """
    Hypothesis 2: Verify strategy-specific value head routing

    Test if different strategies produce different value estimates.
    """
    print("\n" + "="*80)
    print("HYPOTHESIS 2: Strategy-Specific Value Head Routing Error")
    print("="*80)

    try:
        algo = PPO.from_checkpoint(checkpoint_path)
        policy = algo.get_policy("shared_policy")
        model = policy.model
        print(f"✅ Loaded model from checkpoint")
    except Exception as e:
        print(f"❌ Failed to load model: {e}")
        return

    # Check if value heads exist
    has_assault_head = hasattr(model, 'assault_value_head')
    has_defend_head = hasattr(model, 'defend_value_head')
    has_support_head = hasattr(model, 'support_value_head')
    has_retreat_head = hasattr(model, 'retreat_value_head')

    print(f"\n🔍 Value Head Check:")
    print(f"   Assault Value Head: {'✅' if has_assault_head else '❌'}")
    print(f"   Defend Value Head: {'✅' if has_defend_head else '❌'}")
    print(f"   Support Value Head: {'✅' if has_support_head else '❌'}")
    print(f"   Retreat Value Head: {'✅' if has_retreat_head else '❌'}")

    if not all([has_assault_head, has_defend_head, has_support_head, has_retreat_head]):
        print(f"\n🚨 ISSUE DETECTED: Missing strategy-specific value heads!")
        print(f"   Model still using single shared value head (v8.9 or earlier)")
        algo.stop()
        return

    # Test value head routing with different strategies
    print(f"\n🧪 Testing Value Head Routing:")

    # Create test observations with different strategies
    base_obs = np.random.randn(46).astype(np.float32)

    test_cases = [
        ("Assault", np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32)),
        ("Defend", np.array([0.0, 1.0, 0.0, 0.0], dtype=np.float32)),
        ("Support", np.array([0.0, 0.0, 1.0, 0.0], dtype=np.float32)),
        ("Retreat", np.array([0.0, 0.0, 0.0, 1.0], dtype=np.float32)),
    ]

    values = {}
    model.eval()
    with torch.no_grad():
        for strategy_name, strategy_onehot in test_cases:
            obs = np.concatenate([base_obs, strategy_onehot])
            obs_tensor = torch.FloatTensor(obs).unsqueeze(0)

            # Forward pass
            input_dict = {"obs": obs_tensor}
            _, _ = model.forward(input_dict, [], None)
            value = model.value_function()

            values[strategy_name] = value.item()
            print(f"   {strategy_name:8s} → Value = {value.item():8.4f}")

    # Check if values are sufficiently different
    value_list = list(values.values())
    value_std = np.std(value_list)
    value_range = max(value_list) - min(value_list)

    print(f"\n📊 Value Differentiation:")
    print(f"   Std Dev: {value_std:.4f}")
    print(f"   Range: {value_range:.4f}")

    if value_std < 0.01:
        print(f"   🚨 ISSUE DETECTED: Value heads producing nearly identical outputs!")
        print(f"   🔍 Root Cause: Either:")
        print(f"      1. Value heads not properly trained yet")
        print(f"      2. Routing logic has bug (always using same head)")
        print(f"      3. Shared feature extractor dominates output")
    elif value_range < 0.1:
        print(f"   ⚠️  WARNING: Value heads show low differentiation (may need more training)")
    else:
        print(f"   ✅ Value heads showing healthy differentiation")

    algo.stop()


def diagnose_hyperparameters(checkpoint_path):
    """
    Hypothesis 3 & 4: Check hyperparameter settings
    """
    print("\n" + "="*80)
    print("HYPOTHESIS 3 & 4: Hyperparameter Issues")
    print("="*80)

    try:
        algo = PPO.from_checkpoint(checkpoint_path)
        config = algo.config
        print(f"✅ Loaded config from checkpoint")
    except Exception as e:
        print(f"❌ Failed to load config: {e}")
        return

    # Extract hyperparameters
    vf_clip_param = config.get("vf_clip_param", None)
    vf_loss_coeff = config.get("vf_loss_coeff", None)
    entropy_coeff = config.get("entropy_coeff", None)
    entropy_schedule = config.get("entropy_coeff_schedule", None)
    lr = config.get("lr", None)

    print(f"\n📊 Current Hyperparameters:")
    print(f"   VF_CLIP_PARAM: {vf_clip_param}")
    print(f"   VF_LOSS_COEFF: {vf_loss_coeff}")
    print(f"   ENTROPY_COEFF: {entropy_coeff}")
    print(f"   ENTROPY_SCHEDULE: {entropy_schedule}")
    print(f"   LEARNING_RATE: {lr}")

    # Hypothesis 3: VF_CLIP_PARAM too large
    print(f"\n🔍 Hypothesis 3: VF_CLIP_PARAM = {vf_clip_param}")
    if vf_clip_param > 5.0:
        print(f"   🚨 ISSUE DETECTED: VF_CLIP_PARAM = {vf_clip_param} is too large!")
        print(f"   📝 Recommendation: Reduce to 2.0-3.0 range")
        print(f"   💡 Reason: Even with normalized rewards [-10, 10], clip param should match")
        print(f"             typical TD error magnitude, not full reward range")
    else:
        print(f"   ✅ VF_CLIP_PARAM in reasonable range")

    # Hypothesis 4: Entropy schedule vs VF conflict
    print(f"\n🔍 Hypothesis 4: Entropy Schedule = {entropy_schedule}")
    if entropy_schedule and entropy_schedule[0][1] > 0.015:
        initial_entropy = entropy_schedule[0][1]
        print(f"   ⚠️  WARNING: Initial entropy coeff = {initial_entropy} is high")
        print(f"   📝 Recommendation: Start at 0.01 or lower")
        print(f"   💡 Reason: High initial exploration prevents VF from learning stable targets")
    elif entropy_coeff and entropy_coeff > 0.015:
        print(f"   ⚠️  WARNING: Entropy coeff = {entropy_coeff} is high")
        print(f"   📝 Recommendation: Reduce to 0.005-0.01")
    else:
        print(f"   ✅ Entropy settings reasonable")

    algo.stop()


def diagnose_training_metrics(checkpoint_path):
    """
    Analyze TensorBoard metrics from progress.csv
    """
    print("\n" + "="*80)
    print("TRAINING METRICS ANALYSIS")
    print("="*80)

    progress_csv = Path(checkpoint_path).parent / "progress.csv"
    if not progress_csv.exists():
        print(f"❌ progress.csv not found")
        return

    import pandas as pd
    df = pd.read_csv(progress_csv)

    # Get recent metrics
    recent = df.tail(10)

    print(f"\n📊 Recent Training Metrics (last 10 iterations):")

    # Entropy
    if 'info/learner/shared_policy/learner_stats/entropy' in df.columns:
        entropy_col = 'info/learner/shared_policy/learner_stats/entropy'
        entropy = recent[entropy_col].values
        print(f"\n   Entropy:")
        print(f"      Current: {entropy[-1]:.4f}")
        print(f"      Trend: {entropy[0]:.4f} → {entropy[-1]:.4f}")

        if entropy[-1] > 7.0:
            print(f"      🚨 CRITICAL: Entropy > 7.0 indicates VF collapse")
        elif entropy[-1] > 6.0:
            print(f"      ⚠️  WARNING: Entropy > 6.0, monitor VF explained var")

        # Check if rising
        if entropy[-1] > entropy[0]:
            print(f"      🚨 CRITICAL: Entropy is RISING (should be declining)")

    # VF Explained Variance
    if 'info/learner/shared_policy/learner_stats/vf_explained_var' in df.columns:
        vf_var_col = 'info/learner/shared_policy/learner_stats/vf_explained_var'
        vf_var = recent[vf_var_col].values
        print(f"\n   VF Explained Variance:")
        print(f"      Current: {vf_var[-1]:.4f}")
        print(f"      Trend: {vf_var[0]:.4f} → {vf_var[-1]:.4f}")

        if vf_var[-1] < 0.1:
            print(f"      🚨 CRITICAL: VF Explained Var < 0.1 (VALUE FUNCTION COLLAPSED)")
        elif vf_var[-1] < 0.3:
            print(f"      ⚠️  WARNING: VF Explained Var < 0.3 (target: >0.5)")
        else:
            print(f"      ✅ VF Explained Var healthy")

    # VF Loss
    if 'info/learner/shared_policy/learner_stats/vf_loss' in df.columns:
        vf_loss_col = 'info/learner/shared_policy/learner_stats/vf_loss'
        vf_loss = recent[vf_loss_col].values
        print(f"\n   VF Loss:")
        print(f"      Current: {vf_loss[-1]:.4f}")
        print(f"      Trend: {vf_loss[0]:.4f} → {vf_loss[-1]:.4f}")

        if vf_loss[-1] > 2.0:
            print(f"      ⚠️  WARNING: VF Loss > 2.0 (unstable training)")

    # Rewards
    if 'env_runners/episode_reward_mean' in df.columns:
        reward_col = 'env_runners/episode_reward_mean'
        rewards = recent[reward_col].dropna().values
        if len(rewards) > 0:
            print(f"\n   Episode Rewards:")
            print(f"      Current: {rewards[-1]:.2f}")
            print(f"      Trend: {rewards[0]:.2f} → {rewards[-1]:.2f}")


def diagnose_multi_agent_correlation():
    """
    Hypothesis 5: Multi-agent reward correlation

    Note: This requires actual training data, not just checkpoint
    """
    print("\n" + "="*80)
    print("HYPOTHESIS 5: Multi-Agent Reward Correlation")
    print("="*80)
    print("\n⚠️  This analysis requires episode-level data (not available from checkpoint alone)")
    print("   To diagnose this properly:")
    print("   1. Enable per-agent reward logging in callback")
    print("   2. Run training for 100 episodes")
    print("   3. Compute correlation matrix between agent rewards")
    print("\n   High correlation (>0.7) indicates:")
    print("   - Agents' rewards are not independent")
    print("   - May need centralized critic or counterfactual baseline")


def main():
    parser = argparse.ArgumentParser(description="Diagnose CORTEX v8.10 training issues")
    parser.add_argument("--checkpoint", type=str, required=True,
                       help="Path to checkpoint directory")

    args = parser.parse_args()

    checkpoint_path = args.checkpoint
    if not os.path.exists(checkpoint_path):
        print(f"ERROR: Checkpoint not found: {checkpoint_path}")
        sys.exit(1)

    print("\n" + "="*80)
    print("CORTEX v8.10 DIAGNOSTIC REPORT")
    print("="*80)
    print(f"Checkpoint: {checkpoint_path}")
    print("="*80)

    # Run all diagnostics
    diagnose_reward_normalization(checkpoint_path)
    diagnose_value_head_routing(checkpoint_path)
    diagnose_hyperparameters(checkpoint_path)
    diagnose_training_metrics(checkpoint_path)
    diagnose_multi_agent_correlation()

    print("\n" + "="*80)
    print("DIAGNOSTIC COMPLETE")
    print("="*80)
    print("\n📝 Next Steps:")
    print("   1. Review issues flagged with 🚨 (critical) and ⚠️ (warning)")
    print("   2. Apply recommended fixes")
    print("   3. Restart training from last stable checkpoint")
    print("   4. Monitor VF explained variance closely (target: >0.5)")


if __name__ == "__main__":
    main()
