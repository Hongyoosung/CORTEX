# AWS EC2 Training Quick Start Checklist

**Goal:** Get distributed training running on AWS EC2 with minimal steps.

**Estimated Time:** 4-6 hours (one-time setup) + 30-40 hours (training)

**Estimated Cost:** ~$1-2 (testing) or ~$38-131 (full training)

---

## Prerequisites (15 minutes)

- [ ] AWS account with billing enabled
- [ ] AWS CLI installed and configured (`aws configure`)
- [ ] EC2 key pair created (`YOUR_KEY_PAIR_NAME`)
- [ ] UE5 project working locally (Phase 1 complete)
- [ ] Docker Desktop installed

---

## Phase 1: Package & Upload (1-2 hours)

### 1. Package UE5 for Linux
```
In UE Editor:
1. Edit → Project Settings → Platforms → Linux
2. File → Package Project → Linux
3. Output: C:\SBDAPM_Linux_Build
4. Wait ~30-60 minutes
```

**Verification:**
- [ ] `GameAI_Project/Binaries/Linux/GameAI_Project` exists
- [ ] `GameAI_Project/Plugins/Schola/` exists

### 2. Compress Build
```bash
cd C:\SBDAPM_Linux_Build
7z a -tzip GameAI_Project_Linux.zip GameAI_Project\
```

**Expected size:** 500MB - 2GB

### 3. Run AWS Setup Script
```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\CORTEX_Training

# Make executable (Git Bash/WSL)
chmod +x aws_setup.sh

# Run setup
./aws_setup.sh

# Save environment variables
source aws_config.env
```

**Verification:**
- [ ] S3 bucket created
- [ ] Security groups created
- [ ] IAM role created
- [ ] `aws_config.env` file exists

### 4. Upload Game Build
```bash
# Load config
source aws_config.env

# Upload build to S3
aws s3 cp C:\SBDAPM_Linux_Build\GameAI_Project_Linux.zip \
    s3://$BUCKET_NAME/builds/

# Verify upload
aws s3 ls s3://$BUCKET_NAME/builds/
```

**Expected time:** 5-15 minutes (depends on internet speed)

---

## Phase 2: Create Worker AMI (2-3 hours)

### 1. Launch Base Instance
```bash
# Get latest Ubuntu 22.04 AMI
BASE_AMI=$(aws ec2 describe-images \
    --owners 099720109477 \
    --filters "Name=name,Values=ubuntu/images/hvm-ssd/ubuntu-jammy-22.04-amd64-server-*" \
    --query 'sort_by(Images, &CreationDate)[-1].ImageId' \
    --output text)

# Launch instance
SETUP_INSTANCE=$(aws ec2 run-instances \
    --image-id $BASE_AMI \
    --instance-type t3.medium \
    --key-name $KEY_PAIR_NAME \
    --security-group-ids $SG_WORKER \
    --iam-instance-profile Name=SBDAPM-Worker-Profile \
    --block-device-mappings 'DeviceName=/dev/sda1,Ebs={VolumeSize=50,VolumeType=gp3}' \
    --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=SBDAPM-AMI-Setup}]' \
    --query 'Instances[0].InstanceId' \
    --output text)

# Wait for running
aws ec2 wait instance-running --instance-ids $SETUP_INSTANCE

# Get IP
SETUP_IP=$(aws ec2 describe-instances \
    --instance-ids $SETUP_INSTANCE \
    --query 'Reservations[0].Instances[0].PublicIpAddress' \
    --output text)

echo "Setup Instance: $SETUP_INSTANCE ($SETUP_IP)"
```

### 2. Setup Worker Software
```bash
# Copy setup script to instance
scp -i ~/.ssh/$KEY_PAIR_NAME.pem \
    C:/Users/Foryoucom/Documents/GitHub/CORTEX/CORTEX_Training/worker_setup.sh \
    ubuntu@$SETUP_IP:~/

# SSH into instance
ssh -i ~/.ssh/$KEY_PAIR_NAME.pem ubuntu@$SETUP_IP

# Run setup script (on the instance)
chmod +x worker_setup.sh
./worker_setup.sh YOUR_BUCKET_NAME
```

