# AUTO-GENERATED FROM C++ RL/RLTypes.h - DO NOT EDIT MANUALLY
# Run: python tools/sync_config_from_cpp.py
# Last synced: 2026-01-09T17:31:10.533682

class RLConfig:
    """RL training configuration (synced from C++)

    CRITICAL: Values must match C++ RLConfig namespace exactly.
    Any drift will cause trained models to fail in-game.
    """

    AGENT_WALK_SPEED = 600.0
    AGENT_RUN_SPEED = 900.0
    AGENT_SPRINT_SPEED = 1200.0
    PERCEPTION_RADIUS = 3000.0
    RAYCAST_COUNT = 16
    RAYCAST_LENGTH = 2000.0
    RAYCAST_ANGLE_SPREAD = 180.0
    BASE_DAMAGE = 10.0
    MAX_HEALTH = 100.0
    FIRE_RATE = 0.1
    MAX_DISTANCE_NORMALIZATION = 5000.0
    MAX_VELOCITY_NORMALIZATION = 1200.0
    NUM_STRATEGIES = 4
    NUM_TARGETS = 11
    OBSERVATION_SIZE = 68

