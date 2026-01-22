"""
Diagnose agent registration issue in multi-environment setup.
"""

import os
import sys
import time

host = os.environ.get("HOST", "host.docker.internal")
port = int(os.environ.get("PORT", "50051"))

print("=" * 80)
print("Agent Registration Diagnostic")
print("=" * 80)
print(f"Target: {host}:{port}\n")

try:
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
    from schola.core.env import ScholaEnv

    print("[1/3] Connecting to UE5...")
    connection = UnrealEditorConnection(url=host, port=port)
    env = ScholaEnv(unreal_connection=connection, verbosity=1)
    print("  ✓ Connected\n")

    # Check environment structure
    print("[2/3] Checking environment structure...")
    if hasattr(env, 'ids'):
        num_envs = len(env.ids)
        print(f"  ✓ Number of environments: {num_envs}")

        total_agents = 0
        for env_idx, agent_list in enumerate(env.ids):
            num_agents = len(agent_list)
            total_agents += num_agents
            print(f"    - Environment {env_idx}: {num_agents} agents")

        print(f"  ✓ Total agents: {total_agents}")

        # Expected configuration
        expected_envs = 4
        expected_agents_per_env = 8
        expected_total = expected_envs * expected_agents_per_env

        print(f"\n  Expected configuration:")
        print(f"    - Environments: {expected_envs}")
        print(f"    - Agents per environment: {expected_agents_per_env}")
        print(f"    - Total agents: {expected_total}")

        print(f"\n  Status:")
        if num_envs == expected_envs:
            print(f"    ✓ Environment count correct: {num_envs}")
        else:
            print(f"    ✗ Environment count mismatch: {num_envs} (expected {expected_envs})")
            print(f"      → UE5 might have fewer ScholaCombatEnvironment actors")

        if total_agents == expected_total:
            print(f"    ✓ Total agent count correct: {total_agents}")
        else:
            print(f"    ✗ Total agent count mismatch: {total_agents} (expected {expected_total})")

        # Check distribution
        agents_per_env_list = [len(agent_list) for agent_list in env.ids]
        all_equal = all(n == expected_agents_per_env for n in agents_per_env_list)

        if all_equal and num_envs == expected_envs:
            print(f"    ✓ Agent distribution correct: {expected_agents_per_env} per environment")
        else:
            print(f"    ✗ Agent distribution incorrect:")
            for env_idx, num_agents in enumerate(agents_per_env_list):
                status = "✓" if num_agents == expected_agents_per_env else "✗"
                print(f"      {status} Env {env_idx}: {num_agents} agents (expected {expected_agents_per_env})")

    else:
        print("  ✗ Cannot determine environment structure\n")
        sys.exit(1)

    print()

    # Try hard_reset
    print("[3/3] Testing hard_reset()...")
    start = time.time()
    obs = env.hard_reset()
    elapsed = time.time() - start

    print(f"  ✓ hard_reset() completed in {elapsed:.2f}s")
    print()

    connection.close()

    print("=" * 80)
    print("DIAGNOSIS COMPLETE")
    print("=" * 80)

    # Summary
    if num_envs == expected_envs and total_agents == expected_total and all_equal:
        print("\n✓ Configuration looks correct!")
        print("  If training still fails, the issue is elsewhere (check timeout patch)")
    else:
        print("\n✗ Configuration mismatch detected!")
        print("\nLikely causes:")

        if num_envs < expected_envs:
            print(f"  1. Only {num_envs} ScholaCombatEnvironment actors in UE5 (expected {expected_envs})")
            print(f"     → Solution: Place {expected_envs} ScholaCombatEnvironment actors in your map")

        if total_agents != expected_total:
            print(f"  2. Total agent count is {total_agents} (expected {expected_total})")
            if total_agents > expected_total:
                print(f"     → Possible: All environments registering ALL agents (no filtering)")
                print(f"     → Solution: Set TrainingTeamIDs per environment in UE5 Blueprint")
            else:
                print(f"     → Possible: Not enough agents spawned in UE5")
                print(f"     → Solution: Place {expected_total} FollowerCharacter actors")

        if not all_equal:
            print(f"  3. Uneven agent distribution across environments")
            print(f"     → Solution: Use spatial filtering or Team ID filtering")
            print(f"     → Check ScholaCombatEnvironment::DiscoverAgents() in C++")

except Exception as e:
    print(f"✗ Failed: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)
