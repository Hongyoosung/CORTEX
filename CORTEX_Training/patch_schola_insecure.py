"""
Patch Schola gRPC client to use insecure channels in Docker.

This script modifies the Schola package to use insecure gRPC channels
instead of SSL/TLS, which is required for Docker-to-host communication
on Windows using host.docker.internal.

Without this patch, gRPC SSL verification fails because host.docker.internal
doesn't have a valid SSL certificate.
"""

import os
import sys

def patch_schola_grpc():
    """Patch Schola's UnrealEditorConnection to use insecure channels."""

    # Find schola installation directory
    try:
        import schola
        schola_path = os.path.dirname(schola.__file__)
        print(f"[PATCH] Found Schola at: {schola_path}")
    except ImportError:
        print("[PATCH] Schola not installed - skipping patch")
        return

    # Path to UnrealEditorConnection
    connection_file = os.path.join(schola_path, "core", "unreal_connections", "editor_connection.py")

    if not os.path.exists(connection_file):
        print(f"[PATCH] File not found: {connection_file}")
        return

    # Read current content
    with open(connection_file, 'r', encoding='utf-8') as f:
        content = f.read()

    # Check if already patched
    if "insecure_channel" in content:
        print("[PATCH] Already patched - skipping")
        return

    # Apply patch: Replace grpc.secure_channel with grpc.insecure_channel
    original = "grpc.secure_channel"
    patched = "grpc.insecure_channel"

    if original in content:
        content = content.replace(original, patched)

        # Write patched content
        with open(connection_file, 'w', encoding='utf-8') as f:
            f.write(content)

        print(f"[PATCH] Successfully patched {connection_file}")
        print(f"[PATCH] Changed: {original} -> {patched}")
    else:
        print(f"[PATCH] Pattern not found in {connection_file}")
        print(f"[PATCH] Schola version may have changed - manual patch required")

if __name__ == "__main__":
    print("=" * 60)
    print("Patching Schola for Docker insecure channel")
    print("=" * 60)
    patch_schola_grpc()
    print("=" * 60)
    print("Patch complete!")
    print("=" * 60)
