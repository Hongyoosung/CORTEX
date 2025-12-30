# SBDAPM Training Workflow Guide
**Version:** v3.1 | **Date:** 2025-12-13

---

## Table of Contents
1. [Training Phases Overview](#training-phases-overview)
2. [Phase 1: Local Development Training](#phase-1-local-development-training)
3. [Phase 2: Docker Local Multi-Instance](#phase-2-docker-local-multi-instance)
4. [Phase 3: AWS Distributed Training](#phase-3-aws-distributed-training)
5. [Level Progression Strategy](#level-progression-strategy)
6. [Monitoring & Debugging](#monitoring--debugging)
7. [Model Export & Deployment](#model-export--deployment)

---

## Training Phases Overview

```
Phase 1: Local Dev (1 UE instance)
  ├─ Goal: Validate training loop, debug issues
  ├─ Duration: T1 only (~1-2 hours)
  └─ Environment: Windows/Mac, local Python

Phase 2: Docker Local (2-4 UE instances)
  ├─ Goal: Test parallel training, benchmark speedup
  ├─ Duration: T2-T3 (~5-7 hours total)
  └─ Environment: Docker containers, local UE instances

Phase 3: AWS Distributed (8+ UE instances)
  ├─ Goal: Full curriculum (T4-T10), large-scale training
  ├─ Duration: T4-T10 (~20-30 hours total)
  └─ Environment: EC2 g4dn.xlarge fleet, Docker orchestration
```

---

## Phase 1: Local Development Training

### Prerequisites
- [x] UE5 project compiled
- [x] Schola plugin installed
- [x] Python 3.10+ with RLlib installed
- [x] Level T1_BasicCombat_2v2 created
- [x] Spawn points added (TeamASpawn, TeamBSpawn)
- [x] ~~**FPS capped in Config/DefaultEngine.ini**~~ **NO LONGER NEEDED** (action throttling handles this automatically)

### Step-by-Step Instructions

#### 1.0 FPS-Independent Training (2025-12-16 Update) ✅

**Previous Issue (FIXED):** Actions were frame-rate dependent, causing:
- **High FPS (30+):** Agents oscillated in place (actions applied too frequently)
- **Low FPS (5 or lower):** Agents moved normally

**Root Cause:** `AddMovementInput()` was called every UE tick, but Schola actions arrive at ~10-20 Hz. At 60 FPS, the same action was applied 6x more than needed, causing overcorrection.

**Solution (Implemented 2025-12-16):** Action throttling in `STTask_ExecuteObjective`

All actions now execute at a **fixed rate (20 Hz by default)** regardless of rendering FPS. This is controlled by the `ActionApplicationInterval` parameter (default: 0.05s = 20 Hz).

**Key Changes:**
- `STTask_ExecuteObjective.h:65` - Added `ActionApplicationInterval` parameter
- `STTask_ExecuteObjective.cpp:86-101` - Action throttling in Tick()
- `Config/DefaultEngine.ini` - FPS locking disabled (no longer needed)

**Benefits:**
- ✅ Consistent training at any FPS (30, 60, 144 Hz all behave identically)
- ✅ No need to lock FPS (can run at maximum performance)
- ✅ Training behavior matches deployment behavior
- ✅ Frame-rate independent by design (proper game development practice)

**Tuning (Optional):**
You can adjust the action rate in the StateTree asset:
1. Open your StateTree asset in UE Editor
2. Select the "Execute Objective" task
3. Modify `ActionApplicationInterval`:
   - 0.05s = 20 Hz (default, matches typical RL policy rates)
   - 0.033s = 30 Hz (faster reactions, higher CPU load)
   - 0.1s = 10 Hz (slower, more stable for early training)

**No Configuration Required** - This fix is automatic and requires no manual setup.

#### 1.1 Launch UE5 in Game Mode
```bash
# Option A: Launch from UE Editor
Play → Standalone Game (Alt+P)

# Option B: Launch packaged game
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\Binaries\Win64
GameAI_Project.exe -game -ResX=1280 -ResY=720 -windowed

# Verify Schola server started
# Console should show: "[Schola] gRPC server started on port 50051"
```

#### 1.2 Test Schola Connection
```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\CORTEX_Training

# Test connection (should not error)
python -c "from schola.gym.env import GymEnv; print('Schola import OK')"
```

#### 1.3 Run Training Script
```bash
# Single-worker training (no parallelization)
python train_rllib.py --iterations 100 --host localhost --port 50051
```

**Expected Output:**
```
============================================================
CORTEX RLlib Training
============================================================

Output directory: training_results/20251213_143022
TensorBoard logs: training_results/20251213_143022
Training for 100 iterations

Iteration    1 (UE Episodes: ~1): reward=   -25.43, len= 342.0, agent_steps=1368, env_steps=342
Iteration    2 (UE Episodes: ~3): reward=   -18.92, len= 389.5, agent_steps=2926, env_steps=731
Iteration    3 (UE Episodes: ~5): reward=   -12.34, len= 412.1, agent_steps=4574, env_steps=1143
...
```

#### 1.4 Monitor with TensorBoard
```bash
# In separate terminal
tensorboard --logdir=training_results

# Open browser: http://localhost:6006
```

**Key Metrics to Watch:**
- `episode_reward_mean`: Should increase (start negative, approach 0)
- `episode_len_mean`: Should stabilize (300-600 steps typical for T1)
- `policy_loss`: Should decrease and stabilize
- `vf_loss` (value function loss): Should decrease

#### 1.5 Success Criteria for Phase 1
- [ ] Training runs without errors for 10+ iterations
- [ ] `episode_reward_mean` shows upward trend
- [ ] TensorBoard graphs update in real-time
- [ ] UE5 episodes auto-reset (agents respawn after team elimination)
- [ ] No connection timeout errors

**Troubleshooting:**
| Issue | Solution |
|-------|----------|
| `ConnectionRefusedError` | Verify UE5 Schola server started (check console) |
| `Observation shape mismatch` | Check `sbdapm_env.py:68` - should be (78,) |
| `Agent IDs mismatch` | Check `sbdapm_env.py:103-136` - CDO filtering logic |
| `Training too slow` | Reduce `MAX_EPISODE_STEPS` in `train_rllib.py:56` |
| **Agent runs in place (any FPS)** | **FIXED (2025-12-16)** - Action throttling handles this automatically |
| **Episodes end too quickly** | **FIXED (2025-12-16)** - Update to latest code |
| Docker: `DEADLINE_EXCEEDED` | See [Docker Connection Fix](#docker-connection-troubleshooting) below |

---

## Phase 2: Docker Local Multi-Instance

### When to Start
- After Phase 1 completes successfully (T1 converged)
- Before starting T2 (Cover Usage level)

### Prerequisites
- [x] Docker Desktop installed and running
- [x] Phase 1 training validated
- [x] 16GB+ RAM (4GB per UE instance × 2-4 instances)

### Docker Connection Troubleshooting

**Problem:** Docker container times out connecting to UE5 with `grpc._channel._InactiveRpcError: DEADLINE_EXCEEDED`

**Root Cause:** UE5 Schola gRPC server binds to `127.0.0.1` (localhost only), which Docker containers cannot reach.

**Solution:**

1. **Update UE5 Schola Configuration** (REQUIRED):
   Edit `Config/DefaultEngine.ini`:
   ```ini
   [/Script/Schola.ScholaManagerSubsystemSettings]
   CommunicatorSettings=(Address="0.0.0.0",Port=50051,Timeout=60)
   ```
   Change `Address="127.0.0.1"` → `Address="0.0.0.0"`

2. **Add Windows Firewall Rule** (if needed):
   ```powershell
   # Run in PowerShell as Administrator
   New-NetFirewallRule -DisplayName "UE5 Schola gRPC" `
       -Direction Inbound `
       -LocalPort 50051 `
       -Protocol TCP `
       -Action Allow
   ```

3. **Test Connection Before Training**:
   ```bash
   # Run the connection test
   run_docker_training.bat
   # Select: 4 (Test connection to UE5)
   ```

   **Expected Output:**
   ```
   Testing connection to host.docker.internal:50051...
   ✓ DNS resolved to 192.168.65.2
   ✓ TCP connection successful!
   ```

4. **Restart UE5** after changing DefaultEngine.ini for changes to take effect.

### Step-by-Step Instructions

#### 2.1 Build Docker Image
```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\CORTEX_Training

# Build image (only needed once, or when dependencies change)
docker build -t cortex_training:latest -f CORTEX_Training/Dockerfile .
```

**Verify Build:**
```bash
docker images | grep cortex_training
# Should show: cortex_training   latest   [IMAGE_ID]   [SIZE]
```

#### 2.2 Launch Multiple UE Instances

**Terminal 1: UE Instance 1 (Port 50051)**
```bash
"C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe" "C:\Users\Foryoucom\Documents\GitHub\CORTEX\GameAI_Project.uproject" -game -ResX=800 -ResY=600 -windowed -ScholaPort=50051
```

**Terminal 2: UE Instance 2 (Port 50052)**
```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\Binaries\Win64
GameAI_Project.exe -game -ResX=800 -ResY=600 -windowed -ScholaPort=50052 -WinX=810
```

**Terminal 3: UE Instance 3 (Port 50053) - Optional**
```bash
GameAI_Project.exe -game -ResX=800 -ResY=600 -windowed -ScholaPort=50053 -WinX=1620
```

**Verify All Started:**
```bash
# Each console should show: "[Schola] gRPC server started on port 5005X"
```

#### 2.3 Configure RLlib for Multi-Worker

Edit `train_rllib.py:73`:
```python
# Change from:
NUM_WORKERS = 0

# To (for 2 UE instances):
NUM_WORKERS = 2
```

**Worker-Port Mapping:**
- Worker 0 (main): port 50051
- Worker 1: port 50052
- Worker 2: port 50053 (if NUM_WORKERS=3)
- Worker 3: port 50054 (if NUM_WORKERS=4)

#### 2.4 Run Docker Training
```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\CORTEX_Training

# Option A: Using batch script
run_docker_training.bat
# Select: 2 (Multi-worker)

# Option B: Direct docker-compose
docker-compose --profile multi up --build
```

**Expected Output:**
```
Creating sbdapm_training_multi ... done
Attaching to sbdapm_training_multi
sbdapm_training_multi | [RLlib] Starting 2 workers...
sbdapm_training_multi | [RLlib] Worker 0 connected to localhost:50051
sbdapm_training_multi | [RLlib] Worker 1 connected to localhost:50052
sbdapm_training_multi | Iteration 1: reward=-22.15, len=367.3, agent_steps=2934, env_steps=734
...
```

#### 2.5 Performance Benchmark

**Single Worker (NUM_WORKERS=0):**
- ~100 env steps/sec (1 UE instance)
- ~400 agent steps/sec (4 agents per env)

**2 Workers (NUM_WORKERS=2):**
- ~200 env steps/sec (2 UE instances)
- ~800 agent steps/sec (8 agents total)
- **2x speedup**

**4 Workers (NUM_WORKERS=4):**
- ~400 env steps/sec (4 UE instances)
- ~1600 agent steps/sec (16 agents total)
- **4x speedup**

#### 2.6 Success Criteria for Phase 2
- [ ] All UE instances connect successfully
- [ ] Training speed increases linearly with workers (±10%)
- [ ] No worker crashes or timeouts
- [ ] Models converge faster than Phase 1

---

## Phase 3: AWS Distributed Training

### When to Start
- After T3 (Positioning 3v3) completes locally
- Before T4 (Crossfire) - requires large-scale parallelization

### AWS Architecture

```
┌─────────────────────────────────────────────────────────┐
│ EC2 Head Node (t3.xlarge)                               │
│ ├─ Docker: RLlib training script                        │
│ └─ Connects to worker fleet via public IPs             │
└─────────────────────────────────────────────────────────┘
                        │
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ EC2 Worker 1 │ │ EC2 Worker 2 │ │ EC2 Worker N │
│ g4dn.xlarge  │ │ g4dn.xlarge  │ │ g4dn.xlarge  │
│ ├─ UE5 Game  │ │ ├─ UE5 Game  │ │ ├─ UE5 Game  │
│ └─ Schola    │ │ └─ Schola    │ │ └─ Schola    │
│    :50051    │ │    :50051    │ │    :50051    │
└──────────────┘ └──────────────┘ └──────────────┘
```

### Step-by-Step AWS Setup

#### 3.1 Prepare Game Build for Linux

**In Unreal Editor:**
1. Edit → Project Settings → Platforms → Linux
2. Packaging → Build Configuration: Shipping
3. Package Project → Linux
4. Copy `LinuxNoEditor` folder to S3 bucket

#### 3.2 Launch EC2 Worker Fleet

**AMI:** Ubuntu 22.04 LTS (Deep Learning AMI recommended)

**Instance Type:** g4dn.xlarge (NVIDIA T4 GPU, 4 vCPU, 16GB RAM)

**Launch Script** (save as `launch_workers.sh`):
```bash
#!/bin/bash

# Install dependencies
sudo apt-get update
sudo apt-get install -y python3.10 python3-pip unzip

# Download game build from S3
aws s3 cp s3://YOUR_BUCKET/LinuxNoEditor.zip /home/ubuntu/
unzip LinuxNoEditor.zip -d /home/ubuntu/game

# Install Schola plugin (if not bundled)
pip3 install schola[rllib]

# Launch UE5 game with Schola
cd /home/ubuntu/game/LinuxNoEditor
./GameAI_Project.sh -game -nullrhi -ScholaPort=50051 &

# Wait for Schola to start
sleep 10
echo "Worker ready on port 50051"
```

**Launch Instances:**
```bash
# Launch 8 workers (adjust count as needed)
aws ec2 run-instances \
    --image-id ami-XXXXXXXXX \
    --instance-type g4dn.xlarge \
    --count 8 \
    --key-name YOUR_KEY \
    --security-group-ids sg-XXXXXXXXX \
    --user-data file://launch_workers.sh \
    --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=SBDAPM-Worker}]'
```

#### 3.3 Configure Security Groups

**Inbound Rules:**
- Port 50051: TCP from head node security group (Schola gRPC)
- Port 22: SSH from your IP (admin access)

**Outbound Rules:**
- All traffic (allow)

#### 3.4 Update Training Script for AWS

Edit `train_rllib.py` to use EC2 public IPs:

```python
# Create environment config with worker IPs
def create_env_config_aws(worker_ips):
    """Create distributed environment config."""
    return {
        "host": "placeholder",  # Overridden per worker
        "port": 50051,
        "max_episode_steps": 1000,
        "worker_ips": worker_ips  # Pass to env creator
    }

# In register_env():
def env_creator(config):
    worker_index = config.get("worker_index", 0)
    worker_ips = config.get("worker_ips", ["localhost"])
    host = worker_ips[worker_index % len(worker_ips)]

    return SBDAPMMultiAgentEnv(
        host=host,
        port=config["port"],
        max_episode_steps=config["max_episode_steps"]
    )
```

#### 3.5 Launch Distributed Training

**On EC2 Head Node:**
```bash
# Get worker IPs
WORKER_IPS=$(aws ec2 describe-instances \
    --filters "Name=tag:Name,Values=SBDAPM-Worker" "Name=instance-state-name,Values=running" \
    --query "Reservations[*].Instances[*].PublicIpAddress" \
    --output text)

echo "Worker IPs: $WORKER_IPS"

# Run training with 8 workers
docker run -it \
    -v $(pwd)/training_results:/app/training_results \
    -e NUM_WORKERS=8 \
    -e WORKER_IPS="$WORKER_IPS" \
    sbdapm_training:latest \
    python train_rllib.py --iterations 1000 --checkpoint-freq 50
```

#### 3.6 Monitor AWS Training

**TensorBoard (Remote):**
```bash
# On head node
tensorboard --logdir=training_results --host=0.0.0.0 --port=6006

# Access from local machine (SSH tunnel)
ssh -L 6006:localhost:6006 ubuntu@HEAD_NODE_IP
# Open: http://localhost:6006
```

**CloudWatch Logs:**
```bash
# Stream UE worker logs
aws logs tail /aws/ec2/sbdapm-workers --follow
```

#### 3.7 Cost Estimation

**EC2 Pricing** (us-east-1, as of 2024):
- g4dn.xlarge: $0.526/hour (on-demand)
- t3.xlarge (head): $0.1664/hour

**Example: T4-T10 Training (8 workers, 30 hours)**
- Workers: 8 × $0.526 × 30 = $126.24
- Head: 1 × $0.1664 × 30 = $4.99
- **Total: ~$131**

**Savings with Spot Instances:**
- g4dn.xlarge spot: ~$0.16/hour (70% savings)
- **Total: ~$38**

---

## Level Progression Strategy

### DO NOT Repackage UE5 for Each Level ✅

**Correct Approach:** Use runtime level switching

#### Option 1: Console Command (Manual)
```bash
# In UE5 console (~ key)
open Training_CoverUsage_2v2_v01

# Training script continues automatically on new level
```

#### Option 2: Blueprint Curriculum Manager (Automated)

**Create `BP_CurriculumManager` Actor:**

1. Add to persistent level (e.g., `MainMenu.umap`)
2. Bind to `SimulationManagerGameMode::OnEpisodeEnded`
3. Check convergence criteria
4. Call `OpenLevel()` when ready

**Example Blueprint Logic:**
```
Event OnEpisodeEnded
  │
  ├─ Get Win Rate (last 100 episodes)
  ├─ Branch: Win Rate in [0.48, 0.52]?
  │   ├─ True → Increment Convergence Counter
  │   └─ False → Reset Counter
  │
  ├─ Branch: Convergence Counter ≥ 5?
  │   ├─ True → Load Next Curriculum Level
  │   └─ False → Continue
  │
  └─ Open Level (e.g., "Training_CoverUsage_2v2_v01")
```

#### Option 3: Python Curriculum Manager (Recommended)

**Use provided `curriculum_manager.py`:**

```bash
# Run in parallel with training
python curriculum_manager.py --tensorboard-dir training_results --check-interval 60
```

**How It Works:**
1. Monitors TensorBoard logs every 60s
2. Checks convergence criteria (win rate, episode count)
3. Writes level switch command to `curriculum_command.json`
4. UE5 Blueprint polls this file and switches levels

**UE5 Blueprint Polling:**
```
Event Tick
  │
  ├─ Load JSON File: curriculum_command.json
  ├─ Parse Command
  ├─ Branch: Command == "load_level"?
  │   ├─ True → Open Level (map_name)
  │   └─ False → Continue
  │
  └─ Delete File (prevent re-trigger)
```

### Curriculum Levels & Timing

| Level | Type | Agents | Duration | Key Focus |
|-------|------|--------|----------|-----------|
| T1 | Basic Combat | 2v2 | 1-2h | Shooting, health management |
| T2 | Cover Usage | 2v2 | 2-3h | EQS cover system |
| T3 | Positioning | 3v3 | 3-4h | Formation coherence |
| T4 | Crossfire | 3v3 | 4-5h | Combined fire |
| T5 | Flanking | 4v4 | 5-6h | Multi-angle attacks |
| T6 | Rescue | 4v4 | 4-5h | Role specialization |
| T7 | Capture | 4v4 | 5-6h | Objective capture |
| T8 | Defend | 4v4 | 5-6h | Defensive tactics |
| T9 | Mixed | 4v4 | 6-7h | Dynamic switching |
| T10 | Full Scenario | 4v4 | 8-10h | All mechanics |
| **TOTAL** | | | **43-54h** | |

**Optimization:** Run T1-T3 locally (Phase 2), T4-T10 on AWS (Phase 3)

---

## Monitoring & Debugging

### TensorBoard Metrics

**Essential Graphs:**
- `episode_reward_mean`: Primary convergence indicator
- `episode_len_mean`: Should stabilize (too short = instant deaths, too long = indecisive)
- `policy_loss`: Should decrease and stabilize
- `policy_entropy`: Should remain > 0.5 (exploration)
- `vf_loss`: Value function accuracy

**Custom Metrics** (from Schola logging):
- `win_rate`: Per-team win rate (should → 50% for self-play)
- `cover_usage_rate`: % time agents spend in cover
- `coordination_rate`: % kills from combined actions
- `formation_coherence`: Team spatial coherence [0,1]

### Common Issues

| Symptom | Diagnosis | Fix |
|---------|-----------|-----|
| **Agent runs in place** | **FIXED (2025-12-16)** | **Action throttling implemented - update to latest code** |
| **Episodes end too quickly** | **FIXED (2025-12-16)** | **Same as above** |
| Reward plateaus | Learning rate too high/low | Adjust `LEARNING_RATE` in `train_rllib.py:62` |
| Agents always same action | Exploration collapsed | Increase `ENTROPY_COEFF` (default: 0.01 → 0.05) |
| Training crash after N iters | OOM (out of memory) | Reduce `TRAIN_BATCH_SIZE` (default: 4000 → 2000) |
| Win rate stuck at 0/100% | Policy divergence | Reduce `CLIP_PARAM` (default: 0.2 → 0.1) |
| Episode too short | Agents die instantly | Check spawn distance, weapon damage |

### Debug Logging

**Enable verbose Schola logging:**
```python
# In sbdapm_env.py, add at top:
import logging
logging.basicConfig(level=logging.DEBUG)
```

**Enable UE5 detailed logs:**
```bash
# Launch UE with verbose logging
GameAI_Project.exe -game -log -verbose
```

---

## Model Export & Deployment

### Export Trained Model

Training script auto-exports ONNX model:
```
training_results/20251213_143022/rl_policy_network.onnx
```

**Manual Export:**
```bash
python -c "
from ray.rllib.algorithms.ppo import PPO
algo = PPO.from_checkpoint('training_results/20251213_143022/checkpoint_000100')
# Export logic in train_rllib.py:163-242
"
```

### Deploy to UE5

1. Copy ONNX model:
```bash
cp training_results/LATEST/rl_policy_network.onnx Content/Models/
```

2. Update `RLPolicyNetwork` component:
   - In `RLPolicyNetwork.cpp:107-134` → Load `rl_policy_network.onnx`
   - Verify input shape: (1, 78)
   - Verify output shapes: (1, 8) for actions, (1, 1) for value

3. Test in PIE (Play in Editor):
   - Place agents in level
   - Verify actions execute correctly
   - Check console for "RL Policy loaded successfully"

### Versioning Models

**Recommended Structure:**
```
Content/Models/
├── v1.0_T1_BasicCombat/
│   └── rl_policy_network.onnx (100 iters, T1 only)
├── v1.5_T3_Positioning/
│   └── rl_policy_network.onnx (500 iters, T1-T3)
└── v2.0_T10_Full/
    └── rl_policy_network.onnx (2000 iters, full curriculum)
```

---

## Quick Start Checklist

### Phase 1: Local Training (Day 1)
- [ ] Build UE5 project
- [ ] Add spawn points to T1 level
- [ ] Install Python dependencies: `pip install -r requirements.txt`
- [ ] Run local training: `python train_rllib.py --iterations 100`
- [ ] Monitor TensorBoard: http://localhost:6006
- [ ] Verify convergence: win rate → 50% ± 2%

### Phase 2: Docker Local (Day 2-3)
- [ ] Build Docker image: `docker build -t sbdapm_training .`
- [ ] Launch 2 UE instances (ports 50051-50052)
- [ ] Update `NUM_WORKERS=2` in train_rllib.py
- [ ] Run Docker training: `docker-compose --profile multi up`
- [ ] Benchmark speedup: should be ~2x faster

### Phase 3: AWS Distributed (Week 1-2)
- [ ] Package UE5 for Linux
- [ ] Upload to S3
- [ ] Launch EC2 worker fleet (8× g4dn.xlarge)
- [ ] Configure security groups
- [ ] Run distributed training with curriculum manager
- [ ] Export final model after T10

---

**End of Training Workflow Guide**

For questions or issues:
- Check `CLAUDE.md` for architecture reference
- Check `LevelDesignTemplate.md` for curriculum specs
- Check UE5 console logs for Schola errors
