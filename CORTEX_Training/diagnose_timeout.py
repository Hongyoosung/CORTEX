"""
Diagnose DEADLINE_EXCEEDED error - find where timeout is being set.
"""
import os
import sys
import grpc
import time
from datetime import datetime

host = os.environ.get("HOST", "host.docker.internal")
port = int(os.environ.get("PORT", "50051"))

print("=" * 80)
print("DEADLINE_EXCEEDED Diagnostic Tool")
print("=" * 80)
print(f"Target: {host}:{port}")
print(f"Time: {datetime.now()}\n")

# Test 1: Raw channel connection (should work)
print("[1/4] Testing raw channel connection...")
try:
    channel = grpc.insecure_channel(
        f'{host}:{port}',
        options=[
            ('grpc.keepalive_time_ms', 30000),
            ('grpc.keepalive_timeout_ms', 10000),
            ('grpc.http2.max_pings_without_data', 2),
            ('grpc.http2.min_time_between_pings_ms', 30000),
        ]
    )

    print("  Waiting for channel ready (120s timeout)...")
    start = time.time()
    grpc.channel_ready_future(channel).result(timeout=120)
    elapsed = time.time() - start
    print(f"  ✓ Channel ready in {elapsed:.2f}s\n")
    channel.close()
except grpc.FutureTimeoutError as e:
    print(f"  ✗ Timeout after 120s: {e}\n")
    print("  UE5 is not responding. Check:")
    print("    1. UE5 is running")
    print("    2. Level is loaded and PLAY is pressed")
    print("    3. Schola gRPC server started (check UE5 Output Log)")
    sys.exit(1)
except Exception as e:
    print(f"  ✗ Connection failed: {e}\n")
    sys.exit(1)

# Test 2: Check Schola timeout configuration
print("[2/4] Checking Schola configuration...")
try:
    import schola
    schola_path = os.path.dirname(schola.__file__)

    # Check base_connection.py
    base_conn_file = os.path.join(schola_path, "core", "unreal_connections", "base_connection.py")
    if os.path.exists(base_conn_file):
        with open(base_conn_file, 'r') as f:
            content = f.read()

        if "DOCKER_PATCHED" in content:
            print("  ✓ Schola is patched for Docker")
        else:
            print("  ✗ Schola NOT patched!")

        if "_TimeoutInterceptor" in content:
            print("  ✓ Timeout interceptor present")
            # Check timeout value
            if "timeout = 120" in content or "timeout=120" in content:
                print("  ✓ Timeout set to 120s")
            elif "timeout = 60" in content or "timeout=60" in content:
                print("  ⚠ Timeout is 60s (might be too short)")
            else:
                print("  ⚠ Timeout value unclear")
        else:
            print("  ✗ Timeout interceptor NOT found")

    # Check env.py for explicit timeouts
    env_file = os.path.join(schola_path, "core", "env.py")
    if os.path.exists(env_file):
        with open(env_file, 'r') as f:
            env_content = f.read()

        # Look for timeout= in function calls
        import re
        timeout_calls = re.findall(r'timeout\s*=\s*(\d+)', env_content)
        if timeout_calls:
            print(f"  ⚠ Found explicit timeouts in env.py: {timeout_calls}")
        else:
            print("  ✓ No explicit timeouts in env.py")

    print()

except ImportError:
    print("  ✗ Schola not installed\n")
    sys.exit(1)

# Test 3: Try Schola connection
print("[3/4] Testing Schola UnrealEditorConnection...")
try:
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection

    print(f"  Connecting to {host}:{port}...")
    start = time.time()

    connection = UnrealEditorConnection(url=host, port=port)

    elapsed = time.time() - start
    print(f"  ✓ Connection created in {elapsed:.2f}s")

    # Try to close
    try:
        connection.close()
        print("  ✓ Connection closed successfully\n")
    except:
        print("  ⚠ Warning: Could not close connection\n")

except grpc.RpcError as e:
    print(f"  ✗ RPC Error: {e}")
    print(f"  Status: {e.code()}")
    print(f"  Details: {e.details()}")

    if e.code() == grpc.StatusCode.DEADLINE_EXCEEDED:
        print("\n  DIAGNOSIS: Timeout is too short!")
        print("  The gRPC call deadline was exceeded before UE5 responded.")
        print("  Possible causes:")
        print("    1. UE5 is slow to initialize Schola (first connection)")
        print("    2. Schola has explicit timeout < 120s")
        print("    3. Timeout interceptor not working")
        print("    4. Network latency (Docker bridge)")

    sys.exit(1)
except Exception as e:
    print(f"  ✗ Failed: {e}\n")
    sys.exit(1)

# Test 4: Try hard_reset (the actual problematic call)
print("[4/4] Testing ScholaEnv.hard_reset() (the critical call)...")
try:
    from schola.core.env import ScholaEnv
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection

    print(f"  Creating ScholaEnv...")
    start = time.time()

    connection = UnrealEditorConnection(url=host, port=port)
    env = ScholaEnv(unreal_connection=connection, verbosity=1)

    elapsed = time.time() - start
    print(f"  ✓ ScholaEnv created in {elapsed:.2f}s")

    # Try hard_reset (this is what sbdapm_env.py calls)
    print("  Testing env.hard_reset() (THIS IS WHERE TIMEOUT HAPPENS)...")
    print("  IMPORTANT: UE5 must be in PLAY mode with agents spawned!")
    start = time.time()

    try:
        obs = env.hard_reset()
        elapsed = time.time() - start
        print(f"  ✓ hard_reset() completed in {elapsed:.2f}s")
        print(f"  ✓ Returned observation")
        print()
    except grpc.RpcError as e:
        elapsed = time.time() - start
        print(f"  ✗ RPC Error after {elapsed:.2f}s")
        print(f"  Status: {e.code()}")
        print(f"  Details: {e.details()}")

        if e.code() == grpc.StatusCode.DEADLINE_EXCEEDED:
            print("\n  DIAGNOSIS: hard_reset() timeout!")
            print("  This RPC call is waiting for UE5 to:")
            print("    1. Spawn all agents")
            print("    2. Initialize environment state")
            print("    3. Return first observations")
            print("\n  Possible fixes:")
            print("    ✓ We already set 120s timeout in interceptor")
            print("    → Check if UE5 level loads agents properly")
            print("    → Check UE5 Output Log for errors during spawn")
            print("    → Try manually spawning agents in editor first")

        raise

except grpc.RpcError as e:
    sys.exit(1)
except Exception as e:
    print(f"  ✗ Failed: {e}\n")
    import traceback
    traceback.print_exc()
    sys.exit(1)

print("=" * 80)
print("SUCCESS - All tests passed!")
print("=" * 80)
print("\nThe connection is working. If training still fails, the issue is elsewhere.")
