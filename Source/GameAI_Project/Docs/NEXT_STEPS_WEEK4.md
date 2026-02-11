# Week 4 Next Steps - Testing & Training

**Prerequisites:** Week 3 implementation complete ✅

---

## Immediate Actions (Before Training)

### 1. Compile Project
```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX

# Generate VS project files
# Right-click GameAI_Project.uproject → "Generate Visual Studio project files"

# Open and build
# Open GameAI_Project.sln in Visual Studio 2022
# Build → Build Solution (Ctrl+Shift+B)
```

**Expected Result:**
- ✅ 0 errors
- ⚠️ 3-5 warnings (deprecated methods - this is normal)
- Build time: 5-10 minutes

---

### 2. Run Initial Tests

#### Test 1: MCTS Initialization
**Goal:** Verify TeamMCTS creates successfully

```cpp
// In SquadManager::Initialize(), check logs for:
"Squad Commander initialized with Team MCTS for Team 0"
```

**Pass Criteria:**
- No crash on initialization
- TeamMCTSPlanner is not nullptr
- TeamWorldModel is not nullptr

---

#### Test 2: First Planning Cycle
**Goal:** Verify MCTS runs and completes within budget

**Steps:**
1. Start PIE (Play In Editor)
2. Wait 0.5 seconds for first planning cycle
3. Check Output Log for:
```
"Team 0 MCTS Planning: X.XX ms, Y iterations"
"Team 0 Agent 0 assigned: Assault"
"Team 0 Agent 1 assigned: Assault"
...
```

**Pass Criteria:**
- Planning time <15ms
- All 5 agents receive commands
- No warnings about MCTS not available

---

#### Test 3: Fog-of-War Update Fix
**Goal:** Verify vision updates continuously

**Steps:**
1. Start PIE
2. Enable debug visualization: `ShowDebug AI` in console
3. Watch agent vision range circles
4. Kill an agent
5. Verify vision circle disappears

**Pass Criteria:**
- Vision updates every frame while alive
- Vision stops updating when agent dies
- No early returns in MocCharacter::Tick()

---

#### Test 4: Command Reception
**Goal:** Verify agents execute commanded strategies

**Steps:**
1. Start PIE
2. Set breakpoint in `MocAIController::Tick()`
3. Verify `CurrentStrategy` matches SquadManager assignment

**Pass Criteria:**
- Blackboard "CurrentStrategy" enum matches commanded strategy
- EQS weights update based on strategy
- Behavior Tree responds to strategy changes

---

## Performance Profiling

### Enable Unreal Insights
```cpp
// In DefaultEngine.ini, add:
[Core.Log]
LogMCTS=Verbose
LogSquadManager=Verbose

// In-game console:
stat unit        // Show frame time
stat game        // Show game thread time
stat ai          // Show AI update time
```

### Capture MCTS Timing
Add telemetry to `SquadManager::PerformTacticalPlanning()`:
```cpp
double StartTime = FPlatformTime::Seconds();
ETacticalPlay BestPlay = TeamMCTSPlanner->FindBestTacticalPlay(GlobalState);
double EndTime = FPlatformTime::Seconds();

float PlanningMs = (EndTime - StartTime) * 1000.0f;

// Log to CSV for analysis
UE_LOG(LogTemp, Display, TEXT("MCTS,%d,%.3f,%d,%s"),
    PlanningCycleCount,
    PlanningMs,
    TeamMCTSPlanner->GetLastIterationCount(),
    *UEnum::GetValueAsString(BestPlay));
```

### Analyze Results
**Target Metrics:**
- P50 (Median): <10ms
- P95: <15ms
- P99: <20ms
- Mean iterations: 30-50

**Warning Thresholds:**
- If P95 >15ms → Reduce batch size or max iterations
- If iterations <20 → Increase time budget slightly
- If any call >50ms → Check for infinite loops

---

## Training Data Collection

### 1. Setup Match Recording
**Goal:** Collect 1000 team trajectories for value network training