**Expected time:** 10-20 minutes

**Verification (on instance):**
- [ ] `netstat -tuln | grep 50051` shows Schola listening
- [ ] No errors in setup script output

### 3. Create AMI
```bash
# Exit SSH session
exit

# Stop instance
aws ec2 stop-instances --instance-ids $SETUP_INSTANCE
aws ec2 wait instance-stopped --instance-ids $SETUP_INSTANCE

# Create AMI
WORKER_AMI=$(aws ec2 create-image \
    --instance-id $SETUP_INSTANCE \
    --name "SBDAPM-Worker-$(date +%Y%m%d)" \
    --description "UE5 SBDAPM Worker with Schola" \
    --query 'ImageId' \
    --output text)

echo "Worker AMI: $WORKER_AMI"

# Wait for AMI (5-10 minutes)
aws ec2 wait image-available --image-ids $WORKER_AMI

# Save AMI ID
echo "export WORKER_AMI=$WORKER_AMI" >> aws_config.env

# Terminate setup instance
aws ec2 terminate-instances --instance-ids $SETUP_INSTANCE
```

---

## Phase 3: Test with 2 Workers (30 minutes)

### 1. Launch 2 Test Workers
```bash
# Load config
source aws_config.env

# Create userdata
cat > worker-userdata.sh <<'EOF'
#!/bin/bash
sudo -u ubuntu /home/ubuntu/start_ue_worker.sh
EOF

# Launch 2 spot instances
WORKER_INSTANCES=$(aws ec2 run-instances \
    --image-id $WORKER_AMI \
    --instance-type g4dn.xlarge \
    --count 2 \
    --key-name $KEY_PAIR_NAME \
    --security-group-ids $SG_WORKER \
    --iam-instance-profile Name=SBDAPM-Worker-Profile \
    --instance-market-options 'MarketType=spot,SpotOptions={MaxPrice=0.30,SpotInstanceType=one-time}' \
    --user-data file://worker-userdata.sh \
    --block-device-mappings 'DeviceName=/dev/sda1,Ebs={VolumeSize=50,VolumeType=gp3}' \
    --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=SBDAPM-Worker}]' \
    --query 'Instances[*].InstanceId' \
    --output text)

echo "Workers: $WORKER_INSTANCES"

# Wait and get IPs
aws ec2 wait instance-running --instance-ids $WORKER_INSTANCES
WORKER_IPS=$(aws ec2 describe-instances \
    --instance-ids $WORKER_INSTANCES \
    --query 'Reservations[*].Instances[*].PublicIpAddress' \
    --output text)

echo "Worker IPs: $WORKER_IPS"
```

### 2. Verify Workers
```bash
# Wait for workers to boot
sleep 120

# Check each worker
for ip in $WORKER_IPS; do
    echo "Testing worker $ip..."
    ssh -i ~/.ssh/$KEY_PAIR_NAME.pem ubuntu@$ip "netstat -tuln | grep 50051"
done
```

**Expected output:** Each worker shows port 50051 listening

