"""
Diagnose why training fails but connection test succeeds.

This script replicates the exact environment creation process used by train_rllib.py
to identify where the timeout occurs.
"""

import os
import sys
import time
from datetime import datetime

host = os.environ.get("HOST", "host.docker.internal")
port = int(os.environ.get("PORT", "50051"))

print("=" * 80)
print("Training Failure Diagnostic")
print("=" * 80)
print(f"Target: {host}:{port}")
print(f"Time: {datetime.now()}\n")

# Test 1: Verify Schola patch
print("[1/5] Verifying Schola patch...")
try:
    import schola
    schola_path = os.path.dirname(schola.__file__)
    base_conn_file = os.path.join(schola_path, "core", "unreal_connections", "base_connection.py")

    if os.path.exists(base_conn_file):
        with open(base_conn_file, 'r') as f:
            content = f.read()

        if "DOCKER_PATCHED" in content:
            print("  ✓ Schola is patched")
            if "timeout = 120" in content or "timeout=120" in content:
                print("  ✓ Timeout set to 120s")
            else:
                print("  ⚠ Timeout value might not be 120s")
        else:
            print("  ✗ Schola NOT patched!")
            print("  Run: docker-compose build training-single")
            sys.exit(1)
    print()
except Exception as e:
    print(f"  ✗ Failed to check patch: {e}\n")
    sys.exit(1)

# Test 2: Check how many environments UE5 has
print("[2/5] Checking UE5 environment configuration...")
try:
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
    from schola.core.env import ScholaEnv

    print(f"  Connecting to {host}:{port}...")
    connection = UnrealEditorConnection(url=host, port=port)
    env = ScholaEnv(unreal_connection=connection, verbosity=1)

    print(f"  ✓ Connected")

    # Check number of environments
    if hasattr(env, 'ids'):
        num_envs = len(env.ids)
        print(f"  ✓ UE5 has {num_envs} environment(s)")

        # Check agents per environment
        for env_idx, agent_list in enumerate(env.ids):
            print(f"    - Environment {env_idx}: {len(agent_list)} agents")
    else:
        print("  ⚠ Cannot determine number of environments")
        num_envs = None

    print()

    # Clean up
    try:
        connection.close()
    except:
        pass

except Exception as e:
    print(f"  ✗ Connection failed: {e}\n")
    sys.exit(1)

# Test 3: Test hard_reset() with timing
print("[3/5] Testing hard_reset() with timing...")
try:
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
    from schola.core.env import ScholaEnv

    print(f"  Creating connection...")
    connection = UnrealEditorConnection(url=host, port=port)
    env = ScholaEnv(unreal_connection=connection, verbosity=1)

    print(f"  Calling hard_reset()...")
    start = time.time()
    obs = env.hard_reset()
    elapsed = time.time() - start

    print(f"  ✓ hard_reset() completed in {elapsed:.2f}s")
    print(f"  ✓ Observation type: {type(obs)}")

    if isinstance(obs, dict):
        print(f"  ✓ Number of environments in observation: {len(obs)}")

    print()

    try:
        connection.close()
    except:
        pass

except Exception as e:
    print(f"  ✗ hard_reset() failed: {e}")
    import traceback
    traceback.print_exc()
    print()
    sys.exit(1)

# Test 4: Test environment creation with num_envs=4
print("[4/5] Testing environment creation with num_envs=4 (training config)...")
try:
    from sbdapm_env import SBDAPMMultiAgentEnv

    env_config = {
        "host": host,
        "base_port": port,
        "num_envs": 4,  # This is what train_rllib.py uses
        "is_docker": True,
        "timeout": 60,
    }

    print(f"  Creating SBDAPMMultiAgentEnv with config:")
    print(f"    - host: {env_config['host']}")
    print(f"    - port: {env_config['base_port']}")
    print(f"    - num_envs: {env_config['num_envs']}")
    print(f"    - timeout: {env_config['timeout']}s")

    start = time.time()
    env = SBDAPMMultiAgentEnv(**env_config)
    elapsed = time.time() - start

    print(f"  ✓ Environment created in {elapsed:.2f}s")

    # Try reset
    print(f"  Calling reset()...")
    start = time.time()
    obs, info = env.reset()
    elapsed = time.time() - start

    print(f"  ✓ reset() completed in {elapsed:.2f}s")
    print(f"  ✓ Number of agents: {len(obs)}")
    print()

    env.close()

except Exception as e:
    elapsed = time.time() - start
    print(f"  ✗ Failed after {elapsed:.2f}s")
    print(f"  Error: {e}")
    import traceback
    traceback.print_exc()
    print()

    if "DEADLINE_EXCEEDED" in str(e):
        print("\n  DIAGNOSIS: DEADLINE_EXCEEDED during training environment creation")
        print("  This is the same error as train_rllib.py!")
        print("\n  Possible causes:")
        print("    1. UE5 configured for 1 environment, but Python expects 4")
        print("    2. hard_reset() takes longer when called via RLlib environment")
        print("    3. Timeout interceptor not applying to specific RPC calls")
        print("\n  Recommended fix:")
        print("    - Set NUM_ENVS_PER_WORKER=1 in train_rllib.py line 218")
        print("    - Or configure UE5 to spawn 4 separate environments")

    sys.exit(1)

# Test 5: Test with num_envs=1
print("[5/5] Testing with num_envs=1 (fallback)...")
try:
    from sbdapm_env import SBDAPMMultiAgentEnv

    env_config = {
        "host": host,
        "base_port": port,
        "num_envs": 1,  # Try with single environment
        "is_docker": True,
        "timeout": 60,
    }

    print(f"  Creating SBDAPMMultiAgentEnv with num_envs=1...")
    start = time.time()
    env = SBDAPMMultiAgentEnv(**env_config)
    elapsed = time.time() - start

    print(f"  ✓ Environment created in {elapsed:.2f}s")

    print(f"  Calling reset()...")
    start = time.time()
    obs, info = env.reset()
    elapsed = time.time() - start

    print(f"  ✓ reset() completed in {elapsed:.2f}s")
    print(f"  ✓ Number of agents: {len(obs)}")
    print()

    env.close()

    print("="*80)
    print("SUCCESS with num_envs=1!")
    print("="*80)
    print("\nRECOMMENDATION:")
    print("  Edit train_rllib.py line 218:")
    print("    NUM_ENVS_PER_WORKER = 1  # Changed from 4")
    print("\n  Or run multiple UE5 instances for parallel training")

except Exception as e:
    elapsed = time.time() - start
    print(f"  ✗ Still failed after {elapsed:.2f}s: {e}\n")
    import traceback
    traceback.print_exc()
    sys.exit(1)
