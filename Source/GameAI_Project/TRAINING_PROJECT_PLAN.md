# MOC v10.2 Training Project Plan

**Status:** Ready to Start | **Date:** 2026-02-10 | **Engine:** UE5.6

---

## Overview

This document outlines the step-by-step plan for training and deploying the MOC v10.2 system. The plan consists of three main phases executed sequentially.

---

## Prerequisites (Before Training)

### ✅ Already Complete
- Week 1-3 infrastructure implemented
- SquadManager with centralized MCTS
- TeamWorldModel for batch predictions
- Schola plugin integration

### ⏳ Required Setup (30 minutes)

#### 1. Unreal Editor Configuration
```
□ Open BP_ScholaEnvironment in level
  - Set bAutoDiscoverAgents = true
  - Set bEnableCentralizedPlanning = false (disable for Phase 1)
  - Set ScholaEnvID = 0

□ Configure BP_MocCharacter
  - Add UScholaMocAgent component
  - Set AIController = BP_MocTrainer
  - Verify HealthComponent and WeaponComponent exist

□ Setup BP_MocTrainer
  - Add UMocTacticalObserver to Observers array
  - Add UTacticalParameterActuator to Actuators array
  - Configure reward parameters (see below)
```

#### 2. Agent Strategy Configuration (per agent)
```
Strategy-Specific Rewards in BP_MocTrainer:

Assault:
  - DamageDealtReward = 1.0
  - MovementReward = 0.01
  - DeathPenalty = 100.0

Defend:
  - PositionHoldReward = 2.0
  - HealthConservationBonus = 0.5
  - DeathPenalty = 100.0

Support:
  - AllyProximityReward = 1.0
  - ObjectiveControlReward = 3.0
  - DeathPenalty = 100.0
```

#### 3. Python Environment Setup (10 minutes)
```bash
# Install dependencies
cd training/
pip install ray[rllib]==2.9.0 torch==2.1.0 gymnasium==0.29.0

# Verify Schola connectivity
python test_connection.py
# Expected: "✓ Connected to Schola on port 9876"
```

---

## Phase 1: RL Agent Training (Per Strategy)

**Goal:** Train 3 separate RL policies (Assault, Defend, Support) that learn optimal EQS weight parameterization.

**Duration:** ~3 days (8-12 hours training per strategy)

### Step 1.1: Train Assault Policy (Day 1)

```bash
# Terminal 1: Start UE5
# 1. Open GameAI_Project.uproject
# 2. Load level with BP_ScholaEnvironment
# 3. Set all 5 agents to "Assault" strategy in BP_MocTrainer
# 4. Play in Editor (Alt+P)

# Terminal 2: Start training
cd training/
python train_strategy.py \
    --strategy assault \
    --num_agents 5 \
    --max_iterations 50000 \
    --checkpoint_dir checkpoints/assault/
```

**Success Criteria:**
- Episode reward converges >50.0 (assault agents should deal damage)
- Average survival time >30 seconds
- Model saved to `checkpoints/assault/policy_final.onnx`

### Step 1.2: Train Defend Policy (Day 2)

```bash
# Change all agents to "Defend" strategy in BP_MocTrainer
# Re-launch PIE

python train_strategy.py \
    --strategy defend \
    --num_agents 5 \
    --max_iterations 50000 \
    --checkpoint_dir checkpoints/defend/
```

**Success Criteria:**
- Episode reward converges >60.0 (defenders should survive longer)
- Average survival time >45 seconds
- Position holding score >70%

### Step 1.3: Train Support Policy (Day 3)

```bash
# Change all agents to "Support" strategy in BP_MocTrainer
# Re-launch PIE

python train_strategy.py \
    --strategy support \
    --num_agents 5 \
    --max_iterations 50000 \
    --checkpoint_dir checkpoints/support/
```

**Success Criteria:**
- Episode reward converges >55.0
- Ally proximity maintained >80% of time
- Objective control score >75%

### Step 1.4: Deploy Trained Policies

