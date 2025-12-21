# Docker Training Guide - v4.0

## Image Built Successfully
Image: `cortex_training-training-single:latest`
Action Space: `MultiDiscrete([4, 11, 3, 3])` - Position [4], Target [11], FireMode [3], Stance [3]

---

## Usage Options

### 1. Single-Worker Training (Recommended for testing)
**Prerequisites:** 1 UE5 instance running on port 50051

```bash
cd /c/Users/Foryoucom/Documents/GitHub/CORTEX/CORTEX_Training
docker compose --profile single up
```

**What it does:**
- Connects to UE5 at `host.docker.internal:50051`
- Runs 100 training iterations
- Saves results to `./training_results/`
- Saves models to `./models/`

**Environment Variables:**
- `NUM_WORKERS=0` (single process training)
- `NUM_ITERATIONS=100`
- `HOST=host.docker.internal`
- `PORT=50051`

---

### 2. Multi-Worker Training (4 parallel environments)
**Prerequisites:** 4 UE5 instances running on ports 50051-50054

```bash
cd /c/Users/Foryoucom/Documents/GitHub/CORTEX/CORTEX_Training
docker compose --profile multi up
```

**What it does:**
- Connects to 4 UE5 instances (ports 50051-50054)
- Parallelizes training across 4 environments
- Faster convergence but requires more setup

---

### 3. TensorBoard Monitoring
**Run alongside training to monitor progress:**

```bash
cd /c/Users/Foryoucom/Documents/GitHub/CORTEX/CORTEX_Training
docker compose --profile monitor up
```

Then open: `http://localhost:6006`

---

### 4. Connection Test
**Test connection to UE5 before training:**

```bash
cd /c/Users/Foryoucom/Documents/GitHub/CORTEX/CORTEX_Training
docker compose --profile test up
```

Runs `test_connection.py` to verify Schola gRPC connection.

---

## Quick Start Workflow

### Step 1: Start UE5
1. Open `GameAI_Project.uproject`
2. Load training level: `Training_BasicCombat_2v2_v01.umap`
3. Press **Alt+P** (Play in Editor)
4. Verify agents spawn correctly

### Step 2: Test Connection
```bash
docker compose --profile test up
```

Expected output:
```
[Schola] Connected to UE5 at host.docker.internal:50051
[Test] Observation space: Box(78,)
[Test] Action space: MultiDiscrete([4, 11, 3, 3])
```

### Step 3: Start Training
```bash
docker compose --profile single up
```

### Step 4: Monitor with TensorBoard (Optional)
```bash
# In a new terminal
docker compose --profile monitor up
```

---

## Stopping Containers

```bash
# Stop all running containers
docker compose down

# Stop and remove volumes
docker compose down -v
```

---

## Troubleshooting

### Issue: "Connection refused" or "Failed to connect"
**Solution:** Ensure UE5 is running in PIE mode and Schola plugin is enabled (port 50051 open)

### Issue: "Action space mismatch"
**Solution:** Verify you rebuilt the image after fixing action space:
```bash
docker compose --profile single build --no-cache
```

### Issue: "No such file or directory" (Windows path issues)
**Solution:** Use Git Bash or WSL2, not CMD/PowerShell for docker compose commands

### Issue: TensorBoard shows no data
**Solution:**
1. Check `./training_results/` exists
2. Wait 30-60 seconds after training starts
3. Refresh TensorBoard browser page

---

## File Locations

- **Training Results:** `CORTEX_Training/training_results/`
- **Saved Models:** `CORTEX_Training/models/`
- **Logs:** Docker container stdout (use `docker compose logs`)

---

## Next Steps After Training

1. **Check Output Log in UE5:**
   - Look for: `[EQS v4.0] ✅ Query returned N positions`
   - Look for: `[StateTree] Executing macro action`

2. **Verify Agent Movement:**
   - Enable EQS debug: `` `eqs debug BP_TestCharacter_0` ``
   - Green spheres should appear at tactical positions

3. **Review TensorBoard:**
   - Monitor `episode_reward_mean`
   - Check policy loss convergence
   - Verify value function learning

4. **Export Policy:**
   - Trained policy saved to `models/tactical_policy.onnx`
   - Use in UE5 via Schola inference
