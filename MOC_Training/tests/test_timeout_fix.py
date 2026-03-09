"""
Test script to verify the timeout interceptor fix works.
This simulates the Docker environment setup and tests a simple connection.
"""

import os
import sys

# Simulate Docker environment
os.environ["IS_DOCKER"] = "true"

print("=" * 80)
print("Testing Timeout Interceptor Fix")
print("=" * 80)

try:
    from sbdapm_env import SBDAPMMultiAgentEnv

    print("\n[1/2] Creating environment with Docker mode enabled...")
    env = SBDAPMMultiAgentEnv(
        host="host.docker.internal",
        port=50051,
        timeout=60,
        is_docker=True
    )

    print("[2/2] Testing reset() with extended timeout...")
    obs, info = env.reset()

    print("\n" + "=" * 80)
    print("SUCCESS - Environment connected and reset successfully!")
    print("=" * 80)
    print(f"Agents detected: {len(obs)}")
    print("The timeout interceptor is working correctly.")

    env.close()
    sys.exit(0)

except Exception as e:
    print("\n" + "=" * 80)
    print("FAILED - Environment connection failed")
    print("=" * 80)
    print(f"Error: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)
