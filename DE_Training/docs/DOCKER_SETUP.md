# DE v8.0 Docker Training Setup

## Why Docker?

Running the Python training in a **Linux Docker container** solves critical Windows + Ray/RLlib compatibility issues:

| Issue (Windows) | Solution (Docker Linux) |
|-----------------|-------------------------|
| Ray object store corruption after first iteration | Linux uses stable 'fork' multiprocessing |
| Worker deadlocks and memory leaks | Better resource isolation and cleanup |
| Plasma store instability | Linux Plasma implementation is more robust |
| gRPC connection flakiness | Dedicated network namespace |

**Architecture:**
```
┌─────────────────────────────────────────────────────────────┐
│  Windows Host                                                │
│  ┌────────────────────────┐                                  │
│  │  UE5 + Schola Plugin   │                                  │
│  │  Port: 50051           │                                  │
│  └────────────┬───────────┘                                  │
│               │ gRPC                                         │
│               ↓                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  Docker Container (Linux)                              │ │
│  │  ┌──────────────────────────────────────────────────┐ │ │
│  │  │  Python 3.10 + Ray/RLlib + PyTorch + Schola     │ │ │
│  │  │  Connection: host.docker.internal:50051          │ │ │
│  │  │  Multiprocessing: fork (Linux-only, efficient)   │ │ │
│  │  └──────────────────────────────────────────────────┘ │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## Prerequisites

1. **Docker Desktop for Windows** (installed and running)
   - Download: https://www.docker.com/products/docker-desktop
   - Verify: `docker --version` (should show v20.10+)
   - Verify: `docker-compose --version` (should show v2.0+)

2. **UE5 with Schola plugin** (running on Windows host)
   - Schola gRPC server must be listening on port 50051
   - Verify in UE5 Output Log: `[Schola] gRPC server started on port 50051`

3. **Windows Defender Firewall** (allow port 50051)
   - Docker containers need access to host port 50051
   - Add inbound rule for TCP port 50051 if needed

---

## Quick Start

### 1. Build Docker Image

From the **DE repository root** (not DE_Training):

```bash
cd C:\Users\Foryoucom\Documents\GitHub\DE
docker-compose -f DE_Training/docker-compose.yml build training-single
```

**Expected output:**
```
[+] Building 120.5s (18/18) FINISHED
 => [internal] load .dockerignore
 => [internal] load build definition from Dockerfile
 => CACHED [1/10] FROM python:3.10-slim
 => [2/10] RUN apt-get update && apt-get install -y build-essential git curl
 => [3/10] COPY DE_Training/requirements.txt .
 => [4/10] RUN pip install --no-cache-dir -r requirements.txt
 => [5/10] COPY Plugins/Schola-1.3.0/Resources/python /tmp/schola
 => [6/10] RUN pip install --no-cache-dir /tmp/schola[rllib]
 => [7/10] COPY DE_Training/ .
 => [8/10] RUN python patch_schola_insecure.py
 => [9/10] RUN python -c "import ray; import torch; import schola"
 => exporting to image
 => => naming to docker.io/library/DE_training-training-single
✓ All packages installed
```

---

### 2. Start UE5 Simulation

1. Open UE5 project: `DE.uproject`
2. Load map: `Content/Maps/Arena_4v4.umap`
3. Press **Play** (PIE mode) or **Simulate**
4. Verify in Output Log:
   ```
   [Schola] gRPC server started on port 50051
   [ScholaEnv] Environment initialized - ready for training
   ```

---

### 3. Test Connection (Optional)

Verify Docker can reach UE5:

```bash
cd C:\Users\Foryoucom\Documents\GitHub\DE
docker-compose -f DE_Training/docker-compose.yml run --rm test-connection
```

**Expected output:**
```
Connecting to host.docker.internal:50051...
✓ Connection successful!
✓ 4 agents detected
Ready for training!
```

---

### 4. Run Training

```bash
cd C:\Users\Foryoucom\Documents\GitHub\DE
docker-compose -f DE_Training/docker-compose.yml run --rm training-single
```

**Expected output:**
```
============================================================
DE v8.0 Training
============================================================
  Host: host.docker.internal:50051
  Iterations: 100

Initializing Ray...
Ray initialized successfully
  Object store: 2GB
  Temp dir: /tmp/ray_DE

Connecting to UE5...
[ENV v8.0] Connecting to host.docker.internal:50051...
[ENV v8.0] Connected!
Connected!

================================================================================
[TRAIN LOOP] Starting iteration 1/100
================================================================================
[TRAIN LOOP] Calling algo.train()... Time=1737140123.45
[STEP 100] Episode=0, Time=10.5s
[STEP 200] Episode=0, Time=21.2s
...
[TRAIN LOOP] algo.train() returned. Duration=168.0s
[ 1/100] reward=0.00, len=0.0, episodes=0, steps=8000, time=168.0s
New best: 0.00
[TRAIN LOOP] Running garbage collection...
[TRAIN LOOP] Iteration 1 complete