### 3. Launch Head Node
```bash
# Build and upload Docker image
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX
docker build -t sbdapm_training:aws -f CORTEX_Training/Dockerfile .
docker save sbdapm_training:aws | gzip > sbdapm_training_aws.tar.gz
aws s3 cp sbdapm_training_aws.tar.gz s3://$BUCKET_NAME/docker/

# Create head node userdata
cat > head-userdata.sh <<SCRIPT
#!/bin/bash
set -e
curl -fsSL https://get.docker.com -o get-docker.sh
sh get-docker.sh
usermod -aG docker ubuntu
apt-get update
apt-get install -y awscli unzip

su - ubuntu -c "
aws s3 cp s3://$BUCKET_NAME/docker/sbdapm_training_aws.tar.gz /tmp/
docker load < /tmp/sbdapm_training_aws.tar.gz
rm /tmp/sbdapm_training_aws.tar.gz
"
SCRIPT

# Launch head node
HEAD_INSTANCE=$(aws ec2 run-instances \
    --image-id $BASE_AMI \
    --instance-type t3.xlarge \
    --key-name $KEY_PAIR_NAME \
    --security-group-ids $SG_HEAD \
    --iam-instance-profile Name=SBDAPM-Worker-Profile \
    --user-data file://head-userdata.sh \
    --block-device-mappings 'DeviceName=/dev/sda1,Ebs={VolumeSize=100,VolumeType=gp3}' \
    --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=SBDAPM-Head}]' \
    --query 'Instances[0].InstanceId' \
    --output text)

# Wait and get IP
aws ec2 wait instance-running --instance-ids $HEAD_INSTANCE
HEAD_IP=$(aws ec2 describe-instances \
    --instance-ids $HEAD_INSTANCE \
    --query 'Reservations[0].Instances[0].PublicIpAddress' \
    --output text)

echo "Head Node: $HEAD_INSTANCE ($HEAD_IP)"
```

### 4. Run Test Training
```bash
# Wait for Docker installation
sleep 180

# SSH into head node
ssh -i ~/.ssh/$KEY_PAIR_NAME.pem ubuntu@$HEAD_IP

# Create training script
cat > run_training.sh <<SCRIPT
#!/bin/bash
export WORKER_IPS="$WORKER_IPS"
export NUM_WORKERS=2

mkdir -p /home/ubuntu/training_results

docker run -it --rm \
    -v /home/ubuntu/training_results:/app/training_results \
    -e NUM_WORKERS=\$NUM_WORKERS \
    -e WORKER_IPS="\$WORKER_IPS" \
    -p 6006:6006 \
    sbdapm_training:aws \
    python train_rllib.py --iterations 10
SCRIPT

chmod +x run_training.sh
./run_training.sh
```

**Expected output:**
```
============================================================
SBDAPM RLlib Training
============================================================
Connection Configuration:
  Host: localhost
  Port: 50051
  Workers: 2

AWS Mode: Using 2 worker IPs
Worker 0 → XX.XXX.XXX.XXX:50051
Worker 1 → XX.XXX.XXX.XXX:50051

Iteration    1: reward=-25.43, len=342.0, agent_steps=2736, env_steps=684
Iteration    2: reward=-18.92, len=389.5, agent_steps=6248, env_steps=1562
...
```

**If training runs successfully for 10 iterations → SETUP COMPLETE!**

---

## Phase 4: Full Training (30-40 hours)

### 1. Scale to 8 Workers
```bash
# Launch 6 more workers (8 total)
ADDITIONAL_WORKERS=$(aws ec2 run-instances \
    --image-id $WORKER_AMI \
    --instance-type g4dn.xlarge \
    --count 6 \
    --key-name $KEY_PAIR_NAME \
    --security-group-ids $SG_WORKER \
    --iam-instance-profile Name=SBDAPM-Worker-Profile \
    --instance-market-options 'MarketType=spot,SpotOptions={MaxPrice=0.30,SpotInstanceType=one-time}' \
    --user-data file://worker-userdata.sh \
    --block-device-mappings 'DeviceName=/dev/sda1,Ebs={VolumeSize=50,VolumeType=gp3}' \
    --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=SBDAPM-Worker}]' \
    --query 'Instances[*].InstanceId' \
    --output text)

# Get all worker IPs
ALL_WORKER_IPS=$(aws ec2 describe-instances \
    --filters "Name=tag:Name,Values=SBDAPM-Worker" "Name=instance-state-name,Values=running" \
    --query 'Reservations[*].Instances[*].PublicIpAddress' \
    --output text)

echo "All Worker IPs: $ALL_WORKER_IPS"
```

