#!/bin/bash
# Docker-based SBDAPM RLlib Training Runner (Linux/macOS)
# Solves Windows multiprocessing (spawn) DLL issues

echo "============================================================"
echo "SBDAPM Docker Training"
echo "============================================================"
echo

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo "ERROR: Docker not found in PATH"
    echo "Please ensure Docker is installed"
    exit 1
fi

# Check Docker daemon
if ! docker info &> /dev/null; then
    echo "ERROR: Docker daemon not running"
    echo "Please start Docker"
    exit 1
fi

echo "Select training mode:"
echo "  1. Single-worker (NUM_WORKERS=0, local only)"
echo "  2. Multi-worker (NUM_WORKERS=4, requires 4 UE instances)"
echo "  3. TensorBoard only (monitoring)"
echo

read -p "Enter choice (1-3): " MODE

case $MODE in
    1)
        echo "Starting single-worker training..."
        docker-compose --profile single up --build
        ;;
    2)
        echo "Starting multi-worker training..."
        echo "NOTE: Ensure 4 UE instances are running on ports 50051-50054"
        docker-compose --profile multi up --build
        ;;
    3)
        echo "Starting TensorBoard..."
        echo "Open http://localhost:6006 in your browser"
        docker-compose --profile monitor up --build
        ;;
    *)
        echo "Invalid choice"
        exit 1
        ;;
esac

echo
echo "Training completed!"
