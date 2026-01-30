"""
Quick analysis of training metrics from progress.csv
"""
import pandas as pd
import sys

if len(sys.argv) < 2:
    print("Usage: python analyze_training_metrics.py <path_to_progress.csv>")
    sys.exit(1)

csv_path = sys.argv[1]

df = pd.read_csv(csv_path)

# Get last 10 rows
last_10 = df.tail(10)

print("\n" + "="*80)
print("TRAINING METRICS - LAST 10 ITERATIONS")
print("="*80)

# Find column names
step_col = [c for c in df.columns if 'agent_steps_sampled' in c and 'this_iter' not in c][0]
reward_max_col = 'env_runners/episode_reward_max'
reward_mean_col = 'env_runners/episode_reward_mean'
reward_min_col = 'env_runners/episode_reward_min'
entropy_col = 'info/learner/shared_policy/learner_stats/entropy'
vf_var_col = 'info/learner/shared_policy/learner_stats/vf_explained_var'
vf_loss_col = 'info/learner/shared_policy/learner_stats/vf_loss'
entropy_coeff_col = 'info/learner/shared_policy/learner_stats/entropy_coeff'

print(f"\n{'Iter':<6} {'Steps':<10} {'Reward':<12} {'Entropy':<10} {'VF_Var':<10} {'VF_Loss':<10} {'Ent_Coeff':<10}")
print("-"*80)

for idx, row in last_10.iterrows():
    steps = int(row[step_col])
    reward_mean = row[reward_mean_col] if pd.notna(row[reward_mean_col]) else 0.0
    entropy = row[entropy_col] if pd.notna(row[entropy_col]) else 0.0
    vf_var = row[vf_var_col] if pd.notna(row[vf_var_col]) else 0.0
    vf_loss = row[vf_loss_col] if pd.notna(row[vf_loss_col]) else 0.0
    entropy_coeff = row[entropy_coeff_col] if pd.notna(row[entropy_coeff_col]) else 0.0

    print(f"{int(row['training_iteration']):<6} {steps:<10} {reward_mean:<12.2f} {entropy:<10.4f} {vf_var:<10.4f} {vf_loss:<10.4f} {entropy_coeff:<10.6f}")

print("\n" + "="*80)
print("ISSUE ANALYSIS")
print("="*80)

last_row = df.tail(1).iloc[0]
steps = int(last_row[step_col])
reward_max = last_row[reward_max_col] if pd.notna(last_row[reward_max_col]) else 0.0
reward_mean = last_row[reward_mean_col] if pd.notna(last_row[reward_mean_col]) else 0.0
reward_min = last_row[reward_min_col] if pd.notna(last_row[reward_min_col]) else 0.0
entropy = last_row[entropy_col] if pd.notna(last_row[entropy_col]) else 0.0
vf_var = last_row[vf_var_col] if pd.notna(last_row[vf_var_col]) else 0.0
vf_loss = last_row[vf_loss_col] if pd.notna(last_row[vf_loss_col]) else 0.0

print(f"\nCurrent Step: {steps}")
print(f"\n1. REWARD NORMALIZATION CHECK:")
print(f"   Max Reward: {reward_max:.2f}")
print(f"   Mean Reward: {reward_mean:.2f}")
print(f"   Min Reward: {reward_min:.2f}")

if abs(reward_max) > 10.0 or abs(reward_min) > 10.0:
    print(f"   🚨 ISSUE: Rewards exceed normalized range [-10, 10]!")
    print(f"   This means EITHER:")
    print(f"      - These are EPISODE CUMULATIVE rewards (not per-step)")
    print(f"      - OR C++ reward normalization not applied")
else:
    print(f"   ✅ Rewards within normalized range")

print(f"\n2. ENTROPY ANALYSIS:")
print(f"   Current Entropy: {entropy:.4f}")

# Check trend
entropy_first = df.tail(10).iloc[0][entropy_col] if pd.notna(df.tail(10).iloc[0][entropy_col]) else 0.0
entropy_trend = "RISING 🚨" if entropy > entropy_first else "FALLING ✅"
print(f"   Trend (last 10): {entropy_first:.4f} → {entropy:.4f} ({entropy_trend})")

if entropy > 7.0:
    print(f"   🚨 CRITICAL: Entropy > 7.0 indicates VF collapse!")
elif entropy > 6.0:
    print(f"   ⚠️  WARNING: Entropy > 6.0")

print(f"\n3. VALUE FUNCTION EXPLAINED VARIANCE:")
print(f"   Current VF Explained Var: {vf_var:.4f}")

if vf_var < 0.1:
    print(f"   🚨 CRITICAL: VF Explained Var < 0.1 (VALUE FUNCTION COLLAPSED)")
elif vf_var < 0.3:
    print(f"   ⚠️  WARNING: VF Explained Var < 0.3 (target: >0.5)")
else:
    print(f"   ✅ VF Explained Var healthy")

print(f"\n4. VALUE FUNCTION LOSS:")
print(f"   Current VF Loss: {vf_loss:.4f}")

if vf_loss > 2.0:
    print(f"   ⚠️  WARNING: VF Loss > 2.0 (unstable)")
else:
    print(f"   ✅ VF Loss reasonable")

print("\n" + "="*80)
print("RECOMMENDATIONS")
print("="*80)

issues = []

if abs(reward_max) > 10.0:
    issues.append("Reward normalization")
if entropy > 7.0 or (entropy > entropy_first and entropy > 6.0):
    issues.append("Rising entropy")
if vf_var < 0.1:
    issues.append("VF collapse")
if vf_loss > 2.0:
    issues.append("High VF loss")

if not issues:
    print("\n✅ No critical issues detected. Training appears healthy.")
else:
    print(f"\n🚨 CRITICAL ISSUES DETECTED: {', '.join(issues)}")
    print("\nRecommended Actions:")

    if "Reward normalization" in issues:
        print("\n  1. REWARD NORMALIZATION:")
        print("     - Verify C++ code was compiled (check UE5 logs for '[REWARD v8.10]' messages)")
        print("     - Note: TensorBoard shows EPISODE rewards (sum of all per-step rewards)")
        print("     - This is EXPECTED - each step is [-10, 10], but episodes have ~1500-1600 steps")
        print("     - Expected episode reward range: [-16000, +16000]")
        print("     - Actual range in TensorBoard is normal if within this bound")

    if "Rising entropy" in issues:
        print("\n  2. ENTROPY SCHEDULE:")
        print("     - Reduce initial entropy coeff from 0.02 to 0.01")
        print("     - Apply more aggressive decay schedule")
        print("     - Consider reducing log_std bounds")

    if "VF collapse" in issues:
        print("\n  3. VALUE FUNCTION FIXES:")
        print("     - Verify strategy-specific value heads are in model")
        print("     - Increase VF_LOSS_COEFF from 0.5 to 1.0")
        print("     - Reduce VF_CLIP_PARAM from 10.0 to 3.0")
        print("     - Ensure reward normalization is working")

    if "High VF loss" in issues:
        print("\n  4. VF LOSS STABILITY:")
        print("     - Reduce learning rate for value function")
        print("     - Tighten VF_CLIP_PARAM")
        print("     - Check for reward scale issues")
