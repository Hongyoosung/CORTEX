# Phase 1 Training Setup Guide - Complete Level Configuration
**MOC v10.2 RL Agent Training**

**Last Updated:** 2026-02-11
**Engine:** UE5.6 | **Language:** C++17

---

## Table of Contents
1. [Prerequisites](#prerequisites)
2. [Level Setup Overview](#level-setup-overview)
3. [Step 1: Create Training Level](#step-1-create-training-level)
4. [Step 2: Configure Game Mode](#step-2-configure-game-mode)
5. [Step 3: Spawn TeamManager Actor](#step-3-spawn-teammanager-actor)
6. [Step 4: Place Capture Points](#step-4-place-capture-points)
7. [Step 5: Configure Agent Blueprints](#step-5-configure-agent-blueprints)
8. [Step 6: Enable Training Strategy Override](#step-6-enable-training-strategy-override)
9. [Step 7: Configure Debug Visualization](#step-7-configure-debug-visualization)
10. [Step 8: Run Training](#step-8-run-training)
11. [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Required Blueprints/Classes
- ✅ `BP_MocCharacter` (inherits from AMocCharacter)
- ✅ `BP_MocTrainer` (inherits from AMocTrainer - AIController)
- ✅ `BP_MocGameMode` (inherits from AMocGameMode)
- ✅ `BP_CapturePoint` (inherits from ACapturePoint)
- ✅ `BP_TeamManager` (inherits from ATeamManager) - **Optional**, can spawn C++ class directly

### Python Training Environment
- Python 3.10+
- PyTorch with ONNX Runtime
- RL training script configured for 5 agents

---

## Level Setup Overview

The MOC training environment requires:
1. **Game Mode** - Orchestrates match flow, spawns entities, handles scoring
2. **TeamManager** - Spawns 5v5 teams, manages respawns, tracks team state
3. **CapturePoints (×5)** - Objective locations that generate passive income
4. **Health Packs (×12)** - Resource pickups (handled by GameMode)
5. **Ammo Crates (×8)** - Resource pickups (handled by GameMode)

**Spatial Layout:**
```
Red Base (-7000, 0, 100) ← Point A
    ↓
North Outpost (-3500, 4000, 150) ← Point B
    ↓
Center Plaza (0, 0, 100) ← Point C [CONTESTED ZONE]
    ↓
South Outpost (3500, -4000, 150) ← Point D
    ↓
Blue Base (7000, 0, 100) ← Point E
```

---

## Step 1: Create Training Level

### 1.1 Create New Level
1. **Open UE5 Editor**
2. **File → New Level → Empty Level**
3. **Save As:** `Content/Game/Maps/Training/Training.umap`

### 1.2 Add Essential Components
1. **Add Lighting:**
   - Place `Directional Light` at (0, 0, 1000)
   - Set Intensity: 3.0 lux
   - Enable "Atmosphere Sun Light"

2. **Add Environment:**
   - Place `Sky Atmosphere` actor (default settings)
   - Place `SkyLight` (Intensity: 1.0)
   - Optional: Add `Floor` static mesh (150m × 150m plane)

3. **Add NavMesh:**
   - Place `NavMeshBoundsVolume` at (0, 0, 0)
   - Set Brush Shape: Box
     - X: 15000 (150m)
     - Y: 15000 (150m)
     - Z: 500 (5m height for vertical navigation)
   - **Press 'P' in viewport** to verify green NavMesh overlay appears

### 1.3 Save Level
- **Ctrl+S** to save
- Verify file saved to `Content/Game/Maps/Training/Training.umap`

---

## Step 2: Configure Game Mode

### 2.1 Set Level Game Mode Override
1. **Open World Settings** (Window → World Settings)
2. **Under "Game Mode":**
   - Game Mode Override: `BP_MocGameMode` (or `AMocGameMode` if using C++ class)
3. **Leave Default Pawn Class empty** (TeamManager spawns agents)

### 2.2 Configure Game Mode Settings (in Details Panel)

**If using Blueprint:**
1. **Open BP_MocGameMode** (double-click in Content Browser)
2. **In Class Defaults panel**, configure:

**Match Settings:**
```
Max Match Duration: 600.0 (10 minutes)
Winning Score: 300
Auto Start Match: ☑ TRUE
```

**Scoring Configuration:**
```
Capture Reward: 25
Kill Points: 5
Passive Income Rate: 1.0 (points/sec per owned point)
```

**Classes to Spawn:**
```
Team Manager Class: BP_TeamManager (or ATeamManager)
Capture Point Class: BP_CapturePoint (or ACapturePoint)
Health Pack Class: BP_HealthPack (if available)
Ammo Crate Class: BP_AmmoCrate (if available)
```

**Capture Point Locations** (match map specification):
```
Point A Location: X=-7000, Y=0, Z=100
Point B Location: X=-3500, Y=4000, Z=150
Point C Location: X=0, Y=0, Z=100
Point D Location: X=3500, Y=-4000, Z=150
Point E Location: X=7000, Y=0, Z=100
```

**Health Pack Locations** (12 total - configure array):
```
[0]: (-5000, 2000, 100)    [6]: (5000, 2000, 100)
[1]: (-5000, -2000, 100)   [7]: (5000, -2000, 100)
[2]: (-2500, 4000, 150)    [8]: (2500, -4000, 150)
[3]: (-2500, 0, 100)       [9]: (2500, 0, 100)
[4]: (0, 2500, 100)        [10]: (0, -2500, 100)
[5]: (-3500, 3000, 150)    [11]: (3500, -3000, 150)
```

**Ammo Crate Locations** (8 total):
```
[0]: (-6000, 0, 100)       [4]: (6000, 0, 100)
[1]: (-3500, 4000, 150)    [5]: (3500, -4000, 150)
[2]: (0, 1500, 100)        [6]: (0, -1500, 100)
[3]: (-1500, 0, 100)       [7]: (1500, 0, 100)
```

**Debug Settings:**
```
Show Debug Info: ☑ TRUE (for Phase 1 training)
```

3. **Compile and Save** Blueprint

---

## Step 3: Spawn TeamManager Actor

### 3.1 Place TeamManager in Level

**Option A: Let Game Mode Spawn It (Recommended)**
- The `AMocGameMode::BeginPlay()` automatically spawns TeamManager
- No manual placement needed
- TeamManager spawns at world origin (0, 0, 0)

**Option B: Manually Place for Configuration**
1. **Content Browser → Search:** "TeamManager"
2. **Drag `BP_TeamManager`** (or C++ class) into viewport
3. **Set Transform:**
   - Location: (0, 0, 0)
   - Rotation: (0, 0, 0)
   - Scale: (1, 1, 1)

### 3.2 Configure TeamManager Details

**Select TeamManager actor in Outliner**, then in Details panel:

**Team Composition:**
```
Agents Per Team: 5
Character Class: BP_MocCharacter
```

**Spawn Locations:**
```
Red Team Spawn Location: X=-5000, Y=0, Z=100
Blue Team Spawn Location: X=5000, Y=0, Z=100
Spawn Radius: 300.0 (agents spawn within 3m radius)
```

**Respawn Settings:**
```
Respawn Delay: 5.0 (seconds after death)
```

**References:**
```
Fog Of War Manager: [Leave Empty - auto-finds in level]
```

**Debug:**
```
Show Debug Info: ☑ TRUE
```

### 3.3 Important TeamManager Functions

The TeamManager provides these key functions:
- `SpawnTeams()` - Called by GameMode at match start
- `QueueRespawn(Agent, TeamID)` - Handles agent death
- `GetTeamAgents(TeamID)` - Returns all active agents
- `RegisterKill(KillerTeamID, VictimTeamID, Victim)` - Updates scores

---

## Step 4: Place Capture Points

### 4.1 Manual Placement Method

**For each capture point (A, B, C, D, E):**

1. **Drag `BP_CapturePoint`** into viewport
2. **Set Transform:**
   - **Point A (Red Base):**
     - Location: (-7000, 0, 100)
     - Point ID: `PointA`
     - Initial Owner: `RedTeam`

   - **Point B (North Outpost):**
     - Location: (-3500, 4000, 150)
     - Point ID: `PointB`
     - Initial Owner: `Neutral`

   - **Point C (Center Plaza):**
     - Location: (0, 0, 100)
     - Point ID: `PointC`
     - Initial Owner: `Neutral`

   - **Point D (South Outpost):**
     - Location: (3500, -4000, 150)
     - Point ID: `PointD`
     - Initial Owner: `Neutral`

   - **Point E (Blue Base):**
     - Location: (7000, 0, 100)
     - Point ID: `PointE`
     - Initial Owner: `BlueTeam`

### 4.2 Configure Each Capture Point

**In Details panel for each CapturePoint:**

**Identity:**
```
Point ID: [Select from dropdown]
Initial Owner: [RedTeam/Neutral/BlueTeam]
```

**Capture Mechanics:**
```
Capture Time: 20.0 (seconds)
Decay Rate: 0.5 (50% per second)
```

**Capture Zone:**
```
Capture Radius: 500.0 (5 meters)
Capture Height: 300.0 (3 meters vertical)
```

**Strategic Bonus** (Optional, for agent learning):
```
Point B: "High Ground (+10% accuracy)"
Point C: "Resource Hub (3 health packs nearby)"
Point D: "Cover Position (defensive advantage)"
```

**Debug:**
```
Show Debug Info: ☑ TRUE
```

### 4.3 Alternative: Let Game Mode Spawn Them

If you configured capture point locations in `BP_MocGameMode` (Step 2.2), the Game Mode will spawn all 5 capture points automatically at `BeginPlay()`.

**To verify auto-spawning:**
1. Press **Alt+P** (Play in Editor)
2. Check Outliner for `CapturePoint_A`, `CapturePoint_B`, etc.
3. Press **Escape** to stop PIE

---

## Step 5: Configure Agent Blueprints

### 5.1 Configure BP_MocCharacter

1. **Open `BP_MocCharacter`** in Content Browser
2. **Go to Class Defaults**

**AI Controller:**
```
Auto Possess AI: PlacedInWorldOrSpawned
AI Controller Class: BP_MocTrainer (or AMocTrainer)
```

**Components:**
Verify the following components exist:
- ✅ `ScholaMocAgent` (UScholaMocAgent component)
- ✅ `CapsuleComponent` (collision)
- ✅ `SkeletalMeshComponent` (visual representation)
- ✅ `CharacterMovement` (navigation)

**Team Settings** (in MocCharacter category):
```
Team ID: 0 (will be set by TeamManager at spawn)
Max Health: 100.0
Movement Speed: 600.0
```

3. **Compile and Save**

### 5.2 Configure BP_MocTrainer (AIController)

1. **Open `BP_MocTrainer`** in Content Browser
2. **Go to Class Defaults**

**Training Configuration:**
```
Episode Max Steps: 6000 (10 minutes at 10Hz)
Steps Per Decision: 6 (10Hz decision frequency at 60 FPS)
```

**EQS Configuration:**
```
EQS Query Template: EQS_MOC_TacticalPositioning
Query Run Frequency: 10.0 Hz (every 0.1 seconds)
```

**Reward Weights** (default balanced values):
```
Assault Movement Reward: 0.01
Assault Health Penalty: 5.0
Defend Position Reward: 2.0
Defend Health Bonus: 2.0
Support Position Reward: 1.0
Support Health Bonus: 1.5
Death Penalty: 100.0
Time Penalty: 0.001
```

**Debug:**
```
Enable Debug Visualization: ☑ TRUE
```

3. **Compile and Save**

---

## Step 6: Enable Training Strategy Override

### 6.1 What is Training Strategy Override?

During **Phase 1 training**, we train **3 separate policies** (one for each strategy: Assault, Defend, Support). The override system allows you to force all agents to use a specific strategy for focused training.

**Training Schedule:**
- **Day 1:** Train Assault policy (all agents use Assault)
- **Day 2:** Train Defend policy (all agents use Defend)
- **Day 3:** Train Support policy (all agents use Support)

### 6.2 Enable Override for Phase 1

**Option A: Configure in Blueprint Default**
1. **Open `BP_MocCharacter`**
2. **Select `ScholaMocAgent` component** in Components panel
3. **In Details → "MOC | Phase1Training" category:**
   ```
   ☑ Override Strategy (Phase 1 Training): TRUE
   Training Strategy: Assault (for Day 1)
   ```
4. **Compile and Save**

**Result:** All agents spawned from this blueprint will use Assault strategy.

**Option B: Configure Per-Agent in Level** (Advanced)
1. If you manually placed agents in the level (not recommended for training)
2. Select each agent in Outliner
3. Find `ScholaMocAgent` component → Configure override

### 6.3 Training Day Configuration

**Day 1: Train Assault Policy**
```
All agents:
  ☑ Override Strategy: TRUE
  Training Strategy: Assault

Run Python script:
  python train_strategy.py --checkpoint_dir checkpoints/assault/
```

**Day 2: Train Defend Policy**
```
All agents:
  ☑ Override Strategy: TRUE
  Training Strategy: Defend

Run Python script:
  python train_strategy.py --checkpoint_dir checkpoints/defend/
```

**Day 3: Train Support Policy**
```
All agents:
  ☑ Override Strategy: TRUE
  Training Strategy: Support

Run Python script:
  python train_strategy.py --checkpoint_dir checkpoints/support/
```

### 6.4 ⚠️ CRITICAL: Disable Override After Phase 1

**Before starting Phase 3 (MCTS integration):**
```
All agents:
  ☐ Override Strategy: FALSE
```

This allows the SquadManager to command strategies dynamically via MCTS.

---

## Step 7: Configure Debug Visualization

### 7.1 Enable Trainer Visualization

1. **Open `BP_MocTrainer`**
2. **Class Defaults → Debug category:**
   ```
   Enable Debug Visualization: ☑ TRUE
   ```

### 7.2 What the Visualization Shows

**Above Each Agent (Cyan Text):**
```
Strategy: ASSAULT [TRAINING OVERRIDE]
Health: 85.0%
Steps: 345 / 6000
Reward: +127.3
Episodes: 42
```

**EQS Weights Panel** (real-time policy output):
```
EQS Weights:
  Cover: 0.73
  Enemy: 0.92
  Ally: 0.45
  Objective: 0.61
  Health: 0.23
  Ammo: 0.12
  Explore: 0.34
  Center: 0.56
```

**Spatial Indicators:**
- **Yellow Sphere + Line:** EQS target location (where agent is moving)
- **Green Spheres:** Allied agent positions
- **Red Spheres + Lines:** Visible enemy positions
- **Orange/Blue/Purple Halo:** Strategy color coding
  - Orange = Assault
  - Blue = Defend
  - Purple = Support

### 7.3 Enable Capture Point Visualization

Each capture point shows:
- **Ownership color:** Red/Neutral/Blue
- **Capture progress bar:** 0-100%
- **Agent count:** "R: 2 | B: 1" (contested)

---

## Step 8: Run Training

### 8.1 Pre-Flight Checklist

Before starting training, verify:

**Level Setup:**
- ✅ Training level saved (`Training.umap`)
- ✅ NavMesh visible (Press 'P' in viewport → green overlay)
- ✅ Game Mode Override set to `BP_MocGameMode`
- ✅ TeamManager configured (spawn locations, character class)
- ✅ 5 Capture Points placed or auto-spawned

**Agent Configuration:**
- ✅ `BP_MocCharacter` has `ScholaMocAgent` component
- ✅ AI Controller Class set to `BP_MocTrainer`
- ✅ Training Strategy Override **ENABLED** for Phase 1
- ✅ Training Strategy set to target strategy (Assault/Defend/Support)

**Visualization:**
- ✅ Debug visualization enabled in Trainer
- ✅ Capture Point debug enabled

**Python Environment:**
- ✅ Python training script configured
- ✅ ONNX Runtime installed
- ✅ Checkpoint directory created

### 8.2 Start Training Session

**1. Launch UE5 Editor**
```
1. Open project
2. Load Training.umap level
3. Verify all settings from checklist
4. Press Alt+P (Play in Editor)
5. Verify 10 agents spawn (5 red, 5 blue)
6. Verify debug text appears above agents
```

**2. Start Python Training Script**
```bash
cd training/
python train_strategy.py \
    --num_agents 5 \
    --max_iterations 50000 \
    --learning_rate 0.0003 \
    --batch_size 256 \
    --checkpoint_dir checkpoints/assault/ \
    --log_interval 100
```

**3. Monitor Training Progress**

**In UE5 Editor:**
- Watch episode reward increasing over time
- Verify agents are moving (not stuck)
- Check EQS weights changing as policy learns
- Observe strategic behavior emerging:
  - **Assault:** Agents push forward aggressively
  - **Defend:** Agents hold positions near capture points
  - **Support:** Agents stay near allies, avoid frontline

**In Python Console:**
```
Episode 100 | Avg Reward: 23.4 | Steps: 1247
Episode 200 | Avg Reward: 45.7 | Steps: 1893
Episode 300 | Avg Reward: 68.2 | Steps: 2341
...
[Target: >50.0 avg reward for convergence]
```

### 8.3 Training Duration

**Recommended Training Time:**
- **Assault Policy:** 4-6 hours (50,000 episodes)
- **Defend Policy:** 4-6 hours (50,000 episodes)
- **Support Policy:** 4-6 hours (50,000 episodes)

**Convergence Criteria:**
- Avg episode reward stabilizes (±5% variance over 1000 episodes)
- Agent behavior matches strategy intent (visual verification)
- Model checkpoint saved successfully

### 8.4 Save Trained Models

After training completes:
```bash
# Models saved automatically to:
checkpoints/assault/policy_final.onnx
checkpoints/defend/policy_final.onnx
checkpoints/support/policy_final.onnx

# Copy to UE5 Content folder:
cp checkpoints/assault/policy_final.onnx \
   Content/AI/Policies/assault_policy.onnx
```

---

## Step 9: Switch Strategies Between Training Days

### 9.1 Switching from Assault → Defend

After completing Assault training:

1. **Stop PIE** (Press Escape in UE5)
2. **Open `BP_MocCharacter`**
3. **Select `ScholaMocAgent` component**
4. **Change Training Strategy:**
   ```
   ☑ Override Strategy: TRUE
   Training Strategy: Defend (changed from Assault)
   ```
5. **Compile and Save**
6. **Restart PIE** (Alt+P)
7. **Run new Python training:**
   ```bash
   python train_strategy.py \
       --checkpoint_dir checkpoints/defend/
   ```

### 9.2 Switching from Defend → Support

Repeat the same process:
1. Stop PIE
2. Change Training Strategy to `Support`
3. Compile and Save
4. Restart PIE
5. Run new Python training with `--checkpoint_dir checkpoints/support/`

---

## Step 10: Validate Training Setup

### 10.1 Quick Validation Test

**Test 1: Spawning**
```
1. Press Alt+P
2. Wait 2 seconds
3. Verify: 10 agents visible (5 red, 5 blue near spawn points)
```

**Test 2: Movement**
```
1. After spawning, observe for 10 seconds
2. Verify: Agents move toward capture points
3. Verify: Yellow debug line shows EQS target
```

**Test 3: Combat**
```
1. Wait for agents to encounter enemies
2. Verify: Agents auto-attack when in range
3. Verify: Health bars decrease on hit
```

**Test 4: Respawn**
```
1. Wait for an agent to die
2. Verify: Agent disappears
3. Wait 5 seconds
4. Verify: Agent respawns at team spawn location
```

**Test 5: Capture Points**
```
1. Watch an agent enter capture zone
2. Verify: Capture progress bar increases
3. Verify: Point changes color when captured
```

### 10.2 Python Connection Test

**Test Python-UE5 Communication:**
```bash
python test_connection.py

Expected output:
✓ Connected to UE5 (port 8080)
✓ Received 5 agent observations
✓ Sent 5 agent actions
✓ Received rewards: [0.1, -0.2, 0.3, 0.0, -0.1]
```

---

## Troubleshooting

### Issue: No agents spawning

**Symptoms:** Level starts, but no agents appear

**Solutions:**
1. **Check TeamManager:**
   - Verify `Character Class` is set to `BP_MocCharacter`
   - Verify `Agents Per Team = 5`
   - Check spawn locations are above ground (Z > 0)

2. **Check Game Mode:**
   - Verify `Team Manager Class` is set
   - Check `bAutoStartMatch = true`

3. **Check NavMesh:**
   - Press 'P' in viewport → should see green overlay
   - If no green, rebuild NavMesh (Build → Build Paths)

4. **Check Output Log:**
   - Window → Developer Tools → Output Log
   - Look for spawn errors

### Issue: Agents spawn but don't move

**Symptoms:** Agents stand still at spawn point

**Solutions:**
1. **Check AI Controller:**
   - Verify `BP_MocCharacter` has `AI Controller Class = BP_MocTrainer`
   - Check `Auto Possess AI = PlacedInWorldOrSpawned`

2. **Check EQS:**
   - Verify `EQS Query Template` is set in Trainer
   - Check EQS asset exists: `Content/AI/EQS/EQS_MOC_TacticalPositioning.uasset`

3. **Check NavMesh:**
   - Verify spawn location is ON NavMesh (green overlay)
   - If spawn is off NavMesh, adjust spawn location Z-height

### Issue: No debug visualization

**Symptoms:** Can't see debug text or indicators

**Solutions:**
1. **Enable in Trainer:**
   - Open `BP_MocTrainer` → `bEnableDebugVisualization = true`

2. **Enable in Capture Points:**
   - Select each CapturePoint → `bShowDebugInfo = true`

3. **Check Console Variables:**
   - Press ` (tilde) in PIE
   - Type: `showdebug ai`
   - Type: `stat fps` (verify game is running)

### Issue: Training not converging

**Symptoms:** Rewards not increasing after many episodes

**Solutions:**
1. **Check Reward Configuration:**
   - Verify reward weights are non-zero
   - Check for reward sign errors (penalties should be negative)

2. **Verify Strategy Override:**
   - Check `[TRAINING OVERRIDE]` appears in debug text
   - If not, enable override in ScholaMocAgent

3. **Check Python Hyperparameters:**
   - Learning rate too high/low?
   - Batch size appropriate for 5 agents?
   - Exploration epsilon decaying properly?

4. **Verify Episode Termination:**
   - Check episodes are ending (not running forever)
   - Max steps should be reasonable (6000 steps = 10 min)

### Issue: Python can't connect to UE5

**Symptoms:** `ConnectionRefusedError` in Python

**Solutions:**
1. **Check PIE is running:**
   - UE5 must be in Play mode (Alt+P)

2. **Check port configuration:**
   - Default port: 8080
   - Verify firewall not blocking

3. **Check Python script:**
   - Verify `UE_HOST = "localhost"`
   - Verify `UE_PORT = 8080`

### Issue: Agents stuck on geometry

**Symptoms:** Agents walk into walls and get stuck

**Solutions:**
1. **Rebuild NavMesh:**
   - Build → Build Paths
   - Verify NavMesh covers walkable areas

2. **Check Collision:**
   - Verify walls have collision enabled
   - Check NavMesh agent radius matches character capsule

3. **Adjust EQS Query:**
   - Increase `Trace Channel` filtering in EQS tests
   - Add `Distance to Geometry` test with minimum threshold

### Issue: "SquadManager commands will be IGNORED" warning

**Symptoms:** Warning message in Output Log

**Solution:**
- **This is expected during Phase 1 training!**
- The warning reminds you that Training Strategy Override is enabled
- After Phase 1, disable override in `ScholaMocAgent`

### Issue: Capture points not working

**Symptoms:** Agents enter zone but capture progress doesn't increase

**Solutions:**
1. **Check Point Configuration:**
   - Verify `Capture Radius > 0`
   - Check `CaptureZone` component is enabled
   - Verify collision is set to "OverlapAllDynamic"

2. **Check Agent Team ID:**
   - Agents must have valid Team ID (0 or 1)
   - Check TeamManager assigned team correctly

3. **Check Event Binding:**
   - Game Mode should subscribe to `OnPointCaptured`
   - Verify events are bound in `BeginPlay()`

---

## Success Metrics (Phase 1)

### Assault Policy Convergence
```
✓ Avg episode reward > 50.0
✓ Avg survival time > 30 seconds
✓ Kill/Death ratio > 0.8
✓ Forward progress metric > 60%
✓ Model saved: checkpoints/assault/policy_final.onnx
```

### Defend Policy Convergence
```
✓ Avg episode reward > 60.0
✓ Avg survival time > 45 seconds
✓ Position holding score > 70%
✓ Objective loss rate < 20%
✓ Model saved: checkpoints/defend/policy_final.onnx
```

### Support Policy Convergence
```
✓ Avg episode reward > 55.0
✓ Ally proximity maintained > 80% of time
✓ Objective control contribution > 75%
✓ Team health differential positive
✓ Model saved: checkpoints/support/policy_final.onnx
```

---

## File Locations Reference

| Component | File Path |
|-----------|-----------|
| **Blueprints** | |
| MocCharacter BP | `Content/Game/Blueprints/Actor/Characters/BP_Agent.uasset` |
| MocTrainer BP | `Content/Game/Blueprints/AI/BP_MocTrainer.uasset` |
| MocGameMode BP | `Content/Game/Blueprints/Core/BP_MocGameMode.uasset` |
| CapturePoint BP | `Content/Game/Blueprints/Actor/BP_CapturePoint.uasset` |
| TeamManager BP | `Content/Game/Blueprints/Team/BP_TeamManager.uasset` |
| **EQS Assets** | |
| Tactical Positioning Query | `Content/Game/Blueprints/AI/EQS/EQS_MOC_TacticalPositioning.uasset` |
| **C++ Classes** | |
| ScholaMocAgent | `Source/GameAI_Project/Public/AI/ScholaMocAgent.h` |
| MocTrainer | `Source/GameAI_Project/Public/AI/MocTrainer.h` |
| MocCharacter | `Source/GameAI_Project/Public/Characters/MocCharacter.h` |
| TeamManager | `Source/GameAI_Project/Public/Team/TeamManager.h` |
| MocGameMode | `Source/GameAI_Project/Public/Core/MocGameMode.h` |
| CapturePoint | `Source/GameAI_Project/Public/Actors/CapturePoint.h` |
| **Training Scripts** | |
| Main training loop | `training/train_strategy.py` |
| Environment interface | `training/ue5_env.py` |
| PPO implementation | `training/ppo_agent.py` |

---

## Next Steps After Phase 1

1. **Verify all 3 policies trained successfully:**
   ```bash
   ls -lh checkpoints/
   # Should see: assault/, defend/, support/ directories
   # Each with policy_final.onnx file
   ```

2. **Copy models to Content folder:**
   ```bash
   cp checkpoints/assault/policy_final.onnx Content/AI/Policies/
   cp checkpoints/defend/policy_final.onnx Content/AI/Policies/
   cp checkpoints/support/policy_final.onnx Content/AI/Policies/
   ```

3. **⚠️ DISABLE Training Override:**
   - Open `BP_MocCharacter`
   - Set `Override Strategy = FALSE`
   - Compile and Save

4. **Proceed to Phase 2: World Model Data Collection**
   - See: `PHASE2_WORLDMODEL_DATACOLLECTION.md`

5. **Prepare for Phase 3: MCTS Integration**
   - Implement `UTeamWorldModel`
   - Train Tactical Play Value Network
   - Enable SquadManager MCTS planning

---

**Phase 1 Setup Complete! 🎯**

You now have a fully configured training environment for MOC v10.2 RL agent training.

For questions or issues, check:
- 📖 `v10.2Architecture.md` - System architecture
- 📖 `MocGameEnvSpecification.md` - Game rules and mechanics
- 📖 `TRAINING_VISUALIZATION_GUIDE.md` - Debug visualization details
