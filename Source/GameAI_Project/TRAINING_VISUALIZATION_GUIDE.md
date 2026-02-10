# Training Visualization & Strategy Override - Quick Reference

**Date:** 2026-02-10 | **For:** Phase 1 RL Training

---

## 🎯 Your Two Questions Answered

### ✅ Question 1: How to select strategy in UE5 editor instead of Python script?

**Answer:** Use the new **Training Strategy Override** feature in `ScholaMocAgent` component.

#### Where to find it:
```
1. Open UE5 Editor
2. Select BP_MocCharacter (or any MocCharacter in level)
3. Find "ScholaMocAgent" component in Details panel
4. Look for category: "MOC | Phase1Training"
5. Enable: ☑ "Override Strategy (Phase 1 Training)"
6. Select: "Training Strategy" dropdown (Assault/Defend/Support)
```

#### Visual Location:
```
World Outliner
  └─ BP_MocCharacter_0
       └─ [Components]
            └─ ScholaMocAgent (UScholaMocAgent)
                 └─ [Details Panel]
                      └─ MOC
                           └─ Current Mode = Training Mode (Python RLlib)
                      └─ MOC | Phase1Training  👈 HERE!
                           ├─ ☑ Override Strategy (Phase 1 Training)
                           └─ Training Strategy = [Assault/Defend/Support]
```

---

### ✅ Question 2: Where is the training visualization?

**Answer:** Visualization exists in `AMocTrainer::DrawTrainingDebug()`. You just need to enable it!

#### Where to enable it:
```
1. Open BP_MocTrainer blueprint (or select MocTrainer in level)
2. Find "Debug" category in Details panel
3. Enable: ☑ "Enable Debug Visualization"
```

#### Visual Location:
```
World Outliner
  └─ BP_MocTrainer_0 (AIController for agent)
       └─ [Details Panel]
            └─ Training | Rewards
                 ├─ Assault Movement Reward = 0.01
                 ├─ Assault Health Penalty = 5.0
                 └─ ...
            └─ Debug  👈 HERE!
                 └─ ☑ Enable Debug Visualization = TRUE
```

---

## 🎨 What the Visualization Shows

### Real-Time Overlay (Above Each Agent):

```
┌─────────────────────────────────────┐
│ Strategy: Assault [TRAINING OVERRIDE]│
│ Health: 85.0%                       │
│ Steps: 1234 / 3000                  │
│ Episode Reward: 45.67               │
│ Total Episodes: 12                  │
│ ---EQS Weights---                   │
│ EnemyObj: 0.85 | AllyObj: -0.23    │
│ Cover: 0.45 | Visibility: 0.67      │
│ AllyProx: -0.12 | Range: 0.34       │
│ Pickup: 0.11 | Height: 0.56         │
└─────────────────────────────────────┘
         ↓ (Cyan text)
      Agent Character
```

### Visual Indicators:

**Around the Agent:**
- **Large colored sphere** around agent indicates strategy:
  - 🟠 Orange = Assault
  - 🔵 Blue = Defend
  - 🟣 Purple = Support

**Target Location:**
- 🟡 **Yellow sphere** = EQS target location (where agent is moving)
- 🟡 **Yellow line** = Path from agent to target
- Distance displayed above target sphere

**Team & Enemies:**
- 🟢 **Green spheres** = Allied agents (4 spheres)
- 🔴 **Red spheres** = Visible enemy agents (up to 5)
- 🔴 **Red lines** = Line of sight to visible enemies

---

## 📋 Complete Setup Checklist

### For Phase 1 Training (Day 1 - Assault):

#### In UE5 Editor:

**1. Configure All 5 Agents:**
```
☑ Select each BP_MocCharacter in level
☑ Find ScholaMocAgent component
☑ Enable "Override Strategy (Phase 1 Training)" = TRUE
☑ Set "Training Strategy" = Assault
☑ Repeat for all 5 agents
```

**2. Enable Visualization:**
```
☑ Select BP_MocTrainer (or each agent's AIController)
☑ Find "Debug" category
☑ Enable "Enable Debug Visualization" = TRUE
```

**3. Configure Rewards (Optional):**
```
☑ In BP_MocTrainer, find "Training | Rewards"
☑ Verify reward parameters match your training plan
   - Assault Movement Reward = 0.01
   - Assault Health Penalty = 5.0
   - Death Penalty = 100.0
```

**4. Set EQS Template:**
```
☑ In BP_MocTrainer, find "AI | EQS"
☑ Set "EQS Query Template" = EQ_TacticalMovement (or your EQS asset)
☑ Set "EQS Search Radius" = 2000.0
```

