# Phase 1 Training Setup Guide
**MOC v10.2 RL Agent Training**

---

## Quick Start: Training a Single Strategy Policy

This guide explains how to use the new **Training Strategy Override** feature to train separate RL policies for each strategy (Assault, Defend, Support).

---

## Step 1: Enable Training Strategy Override in UE5 Editor

### Location: `BP_MocCharacter` → `ScholaMocAgent` Component

1. **Open the UE5 Editor** and load your training level (e.g., `Training.umap`)

2. **Select a MocCharacter** in the level (or open the `BP_MocCharacter` blueprint)

3. **Find the `ScholaMocAgent` component** in the Details panel

4. **Under "MOC | Phase1Training" category**, you'll see:
   - ☑ **Override Strategy (Phase 1 Training)** - Enable this checkbox
   - **Training Strategy** - Select the strategy to train (Assault/Defend/Support)

### Configuration for Each Training Day:

#### **Day 1: Train Assault Policy**
```
All 5 agents:
  ☑ Override Strategy (Phase 1 Training) = TRUE
  Training Strategy = Assault
```

#### **Day 2: Train Defend Policy**
```
All 5 agents:
  ☑ Override Strategy (Phase 1 Training) = TRUE
  Training Strategy = Defend
```

#### **Day 3: Train Support Policy**
```
All 5 agents:
  ☑ Override Strategy (Phase 1 Training) = TRUE
  Training Strategy = Support
```

---

## Step 2: Enable Debug Visualization

### Location: `BP_MocTrainer` (AIController)

