"""
Test script to diagnose SBDAPM environment issues.
"""
import sys
import numpy as np

try:
    from sbdapm_env import SBDAPMScholaEnv, SCHOLA_AVAILABLE
except ImportError as e:
    print(f"Failed to import environment: {e}")
    sys.exit(1)

def test_environment():
    """Test basic environment functionality."""
    print("=" * 60)
    print("Environment Diagnostics")
    print("=" * 60)

    if not SCHOLA_AVAILABLE:
        print("\nError: Schola not available. Cannot proceed.")
        sys.exit(1)

    print("\n1. Creating environment...")
    try:
        env = SBDAPMScholaEnv(host="localhost", port=50051, max_episode_steps=1000)
        print(f"   [OK] Environment created: {type(env).__name__}")
        print(f"   [OK] Num envs: {env.num_envs}")
        print(f"   [OK] Observation space: {env.observation_space}")
        print(f"   [OK] Action space: {env.action_space}")
    except Exception as e:
        print(f"   [FAIL] Failed to create environment: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

    print("\n2. Testing reset()...")
    try:
        obs, info = env.reset()
        print(f"   [OK] Reset successful")
        print(f"   [OK] Obs shape: {obs.shape}, dtype: {obs.dtype}")
        print(f"   [OK] Info type: {type(info)}")
        if isinstance(info, dict):
            print(f"   [OK] Info keys: {list(info.keys())[:5]}...")  # First 5 keys
            print(f"   [OK] Obs sample: {obs[:5]}...")  # First 5 values
    except Exception as e:
        print(f"   [FAIL] Reset failed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

    print("\n3. Testing step()...")
    try:
        # Random action
        action = env.action_space.sample()
        print(f"   -> Action shape: {action.shape}")

        obs, reward, terminated, truncated, info = env.step(action)
        print(f"   [OK] Step successful")
        print(f"   [OK] Obs shape: {obs.shape}")
        print(f"   [OK] Reward: {reward}")
        print(f"   [OK] Terminated: {terminated}")
        print(f"   [OK] Truncated: {truncated}")
        print(f"   [OK] Info type: {type(info)}")
    except Exception as e:
        print(f"   [FAIL] Step failed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

    print("\n4. Closing environment...")
    try:
        env.close()
        print("   [OK] Closed successfully")
    except Exception as e:
        print(f"   [FAIL] Close failed: {e}")

    print("\n" + "=" * 60)
    print("All tests passed! Environment is working correctly.")
    print("=" * 60)

if __name__ == "__main__":
    test_environment()
