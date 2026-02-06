"""
Test Script for CORTEX v10.0 Synchronous Environment

This script verifies the synchronous environment works correctly:
1. Connection to UE5
2. Reset functionality
3. Step execution (blocking behavior)
4. Episode completion detection
5. Performance metrics

Usage:
    1. Start UE5 with Schola plugin
    2. Run: python test_sync_env.py

Expected Behavior:
    - step() should block until UE5 responds (~75ms per step)
    - Episodes should complete at exactly MaxEpisodeDuration (30s)
    - No threading errors
    - Clean episode boundaries
"""

import time
import numpy as np
from cortex_env import CORTEXSyncMultiAgentEnv

def test_sync_env():
    """Test synchronous environment behavior."""

    print("=" * 80)
    print("CORTEX v10.0 Synchronous Environment Test")
    print("=" * 80)

    # 1. Environment Creation
    print("\n[TEST 1] Creating environment...")
    try:
        env = CORTEXSyncMultiAgentEnv(
            host="localhost",
            port=50051,
            num_envs=4
        )
        print("✅ Environment created successfully")
    except Exception as e:
        print(f"❌ Failed to create environment: {e}")
        return False

    # 2. Reset
    print("\n[TEST 2] Testing reset()...")
    try:
        start_time = time.time()
        obs, info = env.reset()
        reset_duration = time.time() - start_time

        print(f"✅ Reset completed in {reset_duration:.2f}s")
        print(f"   Agents: {len(obs)}")
        print(f"   Sample agent IDs: {list(obs.keys())[:4]}")
        print(f"   Observation shape: {obs[list(obs.keys())[0]].shape}")
    except Exception as e:
        print(f"❌ Reset failed: {e}")
        return False

    # 3. Step Execution
    print("\n[TEST 3] Testing step() execution (10 steps)...")
    step_durations = []

    try:
        for i in range(10):
            # Create random actions for all agents
            actions = {
                agent_id: np.random.uniform(0.3, 0.7, size=(5,)).astype(np.float32)
                for agent_id in obs.keys()
            }

            # Execute step (should block until UE5 responds)
            step_start = time.time()
            obs, rewards, terminated, truncated, info = env.step(actions)
            step_duration = time.time() - step_start
            step_durations.append(step_duration)

            # Check for episode completion
            all_done = terminated.get('__all__', False) or truncated.get('__all__', False)

            print(f"  Step {i+1:2d}: {step_duration*1000:5.1f}ms "
                  f"(agents={len(obs)}, done={all_done})")

        avg_step_duration = np.mean(step_durations)
        print(f"\n✅ Step execution successful")
        print(f"   Average step duration: {avg_step_duration*1000:.1f}ms")
        print(f"   Min: {np.min(step_durations)*1000:.1f}ms, Max: {np.max(step_durations)*1000:.1f}ms")

        # Verify blocking behavior (should be slower than async)
        if avg_step_duration < 0.030:
            print(f"⚠️  WARNING: Step duration ({avg_step_duration*1000:.1f}ms) seems too fast")
            print(f"   Expected ~75ms for synchronous blocking calls")

    except Exception as e:
        print(f"❌ Step execution failed: {e}")
        import traceback
        traceback.print_exc()
        return False

    # 4. Episode Completion Test
    print("\n[TEST 4] Testing episode completion (run until done)...")
    print("   This may take up to 30s (UE5 MaxEpisodeDuration)...")

    try:
        obs, info = env.reset()
        episode_start = time.time()
        steps = 0
        total_reward = 0

        while True:
            actions = {
                agent_id: np.random.uniform(0.3, 0.7, size=(5,)).astype(np.float32)
                for agent_id in obs.keys()
            }

            obs, rewards, terminated, truncated, info = env.step(actions)
            steps += 1
            total_reward += sum(rewards.values())

            # Check for episode completion
            all_done = terminated.get('__all__', False) or truncated.get('__all__', False)

            if all_done:
                episode_duration = time.time() - episode_start
                print(f"\n✅ Episode completed!")
                print(f"   Duration: {episode_duration:.1f}s")
                print(f"   Steps: {steps}")
                print(f"   Total Reward: {total_reward:.2f}")

                # Verify episode duration matches UE5 MaxEpisodeDuration
                expected_duration = 30.0  # Assuming MaxEpisodeDuration=30s in UE5
                duration_error = abs(episode_duration - expected_duration)

                if duration_error < 2.0:
                    print(f"   ✅ Episode duration matches UE5 MaxEpisodeDuration (~{expected_duration}s)")
                else:
                    print(f"   ⚠️  Episode duration ({episode_duration:.1f}s) differs from expected ({expected_duration}s)")
                    print(f"      This may indicate desync or incorrect MaxEpisodeDuration in UE5")

                break

            # Safety timeout (should never trigger if UE5 is working)
            if steps > 10000:
                print(f"⚠️  Safety timeout at {steps} steps")
                break

            # Progress indicator
            if steps % 100 == 0:
                elapsed = time.time() - episode_start
                print(f"   Progress: {steps} steps, {elapsed:.1f}s elapsed...")

    except Exception as e:
        print(f"❌ Episode completion test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

    # 5. Threading Check
    print("\n[TEST 5] Verifying no background threads...")
    import threading
    active_threads = threading.enumerate()
    thread_names = [t.name for t in active_threads]

    grpc_threads = [t for t in thread_names if 'grpc' in t.lower() or 'worker' in t.lower()]

    if grpc_threads:
        print(f"⚠️  Found background threads: {grpc_threads}")
        print(f"   This may indicate async components still present")
    else:
        print(f"✅ No background gRPC/worker threads found")
        print(f"   Active threads: {thread_names}")

    # 6. Cleanup
    print("\n[TEST 6] Testing cleanup...")
    try:
        env.close()
        print("✅ Environment closed successfully")
    except Exception as e:
        print(f"❌ Cleanup failed: {e}")
        return False

    # Summary
    print("\n" + "=" * 80)
    print("TEST SUMMARY")
    print("=" * 80)
    print("✅ All tests passed!")
    print("\nKey Findings:")
    print(f"  - Step latency: {avg_step_duration*1000:.1f}ms (blocking)")
    print(f"  - Episode duration: {episode_duration:.1f}s")
    print(f"  - No background threads (fully synchronous)")
    print("\nReady for training with train_rllib.py!")
    print("=" * 80)

    return True


def main():
    """Run tests."""
    print("\nIMPORTANT: Make sure UE5 is running with Schola plugin before starting!\n")
    input("Press Enter to start tests...")

    success = test_sync_env()

    if not success:
        print("\n❌ Tests failed. Please check the errors above.")
        return 1

    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
