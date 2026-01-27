"""
Simple UE5 Schola Connection Test

Tests basic connectivity to UE5 and displays environment configuration.
Use this if diagnose_episode_termination.py fails.

Usage:
    python test_schola_connection.py
"""

import sys

try:
    from schola.core.env import ScholaEnv, AutoResetType
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
except ImportError:
    print("ERROR: schola not installed")
    print("Run: pip install schola")
    sys.exit(1)


def test_connection():
    """Test basic UE5 connection and display info."""
    print("=" * 80)
    print("UE5 SCHOLA CONNECTION TEST")
    print("=" * 80)
    print()
    
    # Step 1: Connection
    print("Step 1: Connecting to UE5...")
    try:
        connection = UnrealEditorConnection(url="localhost", port=50051)
        print(" ✓ Connection established")
    except Exception as e:
        print(f" ✗ Connection failed: {e}")
        print("\n  Troubleshooting:")
        print("    - Is UE5 running?")
        print("    - Is Schola plugin enabled?")
        print("    - Is port 50051 open?")
        return False
    print()
    
    # Step 2: Create environment
    print("Step 2: Creating Schola environment...")
    try:
        env = ScholaEnv(
            unreal_connection=connection,
            num_envs=4,  # CRITICAL FIX: Enable 4-environment vectorization
            verbosity=1,
            auto_reset_type=AutoResetType.SAME_STEP
        )
        print(" ✓ Environment created")

        # Verify vectorization fix
        print()
        print(" Environment structure diagnosis:")
        print(f"   Expected: 4 environments")
        print(f"   Actual: {len(env.ids)} environments")
        for i, agents in enumerate(env.ids):
            print(f"     Env {i}: {len(agents)} agents")

        if len(env.ids) == 1:
            print(" ⚠️  WARNING: Only 1 environment detected!")
            print("     Expected 4 environments. This may indicate:")
            print("     1. UE5 TeamToEnvironmentMap not configured")
            print("     2. All 32 agents assigned to same logical environment")
        elif len(env.ids) == 4:
            print(" ✅ SUCCESS: Vectorization working correctly!")

    except Exception as e:
        print(f" ✗ Environment creation failed: {e}")
        return False
    print()
    
    # Step 3: Reset
    print("Step 3: Performing hard_reset()...")
    try:
        result = env.hard_reset()
        
        # 튜플인지 확인하고 처리
        if isinstance(result, tuple):
            obs = result[0]
            print(f" ✓ Reset successful (tuple returned)")
            print(f"   DEBUG: Return type is tuple with {len(result)} elements")
        else:
            obs = result
            print(f" ✓ Reset successful (dict returned)")
        
        print(f" Environments: {len(obs)}")
    except Exception as e:
        print(f" ✗ Reset failed: {e}")
        env.close()
        return False
    print()
    
    # Step 4: Agent info
    print("Step 4: Checking agents...")
    if not hasattr(env, 'ids'):
        print(" ✗ No 'ids' attribute found")
        env.close()
        return False
    
    if not env.ids:
        print(" ✗ No agents registered")
        print("   Make sure agents are spawned in the UE5 map")
        env.close()
        return False
    
    total_agents = 0
    for env_idx, agent_list in enumerate(env.ids):
        print(f"   Env {env_idx}: {len(agent_list)} agents")
        print(f"     Agent IDs: {agent_list}")
        total_agents += len(agent_list)
    
    print(f" ✓ Total agents: {total_agents}")
    print()
    
    # Step 5: Action space
    print("Step 5: Checking action space...")
    action_defns = getattr(env, 'action_defns', {})
    if not action_defns:
        print(" ✗ No action definitions found")
        env.close()
        return False
    
    action_keys_found = False
    for env_idx, agent_list in enumerate(env.ids):
        for agent_idx in agent_list:
            agent_defn = action_defns.get(env_idx, {}).get(agent_idx, None)
            if agent_defn is None:
                continue
            
            keys_list = []
            if hasattr(agent_defn, 'spaces'):
                keys_list = list(agent_defn.spaces.keys())
            elif isinstance(agent_defn, dict):
                keys_list = list(agent_defn.keys())
            
            if keys_list:
                print(f"   Agent (Env {env_idx}, ID {agent_idx}):")
                print(f"     Action keys: {keys_list}")
                action_keys_found = True
                break  # Just show first agent
        if action_keys_found:
            break
    
    if not action_keys_found:
        print(" ✗ No action keys found")
        env.close()
        return False
    
    print(" ✓ Action space configured")
    print()
    
    # Step 6: Observation space
    print("Step 6: Checking observation space...")
    
    # obs가 튜플이면 첫 번째 요소를 사용
    obs_dict = obs[0] if isinstance(obs, tuple) else obs
    
    try:
        for env_idx in range(min(1, len(obs_dict))):  # Just check first env
            env_agents = obs_dict.get(env_idx, {})
            if not env_agents:
                print(f" ⚠ No agents found in environment {env_idx}")
                continue
            
            for agent_idx in env_agents.keys():
                agent_obs = obs_dict[env_idx][agent_idx]
                
                if isinstance(agent_obs, dict):
                    obs_val = list(agent_obs.values())[0]
                else:
                    obs_val = agent_obs
                
                import numpy as np
                obs_array = np.array(obs_val, dtype=np.float32).flatten()
                
                print(f"   Agent (Env {env_idx}, ID {agent_idx}):")
                print(f"     Observation shape: {obs_array.shape}")
                print(f"     Sample values: {obs_array[:5]}")
                break  # Just show first agent
            break
        
        print(" ✓ Observations received")
    except Exception as e:
        print(f" ✗ Observation check failed: {e}")
        print(f"   DEBUG: obs type = {type(obs)}")
        print(f"   DEBUG: obs_dict type = {type(obs_dict)}")
        env.close()
        return False
    
    print()
    
    # Cleanup
    env.close()
    
    print("=" * 80)
    print("✓ ALL TESTS PASSED")
    print("=" * 80)
    print()
    print("UE5 connection is working correctly!")
    print("You can now run: python diagnose_episode_termination.py")
    print()
    
    return True


if __name__ == "__main__":
    success = test_connection()
    sys.exit(0 if success else 1)
