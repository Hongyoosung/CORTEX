# Team World Model Training Pipeline

**DE v10.2 - Pure Team Model Approach**

This directory contains the training pipeline for the team-level world model neural network.

---

## Overview

The Team World Model predicts team-level state transitions:
- **Input:** FTeamState (60-dim) + ETacticalPlay (10-dim one-hot) = **70-dim**
- **Output:** Next FTeamState (60-dim) + Reward (3-dim) + Confidence (1-dim) = **64-dim**

---

## Pipeline Steps

### 1. Data Collection (In-Game)

Add `UTeamDataCollector` component to your SquadManager:

```cpp
// In ASquadManager::BeginPlay()
UTeamDataCollector* Collector = NewObject<UTeamDataCollector>(this);
Collector->RegisterComponent();
Collector->BeginRecording(MatchID);

// During tactical planning (every 0.5s)
FTeamState CurrentState = GetCurrentTeamState();
ETacticalPlay SelectedPlay = /* MCTS result */;

// Wait 0.5s, then record transition
FTeamState NextState = GetCurrentTeamState();
FCompositeReward Reward = CalculateReward(CurrentState, NextState);

Collector->RecordTransition(CurrentState, SelectedPlay, NextState, Reward);

// At match end
Collector->EndRecording(true); // Auto-saves to CSV
```

**Output:** CSV files in `Content/TrainingData/TeamTransitions/`

---

### 2. Setup Python Environment

```bash
cd Training/
pip install -r requirements.txt
```

**Requirements:**
- Python 3.8+
- PyTorch 2.0+
- ONNX Runtime 1.15+

---

### 3. Train the Model

```bash
python train_team_world_model.py \
  --data_dir ../Content/TrainingData/TeamTransitions \
  --output_dir ./output \
  --epochs 100 \
  --batch_size 256 \
  --lr 1e-3
```

**Training Parameters:**
- `--data_dir`: Directory containing CSV files
- `--output_dir`: Where to save trained models
- `--epochs`: Number of training epochs (default: 100)
- `--batch_size`: Batch size (default: 256)
- `--lr`: Learning rate (default: 1e-3)
- `--val_split`: Validation split ratio (default: 0.1)
- `--device`: cuda or cpu (auto-detected)

**Output:**
- `output/best_model.pth`: Best PyTorch checkpoint
- `output/team_world_model.onnx`: ONNX model for UE5

---

### 4. Deploy to Unreal Engine

1. **Copy ONNX model:**
   ```
   output/team_world_model.onnx → Content/AI/Models/team_world_model.onnx
   ```

2. **Load in C++:**
   ```cpp
   // In ASquadManager or GameMode
   UTeamWorldModel* WorldModel = NewObject<UTeamWorldModel>(this);
   FString ModelPath = FPaths::ProjectContentDir() / TEXT("AI/Models/team_world_model.onnx");

   if (WorldModel->InitModel(ModelPath))
   {
       UE_LOG(LogTemp, Log, TEXT("Team World Model loaded successfully!"));
   }
   ```

3. **Use for predictions:**
   ```cpp
   FTeamBatchInput Input;
   Input.CurrentStates.Add(CurrentTeamState);
   Input.TacticalPlays.Add(ETacticalPlay::Phalanx);

   FTeamBatchOutput Output = WorldModel->PredictBatch(Input);

   FTeamState PredictedNextState = Output.PredictedStates[0];
   FCompositeReward PredictedReward = Output.Rewards[0];
   float Confidence = Output.Confidences[0];
   ```

---

## Model Architecture

```
Input (70-dim)
    ↓
Encoder: Linear(70 → 256) → LayerNorm → ReLU → Dropout
    ↓
Encoder: Linear(256 → 512) → LayerNorm → ReLU → Dropout
    ↓
Residual Block 1: Linear(512 → 512) + Skip Connection
    ↓
Residual Block 2: Linear(512 → 512) + Skip Connection
    ↓
Decoder: Linear(512 → 256) → LayerNorm → ReLU → Dropout
    ↓
├─→ Next State Head: Linear(256 → 60)
├─→ Reward Head: Linear(256 → 3) [WinProb, HealthDelta, ObjScore]
└─→ Confidence Head: Linear(256 → 1)
    ↓
Output (64-dim)
```

**Total Parameters:** ~650K

---

## Loss Function

Custom weighted MSE loss:

```python
Total Loss = 1.0 × MSE(state) + 2.0 × MSE(reward) + 0.5 × MSE(confidence)
```

**Rationale:**
- **State (1.0):** Accurate next state prediction is fundamental
- **Reward (2.0):** Most important for MCTS value estimation
- **Confidence (0.5):** Less critical, used for uncertainty estimation

---

## Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| **Training Loss** | < 0.01 | After 100 epochs |
| **Validation Loss** | < 0.015 | No overfitting |
| **Inference Latency** | < 1.8ms | Batch=16, UE5 NNE |
| **State Error** | < 5% | Positional accuracy |
| **Reward Error** | < 0.1 | WinProb prediction |

---

## Troubleshooting

### No CSV files found
- Ensure `UTeamDataCollector` is recording during gameplay
- Check `Content/TrainingData/TeamTransitions/` exists
- Verify `bIsRecording = true`

### Training loss not decreasing
- Increase epochs (try 200+)
- Adjust learning rate (try 5e-4 or 2e-3)
- Check data quality (are states diverse?)
- Add more training samples (target: 10K+)

### ONNX export fails
- Update PyTorch: `pip install --upgrade torch`
- Ensure opset_version=11 (compatible with UE5 NNE)
- Check model has no unsupported operations

### UE5 model loading fails
- Verify ONNX file exists and is valid
- Check NNE runtime is enabled in Build.cs
- Ensure input/output tensor names match ("input", "output")
- Try loading in stub mode first (no model file)

---

## Data Requirements

**Minimum viable dataset:**
- 5,000 transitions (10 matches × 500 transitions each)
- Diverse tactical plays (all 10 plays represented)
- Mix of winning and losing episodes
- Various team compositions

**Production dataset:**
- 50,000+ transitions
- 100+ matches across different skill levels
- Balanced tactical play distribution
- Edge cases (low health, enemy dominance, etc.)

---

## Next Steps

1. ✅ Collect initial dataset (5K+ samples)
2. ✅ Train baseline model (100 epochs)
3. ⏳ Validate prediction accuracy
4. ⏳ Integrate with centralized MCTS
5. ⏳ Benchmark latency in UE5
6. ⏳ Iterative improvement (more data, architecture tuning)

---

## References

- **CLAUDE.md** - v10.2 Architecture Overview
- **WORLD_MODEL_REFACTOR.md** - Refactoring details
- **TeamWorldModel.h** - C++ API documentation
- **v10.2Architecture.md** - Centralized Commander design

---

**Author:** Claude Code
**Date:** 2026-02-10
**Status:** Ready for training
