# AWS Setup Guide — DE v10.2 Training

This guide walks through provisioning AWS infrastructure, building the Docker image, and launching distributed RLlib training for DE v10.2 (Commander-Executor architecture).

---

## Prerequisites

Install the following tools locally:

| Tool | Version | Purpose |
| :--- | :--- | :--- |
| AWS CLI | v2 | S3, ECR, IAM operations |
| Terraform | >= 1.5 | Infrastructure provisioning |
| Docker | latest | Image build & push |
| Ray | >= 2.x | Cluster management |
| Python | 3.10 | `launch_training.py` |

```bash
pip install ray boto3 wandb
```

Configure your AWS credentials:

```bash
aws configure
# AWS Access Key ID: ...
# AWS Secret Access Key: ...
# Default region: us-east-1
```

---

## Step 1 — Provision Infrastructure with Terraform

`aws/terraform/main.tf` creates all required AWS resources:

- **VPC** with public (head node) and private (workers) subnets
- **NAT Gateway** for worker outbound access
- **Security Groups** for head and worker nodes
- **S3 bucket** for checkpoints and metrics (versioned, AES-256 encrypted, lifecycle 30 days)
- **IAM role + instance profile** for EC2 nodes (S3 access without static keys)
- **SSH key pair**

```bash
cd aws/terraform

terraform init
terraform plan
terraform apply
```

After `apply`, note the outputs — you will need them in the next step:

```
public_subnet_id         = subnet-xxxxxxxxxxxxxxxxx
private_subnet_id        = subnet-xxxxxxxxxxxxxxxxx
head_sg_id               = sg-xxxxxxxxxxxxxxxxx
worker_sg_id             = sg-xxxxxxxxxxxxxxxxx
s3_bucket_name           = de-v10-2-dev-training-xxxxxxxx
iam_instance_profile_name = de-v10-2-dev-ray-node-profile
key_pair_name            = de-v10-2-dev-key
```

### Security note

The default `allowed_ssh_cidr = "0.0.0.0/0"` is open to the internet. In production, restrict it to your IP:

```bash
terraform apply -var="allowed_ssh_cidr=<your-ip>/32"
```

---

## Step 2 — Fill in `cluster.yaml` Placeholders

Open `aws/cluster.yaml` and replace the four `REPLACE_WITH_*` placeholders with the Terraform outputs:

```yaml
# Head node
subnet_id: subnet-REPLACE_WITH_PUBLIC_SUBNET_ID     → public_subnet_id
security_group_ids:
  - sg-REPLACE_WITH_HEAD_SG_ID                       → head_sg_id

# Worker nodes
SubnetId: subnet-REPLACE_WITH_PRIVATE_SUBNET_ID     → private_subnet_id
SecurityGroupIds:
  - sg-REPLACE_WITH_WORKER_SG_ID                     → worker_sg_id
```

Also update the `docker.image` field with your ECR account ID (see Step 3).

---

## Step 3 — Build and Push the Docker Image

`aws/Dockerfile.aws` is based on `nvidia/cuda:12.1.1-cudnn8-runtime-ubuntu20.04` and includes:

- Python 3.10, Ray, RLlib, PyTorch (CUDA 12.1)
- `s3fs` + `awscli` for checkpoint syncing
- Schola gRPC bridge (`Plugins/Schola-2.0.1`)
- Non-root user `de` for security
- `aws/scripts/mount_s3.sh` for S3 mounting

Replace `<account>` with your 12-digit AWS account ID:

```bash
# Authenticate to ECR
aws ecr get-login-password --region us-east-1 \
  | docker login --username AWS --password-stdin \
      <account>.dkr.ecr.us-east-1.amazonaws.com

# Create the ECR repository (first time only)
aws ecr create-repository --repository-name de --region us-east-1

# Build from repo root
docker build -f aws/Dockerfile.aws \
  -t de:v10-2-latest \
  .

# Tag and push
docker tag de:v10-2-latest \
  <account>.dkr.ecr.us-east-1.amazonaws.com/de:v10-2-latest

docker push <account>.dkr.ecr.us-east-1.amazonaws.com/de:v10-2-latest
```

Update `cluster.yaml` with the full image URI:

```yaml
docker:
  image: "<account>.dkr.ecr.us-east-1.amazonaws.com/de:v10-2-latest"
```

---

## Step 4 — Launch Training

`aws/launch_training.py` automates the full workflow:

1. `ray up` — bring up the cluster (1 head + up to 4 workers)
2. `ray submit` — push the training script
3. Poll `s3://<bucket>/metrics/latest.json` every 30 s for win-rate / step count
4. Tear down the cluster when a termination condition is met

### Cluster topology

| Node | Instance | Count | Purpose |
| :--- | :--- | :--- | :--- |
| Head | `g4dn.xlarge` (on-demand) | 1 | Policy inference (GPU), Ray GCS |
| Workers | `c5.2xlarge` (Spot) | 0–4 | Rollout collection (CPU) |

### Run

