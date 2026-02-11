# MOC v10.2 Training Setup

This directory contains the training infrastructure for MOC v10.2 Command-Driven Policy Training.

## Architecture Overview

**MOC v10.2** implements a hierarchical command-driven architecture:

- **Layer 1 (Strategic):** Squad Commander assigns strategies (Assault/Defend/Support) via MCTS
- **Layer 2 (Tactical):** Executors receive commands and output 8-dim EQS weights via RL policy
- **Layer 3 (Spatial):** EQS system performs spatial reasoning using the weights

## Files

### Core Training Scripts

- **`phase1_policy_training_v10_2.py`** - Main training implementation
  - `MultiHeadRLPolicy_v10_2` - Core policy network (3 strategy heads)
  - `MultiHeadRLPolicy_v10_2_RLlib` - RLlib wrapper for training
  - `train_with_rllib()` - Complete training loop

- **`moc_v10_2_env.py`** - Schola environment wrapper
  - `MOCv10_2MultiAgentEnv` - Multi-agent synchronous environment
  - Handles communication with UE5 via Schola gRPC

- **`train_v10_2.py`** - Quick start training script
  - Simplified CLI for starting training
  - Easy configuration via command-line arguments

## Quick Start

### Option A: Docker Training (Recommended for Windows)

Docker eliminates Windows + Ray/RLlib compatibility issues by running training in a Linux container.

#### 1. Prerequisites

- Docker Desktop installed on Windows
- UE5 Editor running with MOC project loaded
- Port 50051 open for gRPC communication

#### 2. Start UE5 Environment

1. Open your MOC project in UE5
2. Ensure Schola plugin is enabled
3. Configure for v10.2 (should already be set up)
4. Play in Editor (PIE)

#### 3. Run Training

```bash
# Navigate to MOC_Training directory
cd C:\Users\PC\Documents\GitHub\CORTEX\MOC_Training

# Run v10.2 training (builds image automatically)
docker-compose --profile v10.2 up --build training-v10.2
```

**Benefits:**
- ✅ No Windows multiprocessing issues
- ✅ Stable Ray object store (uses Linux 'fork' instead of Windows 'spawn')
- ✅ All dependencies pre-installed
- ✅ Consistent environment across machines

#### 4. Monitor Training

In a separate terminal:
```bash
docker-compose --profile v10.2 up tensorboard-v10.2
```

Then open: http://localhost:6006

#### 5. Access Results

Training results are saved to `MOC_Training/training_results_v10_2/` on your host machine.
Model checkpoints are saved to `MOC_Training/models/`.

---

### Option B: Native Python Training

For advanced users who want to run training directly on Windows.

#### 1. Install Dependencies

```bash
pip install ray[rllib] torch schola gymnasium numpy
```

#### 2. Start UE5 Environment

1. Open your MOC project in UE5
2. Ensure Schola plugin is enabled
3. Configure for v10.2:
   - Squad Commander should assign strategies (0=Assault, 1=Defend, 2=Support)
   - Agents should output 52-dim local observations
   - Action space should accept 8-dim EQS weights in range [-1, 1]
4. Play in Editor (PIE)

#### 3. Start Training

Basic usage:
```bash
python phase1_policy_training_v10_2.py --mode rllib --iterations 100
```

With custom settings:
```bash
python phase1_policy_training_v10_2.py --mode rllib --iterations 200 --host localhost --port 50051
```

**Note:** Native Windows training may encounter Ray multiprocessing issues. Docker is recommended.

## Training Configuration

Default hyperparameters (in `MOCv10_2TrainingConfig`):

```python
# Environment
HOST = "localhost"
PORT = 50051
NUM_UE5_ENVIRONMENTS = 4

# Network
HIDDEN_DIMS = [256, 256]  # Shared encoder

# PPO
LEARNING_RATE = 3e-4
TRAIN_BATCH_SIZE = 32000
SGD_MINIBATCH_SIZE = 2048
NUM_SGD_ITER = 10
GAMMA = 0.99
GAE_LAMBDA = 0.95
CLIP_PARAM = 0.2
ENTROPY_COEFF = 0.01
VF_LOSS_COEFF = 0.5
GRAD_CLIP = 0.5
```

## Network Architecture

### Input (55-dim)
- **Base Observation (52-dim):** Local agent state
  - Agent State (4): Health, Ammo, HasWeapon, Alive
  - Combat State (1): IsInCombat
  - Perception (16): EnemyCount, AllyCount, etc.
  - Support State (5): IsCoveringAlly, etc.
  - Enemy Info (16): Closest/Average enemy metrics
  - Tactical (4): CoverQuality, etc.
  - Objective (6): Distance, occupation state
