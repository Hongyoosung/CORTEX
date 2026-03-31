# Docker Quick Start: DE v10.2 Training

## 🚀 Quick Commands

### Start Training (Recommended)

```bash
# 1. Navigate to this directory
cd C:\Users\PC\Documents\GitHub\DE\DE_Training

# 2. Ensure UE5 is running in PIE mode

# 3. Start training
docker-compose --profile v10.2 up --build training-v10.2
```

### Monitor Training

```bash
# In a separate terminal
docker-compose --profile v10.2 up tensorboard-v10.2

# Then open: http://localhost:6006
```

### View Logs

```bash
# Real-time logs
docker-compose logs -f training-v10.2

# Last 100 lines
docker-compose logs --tail=100 training-v10.2
```

### Stop Training

```bash
docker-compose down
```

---

## 📋 Prerequisites Checklist

- [ ] Docker Desktop installed on Windows
- [ ] UE5 Editor running with DE project
- [ ] UE5 in Play-In-Editor (PIE) mode
- [ ] Port 50051 available (Schola gRPC)
- [ ] At least 8GB RAM allocated to Docker (Settings → Resources)

---

## 🛠️ Common Tasks

### Change Number of Iterations

```bash
docker-compose run --rm \
  -e NUM_ITERATIONS=500 \
  training-v10.2 \
  python training/policy_training.py \
    --mode rllib \
    --iterations 500 \
    --host host.docker.internal \
    --port 50051
```

### Change Checkpoint Frequency

```bash
docker-compose run --rm \
  training-v10.2 \
  python training/policy_training.py \
    --mode rllib \
    --iterations 100 \
    --checkpoint-freq 5 \
    --host host.docker.internal \
    --port 50051
```

### Use Different Port

```bash
docker-compose run --rm \
  -e PORT=50052 \
  training-v10.2 \
  python training/ppolicy_training.py \
    --mode rllib \
    --host host.docker.internal \
    --port 50052
```

---

## 🔍 Diagnostics

### Test Connection

```bash
# TCP connection test
docker-compose --profile test up test-connection

# gRPC connection test
docker-compose --profile test up test-grpc

# Full training diagnostics
docker-compose --profile test up diagnose-training
```

### View Architecture Demo

```bash
# No UE5 connection needed
docker-compose --profile v10.2-demo up training-v10.2-example
```

---

## 📂 Output Locations

**Training Results:**
```
DE_Training/training_results/_<timestamp>/
├── de_policy.onnx    # Trained model for UE5
├── checkpoint_NNNNNN/       # Training checkpoints
├── best/                    # Best performing checkpoint
├── progress.csv             # Training metrics
└── events.out.tfevents.*   # TensorBoard logs
```

**Model Checkpoints:**
```
DE_Training/models/
└── de_policy.onnx    # Latest exported model
```

---

## ⚠️ Troubleshooting

### Cannot connect to UE5

1. Ensure UE5 is running and in PIE mode
2. Check Schola plugin is enabled
3. Verify port 50051 is listening:
   ```bash
   netstat -an | findstr "50051"
   ```
4. Test connection:
   ```bash
   docker-compose --profile test up test-grpc
   ```

### Docker build fails

```bash
# Clean rebuild
docker-compose down --volumes
docker-compose --profile v10.2 build --no-cache
```

### Training crashes / OOM errors

1. Increase Docker memory allocation:
   - Docker Desktop → Settings → Resources → Memory
   - Set to 8GB or higher

2. Verify NUM_WORKERS=0 in docker-compose.yml (required for Windows)

3. Close unnecessary applications

### No episodes completing

1. Check UE5 reward signal (add logging in C++)
2. Verify episode boundaries are correct
3. Check strategy assignment is working
4. Ensure agents can actually complete episodes

---

## 🎯 Training Goals

- **Target:** 100,000 transitions
- **Win Rate:** >40% vs baseline
- **Stability:** No divergence (loss should decrease)
- **Output:** 7-dim EQS weights in range [-1, 1]

---

## 📚 Architecture Overview

```
UE5 (Windows Host)              Docker Container (Linux)
┌─────────────────┐            ┌──────────────────────┐
│ Squad Commander │            │  RLlib PPO Trainer   │
│  (MCTS)         │            │                      │
│      ↓          │            │  ┌────────────────┐  │
│ Strategy Assign │            │  │ Multi-Head     │  │
│  ↓  ↓  ↓  ↓  ↓ │  gRPC      │  │ Policy v10.2   │  │
│ Executors (×5)  │◄──────────►│  │                │  │
│                 │  :50051    │  │ 3 Strategy     │  │
│ 52-dim Obs      │            │  │ Heads          │  │
│     ↓           │            │  └────────────────┘  │
│ 7-dim EQS ←─────┤            │         ↓            │
│ Weights         │            │  ONNX Export         │
└─────────────────┘            └──────────────────────┘
```

**Key Points:**
- Centralized planning (Squad Commander)
- Decentralized execution (5 agents)
- Command-driven (strategy assignment)
- 5× computational reduction vs v10.1

---

## 📖 More Information

- Full training guide: `training/README.md`
- Architecture specification: `../Source/GameAI_Project/CLAUDE.md`
- Docker configuration: `docker-compose.yml`
- Training implementation: `training/policy_training.py`

---

**Version:** v10.2  
**Last Updated:** 2026-02-11  
**Status:** Production Ready ✅
