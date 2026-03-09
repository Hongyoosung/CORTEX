"""
Simple TCP connection test for Docker -> UE5.
Tests if Docker can reach the UE5 Schola gRPC server.
"""

import socket
import sys
import os

# Get connection params from environment or use defaults
host = os.environ.get("HOST", "host.docker.internal")
port = int(os.environ.get("PORT", "50051"))

print("=" * 60)
print("Docker -> UE5 Connection Test")
print("=" * 60)

# Step 1: DNS resolution
print(f"\n[1/2] Resolving hostname '{host}'...")
try:
    ip = socket.gethostbyname(host)
    print(f"  ✓ Resolved to {ip}")
except Exception as e:
    print(f"  ✗ DNS resolution failed: {e}")
    sys.exit(1)

# Step 2: TCP connection
print(f"\n[2/2] Testing TCP connection to {host}:{port}...")
try:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(10)
    result = sock.connect_ex((host, port))
    sock.close()

    if result == 0:
        print(f"  ✓ TCP connection successful!")
        print("\n" + "=" * 60)
        print("SUCCESS - Docker can reach UE5")
        print("=" * 60)
        print("\nYou can now run training with option 1 or 2")
        sys.exit(0)
    else:
        print(f"  ✗ TCP connection failed (error code: {result})")
        print("\n" + "=" * 60)
        print("FAILED - Cannot reach UE5")
        print("=" * 60)
        print("\nTroubleshooting:")
        print("1. Make sure UE5 is running")
        print("2. Check DefaultEngine.ini has:")
        print("   CommunicatorSettings=(Address=\"0.0.0.0\",Port=50051,Timeout=60)")
        print("3. Restart UE5 after config changes")
        print("4. Check UE5 console shows: [Schola] gRPC server started on port 50051")
        sys.exit(1)

except Exception as e:
    print(f"  ✗ Connection error: {e}")
    print("\n" + "=" * 60)
    print("ERROR")
    print("=" * 60)
    import traceback
    traceback.print_exc()
    sys.exit(1)
