#!/bin/bash
# CORTEX AWS Infrastructure Setup Script
# Automates VPC, security groups, IAM roles, and S3 bucket creation
#
# Usage: ./aws_setup.sh
# Prerequisites: AWS CLI configured with credentials

set -e

echo "============================================================"
echo "CORTEX AWS Infrastructure Setup"
echo "============================================================"
echo ""

# Configuration
export AWS_REGION=${AWS_REGION:-us-east-1}
export KEY_PAIR_NAME=${KEY_PAIR_NAME:-cortex-key}
export PROJECT_NAME="cortex"

echo "Configuration:"
echo "  AWS Region: $AWS_REGION"
echo "  Key Pair: $KEY_PAIR_NAME"
echo ""

# Check AWS CLI
if ! command -v aws &> /dev/null; then
    echo "Error: AWS CLI not installed"
    echo "Install from: https://aws.amazon.com/cli/"
    exit 1
fi

# Check AWS credentials
echo "Verifying AWS credentials..."
if ! aws sts get-caller-identity &> /dev/null; then
    echo "Error: AWS credentials not configured"
    echo "Run: aws configure"
    exit 1
fi

ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
echo "  Account ID: $ACCOUNT_ID"
echo ""

# Create S3 bucket
echo "[1/5] Creating S3 bucket..."
export BUCKET_NAME="${PROJECT_NAME}-training-${ACCOUNT_ID}"
if aws s3 ls "s3://$BUCKET_NAME" 2>&1 | grep -q 'NoSuchBucket'; then
    aws s3 mb "s3://$BUCKET_NAME" --region $AWS_REGION
    echo "  Created: s3://$BUCKET_NAME"
else
    echo "  Bucket already exists: s3://$BUCKET_NAME"
fi

# Get default VPC
echo ""
echo "[2/5] Setting up VPC..."
export VPC_ID=$(aws ec2 describe-vpcs \
    --filters "Name=isDefault,Values=true" \
    --query "Vpcs[0].VpcId" \
    --output text)

if [ "$VPC_ID" == "None" ] || [ -z "$VPC_ID" ]; then
    echo "  Creating default VPC..."
    aws ec2 create-default-vpc
    export VPC_ID=$(aws ec2 describe-vpcs \
        --filters "Name=isDefault,Values=true" \
        --query "Vpcs[0].VpcId" \
        --output text)
fi

echo "  VPC ID: $VPC_ID"

# Create security groups
echo ""
echo "[3/5] Creating security groups..."

# Worker security group
if aws ec2 describe-security-groups \
    --filters "Name=group-name,Values=${PROJECT_NAME}-worker-sg" \
    --query "SecurityGroups[0].GroupId" \
    --output text 2>&1 | grep -q "sg-"; then
    export SG_WORKER=$(aws ec2 describe-security-groups \
        --filters "Name=group-name,Values=${PROJECT_NAME}-worker-sg" \
        --query "SecurityGroups[0].GroupId" \
        --output text)
    echo "  Worker SG already exists: $SG_WORKER"