- **Strategy One-Hot (3-dim):** [Assault, Defend, Support]

### Network
- **Shared Encoder:** [55 → 256 → 256] with LayerNorm
- **Strategy-Specific Heads:**
  - Assault Head: [256 → 64 → 8] + Tanh
  - Defend Head: [256 → 64 → 8] + Tanh
  - Support Head: [256 → 64 → 8] + Tanh
- **Value Heads (per strategy):**
  - Assault Value: [256 → 64 → 1]
  - Defend Value: [256 → 64 → 1]
  - Support Value: [256 → 64 → 1]

### Output (8-dim, range [-1, 1])
1. EnemyObjectiveProximity
2. AllyObjectiveProximity
3. CoverDensity
4. EnemyVisibility
5. AllyProximity
6. CombatRange
7. PickupProximity
8. HeightAdvantage

## Output

Training produces the following outputs in `training_results_v10_2/v10_2_YYYYMMDD_HHMMSS/`:

- **`moc_policy_v10_2.onnx`** - Trained policy for UE5 inference
- **`checkpoint_NNNNNN/`** - Periodic training checkpoints
- **`best/`** - Best performing checkpoint
- **`progress.csv`** - Training metrics
- **`params.json`** - Configuration snapshot
- **TensorBoard logs** - Training curves

## Monitoring Training

### Console Output

Training progress is displayed in console:
```
✓   12/100      45.23    150.5         32      4096    3.2s      45.23
```

Columns:
- Status (✓ = episode completed, → = collecting)
- Iteration / Total
- Mean Reward
- Mean Episode Length
- Episodes This Iter
- Agent Steps
- Iteration Time
- Best Reward So Far

### TensorBoard

Launch TensorBoard to view detailed metrics:
```bash
tensorboard --logdir training_results_v10_2
```

Key metrics:
- `episode_reward_mean` - Average reward per episode
- `episode_len_mean` - Average episode length
- `policy_loss` - Policy gradient loss
- `vf_loss` - Value function loss
- `entropy` - Policy entropy (exploration)

## Integration with UE5

### UE5 → Python (Observation)

UE5 sends 55-dim observation per agent:
```cpp
// In C++: AMocCharacter::CollectObservation()
TArray<float> Obs = GetLocalObservation();  // 52-dim
TArray<float> StrategyOneHot = GetCommandedStrategyOneHot();  // 3-dim
TArray<float> FullObs = Concat(Obs, StrategyOneHot);  // 55-dim
ScholaAgent->SendObservation(FullObs);
```

### Python → UE5 (Action)

Python sends 8-dim EQS weights:
```python
# In Python: policy outputs actions
eqs_weights = policy(obs, strategy_idx)  # Shape: (8,), Range: [-1, 1]
env.step({agent_id: eqs_weights})
```

### UE5 Action Reception

UE5 receives and applies EQS weights:
```cpp
// In C++: AMocCharacter::OnActionReceived()
TArray<float> EQSWeights = Action;  // 8-dim in [-1, 1]
TacticalParameterActuator->ApplyEQSWeights(EQSWeights);
```

## Troubleshooting

### Connection Issues

**Error:** `[ERROR] Connection failed: ...`

**Solution:**
1. Ensure UE5 is running and in PIE mode
2. Check Schola plugin is enabled
3. Verify port is correct (default: 50051)
4. Check firewall settings

### Observation Size Mismatch

**Error:** `Unexpected obs size ... for agent_X_Y`

**Solution:**
1. Verify UE5 sends 55-dim observations (52 base + 3 strategy)
2. Check `CollectObservation()` implementation in C++
3. Ensure strategy one-hot is appended correctly

### Action Size Mismatch

**Error:** `Expected 8-dim action, got ...`

**Solution:**
1. Check action space configuration in UE5
2. Verify `TacticalParameterActuator_v10_2` expects 8 inputs
3. Ensure action clamping is correct ([-1, 1] range)

### Training Instability

**Symptoms:** Loss exploding, rewards not improving

**Solutions:**
1. Reduce learning rate (try 1e-4)
2. Increase clip_param (try 0.3)
3. Check reward scale (should be in [-5, 5] range)
4. Verify value function is learning (check `vf_explained_var`)

### Low Reward

**Symptoms:** Rewards stuck near 0 or not improving

**Solutions:**
1. Check reward signal from UE5 (add logging)
2. Verify episode boundaries are correct
3. Ensure strategies are being assigned properly
4. Increase exploration (increase entropy_coeff)