```bash
export WANDB_API_KEY=<your-key>

python aws/launch_training.py \
  --s3-bucket  <s3_bucket_name_from_terraform> \
  --wandb-project de-v10-2 \
  --reward-threshold 0.40 \
  --max-steps 100000 \
  -y
```

### Useful flags

| Flag | Default | Description |
| :--- | :--- | :--- |
| `--reward-threshold` | `0.40` | Stop when win-rate ≥ this value |
| `--max-steps` | `100000` | Hard cap on environment steps |
| `--poll-interval` | `30` | Seconds between S3 metric polls |
| `--skip-cluster-up` | false | Skip `ray up` (cluster already running) |
| `--skip-cluster-down` | false | Keep cluster alive after training (for debugging) |
| `-y` | false | Auto-confirm all Ray prompts |

### Termination conditions (whichever comes first)

- `win_rate >= reward_threshold`
- `total_steps >= max_steps`
- Training script sets `training_complete: true` in `metrics/latest.json`
- `ray submit` process exits

---

## Step 5 — Monitor Training

### Ray Dashboard

```bash
ray dashboard aws/cluster.yaml
# Opens http://<head-ip>:8265
```

### W&B

All metrics are logged to the `de-v10-2` W&B project via `WandbLoggerCallback` (`rllib_config.py`). Key metrics:

| Metric | Description |
| :--- | :--- |
| `custom_metrics/win_rate` | Rolling win rate (primary target ≥ 0.40) |
| `episode_reward_mean` | Mean composite reward per episode |
| `policy_reward_mean/assault_policy` | Per-strategy rewards |
| `policy_reward_mean/defend_policy` | |
| `policy_reward_mean/support_policy` | |
| `num_env_steps_sampled_lifetime` | Total environment steps |

### S3 Metrics (programmatic)

The training script writes `metrics/latest.json` to S3 after every iteration:

```json
{
  "iteration": 42,
  "total_steps": 50000,
  "win_rate": 0.31,
  "episode_reward": 1.24,
  "training_complete": false,
  "timestamp": 1711234567.89
}
```

---

## Step 6 — Retrieve Checkpoints

Best checkpoints are automatically synced to `s3://<bucket>/checkpoints/best/` when a new best reward is reached.

```bash
# List checkpoints
aws s3 ls s3://<bucket>/checkpoints/best/

# Download
aws s3 sync s3://<bucket>/checkpoints/best/ ./checkpoints/best/
```

Intermediate checkpoints expire after **30 days** (S3 lifecycle rule).

---

## Teardown

The launcher tears down the cluster automatically. To tear down manually:

```bash
ray down aws/cluster.yaml -y
```

To destroy all AWS infrastructure:

```bash
cd aws/terraform
terraform destroy
```

> **Warning:** `terraform destroy` will delete the S3 bucket only if `force_destroy = true`. By default (`false`), the bucket must be emptied first to avoid accidental data loss.

---

## S3 Mount Helper

`aws/scripts/mount_s3.sh` is pre-installed inside the container at `/usr/local/bin/mount_s3.sh`. It mounts the training bucket via `s3fs` using the EC2 IAM role (no credentials required).

The cluster initialization commands in `cluster.yaml` call this automatically. To run manually inside a node:

```bash
S3_BUCKET=<bucket-name> mount_s3.sh
# Mounts to /mnt/s3-training by default
```

---

## RLlib Configuration Reference

`aws/rllib_config.py` defines the PPO training configuration for DE v10.2:

| Parameter | Value | Notes |
| :--- | :--- | :--- |
| Algorithm | PPO | |
| Observation space | Box(54,) | 51 agent obs + 3 strategy one-hot |
| Action space | Box(6,) in [-1, 1] | 6-dim EQS weights |
| Policies | 3 | `assault_policy`, `defend_policy`, `support_policy` |
| Network | 256×256 MLP | `vf_share_layers=False` |
| `train_batch_size` | 4096 | |
| `sgd_minibatch_size` | 512 | |
| `num_sgd_iter` | 10 | |
| `lr` | 3e-4 | |
| `gamma` | 0.99 | |
| `lambda_` | 0.95 | GAE |
| `clip_param` | 0.2 | PPO clip |
| `entropy_coeff` | 0.01 | |
| `num_rollout_workers` | 4 | Matches cluster worker count |

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
| :--- | :--- | :--- |
| `ray up` fails with SSH timeout | Security group / key mismatch | Verify `head_sg_id` and `ssh_private_key` in `cluster.yaml` |
| Docker pull fails | ECR auth expired | Re-run `aws ecr get-login-password` |
| S3 metrics not appearing | IAM role missing | Confirm instance profile name matches Terraform output |
| Spot interruption mid-training | Spot market | Run with `--skip-cluster-up` after manual restart; checkpoints on S3 allow resume |
| `win_rate` stuck near 0 | `bDataCollectionMode=true` in UE5 | Set `bDataCollectionMode=false` in `ASquadManager` for evaluation runs |