```bash
# Copy ONNX models to project
cp checkpoints/assault/policy_final.onnx Content/AI/Policies/AssaultPolicy.onnx
cp checkpoints/defend/policy_final.onnx Content/AI/Policies/DefendPolicy.onnx
cp checkpoints/support/policy_final.onnx Content/AI/Policies/SupportPolicy.onnx

# Update BP_MocCharacter to load strategy-specific policy
# See: UScholaMocAgent::LoadPolicyForStrategy()
```

---

## Phase 2: World Model Data Collection

**Goal:** Collect 5000+ agent transitions to train AgentWorldModel (state predictor).

**Duration:** ~1 day (overnight data collection)

### Step 2.1: Enable Data Logging

```cpp
// In BP_MocCharacter or C++
UPROPERTY(EditAnywhere)
bool bEnableDataCollection = true;

UPROPERTY(EditAnywhere)
FString DataLogPath = "Saved/TrainingData/agent_transitions.json";
```

### Step 2.2: Run Data Collection Matches

```bash
# Terminal 1: Start UE5
# 1. Enable bEnableDataCollection = true on all agents
# 2. Run 1000 matches (5 agents × 1000 = 5000 transitions)
# 3. Use automated match runner (see below)

# Terminal 2: Launch match automation
python run_data_collection.py \
    --num_matches 1000 \
    --match_duration 300 \
    --output_dir Saved/TrainingData/
```

**Data Format (per transition):**
```json
{
  "timestamp": 1234567890,
  "agent_id": "Team0_Agent2",
  "current_state": {
    "observation": [0.5, -0.3, ...],  // 52-dim
    "strategy": "Defend",
    "health": 0.85,
    "position": [45.0, 23.0, 1.0]
  },
  "action": [0.2, -0.5, 0.8, ...],  // 8-dim EQS weights
  "next_state": {
    "observation": [0.52, -0.28, ...],
    "health": 0.80,
    "position": [46.5, 24.0, 1.0]
  },
  "reward": 2.5,
  "done": false
}
```

### Step 2.3: Train World Model

```bash
cd training/world_model/
python train_agent_world_model.py \
    --data_path ../../Saved/TrainingData/agent_transitions.json \
    --epochs 100 \
    --batch_size 128 \
    --output agent_world_model.onnx
```

**Model Architecture:**
```
Input: 52 (state) + 3 (strategy one-hot) + 8 (action) = 63-dim
Hidden: 256 → 256 → 128
Output: 52 (predicted next state) + 1 (predicted reward)
```

**Success Criteria:**
- Prediction MSE <0.1 on validation set
- Reward prediction MAE <5.0
- Model exports to ONNX successfully

### Step 2.4: Integrate World Model

```bash
# Copy trained model
cp agent_world_model.onnx Content/AI/Models/AgentWorldModel.onnx

# Verify UAgentWorldModel loads it
# Check logs: "Agent World Model loaded successfully (63→53 dims)"
```

---

## Phase 3: MCTS with World Model

**Goal:** Enable centralized SquadManager to use world model for tactical planning.

**Duration:** ~2 days (integration + testing)

### Step 3.1: Enable Centralized Planning

```cpp
// In BP_ScholaEnvironment
bEnableCentralizedPlanning = true;  // ✅ Enable SquadManager

// In BP_MocGameMode
UPROPERTY(EditAnywhere)
bool bUseMCTSPlanning = true;  // Enable MCTS (vs random)
```

### Step 3.2: Configure MCTS Parameters

```cpp
// In ASquadManager or BP_SquadManager
UPROPERTY(EditAnywhere)
float PlanningTimeMs = 15.0;  // Time budget per planning cycle

UPROPERTY(EditAnywhere)
int32 MaxIterations = 50;  // Max MCTS iterations

UPROPERTY(EditAnywhere)
float ExplorationConstant = 1.414;  // UCB exploration (sqrt(2))

UPROPERTY(EditAnywhere)
int32 SimulationDepth = 3;  // How many steps to simulate ahead
```

### Step 3.3: Test Planning Loop

