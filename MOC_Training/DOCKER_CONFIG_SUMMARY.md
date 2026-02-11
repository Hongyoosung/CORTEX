# Docker Configuration Summary for v10.2 Training

## Changes Made

### 1. Updated Dockerfile

**File:** `MOC_Training/Dockerfile`

**Changes:**
- Updated header to v10.2
- Changed base path from `CORTEX_Training/` to `MOC_Training/`
- Now copies both legacy and v10.2 training scripts
- Maintains compatibility with Schola 1.3.0
- Keeps Linux environment for Ray/RLlib compatibility

**Key Features:**
- Base: Python 3.10-slim (Linux)
- Dependencies: Ray[rllib], PyTorch, Schola, Gymnasium
- Network: Configured for `host.docker.internal` (Windows host)
- Ports: 50051-50054 (gRPC), 6006 (TensorBoard)

---

### 2. Updated docker-compose.yml

**File:** `MOC_Training/docker-compose.yml`

**New Services Added:**

#### `training-v10.2` (Main v10.2 Training)
- Profile: `v10.2`
- Command: `python training/phase1_policy_training_v10_2.py --mode rllib --iterations 100`
- Volumes:
  - `./training_results_v10_2:/app/training_results_v10_2`
  - `./models:/app/models`
- Environment:
  - `NUM_WORKERS=0` (Windows compatibility)
  - `NUM_ITERATIONS=100`
  - `HOST=host.docker.internal`
  - `PORT=50051`

#### `training-v10.2-example` (Architecture Demo)
- Profile: `v10.2-demo`
- Command: `python training/phase1_policy_training_v10_2.py --mode example`
- No UE5 connection needed
- Shows network architecture and integration examples

#### `tensorboard-v10.2` (Monitoring)
- Profile: `v10.2`
- Port: `6006:6006`
- LogDir: `/app/training_results_v10_2`

**Legacy Services Updated:**
- Added `legacy` profile to existing services
- Changed legacy TensorBoard port to 6007 to avoid conflicts
- Maintained backward compatibility with v8.0/v10.1

---

### 3. Created Documentation

#### `training/README_v10_2.md` (Updated)
- Added comprehensive Docker section
- Docker vs Native Python comparison
- Troubleshooting guide
- Custom configuration examples

#### `DOCKER_QUICK_START_v10_2.md` (New)
- Quick reference guide
- Common commands
- Prerequisites checklist
- Troubleshooting tips
- Architecture diagram

---

## Docker Architecture

```
┌─────────────────────────────────────────────────────────┐
│ Windows Host                                            │
│                                                         │
│  ┌──────────────┐                 ┌─────────────────┐  │
│  │ UE5 Editor   │                 │ Docker Desktop  │  │
│  │              │                 │                 │  │
│  │ Schola gRPC  │◄────────────────┤ Linux Container │  │
│  │ Port: 50051  │  host.docker.   │                 │  │
│  │              │    internal     │ Python Training │  │
│  └──────────────┘                 │ Ray/RLlib       │  │
│                                   │ PyTorch         │  │
│  ┌──────────────┐                 └─────────────────┘  │
│  │ Web Browser  │                         │            │
│  │ localhost:   │◄────────────────────────┘            │
│  │  6006        │  Port Mapping                        │
│  │ TensorBoard  │                                      │
│  └──────────────┘                                      │
└─────────────────────────────────────────────────────────┘
```

**Networking:**
- UE5 → Docker: Outbound connection accepted from container
- Docker → UE5: `host.docker.internal:50051` (special DNS for Windows host)
- TensorBoard → Browser: Port 6006 mapped to host

---

## Usage Examples

### Basic Training

```bash
# Navigate to MOC_Training
cd C:\Users\PC\Documents\GitHub\CORTEX\MOC_Training

# Start UE5 in PIE mode

# Run training
docker-compose --profile v10.2 up --build training-v10.2
```

### With TensorBoard

Terminal 1:
```bash
docker-compose --profile v10.2 up training-v10.2
```

Terminal 2:
```bash
docker-compose --profile v10.2 up tensorboard-v10.2
```

Browser:
```
http://localhost:6006
```