### 2. Update Training Configuration
```bash
# SSH into head node
ssh -i ~/.ssh/$KEY_PAIR_NAME.pem ubuntu@$HEAD_IP

# Update run_training.sh with all worker IPs and 1000 iterations
nano run_training.sh

# Change:
# export WORKER_IPS="IP1 IP2 IP3 IP4 IP5 IP6 IP7 IP8"
# export NUM_WORKERS=8
# --iterations 1000
```

### 3. Start Full Training
```bash
# On head node
./run_training.sh

# In separate SSH session, start TensorBoard
docker exec -it $(docker ps -q) tensorboard --logdir=/app/training_results --host=0.0.0.0
```

### 4. Monitor Progress (Local Machine)
```bash
# Create SSH tunnel for TensorBoard
ssh -i ~/.ssh/$KEY_PAIR_NAME.pem -L 6006:localhost:6006 ubuntu@$HEAD_IP

# Open browser: http://localhost:6006
```

**Training will run for ~30-40 hours**

---

## Phase 5: Cleanup & Export

### 1. Download Trained Model
```bash
# Wait for training to complete
# Then download model from head node
scp -i ~/.ssh/$KEY_PAIR_NAME.pem -r \
    ubuntu@$HEAD_IP:/home/ubuntu/training_results/ \
    C:/Users/Foryoucom/Documents/GitHub/CORTEX/Models/

# Extract ONNX model
# File: training_results/TIMESTAMP/rl_policy_network.onnx
```

### 2. Terminate All Instances
```bash
# Get all instance IDs
ALL_INSTANCES=$(aws ec2 describe-instances \
    --filters "Name=tag:Name,Values=SBDAPM-*" "Name=instance-state-name,Values=running" \
    --query 'Reservations[*].Instances[*].InstanceId' \
    --output text)

# Terminate all
aws ec2 terminate-instances --instance-ids $ALL_INSTANCES

# Wait for termination
aws ec2 wait instance-terminated --instance-ids $ALL_INSTANCES

echo "All instances terminated"
```

### 3. Optional: Delete Resources
```bash
# Keep S3 bucket and AMI for future use
# Or delete everything:

# Delete S3 bucket
aws s3 rm s3://$BUCKET_NAME --recursive
aws s3 rb s3://$BUCKET_NAME

# Deregister AMI
aws ec2 deregister-image --image-id $WORKER_AMI

# Delete security groups
aws ec2 delete-security-group --group-id $SG_WORKER
aws ec2 delete-security-group --group-id $SG_HEAD
```

---

## Troubleshooting Quick Reference

### Worker not starting Schola
```bash
# SSH into worker
ssh -i ~/.ssh/$KEY_PAIR_NAME.pem ubuntu@WORKER_IP

# Check logs
tail -100 /home/ubuntu/ue_worker.log

# Restart worker
sudo systemctl restart sbdapm-worker
```

### Training connection errors
```bash
# Verify security group allows port 50051
aws ec2 describe-security-groups --group-ids $SG_WORKER

# Test connection from head node
ssh -i ~/.ssh/$KEY_PAIR_NAME.pem ubuntu@$HEAD_IP
telnet WORKER_IP 50051
```

### Check costs
```bash
# Current month spending
aws ce get-cost-and-usage \
    --time-period Start=$(date +%Y-%m-01),End=$(date +%Y-%m-%d) \
    --granularity MONTHLY \
    --metrics BlendedCost
```

---

## Cost Breakdown

**Test Phase (2 workers, 2 hours):**
- Spot: 2 × $0.158 × 2 = **$0.63**
- On-demand: 2 × $0.526 × 2 = **$2.10**

**Full Training (8 workers, 30 hours):**
- Spot: 8 × $0.158 × 30 + head = **~$39**
- On-demand: 8 × $0.526 × 30 + head = **~$131**

---

## Support

For detailed information, see:
- `AWS_EC2_Training_Guide.md` - Complete guide with explanations
- `TrainingWorkflow.md` - General training workflow
- AWS EC2 Documentation: https://docs.aws.amazon.com/ec2/

---

**End of Quick Start Checklist**
