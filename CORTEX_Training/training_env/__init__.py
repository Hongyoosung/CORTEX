"""
CORTEX v6.0 Training Environment Configuration

This package contains auto-generated configuration synced from C++ RLConfig namespace.

CRITICAL: Always run sync script before training to ensure config matches C++ runtime:
    python tools/sync_config_from_cpp.py

This prevents sim2real drift that would cause trained models to fail in-game.
"""

from .config import RLConfig

__all__ = ['RLConfig']
