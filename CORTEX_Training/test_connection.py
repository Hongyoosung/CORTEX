"""
Minimal connection test to isolate crash point.
Tests connection to UE5 step-by-step.
"""

import sys
import time

print("=" * 60)
print("SCHOLA CONNECTION DIAGNOSTIC TEST")
print("=" * 60)

# Step 1: Check imports
print("\n[1/6] Testing imports...")
try:
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
    from schola.gym.env import GymVectorEnv as UnrealVectorEnv
    print("  ✓ Schola imports successful")
except ImportError as e:
    print(f"  ✗ Schola import failed: {e}")
    sys.exit(1)

# Step 2: Create connection
print("\n[2/6] Creating connection to UE5...")
try:
    connection = UnrealEditorConnection(url="localhost", port=50051)
    print("  ✓ Connection object created")
except Exception as e:
    print(f"  ✗ Connection creation failed: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

# Step 3: Create environment
print("\n[3/6] Creating Schola environment...")
try:
    env = UnrealVectorEnv(unreal_connection=connection, verbosity=2)
    print("  ✓ Environment created")
    print(f"  - Num envs: {env.num_envs}")
    print(f"  - Action space type: {type(env.action_space)}")
    print(f"  - Observation space type: {type(env.observation_space)}")
except Exception as e:
    print(f"  ✗ Environment creation failed: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

# Step 4: First reset (CRITICAL - often crashes here)
print("\n[4/6] Calling env.reset() - FIRST CONNECTION TO UE5...")
print("  WARNING: If UE5 crashes, the issue is during initial reset/registration")
try:
    obs, info = env.reset()
    print("  ✓ Reset successful!")
    print(f"  - Observation type: {type(obs)}")
    if isinstance(obs, dict):
        print(f"  - Agent count: {len(obs)}")
        print(f"  - Agent IDs: {list(obs.keys())}")
        for agent_id, agent_obs in obs.items():
            print(f"    - {agent_id}: shape {agent_obs.shape if hasattr(agent_obs, 'shape') else 'N/A'}")
    else:
        print(f"  - Observation shape: {obs.shape if hasattr(obs, 'shape') else 'N/A'}")
except Exception as e:
    print(f"  ✗ Reset failed: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

# Step 5: Take a single step
print("\n[5/6] Taking single step...")
try:
    import numpy as np

    # Create dummy actions for all agents
    if isinstance(env.action_space, dict):
        # Multi-agent dict action space
        actions = {}
        for agent_id in obs.keys():
            actions[agent_id] = np.zeros((env.num_envs, 8), dtype=np.float32)
        print(f"  - Created dict actions for {len(actions)} agents")
    else:
        # Single action space
        actions = np.zeros((env.num_envs, 8), dtype=np.float32)
        print(f"  - Created array actions with shape {actions.shape}")

    obs_next, reward, terminated, truncated, info = env.step(actions)
    print("  ✓ Step successful!")
    print(f"  - Reward: {reward}")
    print(f"  - Terminated: {terminated}")
    print(f"  - Truncated: {truncated}")
except Exception as e:
    print(f"  ✗ Step failed: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

# Step 6: Second reset
print("\n[6/6] Testing second reset...")
try:
    obs, info = env.reset()
    print("  ✓ Second reset successful!")
except Exception as e:
    print(f"  ✗ Second reset failed: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

# Close
print("\n[CLEANUP] Closing environment...")
try:
    env.close()
    print("  ✓ Environment closed")
except:
    pass

print("\n" + "=" * 60)
print("ALL TESTS PASSED - No crash detected")
print("=" * 60)
print("\nIf UE5 crashed during this test, check the console output above")
print("to see which step failed. The crash likely occurs at:")
print("  - Step 4: Initial reset/agent registration")
print("  - Step 5: First action execution")
