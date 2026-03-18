# DE Training Environment (Linux)
# Solves Windows + Ray/RLlib compatibility issues by running in Linux container
# UE5 runs on Windows host, Python training runs in Docker container
#
# Architecture:
#   - UE5 (Windows host) :50051 ←→ gRPC ←→ Docker (Linux) Python/RLlib
#   - Ray uses 'fork' multiprocessing (Linux-only, more efficient than Windows 'spawn')
#   - Eliminates object store corruption and worker deadlock issues
#   - Supports Commander-Executor architecture with multi-head policy training

FROM python:3.10-slim

# Install system dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    curl \
    procps \
    htop \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy requirements first for layer caching
COPY DE_Training/requirements.txt .

# Install Python dependencies
RUN pip install --no-cache-dir -r requirements.txt


# Schola 복사 전 확인
COPY Plugins/Schola-2.0.1 /tmp/Schola-2.0.1
RUN ls -la /tmp/Schola-2.0.1/ || (echo "Schola 복사 실패" && exit 1)

RUN touch /tmp/Schola-2.0.1/README.md

# Run the folder containing setup.py with pip install (install including dependencies)
RUN pip install --no-cache-dir /tmp/Schola-2.0.1/Resources/python[rllib] && \
    rm -rf /tmp/Schola-2.0.1

# Copy the rest of the learning script
COPY DE_Training/ .


# Verify installation
RUN python -c "import ray; import torch; import schola; print('✓ All packages installed')"

# Expose TensorBoard port
EXPOSE 6006

# Expose Schola gRPC ports (50051-50054 for multi-worker)
EXPOSE 50051 50052 50053 50054

# Environment variables (can be overridden in docker-compose.yml)
ENV NUM_WORKERS=1
ENV NUM_ITERATIONS=100
ENV HOST=host.docker.internal
ENV PORT=50051

# Health check: verify gRPC connection to UE5
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD python -c "import grpc; channel = grpc.insecure_channel('${HOST}:${PORT}'); grpc.channel_ready_future(channel).result(timeout=5)" || exit 1

# Default command: run training
CMD ["python", "training/train.py", "--mode", "rllib"]