### Custom Configuration

```bash
docker-compose run --rm \
  -e NUM_ITERATIONS=500 \
  -e PORT=50052 \
  training-v10.2 \
  python training/phase1_policy_training_v10_2.py \
    --mode rllib \
    --iterations 500 \
    --checkpoint-freq 10 \
    --host host.docker.internal \
    --port 50052
```

---

## Benefits of Docker Setup

### ✅ Advantages

1. **Windows Compatibility:** Eliminates Ray multiprocessing issues on Windows
2. **Consistent Environment:** Same dependencies across all machines
3. **Linux Performance:** Ray uses efficient 'fork' instead of Windows 'spawn'
4. **Easy Setup:** No manual dependency installation
5. **Isolation:** Training environment separate from host system
6. **Reproducibility:** Dockerfile ensures consistent builds

### ⚠️ Considerations

1. **Memory:** Docker needs 8GB+ RAM allocation
2. **Disk Space:** Images and volumes use ~5GB
3. **Network:** Slightly higher latency vs native (usually <5ms)
4. **GPU:** GPU passthrough requires additional configuration (not needed for CPU training)

---

## File Structure

```
CORTEX/
└── MOC_Training/
    ├── Dockerfile                           # v10.2 training image
    ├── docker-compose.yml                   # Service definitions
    ├── DOCKER_QUICK_START_v10_2.md         # Quick reference
    ├── DOCKER_CONFIG_SUMMARY.md            # This file
    ├── requirements.txt                     # Python dependencies
    │
    ├── training/
    │   ├── phase1_policy_training_v10_2.py # Main training script
    │   ├── moc_v10_2_env.py                # Environment wrapper
    │   ├── train_v10_2.py                  # Quick start script
    │   └── README_v10_2.md                 # Full documentation
    │
    ├── training_results_v10_2/             # Training output (auto-created)
    │   └── v10_2_<timestamp>/
    │       ├── moc_policy_v10_2.onnx       # Exported model
    │       ├── checkpoint_NNNNNN/          # Checkpoints
    │       └── best/                       # Best checkpoint
    │
    └── models/                              # Model exports (auto-created)
        └── moc_policy_v10_2.onnx           # Latest model
```

---

## Next Steps

1. **Verify Setup:**
   ```bash
   docker-compose --profile v10.2-demo up training-v10.2-example
   ```

2. **Test Connection:**
   ```bash
   docker-compose --profile test up test-grpc
   ```

3. **Start Training:**
   ```bash
   docker-compose --profile v10.2 up --build training-v10.2
   ```

4. **Monitor Progress:**
   ```bash
   docker-compose --profile v10.2 up tensorboard-v10.2
   ```

---

## Troubleshooting

### Build Issues

**Error:** Cannot find Dockerfile
```bash
# Ensure you're in MOC_Training directory
cd C:\Users\PC\Documents\GitHub\CORTEX\MOC_Training
```

**Error:** COPY failed (source not found)
```bash
# Verify directory structure
ls -la ../ | grep -E "(Plugins|MOC_Training)"
```

### Connection Issues

**Error:** Cannot connect to host.docker.internal
```bash
# Verify Docker networking
docker run --rm -it alpine ping -c 3 host.docker.internal
```

**Error:** Connection refused on port 50051
```bash
# Check UE5 is listening
netstat -an | findstr "50051"

# Verify UE5 is in PIE mode
# Check Schola plugin is enabled
```

### Runtime Issues

**Error:** Ray workers crash
```bash
# Increase Docker memory (Settings → Resources → Memory: 8GB+)
# Verify NUM_WORKERS=0 in docker-compose.yml
```

**Error:** Out of disk space
```bash
# Clean up Docker
docker system prune -a --volumes
```

---

## Reference

- **Dockerfile:** Configuration for Linux training environment
- **docker-compose.yml:** Service orchestration
- **phase1_policy_training_v10_2.py:** Core training implementation
- **moc_v10_2_env.py:** Schola environment wrapper
- **CLAUDE.md:** v10.2 architecture specification

---

**Configuration Date:** 2026-02-11  
**Version:** v10.2  
**Status:** Ready for Production ✅
