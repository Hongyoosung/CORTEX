"""
Patch Schola to use insecure gRPC channels for Docker compatibility.

This script modifies the installed Schola package to use grpc.insecure_channel
instead of grpc.secure_channel with local_channel_credentials(), which doesn't
work across Docker network boundaries.
"""

import os
import sys

def patch_schola():
    """Patch Schola's base_connection.py to use insecure channels."""

    # Find Schola installation
    try:
        import schola
        schola_path = os.path.dirname(schola.__file__)
        target_file = os.path.join(schola_path, "core", "unreal_connections", "base_connection.py")

        print(f"Found Schola at: {schola_path}")
        print(f"Patching: {target_file}")

        if not os.path.exists(target_file):
            print(f"ERROR: File not found: {target_file}")
            sys.exit(1)

        # Read file
        with open(target_file, 'r') as f:
            content = f.read()

        # Check if already patched
        if 'grpc.insecure_channel' in content:
            print("Already patched - skipping")
            return

        # Patch: Replace secure_channel with insecure_channel
        original = """    def start(self) -> None:
        \"\"\"
        Open the Connection to Unreal Engine.
        \"\"\"
        self.channel = grpc.secure_channel(
            self.address, grpc.local_channel_credentials()
        ).__enter__()"""

        patched = """    def start(self) -> None:
        \"\"\"
        Open the Connection to Unreal Engine.
        \"\"\"
        # PATCHED: Use insecure_channel for Docker compatibility
        # Original used: grpc.secure_channel(self.address, grpc.local_channel_credentials())
        options = [
            ('grpc.max_receive_message_length', 100 * 1024 * 1024),
            ('grpc.max_send_message_length', 100 * 1024 * 1024),
            ('grpc.keepalive_time_ms', 30000),
            ('grpc.keepalive_timeout_ms', 10000),
        ]
        self.channel = grpc.insecure_channel(self.address, options).__enter__()"""

        if original in content:
            content = content.replace(original, patched)

            # Write back
            with open(target_file, 'w') as f:
                f.write(content)

            print("✓ Patch applied successfully!")
            print("\nChanges:")
            print("  - grpc.secure_channel → grpc.insecure_channel")
            print("  - Added keepalive options for better timeout handling")
        else:
            print("ERROR: Could not find code to patch")
            print("The file may have been updated. Manual patching required.")
            sys.exit(1)

    except ImportError:
        print("ERROR: Schola not installed")
        sys.exit(1)

if __name__ == "__main__":
    print("=" * 60)
    print("Schola Docker Compatibility Patch")
    print("=" * 60)
    patch_schola()
    print("\nSchola is now compatible with Docker networking!")
