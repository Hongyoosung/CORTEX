"""
ScholaEnv Environment Structure Diagnosis
"""
import sys

try:
    from schola.core.env import ScholaEnv, AutoResetType
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
except ImportError:
    print("❌ ERROR: schola not installed")
    sys.exit(1)

def diagnose_environment():
    print("=" * 80)
    print("SCHOLA ENVIRONMENT STRUCTURE DIAGNOSIS")
    print("=" * 80)
    print()
    
    # Connect
    print("Step 1: Connecting to UE5...")
    try:
        connection = UnrealEditorConnection(url="localhost", port=50051)
        print("  ✓ Connection established")
    except Exception as e:
        print(f"  ❌ Connection failed: {e}")
        return False
    print()
    
    # Create environment (WITHOUT num_envs parameter)
    print("Step 2: Creating ScholaEnv...")
    try:
        env = ScholaEnv(
            unreal_connection=connection,
            verbosity=1,
            auto_reset_type=AutoResetType.SAME_STEP
        )
        print("  ✓ Environment created")
    except Exception as e:
        print(f"  ❌ Environment creation failed: {e}")
        return False
    print()
    
    # Initialize
    print("Step 3: Initializing environment...")
    try:
        obs = env.hard_reset()
        if isinstance(obs, tuple):
            obs = obs[0]
        print("  ✓ Environment initialized")
    except Exception as e:
        print(f"  ❌ Initialization failed: {e}")
        env.close()
        return False
    print()
    
    # DIAGNOSE
    print("=" * 80)
    print("ENVIRONMENT STRUCTURE FROM UE5")
    print("=" * 80)
    print()
    
    if not hasattr(env, 'ids') or not env.ids:
        print("❌ ERROR: No agent IDs found")
        env.close()
        return False
    
    num_physical_envs = len(env.ids)
    print(f"Number of physical environments: {num_physical_envs}")
    print()
    
    total_agents = 0
    for i, agents in enumerate(env.ids):
        num_agents = len(agents)
        total_agents += num_agents
        print(f"  Environment {i}:")
        print(f"    - Agent count: {num_agents}")
        print(f"    - Agent IDs: {agents}")
    print()
    
    print(f"Total agents: {total_agents}")
    print("=" * 80)
    print()
    
    # EVALUATION
    print("=" * 80)
    print("DIAGNOSIS RESULTS")
    print("=" * 80)
    print()
    
    if num_physical_envs == 4:
        print("✅ CORRECT: 4 physical environments detected")
        print("   → UE5 is configured correctly")
        print("   → Each environment can terminate independently")
        agents_per_env = [len(agents) for agents in env.ids]
        if all(n == 8 for n in agents_per_env):
            print("✅ CORRECT: Each environment has 8 agents")
        else:
            print(f"⚠️  WARNING: Uneven distribution: {agents_per_env}")
    
    elif num_physical_envs == 1:
        print("❌ PROBLEM: Only 1 physical environment detected")
        print(f"   → All {total_agents} agents are in the same environment")
        print("   → Episodes will terminate synchronously (all at once)")
        print()
        print("ROOT CAUSE:")
        print("  UE5 is not creating separate environment instances.")
        print("  Schola receives environment definitions from UE5 via gRPC.")
        print()
        print("SOLUTION:")
        print("  You need to modify UE5 to define 4 separate environments.")
        print("  This is done in UE5 Blueprint/C++, NOT in Python.")
        print()
        print("WHERE TO FIX:")
        print("  1. Find your Schola environment manager in UE5")
        print("     (likely named BP_ScholaCombatEnvironment or similar)")
        print("  2. Check how agents are assigned to environments")
        print("  3. Ensure logical_env_id (0-3) creates separate gRPC environments")
        print()
        print("EXPECTED UE5 STRUCTURE:")
        print("  - 4 environment definitions sent via TrainingDefinition")
        print("  - Each environment should contain 8 agents")
        print("  - Agent IDs 0-7 → Env 0")
        print("  - Agent IDs 8-15 → Env 1")
        print("  - Agent IDs 16-23 → Env 2")
        print("  - Agent IDs 24-31 → Env 3")
    
    else:
        print(f"⚠️  UNEXPECTED: {num_physical_envs} environments detected")
        print(f"   Expected: 4 or 1")
    
    print()
    print("=" * 80)
    env.close()
    
    return num_physical_envs == 4

if __name__ == "__main__":
    success = diagnose_environment()
    sys.exit(0 if success else 1)