```bash
# Start PIE with verbose logging
# Check Output Log for:

"Team 0 MCTS Planning: 12.3 ms, 45 iterations"
"Selected Tactical Play: AggressivePush"
"Distributing roles: [Assault, Assault, Assault, Defend, Support]"
"Agent 0 commanded: Assault"
...
```

**Success Criteria:**
- Planning completes within 15ms budget (P95)
- All 10 tactical plays evaluated properly
- Agents receive and execute commands
- Win rate vs random baseline >60%

### Step 3.4: Collect Team Trajectories (Optional)

```bash
# For training Team Value Network (future enhancement)
python collect_team_data.py \
    --num_matches 1000 \
    --output Saved/TrainingData/team_trajectories.json
```

**Team Trajectory Format:**
```json
{
  "team_state": [0.5, 0.3, ...],  // 60-dim FTeamState
  "tactical_play": "AggressivePush",
  "role_distribution": ["Assault", "Assault", "Assault", "Defend", "Support"],
  "next_team_state": [0.52, 0.31, ...],
  "team_reward": 15.2,
  "match_result": "WIN"
}
```

---

## Validation & Testing

### Test 1: Single Agent Performance
```
Run 100 matches with single strategy (e.g., all Assault)
Metrics:
  - Average episode reward
  - Survival time
  - Damage dealt
```

### Test 2: Mixed Strategy Performance
```
Run 100 matches with mixed team (2 Assault, 2 Defend, 1 Support)
Metrics:
  - Win rate
  - Team coordination (measured by formation variance)
  - Objective control time
```

### Test 3: v10.2 vs Baseline
```
Run 200 matches (100 v10.2 vs v10.1 baseline)
Metrics:
  - Win rate (target: >55%)
  - Planning time (target: <15ms)
  - Team survival time
```

---

## Timeline Summary

| Phase | Duration | Outcome |
|-------|----------|---------|
| **Prerequisites** | 0.5 days | UE5 + Python setup complete |
| **Phase 1: RL Training** | 3 days | 3 strategy policies trained (ONNX) |
| **Phase 2: World Model** | 1 day | AgentWorldModel trained and integrated |
| **Phase 3: MCTS** | 2 days | Centralized planning working |
| **Testing** | 1 day | Validation complete |
| **Total** | **7-8 days** | Full system operational |

---

## Success Metrics

| Metric | Target | Stretch Goal |
|--------|--------|--------------|
| **RL Policy Convergence** | Reward >50 | Reward >80 |
| **World Model Accuracy** | MSE <0.1 | MSE <0.05 |
| **MCTS Planning Time** | <15ms (P95) | <10ms (P95) |
| **Win Rate (v10.2 vs v10.1)** | >55% | >65% |
| **Team Coordination** | Visible synergy | Measurable improvement |

---

## Troubleshooting

### Issue: RL training not converging
**Solution:** Check reward function, reduce learning rate, increase episodes

### Issue: World model predictions inaccurate
**Solution:** Collect more data (5000→10000), increase model capacity, normalize inputs

### Issue: MCTS planning exceeds budget
**Solution:** Reduce MaxIterations (50→30), reduce SimulationDepth (3→2), optimize batch size

### Issue: Agents ignore commands
**Solution:** Verify SetCommandedStrategy() called, check Blackboard sync, debug AIController

---

## Next Steps After Completion

1. **Team Value Network Training:** Use collected team trajectories to train tactical play value predictor
2. **Adaptive Play Selection:** Use value network as MCTS prior probabilities
3. **Meta-Strategy Learning:** Train high-level policy that selects tactical plays based on opponent behavior
4. **Scalability Testing:** Test with 8v8 or 10v10 team sizes

---

## Questions?

- **Unreal Setup Issues:** Check SCHOLA_v10.2_INTEGRATION.md
- **Python Training Issues:** See training/README_TRAINING.md
- **MCTS Tuning:** Refer to NEXT_STEPS_WEEK4.md
- **Architecture Details:** See v10.2Architecture.md

---

**Ready to start training!** Begin with Prerequisites, then proceed to Phase 1.
