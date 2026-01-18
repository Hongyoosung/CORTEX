"""
Patch Schola gRPC client for Docker compatibility.

This script modifies the Schola package to:
1. Use insecure gRPC channels (required for host.docker.internal)
2. Add extended timeout configuration (Docker networking is slower)
3. Add keepalive options (prevent connection drops)

Without this patch:
- gRPC SSL verification fails (host.docker.internal has no valid cert)
- Default timeouts are too short for Docker networking overhead
"""

import os
import sys
import re

def patch_schola_grpc():
    """Patch Schola's UnrealEditorConnection for Docker."""

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
    if "DOCKER_PATCHED" in content:
        print("[PATCH] Already patched - skipping")
        return

    patches_applied = []

    # Patch 1: Replace grpc.secure_channel with grpc.insecure_channel
    original_channel = "grpc.secure_channel"
    patched_channel = "grpc.insecure_channel"

    if original_channel in content:
        content = content.replace(original_channel, patched_channel)
        patches_applied.append(f"secure_channel -> insecure_channel")

    # Patch 2: Add extended timeout and keepalive options
    # Find the channel creation line and add options
    channel_pattern = r'(grpc\.insecure_channel\(["\']?[^,)]+["\']?)\)'

    channel_replacement = r'''\1,
        options=[
            # Docker networking requires longer timeouts
            ('grpc.keepalive_time_ms', 10000),
            ('grpc.keepalive_timeout_ms', 5000),
            ('grpc.http2.max_pings_without_data', 0),
            ('grpc.http2.min_time_between_pings_ms', 10000),
            ('grpc.max_connection_idle_ms', 300000),
            ('grpc.max_connection_age_ms', 600000),
        ]
    )'''

    if re.search(channel_pattern, content):
        content = re.sub(channel_pattern, channel_replacement, content)
        patches_applied.append("added timeout and keepalive options")

    # Patch 3: Add Docker patch marker (comment at top of file)
    docker_marker = "# DOCKER_PATCHED: Modified for Docker compatibility (insecure channel + extended timeouts)\n"
    if not content.startswith(docker_marker):
        content = docker_marker + content
        patches_applied.append("added patch marker")

    # Write patched content
    if patches_applied:
        with open(connection_file, 'w', encoding='utf-8') as f:
            f.write(content)

        print(f"[PATCH] Successfully patched {connection_file}")
        for patch in patches_applied:
            print(f"[PATCH]   - {patch}")
    else:
        print(f"[PATCH] No patches applied - Schola version may have changed")
        print(f"[PATCH] Manual review required")

if __name__ == "__main__":
    print("=" * 60)
    print("Patching Schola for Docker insecure channel")
    print("=" * 60)
    patch_schola_grpc()
    print("=" * 60)
    print("Patch complete!")
    print("=" * 60)
