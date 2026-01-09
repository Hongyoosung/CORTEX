"""
Test Ray initialization on Windows

This script tests if Ray can start successfully before running full training.
"""

import os
import sys
import tempfile

print("Testing Ray initialization on Windows...")
print("=" * 60)

# Step 1: Check Ray installation
try:
    import ray
    print(f"✅ Ray version: {ray.__version__}")
except ImportError as e:
    print(f"❌ Ray not installed: {e}")
    print("Install with: pip install ray[rllib]")
    sys.exit(1)

# Step 2: Clean up any existing Ray processes
print("\nCleaning up existing Ray processes...")
try:
    ray.shutdown()
    print("✅ Cleaned up existing Ray processes")
except Exception as e:
    print(f"ℹ️  No existing Ray processes to clean up")

# Step 3: Create temp directory
ray_temp_dir = os.path.join(tempfile.gettempdir(), "ray_test")
os.makedirs(ray_temp_dir, exist_ok=True)
print(f"\nUsing temp directory: {ray_temp_dir}")

# Step 4: Initialize Ray with Windows-friendly settings
print("\nInitializing Ray (this may take 10-30 seconds)...")
try:
    ray.init(
        ignore_reinit_error=True,
        include_dashboard=False,
        _temp_dir=ray_temp_dir,
        num_cpus=2,
        object_store_memory=500 * 1024**2,  # 500MB
        _system_config={
            "gcs_rpc_server_reconnect_timeout_s": 300,
        },
        logging_level="ERROR",
    )
    print("✅ Ray initialized successfully!")

    # Step 5: Test basic Ray functionality
    print("\nTesting Ray remote functions...")

    @ray.remote
    def test_func(x):
        return x * 2

    result = ray.get(test_func.remote(21))
    assert result == 42, f"Expected 42, got {result}"
    print(f"✅ Ray remote execution works! (test_func(21) = {result})")

    # Step 6: Shutdown
    print("\nShutting down Ray...")
    ray.shutdown()
    print("✅ Ray shutdown successfully!")

    print("\n" + "=" * 60)
    print("SUCCESS: Ray is working correctly on your system!")
    print("You can now run train_rllib.py")
    print("=" * 60)

except Exception as e:
    print(f"\n❌ Ray initialization failed!")
    print(f"Error: {e}")
    print("\nTroubleshooting steps:")
    print("1. Check Windows Firewall (allow Python)")
    print("2. Run as Administrator")
    print("3. Check antivirus is not blocking Ray")
    print("4. Try: pip install --upgrade ray")
    print("5. Restart your computer")
    sys.exit(1)
