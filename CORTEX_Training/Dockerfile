# Multi-Agent Combat AI Training Environment
# Solves Windows multiprocessing (spawn) DLL issues with Ray RLlib

FROM python:3.10-slim

# Install system dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy requirements first for layer caching
COPY CORTEX_Training/requirements.txt .

# Install Python dependencies (without schola)
RUN pip install --no-cache-dir -r requirements.txt

# Copy Schola Python package from plugin directory
COPY Plugins/Schola-1.3.0/Resources/python /tmp/schola

# Install Schola with rllib extras from local source
RUN echo "Schola Placeholder" > /README.md && \
    pip install --no-cache-dir /tmp/schola[rllib] && \
    rm -rf /tmp/schola /README.md

# Copy training scripts
COPY CORTEX_Training/ .

# Patch Schola for Docker insecure channel compatibility
RUN python patch_schola_insecure.py

# Expose TensorBoard port
EXPOSE 6006

# Expose Schola gRPC ports (50051-50054 for multi-worker)
EXPOSE 50051 50052 50053 50054

# Default command: run training
CMD ["python", "train_rllib.py"]
