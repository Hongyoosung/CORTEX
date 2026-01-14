# CORTEX Training Environment

Docker-based training environment for the CORTEX Multi-Agent Combat AI system. Solves Windows multiprocessing issues with Ray RLlib.

## Current Version: v8.0 (Tactical Parameters + Combat Control)

**Architecture:**
- MCTS assigns strategies → RL outputs tactical parameters + combat priority
- Multi-head policy network (separate heads per strategy)
- Hybrid action space: 4 continuous tactical params [0,1] + 2 discrete combat choices
- Curriculum learning (3 phases: Single → Mixed → Dynamic)
- Unified reward system with strategy-specific weights

**Training:**
- PPO with continuous action tuning (LR=5e-5, batch=8000)
- Parameter monitoring (strategy differentiation tracking)
- Exports to: `cortex_policy_v8.onnx`

## Deprecated: v6.0 (2026-01-14)

**v6.0 Architecture:**
- Single-head strategy selection (4 discrete actions: Assault, Defend, Support, Retreat)
- Exported to: `cortex_policy_v6.onnx`
- Archived to: `Content/AI/Models/v7.0-archive/`
- Rollback: Use branch `v6.0-stable`

## Problem Statement

### Windows Multiprocessing Issue

When `NUM_WORKERS > 0` in Ray RLlib, Windows uses `spawn()` instead of `fork()` for multiprocessing, causing DLL path issues:

**Linux/macOS (fork):**
```python
parent_process = {
    "memory": "torch DLL already loaded",
    "dll_paths": "already in sys.path"
}
child_process = copy(parent_process)  # Memory copied
# ✅ No problem - torch already loaded
```

**Windows (spawn):**
```python
child_process = {
    "memory": "empty (fresh Python interpreter)",
    "dll_paths": "not inherited from parent"
}
# ❌ Must import torch from scratch, but DLL paths missing
```

### Solution: Docker with Linux Base

Docker containers run Linux, which uses `fork()` for multiprocessing. This eliminates DLL path issues and enables parallel training.

## Quick Start

### Prerequisites