================================================================================
[TRAIN LOOP] Starting iteration 2/100  ← Should NOT block here anymore!
================================================================================
```

---

### 5. Monitor Training (Optional)

In a separate terminal, start TensorBoard:

```bash
cd C:\Users\Foryoucom\Documents\GitHub\DE
docker-compose -f DE_Training/docker-compose.yml up tensorboard
```

Open browser: http://localhost:6006

---

## Output Files

Training results are saved to `DE_Training/training_results/` (mounted volume):

```
DE_Training/training_results/
├── 20260117_123456/              # Timestamp directory
│   ├── DE_policy_v8.onnx     # Exported ONNX model
│   ├── best/                     # Best checkpoint
│   │   ├── policies/
│   │   └── checkpoint.json
│   ├── checkpoint_000010/        # Periodic checkpoints
│   └── events.out.tfevents.*     # TensorBoard logs
```

---

## Docker Commands Reference

### Build Image
```bash
docker-compose -f DE_Training/docker-compose.yml build training-single
```

### Run Training (interactive)
```bash
docker-compose -f DE_Training/docker-compose.yml run --rm training-single
```

### Run Training (detached, background)
```bash
docker-compose -f DE_Training/docker-compose.yml up -d training-single
docker-compose -f DE_Training/docker-compose.yml logs -f training-single  # View logs
```

### Stop Training
```bash
docker-compose -f DE_Training/docker-compose.yml down
```

### Clean Up Docker Resources
```bash
# Remove containers
docker-compose -f DE_Training/docker-compose.yml down -v

# Remove image (rebuild required)
docker rmi DE_training-training-single

# Remove all dangling images/volumes
docker system prune -a --volumes
```

### Shell Into Container (debugging)
```bash
docker-compose -f DE_Training/docker-compose.yml run --rm training-single bash
```

---

## Troubleshooting

### Issue: "Cannot connect to host.docker.internal:50051"

**Cause:** Docker can't reach UE5 on Windows host.

**Solution:**
1. Verify UE5 is running and Schola server is active (check Output Log)
2. Check Windows Defender Firewall allows port 50051:
   ```powershell
   netsh advfirewall firewall add rule name="Schola gRPC" dir=in action=allow protocol=TCP localport=50051
   ```
3. Verify `host.docker.internal` resolves:
   ```bash
   docker-compose -f DE_Training/docker-compose.yml run --rm test-connection
   ```

---

### Issue: "Ray object store full"

**Cause:** Insufficient object store memory (default 2GB).

**Solution:** Increase in `train_rllib.py` (already set to 2GB in latest version):
```python
ray.init(object_store_memory=4 * 1024**3)  # 4GB
```

---

### Issue: Build fails on "COPY Plugins/Schola-1.3.0/Resources/python"

**Cause:** Docker build context doesn't include Plugins directory.

**Solution:** Ensure you're building from repository root:
```bash
cd C:\Users\Foryoucom\Documents\GitHub\DE  # Repository root
docker-compose -f DE_Training/docker-compose.yml build
```

---

### Issue: Training still blocks after iteration 1

**Symptoms:** Same as Windows - iteration 1 completes, iteration 2 blocks.

**Possible Causes:**
1. **UE5 not resetting properly** - Check UE5 logs for reset errors
2. **gRPC connection dropped** - UE5 may have crashed/frozen
3. **Schola version mismatch** - Ensure Schola 1.3.0+ is installed

**Debug Steps:**
1. Check UE5 Output Log for errors during episode reset
2. Verify gRPC connection is still active:
   ```bash
   docker-compose -f DE_Training/docker-compose.yml run --rm test-connection
   ```
3. Enable verbose logging in container:
   ```bash
   docker-compose -f DE_Training/docker-compose.yml run --rm \
     -e PYTHONUNBUFFERED=1 -e RAY_LOG_TO_STDERR=1 training-single
   ```

---

## Comparison: Windows vs Docker

| Metric | Windows (Native) | Docker (Linux) |
|--------|------------------|----------------|
| Ray initialization | 5-10s | 3-5s |
| First iteration | ✓ Works | ✓ Works |
| Second iteration | ❌ Blocks | ✓ Should work |
| Multiprocessing | spawn (slow) | fork (fast) |
| Object store stability | Poor | Good |
| Memory cleanup | Manual GC needed | Automatic |
| Debugging | Easier | Requires docker exec |

---

## Advanced: Multi-Worker Training

For 4 parallel UE5 instances (advanced users):

1. Start 4 UE5 instances on ports 50051-50054
2. Build multi-worker image:
   ```bash
   docker-compose -f DE_Training/docker-compose.yml build training-multi
   ```
3. Run multi-worker training:
   ```bash
   docker-compose -f DE_Training/docker-compose.yml run --rm training-multi
   ```

**Warning:** This requires 4× GPU memory and CPU resources.

---

## Next Steps

Once training completes successfully:

1. **Export ONNX model:** Automatically saved to `training_results/<timestamp>/DE_policy_v8.onnx`
2. **Import to UE5:** Copy ONNX file to `Content/AI/NNE/DE_policy_v8.onnx`
3. **Configure NNE:** Set `RLPolicyNetwork` to use NNE inference mode
4. **Test in PIE:** Agents should use learned tactical parameters

---

## Files Created/Modified

- ✅ `DE_Training/Dockerfile` - Updated for v8.0
- ✅ `DE_Training/docker-compose.yml` - Existing, compatible
- ✅ `DE_Training/patch_schola_insecure.py` - Created
- ✅ `DE_Training/DOCKER_SETUP.md` - This file

---

**Version:** v8.0
**Last Updated:** 2026-01-17
**Status:** Ready for testing
