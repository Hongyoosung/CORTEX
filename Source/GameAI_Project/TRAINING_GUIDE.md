# SBDAPM Training Guide - Complete Setup & Execution

**Version:** v3.1 Real-Time PPO Training
**Last Updated:** 2025-12-09
**Platform:** Windows 10/11, UE 5.6, Python 3.9+

---

## Table of Contents
1. [Environment Setup](#environment-setup)
2. [Unreal Engine Configuration](#unreal-engine-configuration)
3. [Python Environment Setup](#python-environment-setup)
4. [Running Training](#running-training)
5. [Model Export & Integration](#model-export--integration)
6. [Troubleshooting](#troubleshooting)

---

## Environment Setup

### Prerequisites

**System Requirements:**
- **OS:** Windows 10/11 (64-bit)
- **CPU:** 8+ cores recommended (for parallel training)
- **RAM:** 16GB minimum, 32GB recommended
- **GPU:** NVIDIA GTX 1060+ (for UE rendering, not required for training)
- **Storage:** 50GB free space (UE project + training logs)

**Software Requirements:**
- **Unreal Engine:** 5.6 (exact version)
- **Python:** 3.9, 3.10, or 3.11 (NOT 3.12 - Ray compatibility issues)
- **Visual Studio:** 2022 with C++ workload (for UE C++ compilation)
- **Git:** For version control

---

## Unreal Engine Configuration

### Step 1: Verify Plugin Installation

**Required Plugins (check `.uproject` file):**
```json
{
  "Plugins": [
    { "Name": "Schola", "Enabled": true },
    { "Name": "StateTree", "Enabled": true },
    { "Name": "GameplayStateTree", "Enabled": true },
    { "Name": "NNERuntimeORT", "Enabled": true },
    { "Name": "GameplayAbilities", "Enabled": true }
  ]
}
```

**Installation Status:**
1. Open UE Editor
2. Go to **Edit → Plugins**
3. Search for each plugin above
4. Verify **Enabled** checkbox is checked
5. Restart editor if you enable any new plugins

### Step 2: Verify Schola Plugin Configuration

**Check Schola Installation:**
1. In UE Editor, open **Window → Developer Tools → Output Log**
2. Start PIE (Play In Editor)
3. Look for log: `[Schola] gRPC server started on port 50051`

**If Schola is Missing:**
- Schola plugin should be in `SBDAPM/Plugins/Schola-1.3.0/`
- If missing, reinstall from: https://github.com/aws-samples/schola
- **Important:** Use version 1.3.0 (compatible with RLlib 2.6.0)

### Step 3: Configure Game Mode

**Ensure SimulationManagerGameMode is Active:**
1. Open **Edit → Project Settings → Maps & Modes**
2. Set **Default GameMode:** `SimulationManagerGameMode`
3. Verify **Default Pawn Class:** `BP_FollowerAgent` (or your custom follower blueprint)

**Start Map:**
- Set **Editor Startup Map:** Your training level (e.g., `Level_Training_4v4`)
- Set **Game Default Map:** Same as startup map

### Step 4: Place Agents in Level

**Team Setup (Example 4v4):**
1. Place 1 **Team Leader** actor per team:
   - Attach `TeamLeaderComponent`
   - Set `TeamID = 0` (Blue team)
   - Set `TeamID = 1` (Red team)

2. Place 4 **Follower Agents** per team:
   - Attach `FollowerAgentComponent`
   - Attach `ScholaAgentComponent` (for RLlib integration)
   - Attach `FollowerStateTreeComponent`
   - Register with corresponding TeamLeader

3. **Verify Component Setup:**
   - Each follower must have:
     - `UHealthComponent`
     - `UWeaponComponent`
     - `UAgentPerceptionComponent`
     - `URewardCalculator`

**Quick Test:**
- Press **Play** in editor
- Check Output Log for: `TeamLeader registered 4 followers`
- Check for: `Schola: Detected 4 agents (CDO filtered)`

---

## Python Environment Setup

### Step 1: Create Virtual Environment

**Open PowerShell/Command Prompt:**
```powershell
cd C:\Users\PC\Documents\GitHub\SBDAPM\Source\GameAI_Project\Scripts

# Create virtual environment
python -m venv venv_sbdapm

# Activate (Windows PowerShell)
.\venv_sbdapm\Scripts\Activate.ps1

# Or (Command Prompt)
.\venv_sbdapm\Scripts\activate.bat
```

**If Activation Fails (PowerShell Execution Policy):**
```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

### Step 2: Install Dependencies

**Install PyTorch First (Windows-specific):**
```bash
# PyTorch must be installed BEFORE Ray to avoid DLL errors
pip install torch==2.0.1 torchvision torchaudio --index-url https://download.pytorch.org/whl/cu118
```

**Install RLlib and Schola:**
```bash
pip install ray[rllib]==2.6.0
pip install schola[rllib]==1.3.0
```

**Install Remaining Dependencies:**
```bash
pip install -r requirements.txt
```

**Verify Installation:**
```bash
python -c "import ray; import torch; import schola; print('✅ All packages installed')"
```

### Step 3: Test Schola Connection

**Start UE Editor First (PIE mode):**
1. Open SBDAPM project in UE
2. Press **Play** to start PIE
3. Wait for log: `Schola: gRPC server started on port 50051`

**Run Connection Test:**
```bash
cd C:\Users\PC\Documents\GitHub\SBDAPM\Source\GameAI_Project\Scripts
python test_connection.py
```

**Expected Output:**
```
[SBDAPMMultiAgentEnv] Filtered agents: ['ScholaAgentComponent_2', 'ScholaAgentComponent_3', ...]
[SBDAPMMultiAgentEnv] Agent count: 4
✅ Connection successful
```

**If Connection Fails:**
- Check firewall (allow Python on port 50051)
- Verify UE is running in PIE mode
- Check Schola plugin enabled
- Try restarting UE editor

---

## Running Training

### Step 1: Prepare Unreal for Training

**Configure for Headless/Background Training:**
1. **Editor Settings:**
   - Edit → Editor Preferences → Performance
   - Uncheck "Use Less CPU when in Background"
   - Set "Max FPS" to 60 (stable frame rate)

2. **Play Settings:**
   - Edit → Project Settings → Play
   - Set "New Window Size" to 1280x720 (lower resolution = faster)
   - Uncheck "Run Under One Process" (for stability)

3. **Start PIE:**
   - Press **Play** in editor
   - Minimize editor window (training can run in background)

### Step 2: Start Training Script

**Basic Training (Default Settings):**
```bash
cd C:\Users\PC\Documents\GitHub\SBDAPM\Source\GameAI_Project\Scripts
python train_rllib.py
```

**With Custom Parameters:**
```bash
python train_rllib.py \
    --iterations 200 \
    --checkpoint-freq 10 \
    --host localhost \
    --port 50051
```

**Parameters Explained:**
- `--iterations`: Number of training iterations (default: 100)
- `--checkpoint-freq`: Save checkpoint every N iterations (default: 10)
- `--host`: Schola gRPC server host (default: localhost)
- `--port`: Schola gRPC server port (default: 50051)

### Step 3: Monitor Training Progress

**Console Output:**
```
Iteration    1: reward=   12.34, len=  245.0, agent_steps=4000
Iteration    2: reward=   18.56, len=  267.0, agent_steps=8000
  Checkpoint: training_results/20251209_143022/checkpoint_000010
Iteration   10: reward=   45.21, len=  312.0, agent_steps=40000
  New best! reward=45.21
```

**What to Monitor:**
- **reward:** Should increase over time (learning)
- **len:** Episode length (stable = good, too short = early termination issues)
- **agent_steps:** Total agent interactions (4 agents × episode steps)

**TensorBoard (Optional):**
```bash
# In separate terminal
tensorboard --logdir training_results
# Open browser to http://localhost:6006
```

### Step 4: Training Duration Expectations

**Performance Benchmarks:**
- **1 iteration** ≈ 4000 agent steps ≈ 1000 environment steps (4 agents)
- **1 environment step** ≈ UE frame time (~16ms at 60 FPS)
- **Training time per iteration:** ~2-5 minutes (depends on episode length)

**Recommended Session Length:**
- **Quick test:** 10 iterations (~20-30 minutes)
- **Full training:** 100-200 iterations (~4-8 hours)
- **Overnight run:** 500+ iterations (convergence expected)

---

## Model Export & Integration

### Step 1: Export Trained Model

**Automatic Export (After Training Completes):**
```
Training Complete!
Final checkpoint: training_results/20251209_143022/checkpoint_000100
[SUCCESS] Dual-head PPO model exported to: training_results/20251209_143022/rl_policy_network.onnx

Model structure:
  - Input: 78 dims (71 obs + 7 objective)
  - Output 1 (Actor): 8 dims (atomic actions)
  - Output 2 (Critic): 1 dim (state value)
```

**Manual Export (From Checkpoint):**
```python
from train_rllib import export_onnx
from ray.rllib.algorithms.ppo import PPO
from pathlib import Path

# Load checkpoint
algo = PPO.from_checkpoint("training_results/20251209_143022/checkpoint_000100")

# Export
export_onnx(algo, Path("training_results/20251209_143022"))
```

### Step 2: Copy Model to Unreal Content

**Windows Command Prompt:**
```cmd
copy training_results\20251209_143022\rl_policy_network.onnx C:\Users\PC\Documents\GitHub\SBDAPM\Content\Models\rl_policy_network.onnx
```

**PowerShell:**
```powershell
Copy-Item "training_results/20251209_143022/rl_policy_network.onnx" `
    -Destination "C:\Users\PC\Documents\GitHub\SBDAPM\Content\Models\rl_policy_network.onnx"
```

**Verify in UE:**
1. Open Content Browser in UE Editor
2. Navigate to **Content/Models/**
3. You should see `rl_policy_network.onnx`

### Step 3: Load Model in C++

**RLPolicyNetwork.cpp already configured to load:**
```cpp
// MCTS.cpp:37 - Model path
FString PolicyModelPath = TEXT("Models/rl_policy_network.onnx");
if (!RLPolicyNetwork->LoadPolicy(PolicyModelPath))
{
    UE_LOG(LogTemp, Warning, TEXT("MCTS: RL Policy model not loaded, using heuristic fallback"));
}
```

**No code changes needed - just copy the ONNX file to Content/Models/**

### Step 4: Test Inference

**Run UE Without Python Training:**
1. Stop Python training script
2. Stop PIE in UE
3. Start PIE again (fresh session)
4. Check Output Log for: `URLPolicyNetwork: ONNX model ready for inference`

**Verify Actions:**
- Agents should now use trained policy instead of random actions
- Check logs for: `[EXEC MOVE] Moving in direction (x, y)` (non-zero values)
- Observe agents in viewport (should exhibit learned behaviors)

---

## Troubleshooting

### Issue: "ModuleNotFoundError: No module named 'ray'"

**Cause:** Virtual environment not activated or Ray not installed

**Fix:**
```bash
# Activate venv first
.\venv_sbdapm\Scripts\Activate.ps1

# Reinstall Ray
pip install ray[rllib]==2.6.0
```

### Issue: "Schola gRPC connection timeout"

**Cause:** UE not running or Schola plugin disabled

**Fix:**
1. Start UE Editor
2. Press **Play** (enter PIE mode)
3. Wait for log: `Schola: gRPC server started`
4. Run Python script within 30 seconds

### Issue: "Expected 4 agents, got 5 (CDO detected)"

**Cause:** Schola CDO filtering not working

**Fix:**
- Already handled in `sbdapm_env.py:SafeUnrealVectorEnv`
- If persists, check agent names in UE (should have numbers: `ScholaAgentComponent_2`)

### Issue: "Training reward stuck at 0"

**Possible Causes:**
1. Agents not receiving combat events
2. RewardCalculator not attached to agents
3. Weapons not firing (check ammo, cooldown)

**Debug Steps:**
```cpp
// Add to RewardCalculator.cpp:99
UE_LOG(LogTemp, Warning, TEXT("Individual Reward: Kills=%d, Damage=%.1f"),
    KillsSinceLastUpdate, DamageSinceLastUpdate);
```

### Issue: "ONNX export failed: No attribute 'model'"

**Cause:** RLlib policy structure mismatch

**Fix:**
- Check `train_rllib.py:164` - Ensure policy is retrieved correctly
- Verify PPO algorithm built successfully
- Check Ray version compatibility (should be 2.6.0)

### Issue: "UE crashes during training"

**Possible Causes:**
1. Memory leak (agents not cleaned up)
2. MCTS tree growing too large
3. Null pointer access in C++ code

**Debug Steps:**
1. Check Windows Event Viewer for crash logs
2. Enable UE crash reporter
3. Add null checks in MCTS.cpp and FollowerAgentComponent.cpp
4. Reduce MaxSimulations to 250 (lower memory usage)

### Issue: "Training very slow (>10 min per iteration)"

**Optimizations:**
1. Lower UE resolution (720p instead of 1080p)
2. Disable UE visual effects (post-processing, shadows)
3. Reduce MaxSimulations in MCTS (500 → 250)
4. Use smaller batch size in `train_rllib.py` (4000 → 2000)

---

## Advanced Configuration

### Hyperparameter Tuning

**Edit `train_rllib.py:49-70`:**
```python
class SBDAPMConfig:
    # PPO hyperparameters
    LEARNING_RATE = 3e-4        # Higher = faster learning, less stable
    GAMMA = 0.99                # Discount factor (0.95-0.99)
    GAE_LAMBDA = 0.95           # Advantage estimation (0.9-0.99)
    CLIP_PARAM = 0.2            # PPO clip ratio (0.1-0.3)
    ENTROPY_COEFF = 0.01        # Exploration bonus (0.001-0.1)
    TRAIN_BATCH_SIZE = 4000     # Steps per update (2000-8000)
```

**Tuning Guidelines:**
- **Faster learning:** Increase `LEARNING_RATE`, decrease `TRAIN_BATCH_SIZE`
- **More exploration:** Increase `ENTROPY_COEFF`
- **More stable:** Decrease `LEARNING_RATE`, `CLIP_PARAM`

### Multi-Environment Training (Future)

**To scale to multiple UE instances:**
1. Launch multiple UE instances on different ports (50051, 50052, ...)
2. Modify `train_rllib.py` to use `num_env_runners > 1`
3. Configure each UE instance with unique Schola port

**Not recommended for v3.1 - single instance is sufficient**

---

## Quick Reference Commands

### Start Training Session
```bash
# Terminal 1: Activate venv
cd C:\Users\PC\Documents\GitHub\SBDAPM\Source\GameAI_Project\Scripts
.\venv_sbdapm\Scripts\Activate.ps1

# Terminal 1: Start training
python train_rllib.py --iterations 100

# Terminal 2 (Optional): TensorBoard
tensorboard --logdir training_results
```

### Export & Deploy Model
```bash
# After training completes
copy training_results\[timestamp]\rl_policy_network.onnx C:\Users\PC\Documents\GitHub\SBDAPM\Content\Models\

# Restart UE to load new model
```

### Test Connection
```bash
# UE must be running in PIE mode
python test_connection.py
```

---

## Checklist: First Training Run

- [ ] UE 5.6 installed and SBDAPM project opens without errors
- [ ] Schola plugin enabled (check Edit → Plugins)
- [ ] Python 3.9-3.11 installed (NOT 3.12)
- [ ] Virtual environment created and activated
- [ ] All dependencies installed (`pip install -r requirements.txt`)
- [ ] `test_connection.py` succeeds (shows 4 agents)
- [ ] UE running in PIE mode before starting Python script
- [ ] Training script runs for at least 10 iterations without crashes
- [ ] Reward increasing over iterations (learning happening)
- [ ] ONNX model exported successfully after training
- [ ] Model copied to `Content/Models/rl_policy_network.onnx`
- [ ] UE loads model and agents use trained policy

---

## Support & Resources

**Documentation:**
- CLAUDE.md - Architecture overview and coding guidelines
- NEXT_SESSION_GUIDE.md - Task priorities for next session
- AI_SYSTEM_DOCUMENTATION.md - Detailed system documentation

**External Resources:**
- Ray RLlib Docs: https://docs.ray.io/en/latest/rllib/
- Schola GitHub: https://github.com/aws-samples/schola
- UE5 StateTree: https://docs.unrealengine.com/5.0/en-US/state-tree-in-unreal-engine/

**Common Issues:**
- Check NEXT_SESSION_GUIDE.md "Troubleshooting Guide" section
- Check CLAUDE.md "Common Debugging (Quick Fixes)" table

---

**Prepared:** 2025-12-09
**For:** SBDAPM v3.1 Training Sessions
**Maintainer:** Development Team