1. **Find the `BP_MocTrainer` blueprint** (or create one if it doesn't exist)

2. **In the Details panel**, find the **"Debug"** category

3. **Enable visualization:**
   - ☑ **Enable Debug Visualization** = TRUE

### What the Visualization Shows:

**Agent Info (Cyan text above agent):**
- Strategy name (with "[TRAINING OVERRIDE]" indicator)
- Current health percentage
- Episode steps (current / max)
- Episode reward (cumulative)
- Total episodes completed

**EQS Weights Display:**
- 8 real-time weight values from the RL policy
- Shows what the agent is currently prioritizing

**Visual Indicators:**
- **Yellow sphere + line**: EQS target location (where agent is moving)
- **Green spheres**: Ally positions
- **Red spheres + lines**: Visible enemy positions
- **Colored sphere around agent**: Strategy indicator
  - Orange = Assault
  - Blue = Defend
  - Purple = Support

---

## Step 3: Configure Reward Parameters (Optional)

### Location: `BP_MocTrainer` → "Training | Rewards" Category

You can fine-tune reward weights for each strategy:

### Assault Strategy Rewards:
```cpp
Assault Movement Reward = 0.01     // Reward for moving forward
Assault Health Penalty = 5.0       // Penalty for taking damage
```

### Defend Strategy Rewards:
```cpp
Defend Position Reward = 2.0       // Reward for staying still
Defend Health Bonus = 2.0          // Bonus for staying healthy
```

### Support Strategy Rewards:
```cpp
Support Position Reward = 1.0      // Reward for moderate movement
Support Health Bonus = 1.5         // Bonus for staying healthy
```

### Common Penalties:
```cpp
Death Penalty = 100.0              // Large penalty for dying
Time Penalty = 0.001               // Small penalty per step
```

---

## Step 4: Python Training Script

Now you DON'T need to pass `--strategy` as an argument!

### Old way (from training plan):
```bash
python train_strategy.py \
    --strategy assault \          # ❌ No longer needed!
    --num_agents 5 \
    --max_iterations 50000
```

### New way (simplified):
```bash
python train_strategy.py \
    --num_agents 5 \
    --max_iterations 50000 \
    --checkpoint_dir checkpoints/assault/
```

The strategy is now configured in the UE5 editor, so the Python script doesn't need to know about it!

---

## Step 5: Run Training

1. **Start UE5 Editor**
   - Load training level
   - Verify all agents have the same strategy override enabled
   - Verify debug visualization is enabled
   - Press **Play in Editor (Alt+P)**

2. **Start Python Training Script**
   ```bash
   cd training/
   python train_strategy.py \
       --num_agents 5 \
       --max_iterations 50000 \
       --checkpoint_dir checkpoints/assault/
   ```

3. **Monitor Training Progress**
   - Watch the visualization in UE5 editor
   - Check episode rewards increasing over time
   - Monitor EQS weights changing as policy learns
   - Check Python console for reward convergence

---

## Step 6: Switch Strategies Between Training Days

After completing Assault training (Day 1):

1. **Stop PIE** (Play in Editor)
2. **Select all MocCharacters** in the level
3. **Change Training Strategy Override** to `Defend`
4. **Restart PIE** and run new Python training script:
   ```bash
   python train_strategy.py \
       --num_agents 5 \
       --max_iterations 50000 \
       --checkpoint_dir checkpoints/defend/
   ```

Repeat for Support on Day 3.

---

## Step 7: Disable Override for Phase 3

**⚠️ IMPORTANT:** After Phase 1 training is complete, you MUST disable the training override!

### Before starting Phase 3 (MCTS integration):
```
All agents:
  ☐ Override Strategy (Phase 1 Training) = FALSE
```

This allows the SquadManager to command strategies dynamically.

---

## Troubleshooting

### Issue: Strategy not changing in visualization
**Solution:** Make sure you set `bUseTrainingStrategyOverride = true` in the ScholaMocAgent component

### Issue: No visualization showing
**Solution:** Enable `bEnableDebugVisualization = true` in BP_MocTrainer

### Issue: Rewards not converging
**Solution:**
- Check reward parameters are configured correctly
- Verify EQS query template is set
- Check Python training script hyperparameters

### Issue: "SquadManager commands will be IGNORED" warning
**Solution:** This is expected during Phase 1 training. The warning reminds you to disable the override before Phase 3.

---

## Architecture Notes

### How Training Override Works:

```
Phase 1 Training (Override Enabled):
  Python Policy → EQS Weights
       ↓
  UScholaMocAgent::GetCommandedStrategy()
       ↓ (returns TrainingStrategyOverride)
  MocTrainer::ComputeReward()
       ↓ (uses overridden strategy)
  Strategy-Specific Rewards

Phase 3 Operation (Override Disabled):
  SquadManager MCTS → Tactical Play → Role Assignment
       ↓
  AMocCharacter::SetCommandedStrategy()
       ↓
  UScholaMocAgent::GetCommandedStrategy()
       ↓ (returns commanded strategy from SquadManager)
  Python Policy → EQS Weights
```

### Why This Design?

1. **Separation of Concerns:** Training configuration stays in UE5, Python script stays generic
2. **Easy Switching:** Change strategy in editor without restarting Python
3. **Visual Feedback:** See which strategy is being trained in real-time
4. **Safety:** Warning messages remind you to disable override before Phase 3

---

## File Locations

| File | Purpose |
|------|---------|
| `ScholaMocAgent.h/.cpp` | Training strategy override implementation |
| `MocTrainer.h/.cpp` | Reward calculation and visualization |
| `BP_MocCharacter` | Blueprint with ScholaMocAgent component |
| `BP_MocTrainer` | Blueprint AIController with training config |

---

## Success Metrics (from Training Plan)

### Assault Policy (Day 1):
- Episode reward converges >50.0
- Average survival time >30 seconds
- Model saved to `checkpoints/assault/policy_final.onnx`

### Defend Policy (Day 2):
- Episode reward converges >60.0
- Average survival time >45 seconds
- Position holding score >70%

### Support Policy (Day 3):
- Episode reward converges >55.0
- Ally proximity maintained >80% of time
- Objective control score >75%

---

## Next Steps After Phase 1

1. Copy trained ONNX models to Content/AI/Policies/
2. Proceed to Phase 2: World Model Data Collection
3. Remember to **DISABLE training override** before Phase 3!

---

**Ready to train! 🚀**