**5. Launch PIE:**
```
☑ Press Alt+P or click "Play in Editor"
☑ Verify you see visualization on all agents
☑ Check Output Log for "[ScholaMocAgent] TRAINING OVERRIDE ENABLED" messages
```

#### In Terminal:

**Start Python Training:**
```bash
cd training/
python train_strategy.py \
    --num_agents 5 \
    --max_iterations 50000 \
    --checkpoint_dir checkpoints/assault/
```

---

## 🔄 Switching Strategies (Day 2, Day 3)

### Day 2: Train Defend Policy

**In UE5:**
```
1. Stop PIE (Esc or Stop button)
2. Select all 5 BP_MocCharacter instances
3. Change "Training Strategy" = Defend
4. Restart PIE (Alt+P)
```

**In Terminal:**
```bash
python train_strategy.py \
    --num_agents 5 \
    --max_iterations 50000 \
    --checkpoint_dir checkpoints/defend/
```

### Day 3: Train Support Policy

**Repeat above steps with:**
- Training Strategy = Support
- --checkpoint_dir checkpoints/support/

---

## 🐛 Debugging Tips

### Check if Override is Working:

**Look for this in Output Log (LogTemp):**
```
LogTemp: [ScholaMocAgent] Agent 0: TRAINING OVERRIDE ENABLED - Strategy locked to Assault
LogTemp: [ScholaMocAgent] SquadManager commands will be IGNORED. Disable this for Phase 3!
```

**Look for this in Visualization:**
```
Strategy: Assault [TRAINING OVERRIDE]
         ^^^^^^^^ Strategy name
                  ^^^^^^^^^^^^^^^^^^^ Override indicator
```

If you see "[TRAINING OVERRIDE]" in the visualization, the override is active! ✅

---

### Check if Visualization is Working:

**If you see NO visualization:**
1. ✅ Verify `bEnableDebugVisualization = true` in BP_MocTrainer
2. ✅ Check you're in Play in Editor mode (not Simulate)
3. ✅ Verify BP_MocTrainer is assigned as AIController for the character
4. ✅ Check camera can see the agents

**If you see visualization but no EQS weights:**
- This means no action has been taken yet
- Wait 1-2 seconds for Python policy to send first action
- Weights will appear after first `ApplyAction()` call

---

## 📊 Monitoring Training Progress

### In UE5 Visualization:

Watch these metrics increase over time:
- **Episode Reward** should trend upward
- **Total Episodes** should increment regularly
- **EQS Weights** should stabilize to consistent patterns
- **Survival time** (Steps before episode ends) should increase

### In Python Console:

Watch for:
```
Episode 0: reward=12.3, length=234
Episode 10: reward=25.6, length=456
Episode 20: reward=38.9, length=678
...
Episode 100: reward=52.1, length=890  ✅ Converging!
```

---

## ⚠️ IMPORTANT: Disable Override Before Phase 3!

After Phase 1 training is complete:

```
☐ Uncheck "Override Strategy (Phase 1 Training)" on ALL agents
☐ Verify SquadManager is enabled in BP_ScholaEnvironment
☐ Set "bEnableCentralizedPlanning = true"
```

If you forget to disable the override, the SquadManager's commands will be ignored!

---

## 🔧 Advanced: Keyboard Shortcuts

Add these to `keybindings.json` for faster training workflow:

```json
{
  "Ctrl+Shift+V": "Toggle Debug Visualization",
  "Ctrl+Shift+S": "Cycle Training Strategy (Assault→Defend→Support)",
  "Ctrl+Shift+R": "Reset Episode Statistics"
}
```

*(Note: You'll need to implement these in Blueprint for your specific workflow)*

---

## 📁 Modified Files Summary

| File | What Changed |
|------|--------------|
| `ScholaMocAgent.h` | Added `bUseTrainingStrategyOverride` and `TrainingStrategyOverride` |
| `ScholaMocAgent.cpp` | Added logging for training override status |
| `MocTrainer.cpp` | Enhanced `DrawTrainingDebug()` to show EQS weights and override status |

No changes needed to Python training script! 🎉

---

## 📖 Additional Resources

- **Full training plan:** `TRAINING_PROJECT_PLAN.md`
- **Setup guide:** `PHASE1_TRAINING_SETUP.md`
- **Architecture details:** `v10.2Architecture.md`
- **Reward tuning:** `MocGameEnvSpecification.md`

---

**Happy Training! 🚀**