## Advanced Usage

### Custom Hyperparameters

Modify `MOCv10_2TrainingConfig` in `phase1_policy_training_v10_2.py`:

```python
class MOCv10_2TrainingConfig:
    LEARNING_RATE = 1e-4  # Lower for stability
    ENTROPY_COEFF = 0.05  # Higher for exploration
    NUM_SGD_ITER = 15  # More updates per batch
    # ... etc
```

### Multi-GPU Training

Enable multi-GPU with Ray:
```python
config = config.resources(
    num_gpus=1,  # Use 1 GPU for training
    num_cpus_per_env_runner=1
)
```

### Curriculum Learning

Implement curriculum by modifying reward shaping in UE5 or adding wrapper in Python:

```python
# In moc_v10_2_env.py
def _parse_step_result_full(self, step_result):
    # ... existing code ...

    # Apply curriculum shaping
    if self.curriculum_stage == 1:
        # Stage 1: Emphasize survival
        reward_dict[flat_id] *= 0.5 if raw_reward < 0 else 1.5
```

## Performance Tips

1. **Batch Size:** Larger batches (32k-48k) improve stability but require more memory
2. **Parallel Environments:** More environments (4-8) increase sample efficiency
3. **Network Size:** Larger networks (512 hidden units) may improve performance but slow training
4. **Checkpointing:** Save frequently to avoid losing progress

## References

- [v10.2 Architecture Document](../../Source/GameAI_Project/CLAUDE.md)
- [RLlib Documentation](https://docs.ray.io/en/latest/rllib/)
- [Schola Documentation](https://github.com/BenedictWilkins/schola)
- [PPO Paper](https://arxiv.org/abs/1707.06347)

## Docker Configuration

### Available Services

The `docker-compose.yml` provides several services:

**Training Services:**
- `training-v10.2` - Main v10.2 policy training (profile: v10.2)
- `training-v10.2-example` - Architecture demo without UE5 (profile: v10.2-demo)

**Monitoring:**
- `tensorboard-v10.2` - TensorBoard for v10.2 training (profile: v10.2, port 6006)

**Diagnostics:**
- `test-connection` - TCP connection test (profile: test)
- `test-grpc` - gRPC connection test (profile: test)
- `diagnose-training` - Training failure diagnostics (profile: test)

### Docker Commands

**Run training:**
```bash
docker-compose --profile v10.2 up --build training-v10.2
```

**Run in background:**
```bash
docker-compose --profile v10.2 up -d training-v10.2
```

**View logs:**
```bash
docker-compose --profile v10.2 logs -f training-v10.2
```

**Stop training:**
```bash
docker-compose --profile v10.2 down
```

**Clean rebuild:**
```bash
docker-compose down --volumes
docker-compose --profile v10.2 build --no-cache
```

### Custom Docker Training

Override environment variables:
```bash
docker-compose run --rm \
  -e NUM_ITERATIONS=500 \
  -e PORT=50052 \
  training-v10.2 \
  python training/phase1_policy_training_v10_2.py \
    --mode rllib \
    --iterations 500 \
    --host host.docker.internal \
    --port 50052
```

### Docker Networking

- **Container → UE5**: Uses `host.docker.internal` to connect to UE5 on Windows host
- **Port Mapping**: No port mapping needed for gRPC client (Docker connects outbound to host:50051)
- **Insecure Channel**: Docker uses `grpc.insecure_channel()` for compatibility

### Docker Troubleshooting

**Error: Cannot connect to UE5**
```bash
# 1. Test TCP connection
docker-compose --profile test up test-connection

# 2. Test gRPC connection
docker-compose --profile test up test-grpc

# 3. Verify UE5 is listening on host
netstat -an | findstr "50051"
```

**Error: Docker build fails**
```bash
# Clean rebuild with no cache
docker-compose build --no-cache training-v10.2
```

**Error: Out of disk space**
```bash
# Clean up old Docker data
docker system prune -a --volumes
```

**Error: Ray workers crash**
- Increase Docker memory to 8GB+ (Docker Desktop → Settings → Resources)
- Ensure NUM_WORKERS=0 for Windows compatibility
- Check Docker logs for OOM errors

## Support

For issues or questions:
1. Check troubleshooting section above
2. Review UE5 logs in `Saved/Logs/`
3. Review Python logs and error messages
4. Check Docker logs: `docker-compose logs training-v10.2`
5. Check that UE5 and Python versions are compatible

---

**Last Updated:** 2026-02-11
**Version:** v10.2
**Status:** Ready for Training (Docker Recommended)