else
    export SG_WORKER=$(aws ec2 create-security-group \
        --group-name "${PROJECT_NAME}-worker-sg" \
        --description "CORTEX UE5 Worker Security Group" \
        --vpc-id $VPC_ID \
        --query 'GroupId' \
        --output text)

    # Get your public IP
    export MY_IP=$(curl -s https://checkip.amazonaws.com)

    # Allow Schola gRPC
    aws ec2 authorize-security-group-ingress \
        --group-id $SG_WORKER \
        --protocol tcp \
        --port 50051 \
        --cidr 0.0.0.0/0

    # Allow SSH from your IP
    aws ec2 authorize-security-group-ingress \
        --group-id $SG_WORKER \
        --protocol tcp \
        --port 22 \
        --cidr $MY_IP/32

    echo "  Created worker SG: $SG_WORKER"
fi

# Head security group
if aws ec2 describe-security-groups \
    --filters "Name=group-name,Values=${PROJECT_NAME}-head-sg" \
    --query "SecurityGroups[0].GroupId" \
    --output text 2>&1 | grep -q "sg-"; then
    export SG_HEAD=$(aws ec2 describe-security-groups \
        --filters "Name=group-name,Values=${PROJECT_NAME}-head-sg" \
        --query "SecurityGroups[0].GroupId" \
        --output text)
    echo "  Head SG already exists: $SG_HEAD"
else
    export SG_HEAD=$(aws ec2 create-security-group \
        --group-name "${PROJECT_NAME}-head-sg" \
        --description "CORTEX Training Head Node" \
        --vpc-id $VPC_ID \
        --query 'GroupId' \
        --output text)

    export MY_IP=$(curl -s https://checkip.amazonaws.com)

    # Allow SSH
    aws ec2 authorize-security-group-ingress \
        --group-id $SG_HEAD \
        --protocol tcp \
        --port 22 \
        --cidr $MY_IP/32

    # Allow TensorBoard
    aws ec2 authorize-security-group-ingress \
        --group-id $SG_HEAD \
        --protocol tcp \
        --port 6006 \
        --cidr $MY_IP/32

    echo "  Created head SG: $SG_HEAD"
fi

# Create IAM role
echo ""
echo "[4/5] Creating IAM role..."

if aws iam get-role --role-name CORTEX-Worker-Role &> /dev/null; then
    echo "  IAM role already exists: CORTEX-Worker-Role"
else
    # Create trust policy
    cat > /tmp/ec2-trust-policy.json <<EOF
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

    aws iam create-role \
        --role-name CORTEX-Worker-Role \
        --assume-role-policy-document file:///tmp/ec2-trust-policy.json

    # Attach policies
    aws iam attach-role-policy \
        --role-name CORTEX-Worker-Role \
        --policy-arn arn:aws:iam::aws:policy/AmazonS3ReadOnlyAccess

    aws iam attach-role-policy \
        --role-name CORTEX-Worker-Role \
        --policy-arn arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore

    # Create instance profile
    aws iam create-instance-profile \
        --instance-profile-name CORTEX-Worker-Profile || true

    aws iam add-role-to-instance-profile \
        --instance-profile-name CORTEX-Worker-Profile \
        --role-name CORTEX-Worker-Role || true

    # Wait for IAM propagation
    sleep 10

    echo "  Created IAM role: CORTEX-Worker-Role"
    rm /tmp/ec2-trust-policy.json
fi

# Create config file
echo ""
echo "[5/5] Saving configuration..."

cat > aws_config.env <<EOF
# CORTEX AWS Configuration
# Generated: $(date)
# Source this file: source aws_config.env

export AWS_REGION=$AWS_REGION
export BUCKET_NAME=$BUCKET_NAME
export VPC_ID=$VPC_ID
export SG_WORKER=$SG_WORKER
export SG_HEAD=$SG_HEAD
export KEY_PAIR_NAME=$KEY_PAIR_NAME
export ACCOUNT_ID=$ACCOUNT_ID
EOF

echo "  Saved to: aws_config.env"
echo ""
echo "============================================================"
echo "Setup Complete!"
echo "============================================================"
echo ""
echo "Configuration saved to aws_config.env"
echo "To use in future sessions: source aws_config.env"
echo ""
echo "Next steps:"
echo "  1. Package UE5 for Linux (see AWS_EC2_Training_Guide.md)"
echo "  2. Upload build: aws s3 cp GameAI_Project_Linux.zip s3://$BUCKET_NAME/builds/"
echo "  3. Create worker AMI (see Phase 3 in guide)"
echo "  4. Launch training infrastructure"
echo ""
echo "Resources created:"
echo "  S3 Bucket: s3://$BUCKET_NAME"
echo "  VPC: $VPC_ID"
echo "  Worker SG: $SG_WORKER"
echo "  Head SG: $SG_HEAD"
echo "  IAM Role: CORTEX-Worker-Role"
echo ""
