#!/bin/bash
# SBDAPM AWS Training Monitor
# Displays real-time status of head node and workers
#
# Usage: ./monitor_aws_training.sh
# Prerequisites: source aws_config.env

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "============================================================"
echo "SBDAPM AWS Training Monitor"
echo "============================================================"
echo ""

# Check if config loaded
if [ -z "$AWS_REGION" ]; then
    echo "Error: AWS configuration not loaded"
    echo "Run: source aws_config.env"
    exit 1
fi

# Get head node
echo "Checking head node..."
HEAD_INSTANCE=$(aws ec2 describe-instances \
    --filters "Name=tag:Name,Values=SBDAPM-Head" "Name=instance-state-name,Values=running" \
    --query 'Reservations[0].Instances[0].InstanceId' \
    --output text 2>/dev/null)

if [ "$HEAD_INSTANCE" == "None" ] || [ -z "$HEAD_INSTANCE" ]; then
    echo -e "${RED}✗ Head node not running${NC}"
    HEAD_RUNNING=false
else
    HEAD_IP=$(aws ec2 describe-instances \
        --instance-ids $HEAD_INSTANCE \
        --query 'Reservations[0].Instances[0].PublicIpAddress' \
        --output text)
    echo -e "${GREEN}✓ Head node running${NC}"
    echo "  Instance ID: $HEAD_INSTANCE"
    echo "  IP: $HEAD_IP"
    HEAD_RUNNING=true
fi

echo ""

# Get workers
echo "Checking workers..."
WORKER_INSTANCES=$(aws ec2 describe-instances \
    --filters "Name=tag:Name,Values=SBDAPM-Worker" "Name=instance-state-name,Values=running" \
    --query 'Reservations[*].Instances[*].InstanceId' \
    --output text)

if [ -z "$WORKER_INSTANCES" ]; then
    echo -e "${RED}✗ No workers running${NC}"
    exit 1
fi

WORKER_COUNT=$(echo $WORKER_INSTANCES | wc -w)
echo -e "${GREEN}✓ $WORKER_COUNT workers running${NC}"

# Get worker IPs
WORKER_IPS=$(aws ec2 describe-instances \
    --instance-ids $WORKER_INSTANCES \
    --query 'Reservations[*].Instances[*].PublicIpAddress' \
    --output text)

echo ""
echo "Worker Details:"
i=1
for ip in $WORKER_IPS; do
    echo "  Worker $i: $ip"
    i=$((i+1))
done

echo ""
echo "============================================================"
echo "Health Checks"
echo "============================================================"
echo ""

# Check worker Schola servers
echo "Checking worker Schola gRPC servers..."
HEALTHY_WORKERS=0
for ip in $WORKER_IPS; do
    if timeout 5 bash -c "echo > /dev/tcp/$ip/50051" 2>/dev/null; then
        echo -e "  ${GREEN}✓${NC} Worker $ip:50051 - Reachable"
        HEALTHY_WORKERS=$((HEALTHY_WORKERS+1))
    else
        echo -e "  ${RED}✗${NC} Worker $ip:50051 - Unreachable"
    fi
done

echo ""
echo "Summary: $HEALTHY_WORKERS/$WORKER_COUNT workers healthy"

if [ $HEALTHY_WORKERS -lt $WORKER_COUNT ]; then
    echo -e "${YELLOW}Warning: Some workers are not responding${NC}"
    echo "Troubleshooting steps:"
    echo "  1. Wait 2-3 minutes for workers to start"
    echo "  2. SSH into worker: ssh -i ~/.ssh/KEY.pem ubuntu@WORKER_IP"
    echo "  3. Check logs: tail -100 /home/ubuntu/ue_worker.log"
    echo "  4. Restart worker: sudo systemctl restart sbdapm-worker"
fi

echo ""

# Check head node training
if [ "$HEAD_RUNNING" = true ]; then
    echo "Checking training status on head node..."

    # Check if Docker container running
    CONTAINER_RUNNING=$(ssh -i ~/.ssh/${KEY_PAIR_NAME}.pem -o StrictHostKeyChecking=no \
        ubuntu@$HEAD_IP "docker ps -q" 2>/dev/null || echo "")

    if [ -z "$CONTAINER_RUNNING" ]; then
        echo -e "${YELLOW}⚠${NC} Training container not running"
        echo ""
        echo "To start training:"
        echo "  ssh -i ~/.ssh/${KEY_PAIR_NAME}.pem ubuntu@$HEAD_IP"
        echo "  ./run_training.sh"
    else
        echo -e "${GREEN}✓${NC} Training container running (ID: ${CONTAINER_RUNNING:0:12})"

        # Get latest training output
        echo ""
        echo "Latest training output (last 10 lines):"
        echo "----------------------------------------"
        ssh -i ~/.ssh/${KEY_PAIR_NAME}.pem -o StrictHostKeyChecking=no \
            ubuntu@$HEAD_IP "docker logs --tail 10 $CONTAINER_RUNNING" 2>/dev/null || \
            echo "Could not fetch logs"
        echo "----------------------------------------"

        # Check TensorBoard
        echo ""
        echo "TensorBoard Status:"
        TENSORBOARD_RUNNING=$(ssh -i ~/.ssh/${KEY_PAIR_NAME}.pem -o StrictHostKeyChecking=no \
            ubuntu@$HEAD_IP "docker exec $CONTAINER_RUNNING pgrep tensorboard" 2>/dev/null || echo "")

        if [ -z "$TENSORBOARD_RUNNING" ]; then
            echo -e "${YELLOW}⚠${NC} TensorBoard not running"
            echo "To start TensorBoard:"
            echo "  ssh ubuntu@$HEAD_IP"
            echo "  docker exec -it $CONTAINER_RUNNING tensorboard --logdir=/app/training_results --host=0.0.0.0"
        else
            echo -e "${GREEN}✓${NC} TensorBoard running"
            echo "  Access via SSH tunnel: ssh -L 6006:localhost:6006 ubuntu@$HEAD_IP"
            echo "  Then open: http://localhost:6006"
        fi
    fi
fi

echo ""
echo "============================================================"
echo "Quick Actions"
echo "============================================================"
echo ""
echo "View live training logs:"
echo "  ssh -i ~/.ssh/${KEY_PAIR_NAME}.pem ubuntu@$HEAD_IP"
echo "  docker logs -f \$(docker ps -q)"
echo ""
echo "SSH to head node:"
echo "  ssh -i ~/.ssh/${KEY_PAIR_NAME}.pem ubuntu@$HEAD_IP"
echo ""
echo "SSH to worker 1:"
echo "  ssh -i ~/.ssh/${KEY_PAIR_NAME}.pem ubuntu@$(echo $WORKER_IPS | awk '{print $1}')"
echo ""
echo "Create TensorBoard tunnel:"
echo "  ssh -i ~/.ssh/${KEY_PAIR_NAME}.pem -L 6006:localhost:6006 ubuntu@$HEAD_IP"
echo ""
echo "Download training results:"
echo "  scp -i ~/.ssh/${KEY_PAIR_NAME}.pem -r ubuntu@$HEAD_IP:/home/ubuntu/training_results ./"
echo ""
echo "Terminate all instances:"
echo "  aws ec2 terminate-instances --instance-ids $WORKER_INSTANCES $HEAD_INSTANCE"
echo ""
