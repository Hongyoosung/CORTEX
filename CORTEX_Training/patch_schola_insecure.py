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

    # Path to BaseUnrealConnection (where start() method is)
    connection_file = os.path.join(schola_path, "core", "unreal_connections", "base_connection.py")

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

    # Patch 2: Replace the entire start() method to add timeout interceptor
    start_method_pattern = r'(    def start\(self\) -> None:.*?""".*?""")\s+self\.channel = grpc\.insecure_channel\(\s+self\.address.*?\).__enter__\(\)'

    start_method_replacement = r'''\1
        # Create channel with extended timeout for Docker networking
        import grpc

        # Multi-type timeout interceptor (handles all RPC types)
        class _TimeoutInterceptor(
            grpc.UnaryUnaryClientInterceptor,
            grpc.UnaryStreamClientInterceptor,
            grpc.StreamUnaryClientInterceptor,
            grpc.StreamStreamClientInterceptor
        ):
            """Add default 120s timeout to all RPC calls (Docker networking is slow)"""
            def _add_timeout(self, client_call_details):
                # Use 120s timeout for Docker (UE5 initialization can be slow)
                timeout = 120
                if client_call_details.timeout is None or client_call_details.timeout < timeout:
                    return client_call_details._replace(timeout=timeout)
                return client_call_details

            def intercept_unary_unary(self, continuation, client_call_details, request):
                return continuation(self._add_timeout(client_call_details), request)

            def intercept_unary_stream(self, continuation, client_call_details, request):
                return continuation(self._add_timeout(client_call_details), request)

            def intercept_stream_unary(self, continuation, client_call_details, request_iterator):
                return continuation(self._add_timeout(client_call_details), request_iterator)

            def intercept_stream_stream(self, continuation, client_call_details, request_iterator):
                return continuation(self._add_timeout(client_call_details), request_iterator)

        # Create channel with keepalive options (conservative settings to prevent "Too many pings")
        base_channel = grpc.insecure_channel(
            self.address,
            options=[
                ('grpc.keepalive_time_ms', 30000),              # Ping every 30s (reduced frequency)
                ('grpc.keepalive_timeout_ms', 10000),           # Wait 10s for ping response
                ('grpc.http2.max_pings_without_data', 2),       # Max 2 pings without data (was 0=unlimited)
                ('grpc.http2.min_time_between_pings_ms', 30000), # Minimum 30s between pings
                ('grpc.max_connection_idle_ms', 300000),        # 5 min idle timeout
                ('grpc.max_connection_age_ms', 600000),         # 10 min max connection age
                ('grpc.initial_reconnect_backoff_ms', 1000),    # Start reconnect backoff at 1s
                ('grpc.max_reconnect_backoff_ms', 10000),       # Max reconnect backoff 10s
            ]
        )

        # Apply timeout interceptor
        self.channel = grpc.intercept_channel(base_channel, _TimeoutInterceptor()).__enter__()'''

    if re.search(start_method_pattern, content, re.DOTALL):
        content = re.sub(start_method_pattern, start_method_replacement, content, flags=re.DOTALL)
        patches_applied.append("replaced start() method with timeout interceptor")
    else:
        # Fallback: Just add options if pattern doesn't match
        channel_pattern = r'(grpc\.insecure_channel\(["\']?[^,)]+["\']?)\)'
        channel_replacement = r'''\1,
            options=[
                ('grpc.keepalive_time_ms', 30000),
                ('grpc.keepalive_timeout_ms', 10000),
                ('grpc.http2.max_pings_without_data', 2),
                ('grpc.http2.min_time_between_pings_ms', 30000),
                ('grpc.max_connection_idle_ms', 300000),
                ('grpc.max_connection_age_ms', 600000),
                ('grpc.initial_reconnect_backoff_ms', 1000),
                ('grpc.max_reconnect_backoff_ms', 10000),
            ]
        )'''
        if re.search(channel_pattern, content):
            content = re.sub(channel_pattern, channel_replacement, content)
            patches_applied.append("added keepalive options (fallback)")

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
