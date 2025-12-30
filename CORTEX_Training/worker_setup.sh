#!/bin/bash
# CORTEX Worker Instance Setup Script
# Run this on the base EC2 instance to prepare it for AMI creation
#
# Usage: ./worker_setup.sh <S3_BUCKET_NAME>

set -e

if [ -z "$1" ]; then
    echo "Error: S3 bucket name required"
    echo "Usage: ./worker_setup.sh <S3_BUCKET_NAME>"
    exit 1
fi

BUCKET_NAME=$1

echo "============================================================"
echo "CORTEX Worker Setup"
echo "============================================================"
echo ""
echo "S3 Bucket: $BUCKET_NAME"
echo ""

# Update system
echo "[1/7] Updating system packages..."
sudo apt-get update
sudo apt-get upgrade -y

# Install system dependencies
echo ""
echo "[2/7] Installing system dependencies..."
sudo apt-get install -y \
    build-essential \
    python3.10 \
    python3-pip \
    unzip \
    curl \
    wget \
    libgl1-mesa-glx \
    libglu1-mesa \
    libxcursor1 \
    libxi6 \
    libxrandr2 \
    libxxf86vm1 \
    libopenal1 \
    libvulkan1 \
    mesa-vulkan-drivers \
    net-tools \
    htop

# Install AWS CLI
echo ""
echo "[3/7] Installing AWS CLI..."
if ! command -v aws &> /dev/null; then
    curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
    unzip awscliv2.zip
    sudo ./aws/install
    rm -rf aws awscliv2.zip
    echo "  AWS CLI installed"
else
    echo "  AWS CLI already installed"
fi

# Install Python dependencies
echo ""
echo "[4/7] Installing Python dependencies..."
pip3 install --upgrade pip
pip3 install numpy torch

# Create game directory
echo ""
echo "[5/7] Downloading game build from S3..."
mkdir -p /home/ubuntu/game
cd /home/ubuntu/game

# Download game build
if [ ! -f "GameAI_Project_Linux.zip" ]; then
    aws s3 cp "s3://$BUCKET_NAME/builds/GameAI_Project_Linux.zip" ./
    echo "  Download complete"
else
    echo "  Build already downloaded"
fi

# Extract game
echo ""
echo "[6/7] Extracting game build..."
if [ ! -d "GameAI_Project" ]; then
    unzip GameAI_Project_Linux.zip
    rm GameAI_Project_Linux.zip
    echo "  Extraction complete"
else
    echo "  Build already extracted"
fi

# Set permissions
echo "  Setting permissions..."
chmod +x GameAI_Project/GameAI_Project.sh
chmod +x GameAI_Project/Binaries/Linux/GameAI_Project

# Install Schola plugin
echo ""
echo "[7/7] Installing Schola Python package..."
if [ -d "GameAI_Project/Plugins/Schola/Resources/python" ]; then
    cd GameAI_Project/Plugins/Schola/Resources/python
    pip3 install -e .[rllib]
    echo "  Schola installed"
else
    echo "  ERROR: Schola plugin not found in packaged build!"
    echo "  Please ensure Schola plugin is included when packaging UE5"
    exit 1
fi

# Create launcher script
echo ""
echo "Creating worker launcher script..."
cat > /home/ubuntu/start_ue_worker.sh <<'LAUNCHER_SCRIPT'
#!/bin/bash
# CORTEX UE5 Worker Launcher
# Starts UE5 game with Schola gRPC server in headless mode

echo "Starting CORTEX UE5 Worker..."
cd /home/ubuntu/game/GameAI_Project

# Kill any existing instances
pkill -f GameAI_Project || true
sleep 2

# Launch UE5 with Schola
nohup ./GameAI_Project.sh \
    -game \
    -nullrhi \
    -nosound \
    -novsync \
    -ScholaPort=50051 \
    -ScholaAddress=0.0.0.0 \
    -log \
    > /home/ubuntu/ue_worker.log 2>&1 &

UE_PID=$!
echo "UE5 process started (PID: $UE_PID)"

# Wait for Schola to start (up to 60 seconds)
echo "Waiting for Schola gRPC server..."
for i in {1..60}; do
    if netstat -tuln | grep -q ":50051"; then
        echo "Schola gRPC server started successfully on port 50051"
        echo "Worker ready for training"
        exit 0
    fi
    sleep 1
    echo "  Waiting... ($i/60)"
done

# Timeout
echo "ERROR: Schola gRPC server failed to start within 60 seconds"
echo "Check logs: tail -100 /home/ubuntu/ue_worker.log"
exit 1
LAUNCHER_SCRIPT

chmod +x /home/ubuntu/start_ue_worker.sh

# Create systemd service for auto-start on boot
echo ""
echo "Creating systemd service..."
sudo tee /etc/systemd/system/cortex-worker.service > /dev/null <<SERVICE
[Unit]
Description=CORTEX UE5 Worker
After=network.target

[Service]
Type=forking
User=ubuntu
WorkingDirectory=/home/ubuntu
ExecStart=/home/ubuntu/start_ue_worker.sh
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
SERVICE

# Enable service
sudo systemctl daemon-reload
sudo systemctl enable cortex-worker.service

echo ""
echo "============================================================"
echo "Worker Setup Complete!"
echo "============================================================"
echo ""
echo "Installation Summary:"
echo "  - Game installed: /home/ubuntu/game/GameAI_Project"
echo "  - Launcher script: /home/ubuntu/start_ue_worker.sh"
echo "  - Systemd service: cortex-worker.service (enabled)"
echo ""
echo "Testing worker startup..."
echo "Running: /home/ubuntu/start_ue_worker.sh"
echo ""

# Test the worker
/home/ubuntu/start_ue_worker.sh

if [ $? -eq 0 ]; then
    echo ""
    echo "SUCCESS! Worker is running and ready."
    echo ""
    echo "Next steps:"
    echo "  1. Exit this SSH session"
    echo "  2. Stop the instance: aws ec2 stop-instances --instance-ids <INSTANCE_ID>"
    echo "  3. Create AMI: aws ec2 create-image --instance-id <INSTANCE_ID> --name CORTEX-Worker-$(date +%Y%m%d)"
    echo ""
else
    echo ""
    echo "WARNING: Worker startup test failed"
    echo "Check logs: tail -100 /home/ubuntu/ue_worker.log"
    echo ""
fi
