#!/bin/bash
# v6.0 Training Pipeline for Linux/Mac
# Ensures config is synced from C++ before training

set -e  # Exit on error

echo "==================================="
echo "CORTEX v6.0 Training Pipeline"
echo "==================================="
echo ""

# Step 1: Sync configuration from C++
echo "[1/2] Syncing config from C++..."
python tools/sync_config_from_cpp.py
if [ $? -ne 0 ]; then
    echo ""
    echo "[ERROR] Config sync failed! Training aborted."
    echo "Please check that RLTypes.h exists and is valid."
    exit 1
fi
echo ""

# Step 2: Start training
echo "[2/2] Starting training..."
echo "Training arguments: $@"
echo ""

# Check if specific training script was specified
if [ $# -eq 0 ]; then
    echo "Using default training script: train_rllib.py"
    python train_rllib.py
else
    python "$@"
fi

if [ $? -ne 0 ]; then
    echo ""
    echo "[ERROR] Training failed!"
    exit 1
fi

echo ""
echo "==================================="
echo "Training completed successfully!"
echo "==================================="
