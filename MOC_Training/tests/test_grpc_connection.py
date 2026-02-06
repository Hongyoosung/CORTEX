"""
Test gRPC connection to UE5 with detailed diagnostics.
Tests both insecure and secure channels to identify the issue.
"""

import os
import sys
import grpc
from grpc import StatusCode

host = os.environ.get("HOST", "host.docker.internal")
port = int(os.environ.get("PORT", "50051"))

print("=" * 60)
print("gRPC Connection Diagnostics")
print("=" * 60)
print(f"Target: {host}:{port}\n")

# Test 1: Insecure channel with short timeout
print("[1/3] Testing insecure channel (5s timeout)...")
try:
    channel = grpc.insecure_channel(f'{host}:{port}')
    grpc.channel_ready_future(channel).result(timeout=5)
    print("  ✓ Insecure channel connected successfully!\n")
    channel.close()
except grpc.FutureTimeoutError:
    print("  ✗ Timeout after 5 seconds")
    print("  This suggests gRPC server is slow to respond or not configured properly\n")
except Exception as e:
    print(f"  ✗ Connection failed: {e}\n")

# Test 2: Insecure channel with longer timeout
print("[2/3] Testing insecure channel (30s timeout)...")
try:
    channel = grpc.insecure_channel(
        f'{host}:{port}',
        options=[
            ('grpc.keepalive_time_ms', 30000),              # Ping every 30s
            ('grpc.keepalive_timeout_ms', 10000),           # Wait 10s for response
            ('grpc.http2.max_pings_without_data', 2),       # Max 2 pings without data
            ('grpc.http2.min_time_between_pings_ms', 30000), # Min 30s between pings
        ]
    )
    grpc.channel_ready_future(channel).result(timeout=30)
    print("  ✓ Insecure channel connected (with extended timeout)!\n")
    channel.close()

    print("=" * 60)
    print("SUCCESS - gRPC works with longer timeout!")
    print("=" * 60)
    print("\nThe issue is timeout configuration.")
    print("Schola's default timeout is too short for Docker networking.")
    sys.exit(0)

except grpc.FutureTimeoutError:
    print("  ✗ Still timeout after 30 seconds")
    print("  This suggests UE5's gRPC server is not responding\n")
except Exception as e:
    print(f"  ✗ Connection failed: {e}\n")

# Test 3: Check if Schola is trying to use secure channel
print("[3/3] Checking Schola installation...")
try:
    import schola
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection

    schola_path = os.path.dirname(schola.__file__)
    connection_file = os.path.join(schola_path, "core", "unreal_connections", "editor_connection.py")

    with open(connection_file, 'r') as f:
        content = f.read()

    if 'insecure_channel' in content:
        print("  ✓ Schola is patched to use insecure channels")
    else:
        print("  ✗ Schola is NOT patched - still using secure_channel!")
        print("  This will cause SSL verification failures")

except ImportError:
    print("  ✗ Schola not installed")
except Exception as e:
    print(f"  ✗ Error checking Schola: {e}")

print("\n" + "=" * 60)
print("DIAGNOSTICS COMPLETE")
print("=" * 60)