```cpp
// In ASquadManager, add data logger
UPROPERTY()
UMatchDataLogger* DataLogger;

// In PerformTacticalPlanning(), log:
DataLogger->RecordTeamTransition(
    GlobalState,           // Before state
    BestPlay,              // Action taken
    NextGlobalState,       // After state (0.5s later)
    MatchOutcome           // Win/Loss at end
);
```

### 2. Required Data Format
```json
{
  "timestamp": 1234567890,
  "team_id": 0,
  "state": {
    "friendly_positions": [[x,y,z], ...],
    "friendly_healths": [0.8, 0.6, ...],
    "enemy_positions": [[x,y,z], ...],
    "average_health": 0.72
  },
  "tactical_play": "AggressivePush",
  "role_distribution": ["Assault", "Assault", "Assault", "Assault", "Support"],
  "next_state": { ... },
  "reward": {
    "win_prob": 0.65,
    "team_health_delta": -15.0,
    "objective_score": 2.5,
    "diversity_bonus": 0.5,
    "total": 0.92
  },
  "match_result": "WIN"
}
```

### 3. Training Pipeline
```python
# Train tactical play value network
python train_team_value_network.py \
    --data_path trajectories/*.json \
    --epochs 100 \
    --batch_size 64 \
    --learning_rate 0.001

# Expected output: team_value_network.onnx
```

---

## Integration with Reward System

### Current State
- TeamWorldModel returns FTeamReward
- MCTS uses TotalReward for backpropagation
- No training loop yet

### Week 4 Integration
1. **Collect Episode Rewards:**
   - Track cumulative team reward per match
   - Store final win/loss outcome
   - Associate with tactical play sequences

2. **Reward Attribution:**
   - Credit tactical plays with delayed rewards
   - Apply discount factor (γ=0.99)
   - Normalize rewards across matches

3. **Value Network Training:**
   - Input: FTeamState (60-dim)
   - Output: Expected team value (1-dim)
   - Loss: MSE between predicted and actual returns

---

## Debugging Common Issues

### Issue 1: MCTS Returns Default Play
**Symptom:** Always returns StandardComp

**Diagnosis:**
```cpp
// Check if TeamWorldModel initialized
if (!TeamWorldModel) { /* This should NOT happen */ }

// Check if batch predictions valid
FTeamBatchOutput Output = TeamWorldModel->PredictBatch(Input);
if (!Output.IsValid()) { /* World model failing */ }
```

**Fix:**
- Verify AgentWorldModel ONNX file exists
- Check TeamWorldModel::Initialize() returned true
- Enable verbose logging in TeamWorldModel

---

### Issue 2: Planning Time Exceeds Budget
**Symptom:** Warnings about >15ms planning time

**Diagnosis:**
```cpp
// Profile each phase
double SelectTime = ...;
double BatchTime = ...;
double BackpropTime = ...;
```

**Fix:**
- Reduce batch size: 8 → 4
- Reduce max iterations: 50 → 30
- Increase planning interval: 0.5s → 1.0s

---

### Issue 3: Agents Ignore Commands
**Symptom:** Agents don't execute commanded strategies

**Diagnosis:**
```cpp
// In MocAIController::Tick()
UE_LOG(LogTemp, Display, TEXT("Commanded: %s, Blackboard: %s"),
    *UEnum::GetValueAsString(MyChar->GetCommandedStrategy()),
    *UEnum::GetValueAsString(BlackboardComp->GetValueAsEnum("CurrentStrategy")));
```

**Fix:**
- Verify SetCommandedStrategy() called in DistributeRoles()
- Check Blackboard key names match Behavior Tree
- Restart Behavior Tree subtree on command change

---

### Issue 4: Fog-of-War Still Broken
**Symptom:** Vision updates don't happen

**Diagnosis:**
```cpp
// In MocCharacter::Tick()
UE_LOG(LogTemp, Verbose, TEXT("Tick: bIsAlive=%d, World=%p, FoWManager=%p"),
    bIsAlive, World, FoWManager);
```