1. **Docker Desktop** - Install from [docker.com](https://www.docker.com/products/docker-desktop)
2. **Unreal Engine 5.6** - With Schola plugin configured
3. **Git** - For cloning the repository

### Installation

1. Navigate to Training directory:
```bash
cd Training
```

2. Build Docker image:
```bash
docker-compose build
```

### Running Training

#### Option 1: Single-Worker Training (Recommended for Testing)

```bash
# Windows
run_docker_training.bat

# Linux/macOS
./run_docker_training.sh
```

Select option **1** for single-worker mode (`NUM_WORKERS=0`).

#### Option 2: Manual Docker Commands

**Single-worker:**
```bash
docker-compose --profile single up
```

**Multi-worker (requires 4 UE instances on ports 50051-50054):**
```bash
docker-compose --profile multi up
```

**TensorBoard monitoring:**
```bash
docker-compose --profile monitor up
```
Then open http://localhost:6006

### Training Workflow

1. **Start Unreal Engine:**
   - Open CORTEX project in UE5.6
   - Start PIE (Play in Editor) or standalone game
   - Schola plugin listens on port 50051 (default)

2. **Start Training:**
   - Run `run_docker_training.bat` (Windows) or `./run_docker_training.sh` (Linux/macOS)
   - Select single-worker mode (option 1)
   - Training begins automatically

3. **Monitor Progress:**
   - Check console output for iteration metrics
   - Start TensorBoard: `docker-compose --profile monitor up`
   - View at http://localhost:6006

4. **Export Model:**
   - Training auto-exports ONNX model to `training_results/<timestamp>/rl_policy_network.onnx`
   - Copy to UE: `Content/Models/rl_policy_network.onnx`

## Project Structure

```
Training/
├── Dockerfile               # Docker image definition
├── docker-compose.yml       # Multi-container orchestration
├── .dockerignore           # Files excluded from Docker build
├── requirements.txt        # Python dependencies
├── train_rllib.py          # Main training script
├── sbdapm_env.py          # RLlib environment wrapper
├── run_docker_training.bat # Windows launcher
├── run_docker_training.sh  # Linux/macOS launcher
├── training_results/       # Training outputs (mounted volume)
├── models/                 # Exported ONNX models (mounted volume)
└── README.md              # This file
```

## Configuration

### Environment Variables

Edit `docker-compose.yml` to customize:

| Variable | Default | Description |
|----------|---------|-------------|
| `NUM_WORKERS` | 0 | Number of parallel workers (0 = local only) |
| `NUM_ITERATIONS` | 100 | Training iterations |
| `HOST` | `host.docker.internal` | UE Schola gRPC host |
| `PORT` | 50051 | Base gRPC port |

### Training Parameters

Edit `train_rllib.py` `SBDAPMConfig` class:

```python
class SBDAPMConfig:
    # PPO hyperparameters
    LEARNING_RATE = 3e-4
    TRAIN_BATCH_SIZE = 4000
    SGD_MINIBATCH_SIZE = 128
    NUM_SGD_ITER = 10
    GAMMA = 0.99

    # Workers (0 = single process, 4 = 4 parallel UE instances)
    NUM_WORKERS = 0
    NUM_ITERATIONS = 100
    CHECKPOINT_FREQ = 10
```

## Multi-Worker Training

For parallel training with `NUM_WORKERS > 0`:

### Requirements

1. Run multiple UE instances on different ports:
   - Instance 1: Port 50051
   - Instance 2: Port 50052
   - Instance 3: Port 50053
   - Instance 4: Port 50054

2. Update `train_rllib.py` config:
```python
NUM_WORKERS = 4
```

3. Run multi-worker training:
```bash
docker-compose --profile multi up
```

### Port Configuration

Each worker connects to UE via `base_port + worker_index`:
- Worker 0 → Port 50051
- Worker 1 → Port 50052
- Worker 2 → Port 50053
- Worker 3 → Port 50054

## Troubleshooting

### Docker Issues

**Docker daemon not running:**
```bash
# Start Docker Desktop application
```

**Container can't connect to UE:**
- Verify UE is running and Schola plugin active
- Check port 50051 is not blocked by firewall
- On Windows, ensure Docker Desktop uses WSL2 backend

**Build fails with dependency errors:**
```bash
# Rebuild without cache
docker-compose build --no-cache
```

### Training Issues

**"Connection refused" error:**
- Ensure UE is running before starting training
- Check Schola plugin configuration
- Verify port matches (default: 50051)

**Training doesn't converge:**
- Reduce learning rate: `LEARNING_RATE = 1e-4`
- Increase batch size: `TRAIN_BATCH_SIZE = 8000`
- Check reward signals in TensorBoard

**Out of memory:**
- Reduce batch size: `TRAIN_BATCH_SIZE = 2000`
- Reduce workers: `NUM_WORKERS = 2`
- Increase Docker memory limit (Docker Desktop → Settings → Resources)

## Performance Benchmarks

| Mode | NUM_WORKERS | UE Instances | Training Speed | Best For |
|------|-------------|--------------|----------------|----------|
| Single | 0 | 1 | 1x | Testing, debugging |
| Multi | 4 | 4 | 3-4x | Production training |

## Model Export

After training, models are automatically exported to ONNX format:

**Location:** `training_results/<timestamp>/rl_policy_network.onnx`

**Structure:**
- Input: 78 features (71 observation + 7 objective embedding)
- Output 1 (Actor): 8 dims (atomic actions)
- Output 2 (Critic): 1 dim (state value)

**Integration with UE:**
1. Copy ONNX file to `Content/Models/`
2. RLPolicyNetwork component loads dual-head model
3. Actor head → action selection
4. Critic head → MCTS value estimation

## Development

### Rebuilding Image

After modifying Python code:
```bash
docker-compose build
```

### Accessing Container Shell

For debugging:
```bash
docker-compose run training-single /bin/bash
```

### Mounting Local Code

For live development, edit `docker-compose.yml`:
```yaml
volumes:
  - ./:/app  # Mount entire directory
  - ./training_results:/app/training_results
```

## References

- **Ray RLlib:** [docs.ray.io/en/latest/rllib](https://docs.ray.io/en/latest/rllib/)
- **Schola Plugin:** AMD Schola UE5 integration
- **Docker Desktop:** [docker.com/products/docker-desktop](https://www.docker.com/products/docker-desktop)
- **PPO Paper:** [Proximal Policy Optimization (Schulman et al., 2017)](https://arxiv.org/abs/1707.06347)

## Next Steps

1. **Run baseline training** - 100 iterations, single-worker
2. **Evaluate convergence** - Monitor TensorBoard metrics
3. **Export best model** - Copy ONNX to UE Content/Models/
4. **Test in UE** - Verify agent behavior with new policy
5. **Scale to multi-worker** - Run 4 UE instances for parallel training

---

**Version:** 1.0
**Last Updated:** 2025-12-12
**Compatibility:** UE 5.6, Ray RLlib 2.7+, Python 3.10
