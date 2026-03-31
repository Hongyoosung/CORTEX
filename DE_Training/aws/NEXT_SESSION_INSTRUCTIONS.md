# DE Training — Next Session Instructions
## Distributed Training: 4 UE5 Binaries + Ray Workers (5 iterations)

---

## Context

Training infrastructure already set up on EC2. All software is installed.
Do **not** reinstall anything — just resize, launch UE5 ×4, and run.

| Item | Value |
|------|-------|
| EC2 Instance ID | `i-05bff238f7e833bd8` |
| Region | `ap-northeast-2` |
| SSH Key | `~/.ssh/de_training_key` |
| S3 Bucket | `de-v10-2-dev-training-22de2017` |
| Resume Checkpoint | `/app/training_results/20260328_032546/best` |
| Training scripts | `/app/training/` |
| UE5 binary | `/app/ue5/DE.sh` |

---

## Step 1 — Resize instance to g4dn.2xlarge

The current instance (g4dn.xlarge, 4 vCPU / 16 GB) cannot run 4 UE5 binaries simultaneously.
Resize to **g4dn.2xlarge** (8 vCPU / 32 GB) before starting.

```bash
# Stop instance
conda run -n gameai aws ec2 stop-instances \
  --instance-ids i-05bff238f7e833bd8 --region ap-northeast-2

conda run -n gameai aws ec2 wait instance-stopped \
  --instance-ids i-05bff238f7e833bd8 --region ap-northeast-2

# Resize
conda run -n gameai aws ec2 modify-instance-attribute \
  --instance-id i-05bff238f7e833bd8 \
  --instance-type '{"Value":"g4dn.2xlarge"}' \
  --region ap-northeast-2

# Start
conda run -n gameai aws ec2 start-instances \
  --instance-ids i-05bff238f7e833bd8 --region ap-northeast-2

conda run -n gameai aws ec2 wait instance-running \
  --instance-ids i-05bff238f7e833bd8 --region ap-northeast-2

# Get new public IP
conda run -n gameai aws ec2 describe-instances \
  --instance-ids i-05bff238f7e833bd8 --region ap-northeast-2 \
  --query 'Reservations[0].Instances[0].PublicIpAddress' --output text
```

---

## Step 2 — SSH in (use EC2 Instance Connect — no key file needed)

The SSH key (`~/.ssh/de_training_key`) works for direct SSH.
If it fails (key expired), push a temporary key via EC2 Instance Connect first:

```bash
PUB_KEY=$(cat ~/.ssh/de_training_key.pub)
conda run -n gameai aws ec2-instance-connect send-ssh-public-key \
  --instance-id i-05bff238f7e833bd8 \
  --instance-os-user ubuntu \
  --ssh-public-key "$PUB_KEY" \
  --availability-zone ap-northeast-2a \
  --region ap-northeast-2

ssh -i ~/.ssh/de_training_key ubuntu@<NEW_IP>
```

---

## Step 3 — Write the distributed start script

On the EC2 instance, create `/app/start_distributed.sh`:

```bash
cat > /app/start_distributed.sh << 'EOF'
#!/bin/bash
set -e
export PATH="$HOME/.local/bin:$PATH"
export S3_BUCKET="de-v10-2-dev-training-22de2017"
export NUM_WORKERS=4          # 4 Ray env_runners, one per UE5 binary
export NUM_SCHOLA_ENVS=2      # 2 game environments per UE5 binary (8 total)
export NUM_ITERATIONS=5

CHECKPOINT="/app/training_results/20260328_032546/best"
LOG_DIR="/app/logs"
mkdir -p "$LOG_DIR"

echo "=== Starting 4 UE5 binaries on ports 50051-50054 ==="
chmod +x /app/ue5/DE.sh

for PORT in 50051 50052 50053 50054; do
  /app/ue5/DE.sh \
    -RenderOffscreen -nullrhi -nosound -log \
    -port=$PORT \
    > "$LOG_DIR/ue5_${PORT}.log" 2>&1 &
  echo "UE5 started on port $PORT (PID $!)"
done

echo "Waiting 45s for all UE5 instances to initialize..."
sleep 45

echo "=== Starting distributed Ray training ==="
cd /app/training
python3 train.py \
  --mode rllib \
  --iterations $NUM_ITERATIONS \
  --resume "$CHECKPOINT" \
  2>&1 | tee "$LOG_DIR/train.log"

echo "=== Syncing results to S3 ==="
aws s3 sync /app/training_results/ \
  s3://$S3_BUCKET/results/ \
  --exclude "*.pyc" --region ap-northeast-2
echo "Done."
EOF

chmod +x /app/start_distributed.sh
```

---

## Step 4 — Update train.py config for distributed training

The `_resolve_port()` method in `env_wrapper.py` already handles per-worker port assignment automatically when `base_port` is passed.
You only need to update `DETrainingConfig` in `/app/training/train.py`:

```bash
# Change NUM_WORKERS and base_port in the config block
sed -i 's/NUM_WORKERS.*= int(os.environ.get.*NUM_WORKERS.*, 0))/NUM_WORKERS          = int(os.environ.get("NUM_WORKERS", 4))/' \
  /app/training/train.py
```

Then verify `create_ppo_config()` passes `base_port` (not `port`) in env_config.
Check with:
```bash
grep -n 'base_port\|"port"' /app/training/train.py
```

If it shows `"port": DETrainingConfig.PORT` instead of `"base_port"`, change it:
```bash
sed -i 's/"port": DETrainingConfig.PORT/"base_port": DETrainingConfig.PORT/' \
  /app/training/train.py
```

---

## Step 5 — Launch in tmux

```bash
export PATH="$HOME/.local/bin:$PATH"
tmux new-session -d -s training '/app/start_distributed.sh'
```

Monitor:
```bash
tmux attach -t training          # live output
tail -f /app/logs/train.log      # training log
tail -f /app/logs/ue5_50051.log  # UE5 instance 0 log
```

CloudWatch: Log group `de-training`, streams `train` and `ue5_*` (CloudWatch agent already configured).

---

## Step 6 — After training: stop the instance

```bash
# From your local PC
conda run -n gameai aws ec2 stop-instances \
  --instance-ids i-05bff238f7e833bd8 --region ap-northeast-2
```

Results are automatically synced to:
`s3://de-v10-2-dev-training-22de2017/results/<timestamp>/`

---

## Key notes for the next session

- **env_wrapper port logic**: `_resolve_port()` returns `base_port + max(0, worker_index - 1)`.
  With `base_port=50051` and `num_env_runners=4`: workers 1→4 get ports 50051, 50052, 50053, 50054.
- **NUM_SCHOLA_ENVS=2**: Each UE5 binary runs 2 game environments → 4 workers × 2 envs = 8 total (same as before but distributed).
- **Ray is already installed** at `~/.local/bin/` (Ray 2.52.1, matching checkpoint).
- **Schola is already installed** from `/app/schola_pkg`.
- **PyTorch 2.6+cu124 + CUDA 12.6** already installed.
- The instance was **stopped cleanly** — no data loss, all files at `/app/` are intact.
