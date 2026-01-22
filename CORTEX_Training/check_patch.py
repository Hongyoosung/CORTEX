"""Check if Schola patch was applied correctly."""
import os
import schola

path = os.path.join(os.path.dirname(schola.__file__), 'core', 'unreal_connections', 'base_connection.py')
content = open(path).read()

# Check for patch marker
if "DOCKER_PATCHED" in content:
    print("[✓] Patch marker found")
else:
    print("[✗] Patch marker NOT found")

# Check for timeout interceptor
if "_TimeoutInterceptor" in content:
    print("[✓] Timeout interceptor found")
else:
    print("[✗] Timeout interceptor NOT found")

# Check for keepalive options
if "grpc.keepalive_time_ms" in content:
    print("[✓] Keepalive options found")
else:
    print("[✗] Keepalive options NOT found")

# Print start() method
start_idx = content.find('def start(self)')
if start_idx != -1:
    print("\n" + "="*60)
    print("start() method:")
    print("="*60)
    print(content[start_idx:start_idx+1500])
