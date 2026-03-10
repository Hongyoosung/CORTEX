"""
Test the patch script locally to verify it works correctly.
This simulates what happens during Docker build.
"""

import os
import sys
import tempfile
import shutil

# Create sample base_connection.py (what gets patched)
base_connection_code = '''# Copyright notice

import grpc

class BaseUnrealConnection:
    def __init__(self, url: str, port: int):
        self.url = url
        self.port = port
        self.channel = None

    def start(self) -> None:
        """
        Open the Connection to Unreal Engine.
        """
        self.channel = grpc.secure_channel(
            self.address, grpc.local_channel_credentials()
        ).__enter__()

    @property
    def address(self) -> str:
        return self.url + ":" + str(self.port)
'''

# Create sample editor_connection.py (so patch can find it)
editor_connection_code = '''from schola.core.unreal_connections.base_connection import BaseUnrealConnection

class UnrealEditorConnection(BaseUnrealConnection):
    def __init__(self, url: str, port: int):
        super().__init__(url, port)
'''

print("=" * 80)
print("Testing Patch Script Locally")
print("=" * 80)

# Create temp directory
with tempfile.TemporaryDirectory() as tmpdir:
    # Create fake schola structure
    schola_dir = os.path.join(tmpdir, "schola", "core", "unreal_connections")
    os.makedirs(schola_dir, exist_ok=True)

    # Write both files
    base_file = os.path.join(schola_dir, "base_connection.py")
    editor_file = os.path.join(schola_dir, "editor_connection.py")

    with open(base_file, 'w') as f:
        f.write(base_connection_code)

    with open(editor_file, 'w') as f:
        f.write(editor_connection_code)

    test_file = base_file  # Patch targets base_connection.py (where start() is)

    print(f"\n[1/4] Created test files in: {schola_dir}")
    print(f"[1/4] Patch will modify: base_connection.py")
    print("[1/4] Original start() method:")
    print("-" * 40)
    with open(test_file, 'r') as f:
        print(f.read())

    # Temporarily modify sys.path to find our fake schola
    sys.path.insert(0, tmpdir)

    # Create __init__.py to make it a package
    with open(os.path.join(tmpdir, "schola", "__init__.py"), 'w') as f:
        f.write("")

    # Import and run patch - need to manually call the function
    print("\n[2/4] Running patch script...")
    import patch_schola_insecure
    patch_schola_insecure.patch_schola_grpc()

    # Check patched content
    print("\n[3/4] Patched content:")
    print("-" * 40)
    with open(test_file, 'r') as f:
        patched = f.read()
        print(patched)

    # Verify patches
    print("\n[4/4] Verification:")
    print("-" * 40)

    success = True
    if "DOCKER_PATCHED" in patched:
        print("✓ Patch marker added")
    else:
        print("✗ Patch marker missing")
        success = False

    if "grpc.insecure_channel" in patched and "grpc.secure_channel" not in patched:
        print("✓ Changed to insecure_channel")
    else:
        print("✗ Still using secure_channel")
        success = False

    if "_TimeoutInterceptor" in patched:
        print("✓ Timeout interceptor added")
    else:
        print("✗ Timeout interceptor missing")
        success = False

    if "grpc.intercept_channel" in patched:
        print("✓ Interceptor applied to channel")
    else:
        print("✗ Interceptor not applied")
        success = False

    print("\n" + "=" * 80)
    if success:
        print("SUCCESS - All patches applied correctly!")
        print("The fix should work when Docker image is rebuilt.")
    else:
        print("FAILED - Some patches missing")
        print("The regex patterns may need adjustment.")
    print("=" * 80)

    sys.path.remove(tmpdir)