**Fix:**
- Verify bIsAlive is true (not bIsDead)
- Check FogOfWarManager exists in TeamManager
- Ensure VisionRange > 0

---

## AWS Training Setup (Optional)

### EC2 Instance Recommendation
- **Instance Type:** g4dn.xlarge (1 GPU, $0.526/hr)
- **Storage:** 100GB SSD
- **OS:** Ubuntu 22.04 Deep Learning AMI
- **Framework:** PyTorch 2.0 + ONNX Runtime

### Training Script
```bash
# On EC2
git clone https://github.com/your-repo/cortex-training
cd cortex-training

# Install dependencies
pip install torch torchvision onnx onnxruntime numpy

# Download trajectories from S3
aws s3 sync s3://cortex-training-data/trajectories ./data/

# Train value network
python train_team_value_network.py \
    --data_path data/*.json \
    --output team_value_network.onnx

# Upload trained model
aws s3 cp team_value_network.onnx s3://cortex-models/
```

---

## Ablation Study Design

### Hypothesis
v10.2 (centralized) achieves higher win rate than v10.1 (decentralized)

### Experimental Setup
- **Matches:** 100 (50 v10.1 vs v10.1, 50 v10.2 vs v10.2)
- **Map:** Standard 150x150m arena
- **Match Duration:** 5 minutes
- **Metrics:** Win rate, average planning time, team health at end

### Variants to Test
1. **v10.1 Baseline:** 5 agents, individual MCTS
2. **v10.2 Full:** Centralized MCTS, 10 tactical plays
3. **v10.2 Ablation 1:** Centralized MCTS, 3 plays only (Assault/Defend/Support)
4. **v10.2 Ablation 2:** Centralized MCTS, no pruning (all 10 plays always)

### Success Criteria
- v10.2 win rate >55% vs v10.1
- v10.2 planning time <20% of v10.1
- No significant performance regression

---

## Week 4 Timeline

### Day 1-2: Testing & Debugging
- [ ] Compile project successfully
- [ ] Run all 4 integration tests
- [ ] Profile MCTS performance
- [ ] Fix any bugs discovered

### Day 3-4: Data Collection
- [ ] Implement match data logger
- [ ] Run 1000 training matches
- [ ] Export trajectories to JSON
- [ ] Validate data format

### Day 5-6: Training
- [ ] Train tactical play value network
- [ ] Export to ONNX
- [ ] Integrate into MCTS (prior probabilities)
- [ ] Re-test with learned priors

### Day 7: Evaluation
- [ ] Run ablation study (100 matches)
- [ ] Analyze results
- [ ] Document findings
- [ ] Plan Week 5 improvements

---

## Success Metrics Summary

| Metric | Target | Stretch Goal |
|--------|--------|--------------|
| Compilation | 0 errors | No warnings |
| MCTS Planning Time (P95) | <15ms | <10ms |
| Command Distribution | 100% | 100% |
| Fog-of-War Updates | Working | Optimized |
| Win Rate (v10.2 vs v10.1) | >55% | >65% |
| Training Data Quality | 1000 matches | 5000 matches |
| Value Network Accuracy | >70% | >85% |

---

## Questions to Answer in Week 4

1. **Performance:** Does centralized MCTS meet 15ms budget consistently?
2. **Quality:** Do tactical plays produce coherent team behavior?
3. **Learning:** Can value network predict team outcomes accurately?
4. **Scalability:** Does system handle 10+ concurrent matches?
5. **Robustness:** How does system handle edge cases (1v5, full wipe)?

---

## Contact & Support

- **Codebase:** `C:\Users\Foryoucom\Documents\GitHub\CORTEX`
- **Documentation:** `Source/GameAI_Project/*.md`
- **Logs:** `Saved/Logs/GameAI_Project.log`
- **Profiling:** Unreal Insights session files

**Ready to proceed with Week 4 testing!** 🚀
