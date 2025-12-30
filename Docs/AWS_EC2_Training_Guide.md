# AWS EC2 Distributed Training Guide for SBDAPM
**Version:** 1.0 | **Date:** 2025-12-30

---

## Table of Contents
1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Phase 1: Package UE5 for Linux](#phase-1-package-ue5-for-linux)
4. [Phase 2: AWS Infrastructure Setup](#phase-2-aws-infrastructure-setup)
5. [Phase 3: Create Worker AMI](#phase-3-create-worker-ami)
6. [Phase 4: Launch Training Infrastructure](#phase-4-launch-training-infrastructure)
7. [Phase 5: Run Distributed Training](#phase-5-run-distributed-training)
8. [Phase 6: Monitoring & Debugging](#phase-6-monitoring--debugging)
9. [Phase 7: Model Export & Cleanup](#phase-7-model-export--cleanup)
10. [Cost Optimization](#cost-optimization)
11. [Troubleshooting](#troubleshooting)

---

## Overview

This guide walks through setting up distributed RL training on AWS EC2 for the SBDAPM system using:
- **Head Node**: t3.xlarge running RLlib training in Docker
- **Worker Nodes**: 2-8 g4dn.xlarge instances running UE5 + Schola gRPC servers

**Expected Costs:**
- Development/Testing (2 workers, 2 hours): ~$1-2
- Full Training (8 workers, 30 hours): ~$38 (spot) or ~$131 (on-demand)

**Timeline:**
- Setup (one-time): 4-6 hours
- Training T1-T10: 30-40 hours (automated)

---

## Prerequisites

### Local Machine
- [x] Windows PC with working Phase 1 training (verified)
- [x] UE5 project compiled and tested locally
- [x] Docker Desktop installed
- [x] AWS CLI installed: https://aws.amazon.com/cli/
- [x] Git bash or WSL2 for running shell scripts

### AWS Account
- [x] AWS account with billing enabled
- [x] IAM user with EC2, S3, and VPC full access
- [x] AWS CLI configured: `aws configure`
- [x] EC2 key pair created in target region (us-east-1 recommended)

### Verify AWS CLI Setup
```bash
# Test AWS credentials
aws sts get-caller-identity

# Should output:
# {
#     "UserId": "AIDAXXXXXXXXX",
#     "Account": "123456789012",
#     "Arn": "arn:aws:iam::123456789012:user/your-username"
# }
```

---

## Phase 1: Package UE5 for Linux

### 1.1 Install Linux Cross-Compilation Toolchain

**In Unreal Engine Editor:**

1. Edit → Project Settings → Platforms → Linux
2. Install Prerequisites → Click "Install Linux Prerequisites"
3. Wait for toolchain download (~2GB)

**Verify Installation:**
- Check `C:\Program Files\Epic Games\UE_5.X\Engine\Extras\ThirdPartyNotUE\SDKs\HostLinux`
- Should contain `Linux_x64` folder with clang toolchain

### 1.2 Configure Packaging Settings

**Project Settings → Packaging:**
```
Build Configuration: Shipping
Include Prerequisites Installer: Unchecked (server deployment)
Create Compressed Cooked Packages: Checked (reduce size)
```

**Project Settings → Platforms → Linux:**
```
Target Architecture: x86_64-unknown-linux-gnu
```

### 1.3 Package Project for Linux

**In UE Editor:**
1. File → Package Project → Linux
2. Select output directory: `C:\SBDAPM_Linux_Build`
3. Wait for packaging (~30-60 minutes)

**Expected Output:**
```
C:\SBDAPM_Linux_Build\
├── GameAI_Project/
│   ├── Binaries/
│   │   └── Linux/
│   │       └── GameAI_Project (executable)
│   ├── Content/
│   ├── Plugins/
│   │   └── Schola/  # Must be included!
│   └── GameAI_Project.sh (launcher script)
```

### 1.4 Verify Schola Plugin is Included

**Check packaged build:**
```bash
cd C:\SBDAPM_Linux_Build\GameAI_Project\Plugins
ls -la

# Should show:
# Schola/
#   ├── Binaries/
#   ├── Resources/
#   │   └── python/  # Schola Python package
#   └── Schola.uplugin
```

**If Schola is missing:**
1. Edit `Config/DefaultGame.ini`:
```ini
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysCook=(Path="/Schola")
bIncludePluginSource=True
```
2. Re-package the project

### 1.5 Compress Build for Upload

```bash
# Using 7-Zip (install from https://www.7-zip.org/)
cd C:\SBDAPM_Linux_Build
7z a -tzip GameAI_Project_Linux.zip GameAI_Project\

# Verify size (should be 500MB-2GB depending on content)
dir GameAI_Project_Linux.zip
```

---

## Phase 2: AWS Infrastructure Setup

### 2.1 Create S3 Bucket for Game Build

```bash
# Set your AWS region
export AWS_REGION=us-east-1

# Create bucket (must be globally unique)
export BUCKET_NAME=sbdapm-training-$(date +%s)
aws s3 mb s3://$BUCKET_NAME --region $AWS_REGION

# Upload packaged game
aws s3 cp C:\SBDAPM_Linux_Build\GameAI_Project_Linux.zip \
    s3://$BUCKET_NAME/builds/GameAI_Project_Linux.zip

# Verify upload
aws s3 ls s3://$BUCKET_NAME/builds/
```

**Expected time:** 5-15 minutes (depends on upload speed)

### 2.2 Create VPC and Security Groups

**Create VPC (Optional - use default VPC if available):**
```bash
# Check for default VPC
aws ec2 describe-vpcs --filters "Name=isDefault,Values=true"

# If no default VPC, create one
aws ec2 create-default-vpc
```

**Create Security Group for Workers:**
```bash
# Get default VPC ID
export VPC_ID=$(aws ec2 describe-vpcs --filters "Name=isDefault,Values=true" \
    --query "Vpcs[0].VpcId" --output text)

# Create security group
export SG_WORKER=$(aws ec2 create-security-group \
    --group-name sbdapm-worker-sg \
    --description "SBDAPM UE5 Worker Security Group" \
    --vpc-id $VPC_ID \
    --query 'GroupId' --output text)

echo "Worker Security Group: $SG_WORKER"

# Allow Schola gRPC from anywhere (port 50051)
aws ec2 authorize-security-group-ingress \
    --group-id $SG_WORKER \
    --protocol tcp \
    --port 50051 \
    --cidr 0.0.0.0/0

# Allow SSH from your IP only
export MY_IP=$(curl -s https://checkip.amazonaws.com)
aws ec2 authorize-security-group-ingress \
    --group-id $SG_WORKER \
    --protocol tcp \
    --port 22 \
    --cidr $MY_IP/32
```

**Create Security Group for Head Node:**
```bash
export SG_HEAD=$(aws ec2 create-security-group \
    --group-name sbdapm-head-sg \
    --description "SBDAPM Training Head Node" \
    --vpc-id $VPC_ID \
    --query 'GroupId' --output text)

echo "Head Security Group: $SG_HEAD"

# Allow SSH from your IP
aws ec2 authorize-security-group-ingress \
    --group-id $SG_HEAD \
    --protocol tcp \
    --port 22 \
    --cidr $MY_IP/32

# Allow TensorBoard (port 6006) from your IP
aws ec2 authorize-security-group-ingress \
    --group-id $SG_HEAD \
    --protocol tcp \
    --port 6006 \
    --cidr $MY_IP/32

# Allow all outbound (default)
```

### 2.3 Create IAM Role for EC2 Instances

**Create trust policy file:**
```bash
cat > ec2-trust-policy.json <<EOF
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": {
        "Service": "ec2.amazonaws.com"
      },
      "Action": "sts:AssumeRole"
    }
  ]
}
EOF

# Create IAM role
aws iam create-role \
    --role-name SBDAPM-Worker-Role \
    --assume-role-policy-document file://ec2-trust-policy.json

# Attach S3 read-only policy (for downloading game build)
aws iam attach-role-policy \
    --role-name SBDAPM-Worker-Role \
    --policy-arn arn:aws:iam::aws:policy/AmazonS3ReadOnlyAccess

# Attach SSM policy (for Session Manager - optional but recommended)
aws iam attach-role-policy \
    --role-name SBDAPM-Worker-Role \
    --policy-arn arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore

# Create instance profile
aws iam create-instance-profile \
    --instance-profile-name SBDAPM-Worker-Profile

# Add role to instance profile
aws iam add-role-to-instance-profile \
    --instance-profile-name SBDAPM-Worker-Profile \
    --role-name SBDAPM-Worker-Role
```

---

## Phase 3: Create Worker AMI

Instead of installing dependencies on every instance, we'll create a custom AMI once.

### 3.1 Launch Base Instance

```bash
# Get latest Ubuntu 22.04 AMI
export BASE_AMI=$(aws ec2 describe-images \
    --owners 099720109477 \
    --filters "Name=name,Values=ubuntu/images/hvm-ssd/ubuntu-jammy-22.04-amd64-server-*" \
    --query 'sort_by(Images, &CreationDate)[-1].ImageId' \
    --output text)

echo "Base AMI: $BASE_AMI"

# Launch instance for AMI creation (t3.medium is sufficient for setup)
export SETUP_INSTANCE=$(aws ec2 run-instances \
    --image-id $BASE_AMI \
    --instance-type t3.medium \
    --key-name YOUR_KEY_PAIR_NAME \
    --security-group-ids $SG_WORKER \
    --iam-instance-profile Name=SBDAPM-Worker-Profile \
    --block-device-mappings 'DeviceName=/dev/sda1,Ebs={VolumeSize=50,VolumeType=gp3}' \
    --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=SBDAPM-AMI-Setup}]' \
    --query 'Instances[0].InstanceId' \
    --output text)

echo "Setup Instance: $SETUP_INSTANCE"

# Wait for instance to be running
aws ec2 wait instance-running --instance-ids $SETUP_INSTANCE

# Get public IP
export SETUP_IP=$(aws ec2 describe-instances \
    --instance-ids $SETUP_INSTANCE \
    --query 'Reservations[0].Instances[0].PublicIpAddress' \
    --output text)

echo "Setup Instance IP: $SETUP_IP"
```

### 3.2 Install Dependencies on Base Instance

**SSH into instance:**
```bash
ssh -i ~/.ssh/YOUR_KEY_PAIR.pem ubuntu@$SETUP_IP
```

**Run setup script on the instance:**
```bash
#!/bin/bash
set -e

echo "=== Installing System Dependencies ==="
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    python3.10 \
    python3-pip \
    unzip \
    curl \
    libgl1-mesa-glx \
    libglu1-mesa \
    libxcursor1 \
    libxi6 \
    libxrandr2 \
    libxxf86vm1 \
    libopenal1 \
    libvulkan1 \
    mesa-vulkan-drivers

echo "=== Installing AWS CLI ==="
curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
unzip awscliv2.zip
sudo ./aws/install
rm -rf aws awscliv2.zip

echo "=== Installing Python Dependencies ==="
pip3 install --upgrade pip
pip3 install numpy torch

echo "=== Creating Game Directory ==="
mkdir -p /home/ubuntu/game
cd /home/ubuntu/game

echo "=== Downloading Game Build from S3 ==="
# Replace with your bucket name
export BUCKET_NAME=YOUR_BUCKET_NAME_HERE
aws s3 cp s3://$BUCKET_NAME/builds/GameAI_Project_Linux.zip ./
unzip GameAI_Project_Linux.zip
rm GameAI_Project_Linux.zip

echo "=== Making Game Executable ==="
chmod +x GameAI_Project/GameAI_Project.sh
chmod +x GameAI_Project/Binaries/Linux/GameAI_Project

echo "=== Testing Schola Plugin ==="
cd GameAI_Project/Plugins/Schola/Resources/python
pip3 install -e .[rllib]

echo "=== Creating Launcher Script ==="
cat > /home/ubuntu/start_ue_worker.sh <<'SCRIPT'
#!/bin/bash
# SBDAPM UE5 Worker Launcher

cd /home/ubuntu/game/GameAI_Project

# Launch UE5 with Schola in headless mode
./GameAI_Project.sh \
    -game \
    -nullrhi \
    -nosound \
    -novsync \
    -ScholaPort=50051 \
    -ScholaAddress=0.0.0.0 \
    -log &

# Wait for Schola to start
sleep 15

# Check if Schola server started
if netstat -tuln | grep -q ":50051"; then
    echo "Schola gRPC server started successfully on port 50051"
else
    echo "ERROR: Schola gRPC server failed to start"
    exit 1
fi

# Keep container running
tail -f /dev/null
SCRIPT

chmod +x /home/ubuntu/start_ue_worker.sh

echo "=== Setup Complete ==="
echo "Worker AMI is ready to be created"
```

**IMPORTANT:** Replace `YOUR_BUCKET_NAME_HERE` with your actual bucket name before running!

**Save the script and run:**
```bash
# On the EC2 instance
nano setup_worker.sh
# Paste the script above, edit BUCKET_NAME
chmod +x setup_worker.sh
./setup_worker.sh
```

**Expected time:** 10-15 minutes

### 3.3 Test Worker Setup

```bash
# On the EC2 instance
/home/ubuntu/start_ue_worker.sh &

# Check if Schola is running (wait ~20 seconds)
sleep 20
netstat -tuln | grep 50051

# Should output:
# tcp        0      0 0.0.0.0:50051           0.0.0.0:*               LISTEN
```

**If successful, proceed to create AMI. If not, check troubleshooting section.**

### 3.4 Create Custom AMI

**From your local machine:**
```bash
# Stop the instance first
aws ec2 stop-instances --instance-ids $SETUP_INSTANCE
aws ec2 wait instance-stopped --instance-ids $SETUP_INSTANCE

# Create AMI
export WORKER_AMI=$(aws ec2 create-image \
    --instance-id $SETUP_INSTANCE \
    --name "SBDAPM-Worker-$(date +%Y%m%d)" \
    --description "UE5 SBDAPM Worker with Schola" \
    --query 'ImageId' \
    --output text)

echo "Worker AMI: $WORKER_AMI"

# Wait for AMI to be available (5-10 minutes)
aws ec2 wait image-available --image-ids $WORKER_AMI

# Terminate setup instance (no longer needed)
aws ec2 terminate-instances --instance-ids $SETUP_INSTANCE

echo "AMI created successfully: $WORKER_AMI"
```

---

## Phase 4: Launch Training Infrastructure

### 4.1 Launch Worker Fleet

**Create user data script for workers:**
```bash
cat > worker-userdata.sh <<'EOF'
#!/bin/bash
# Start UE5 worker on boot
sudo -u ubuntu /home/ubuntu/start_ue_worker.sh
EOF
```

**Launch 2 workers (for testing):**
```bash
# Launch worker instances
export NUM_WORKERS=2
export WORKER_INSTANCE_TYPE=g4dn.xlarge

# For spot instances (70% cheaper):
export WORKER_INSTANCES=$(aws ec2 run-instances \
    --image-id $WORKER_AMI \
    --instance-type $WORKER_INSTANCE_TYPE \
    --count $NUM_WORKERS \
    --key-name YOUR_KEY_PAIR_NAME \
    --security-group-ids $SG_WORKER \
    --iam-instance-profile Name=SBDAPM-Worker-Profile \
    --instance-market-options 'MarketType=spot,SpotOptions={MaxPrice=0.30,SpotInstanceType=one-time}' \
    --user-data file://worker-userdata.sh \
    --block-device-mappings 'DeviceName=/dev/sda1,Ebs={VolumeSize=50,VolumeType=gp3}' \
    --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=SBDAPM-Worker}]' \
    --query 'Instances[*].InstanceId' \
    --output text)

echo "Worker Instances: $WORKER_INSTANCES"

# Wait for instances to be running
aws ec2 wait instance-running --instance-ids $WORKER_INSTANCES

# Get worker IPs
export WORKER_IPS=$(aws ec2 describe-instances \
    --instance-ids $WORKER_INSTANCES \
    --query 'Reservations[*].Instances[*].PublicIpAddress' \
    --output text)

echo "Worker IPs: $WORKER_IPS"
```

**For on-demand instances (remove spot options):**
```bash
# Remove --instance-market-options flag
export WORKER_INSTANCES=$(aws ec2 run-instances \
    --image-id $WORKER_AMI \
    --instance-type $WORKER_INSTANCE_TYPE \
    --count $NUM_WORKERS \
    --key-name YOUR_KEY_PAIR_NAME \
    --security-group-ids $SG_WORKER \
    --iam-instance-profile Name=SBDAPM-Worker-Profile \
    --user-data file://worker-userdata.sh \
    --block-device-mappings 'DeviceName=/dev/sda1,Ebs={VolumeSize=50,VolumeType=gp3}' \
    --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=SBDAPM-Worker}]' \
    --query 'Instances[*].InstanceId' \
    --output text)
```

### 4.2 Verify Workers Started

```bash
# Wait for workers to boot (~2 minutes)
sleep 120

# SSH into first worker
export FIRST_WORKER_IP=$(echo $WORKER_IPS | awk '{print $1}')
ssh -i ~/.ssh/YOUR_KEY_PAIR.pem ubuntu@$FIRST_WORKER_IP

# Check Schola status
netstat -tuln | grep 50051

# Should show:
# tcp        0      0 0.0.0.0:50051           0.0.0.0:*               LISTEN

# Check UE5 logs
tail -f /home/ubuntu/game/GameAI_Project/Saved/Logs/GameAI_Project.log

# Look for:
# [Schola] gRPC server started on port 50051
```

### 4.3 Launch Head Node

**Create Dockerfile for head node training:**
```bash
cat > Dockerfile.head <<'EOF'
FROM python:3.10-slim

RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy requirements
COPY CORTEX_Training/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy training scripts
COPY CORTEX_Training/ .

# Install Schola (from worker AMI's bundled version)
RUN pip install --no-cache-dir schola[rllib]

EXPOSE 6006

CMD ["python", "train_rllib.py"]
EOF
```

**Build Docker image locally:**
```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX

docker build -t sbdapm_training:aws -f Dockerfile.head .
docker save sbdapm_training:aws | gzip > sbdapm_training_aws.tar.gz

# Upload to S3
aws s3 cp sbdapm_training_aws.tar.gz s3://$BUCKET_NAME/docker/
```

**Launch head node:**
```bash
# Create head node userdata
cat > head-userdata.sh <<'SCRIPT'
#!/bin/bash
set -e

# Install Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sh get-docker.sh
usermod -aG docker ubuntu

# Install AWS CLI
apt-get update
apt-get install -y awscli

# Download Docker image
su - ubuntu -c "
aws s3 cp s3://YOUR_BUCKET_NAME/docker/sbdapm_training_aws.tar.gz /tmp/
docker load < /tmp/sbdapm_training_aws.tar.gz
rm /tmp/sbdapm_training_aws.tar.gz
"

echo "Head node ready"
SCRIPT

# Launch head node
export HEAD_INSTANCE=$(aws ec2 run-instances \
    --image-id $BASE_AMI \
    --instance-type t3.xlarge \
    --key-name YOUR_KEY_PAIR_NAME \
    --security-group-ids $SG_HEAD \
    --iam-instance-profile Name=SBDAPM-Worker-Profile \
    --user-data file://head-userdata.sh \
    --block-device-mappings 'DeviceName=/dev/sda1,Ebs={VolumeSize=100,VolumeType=gp3}' \
    --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=SBDAPM-Head}]' \
    --query 'Instances[0].InstanceId' \
    --output text)

echo "Head Instance: $HEAD_INSTANCE"

# Wait and get IP
aws ec2 wait instance-running --instance-ids $HEAD_INSTANCE
export HEAD_IP=$(aws ec2 describe-instances \
    --instance-ids $HEAD_INSTANCE \
    --query 'Reservations[0].Instances[0].PublicIpAddress' \
    --output text)

echo "Head Node IP: $HEAD_IP"
```

---

## Phase 5: Run Distributed Training

### 5.1 Prepare Training Configuration

**SSH into head node:**
```bash
ssh -i ~/.ssh/YOUR_KEY_PAIR.pem ubuntu@$HEAD_IP
```

**Create training launcher script:**
```bash
cat > /home/ubuntu/run_training.sh <<'SCRIPT'
#!/bin/bash
# SBDAPM Distributed Training Launcher

# Worker IPs (set these manually)
export WORKER_IPS="WORKER_IP_1 WORKER_IP_2"
export NUM_WORKERS=2
export NUM_ITERATIONS=1000

# Create output directory
mkdir -p /home/ubuntu/training_results

# Run training container
docker run -it --rm \
    --name sbdapm_training \
    -v /home/ubuntu/training_results:/app/training_results \
    -e NUM_WORKERS=$NUM_WORKERS \
    -e WORKER_IPS="$WORKER_IPS" \
    -p 6006:6006 \
    sbdapm_training:aws \
    python train_rllib.py \
        --iterations $NUM_ITERATIONS \
        --checkpoint-freq 50
SCRIPT

chmod +x /home/ubuntu/run_training.sh
```

**Update worker IPs in the script:**
```bash
# Replace WORKER_IP_1 WORKER_IP_2 with actual IPs
nano /home/ubuntu/run_training.sh
# Edit the WORKER_IPS line
```

### 5.2 Modify Training Script for AWS

**Create AWS-specific training script:**
```bash
cat > /home/ubuntu/train_rllib_aws.py <<'PYTHON'
# Modification to train_rllib.py for distributed AWS training

import os

# Get worker IPs from environment
worker_ips_str = os.getenv("WORKER_IPS", "localhost")
worker_ips = worker_ips_str.split()

print(f"Connecting to {len(worker_ips)} workers: {worker_ips}")

# Modify env_creator to use worker IPs
def env_creator(config):
    from sbdapm_env import SBDAPMMultiAgentEnv

    worker_index = config.get("worker_index", 0)
    host = worker_ips[worker_index % len(worker_ips)]

    print(f"Worker {worker_index} connecting to {host}:50051")

    return SBDAPMMultiAgentEnv(
        host=host,
        port=50051,
        max_episode_steps=config.get("max_episode_steps", 1000)
    )

# Rest of train_rllib.py code...
PYTHON
```

**Better approach: Modify existing train_rllib.py to support AWS mode:**

Add this to your local `CORTEX_Training/train_rllib.py` before rebuilding:

```python
# In create_env_config() function, add:
def create_env_config():
    """Create environment configuration for Schola."""

    # AWS distributed mode
    worker_ips_str = os.getenv("WORKER_IPS", None)
    if worker_ips_str:
        worker_ips = worker_ips_str.split()
        print(f"AWS Mode: Using {len(worker_ips)} worker IPs")
    else:
        worker_ips = None

    return {
        "host": SBDAPMConfig.HOST,
        "base_port": SBDAPMConfig.PORT,
        "max_episode_steps": SBDAPMConfig.MAX_EPISODE_STEPS,
        "worker_ips": worker_ips,  # Pass to env creator
    }

# In env_creator function:
def env_creator(config):
    from sbdapm_env import SBDAPMMultiAgentEnv

    # AWS distributed mode
    worker_ips = config.get("worker_ips")
    if worker_ips:
        worker_index = config.get("worker_index", 0)
        host = worker_ips[worker_index % len(worker_ips)]
        print(f"Worker {worker_index} → {host}:50051")
    else:
        # Local mode: use base_port + worker_index
        worker_index = config.get("worker_index", 0)
        host = config.get("host", "localhost")

    return SBDAPMMultiAgentEnv(
        host=host,
        port=config.get("base_port", 50051),
        max_episode_steps=config.get("max_episode_steps", 1000)
    )
```

### 5.3 Start Training

**On head node:**
```bash
# Start training
./run_training.sh

# Expected output:
# ============================================================
# SBDAPM RLlib Training
# ============================================================
# AWS Mode: Using 2 worker IPs
# Worker 0 → 34.XXX.XXX.XXX:50051
# Worker 1 → 54.XXX.XXX.XXX:50051
#
# Iteration    1: reward=-25.43, len=342.0, agent_steps=2736, env_steps=684
# Iteration    2: reward=-18.92, len=389.5, agent_steps=6248, env_steps=1562
# ...
```

### 5.4 Monitor Training with TensorBoard

**On head node (separate SSH session):**
```bash
ssh -i ~/.ssh/YOUR_KEY_PAIR.pem ubuntu@$HEAD_IP

# Start TensorBoard
docker exec -it sbdapm_training tensorboard --logdir=/app/training_results --host=0.0.0.0 --port=6006
```

**On your local machine:**
```bash
# Create SSH tunnel
ssh -i ~/.ssh/YOUR_KEY_PAIR.pem -L 6006:localhost:6006 ubuntu@$HEAD_IP

# Open browser: http://localhost:6006
```

---

## Phase 6: Monitoring & Debugging

### 6.1 Real-Time Monitoring

**Monitor worker health:**
```bash
# Create monitoring script
cat > monitor_workers.sh <<'BASH'
#!/bin/bash
for ip in $WORKER_IPS; do
    echo "=== Worker $ip ==="
    ssh -i ~/.ssh/YOUR_KEY_PAIR.pem ubuntu@$ip "
        echo 'Schola Status:'
        netstat -tuln | grep 50051
        echo 'CPU/Memory:'
        top -bn1 | head -15
        echo 'Recent UE logs:'
        tail -20 /home/ubuntu/game/GameAI_Project/Saved/Logs/GameAI_Project.log
    "
    echo ""
done
BASH

chmod +x monitor_workers.sh
./monitor_workers.sh
```

### 6.2 CloudWatch Logs (Optional)

**Install CloudWatch agent on workers:**
```bash
# On each worker
wget https://s3.amazonaws.com/amazoncloudwatch-agent/ubuntu/amd64/latest/amazon-cloudwatch-agent.deb
sudo dpkg -i amazon-cloudwatch-agent.deb

# Configure to stream UE logs
sudo /opt/aws/amazon-cloudwatch-agent/bin/amazon-cloudwatch-agent-ctl \
    -a fetch-config \
    -m ec2 \
    -c file:/etc/cloudwatch-config.json \
    -s
```

### 6.3 Training Progress Checks

**Key metrics to monitor:**
- `episode_reward_mean`: Should increase
- `num_env_steps_sampled`: Should increase linearly
- Worker CPU: Should be 60-90% (if 100%, UE may be bottleneck)
- Network: ~1-5 Mbps per worker (gRPC overhead)

**Expected training speed:**
- 2 workers: ~200-300 env steps/sec
- 4 workers: ~400-600 env steps/sec
- 8 workers: ~800-1200 env steps/sec

---

## Phase 7: Model Export & Cleanup

### 7.1 Export Trained Model

Training script auto-exports ONNX model to `/app/training_results/TIMESTAMP/rl_policy_network.onnx`

**Download model from head node:**
```bash
# From your local machine
scp -i ~/.ssh/YOUR_KEY_PAIR.pem \
    ubuntu@$HEAD_IP:/home/ubuntu/training_results/*/rl_policy_network.onnx \
    C:\Users\Foryoucom\Documents\GitHub\CORTEX\Content\Models\
```

### 7.2 Cleanup AWS Resources

**Stop all instances:**
```bash
# Terminate workers
aws ec2 terminate-instances --instance-ids $WORKER_INSTANCES

# Terminate head node
aws ec2 terminate-instances --instance-ids $HEAD_INSTANCE

# Wait for termination
aws ec2 wait instance-terminated --instance-ids $WORKER_INSTANCES $HEAD_INSTANCE
```

**Delete S3 bucket (optional):**
```bash
# Delete all objects first
aws s3 rm s3://$BUCKET_NAME --recursive

# Delete bucket
aws s3 rb s3://$BUCKET_NAME
```

**Delete security groups:**
```bash
aws ec2 delete-security-group --group-id $SG_WORKER
aws ec2 delete-security-group --group-id $SG_HEAD
```

**Keep AMI for future use:**
```bash
# Tag AMI for easy identification
aws ec2 create-tags \
    --resources $WORKER_AMI \
    --tags Key=Project,Value=SBDAPM Key=Type,Value=Worker
```

---

## Cost Optimization

### 1. Use Spot Instances

**Savings:** 60-80% vs on-demand

```bash
# Already included in Phase 4.1 launch command
--instance-market-options 'MarketType=spot,SpotOptions={MaxPrice=0.30,SpotInstanceType=one-time}'
```

**Interruption handling:**
- Checkpoint frequency: Every 50 iterations (already configured)
- If interrupted, resume from last checkpoint

### 2. Use Scheduled Scaling

**Launch workers only when needed:**
```bash
# Create EventBridge rule to start at 9 AM
aws events put-rule \
    --name start-sbdapm-training \
    --schedule-expression "cron(0 9 * * ? *)"

# Stop at 6 PM
aws events put-rule \
    --name stop-sbdapm-training \
    --schedule-expression "cron(0 18 * * ? *)"
```

### 3. Regional Pricing

**Cheapest regions for g4dn.xlarge (as of 2024):**
1. us-east-1: $0.526/hr (on-demand), ~$0.158/hr (spot)
2. us-west-2: $0.526/hr (on-demand), ~$0.165/hr (spot)
3. eu-west-1: $0.584/hr (on-demand), ~$0.175/hr (spot)

### 4. Cost Estimation

**Full Training (T1-T10, 30 hours, 8 workers):**
- **Spot instances:**
  - Workers: 8 × $0.158 × 30 = $37.92
  - Head: 1 × $0.0416 × 30 = $1.25
  - **Total: ~$39**

- **On-demand:**
  - Workers: 8 × $0.526 × 30 = $126.24
  - Head: 1 × $0.1664 × 30 = $4.99
  - **Total: ~$131**

**Development/Testing (2 hours, 2 workers):**
- Spot: 2 × $0.158 × 2 = **$0.63**
- On-demand: 2 × $0.526 × 2 = **$2.10**

---

## Troubleshooting

### Issue: Worker fails to start Schola

**Symptoms:**
- `netstat -tuln | grep 50051` shows nothing
- UE logs show `[Schola] Failed to start gRPC server`

**Solution:**
```bash
# Check UE dependencies
ldd /home/ubuntu/game/GameAI_Project/Binaries/Linux/GameAI_Project

# Install missing libraries
sudo apt-get install -y libgl1-mesa-glx libglu1-mesa libxcursor1 libxi6

# Restart worker
sudo reboot
```

### Issue: Training script can't connect to workers

**Symptoms:**
- `grpc._channel._InactiveRpcError: DEADLINE_EXCEEDED`

**Solution:**
```bash
# Check security group allows port 50051
aws ec2 describe-security-groups --group-ids $SG_WORKER

# Test connection from head node
telnet WORKER_IP 50051

# If fails, add inbound rule
aws ec2 authorize-security-group-ingress \
    --group-id $SG_WORKER \
    --protocol tcp \
    --port 50051 \
    --cidr 0.0.0.0/0
```

### Issue: Training is slow

**Symptoms:**
- <100 env steps/sec per worker

**Diagnosis:**
```bash
# Check worker CPU
ssh ubuntu@WORKER_IP "top -bn1 | head -20"

# Check UE frame rate
ssh ubuntu@WORKER_IP "tail -100 /home/ubuntu/game/GameAI_Project/Saved/Logs/GameAI_Project.log | grep FPS"
```

**Solutions:**
- **CPU bottleneck:** Upgrade to g4dn.2xlarge (8 vCPUs)
- **UE rendering:** Already using `-nullrhi` (headless)
- **Action throttling:** Check `ActionApplicationInterval` in StateTree

### Issue: Spot instance interrupted

**Symptoms:**
- Worker terminates mid-training
- Training errors: `Connection refused`

**Solution:**
```bash
# Resume from last checkpoint
# On head node:
export CHECKPOINT_DIR="/home/ubuntu/training_results/LATEST_RUN/checkpoint_000100"

docker run -it --rm \
    -v /home/ubuntu/training_results:/app/training_results \
    -v $CHECKPOINT_DIR:/app/checkpoint \
    sbdapm_training:aws \
    python train_rllib.py \
        --resume /app/checkpoint \
        --iterations 1000
```

**Prevention:**
- Use on-demand for critical phases (T9-T10)
- Set spot max price higher ($0.40 instead of $0.30)
- Use multiple spot instance types (fallback)

---

## Quick Reference Commands

### Get Worker IPs
```bash
aws ec2 describe-instances \
    --filters "Name=tag:Name,Values=SBDAPM-Worker" "Name=instance-state-name,Values=running" \
    --query 'Reservations[*].Instances[*].PublicIpAddress' \
    --output text
```

### Check Worker Status
```bash
for ip in $(aws ec2 describe-instances --filters "Name=tag:Name,Values=SBDAPM-Worker" "Name=instance-state-name,Values=running" --query 'Reservations[*].Instances[*].PublicIpAddress' --output text); do
    echo "Worker $ip:"
    ssh -i ~/.ssh/YOUR_KEY.pem ubuntu@$ip "netstat -tuln | grep 50051"
done
```

### Download Training Logs
```bash
scp -i ~/.ssh/YOUR_KEY.pem -r \
    ubuntu@$HEAD_IP:/home/ubuntu/training_results/ \
    ./local_results/
```

### Cost Monitoring
```bash
# Get current month's cost
aws ce get-cost-and-usage \
    --time-period Start=$(date +%Y-%m-01),End=$(date +%Y-%m-%d) \
    --granularity MONTHLY \
    --metrics BlendedCost
```

---

## Next Steps

After completing AWS training:
1. Deploy model to UE5 (see TrainingWorkflow.md:623-670)
2. Test in PIE (Play In Editor)
3. Run performance benchmarks
4. Iterate on curriculum levels if needed

---

**End of AWS EC2 Training Guide**

For questions or issues:
- Check TrainingWorkflow.md for general training workflow
- Check CORTEX_Training/README.md for Python setup
- AWS Documentation: https://docs.aws.amazon.com/ec2/
