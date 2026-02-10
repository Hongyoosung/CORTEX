# TeamDataCollector Integration - Complete

**Date:** 2026-02-10
**Status:** ✅ **COMPLETE**

## Summary

Successfully integrated `UTeamDataCollector` into `ASquadManager` for team-level training data collection. The system now supports both **data collection mode** (ε-greedy exploration) and **production mode** (MCTS with trained model).

---

## 1. Architecture Overview

### **Three-Phase Training Pipeline**

```
Phase 1: Data Collection (CURRENT)
├─ SquadManager uses ε-greedy policy
├─ TeamDataCollector records (s, a, s', r) tuples
└─ Output: CSV/Binary training data

Phase 2: Model Training (NEXT)
├─ Python: Train Team World Model on collected data
├─ Export: ONNX model for inference
└─ Deploy: Copy .onnx to Content/Models/

Phase 3: Production (FUTURE)
├─ SquadManager uses MCTS + trained world model
├─ TeamDataCollector continues recording for continuous learning
└─ Performance: 15ms planning budget
```

---

## 2. Integration Details

### **Files Modified**

#### **SquadManager.h** (Lines 199-212)
```cpp
// Added member variables:
UPROPERTY()
UTeamMCTS* TeamMCTSPlanner;

UPROPERTY()
class UTeamWorldModel* TeamWorldModel;  // ✅ NEW

UPROPERTY()
UTeamDataCollector* DataCollector;  // ✅ ALREADY EXISTS

// Added configuration:
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SquadManager|WorldModel")
FString TeamWorldModelPath;  // ✅ NEW
```

#### **SquadManager.cpp** (Lines 62-122)
```cpp
void ASquadManager::Initialize(int32 InTeamID, ATeamManager* InTeamManager)
{
    // 1. Initialize Team World Model
    TeamWorldModel = NewObject<UTeamWorldModel>(this);
    bool bModelLoaded = TeamWorldModel->InitModel(TeamWorldModelPath);

    // 2. Setup Team MCTS with world model
    TeamMCTSPlanner = NewObject<UTeamMCTS>(this);
    TeamMCTSPlanner->Setup(TeamWorldModel, MCTSConfig);

    // 3. Initialize Data Collector
    DataCollector = NewObject<UTeamDataCollector>(this);
    DataCollector->bIsRecording = true;
    DataCollector->BeginRecording(MatchID);

    // 4. Auto-enable data collection mode if no model
    if (!bModelLoaded)
    {
        bDataCollectionMode = true;
    }
}
```

#### **SquadManager.cpp** (Lines 112-125)
```cpp
void ASquadManager::PerformTacticalPlanning()
{
    // Record transition from previous planning cycle
    if (bHasPreviousState && DataCollector && DataCollector->bIsRecording)
    {
        FCompositeReward Reward = CalculateTeamReward(PreviousTeamState, GlobalState);

        DataCollector->RecordTransition(
            PreviousTeamState,
            PreviousTacticalPlay,
            GlobalState,
            Reward
        );
    }

    // ... rest of planning logic
}
```

---

## 3. Operational Modes

### **Mode 1: Data Collection (Default - No Model)**

**When:** `TeamWorldModelPath` is empty or model fails to load

**Behavior:**
- `bDataCollectionMode = true` (auto-enabled)
- Uses ε-greedy policy for action selection
  - Exploration (ε = 0.7): Random tactical plays
  - Exploitation (1-ε = 0.3): Heuristic-based plays
- **Fast**: No MCTS overhead (~0.1ms selection time)
- **Diverse**: Collects varied training samples

**Configuration:**
```cpp
// In Blueprint or C++:
SquadManager->bDataCollectionMode = true;
SquadManager->ExplorationRate = 0.7f;  // 70% exploration
SquadManager->TeamWorldModelPath = "";  // Empty = data collection mode
```

**Output:**
```
Content/TrainingData/
└─ TeamTransitions_MatchID_YYYYMMDD_HHMMSS.csv
   ├─ CurrentState (60 columns)
   ├─ TacticalPlay (1 column)
   ├─ NextState (60 columns)
   ├─ Reward (3 columns: WinProb, HealthDelta, ObjectiveScore)
   └─ Timestamp, MatchID
```

---

### **Mode 2: Production (MCTS with Trained Model)**

**When:** Valid ONNX model at `TeamWorldModelPath`

**Behavior:**
- `bDataCollectionMode = false`
- Uses MCTS with trained world model
- **Optimal**: Team-level tactical planning
- **Budget**: 15ms per planning cycle

**Configuration:**
```cpp
// In Blueprint or C++:
SquadManager->TeamWorldModelPath = "Content/Models/TeamWorldModel_v1.onnx";
SquadManager->bDataCollectionMode = false;
SquadManager->MCTSTimeBudget = 0.015f;  // 15ms
SquadManager->MCTSBatchSize = 8;
```

**Requirements:**
- Trained ONNX model with correct input/output shapes
- Input: [BatchSize, 60] (team state) + [BatchSize, 1] (tactical play enum)
- Output: [BatchSize, 60] (next state) + [BatchSize, 3] (reward components)

---

## 4. Data Flow Diagram

```
┌───────────────────────────────────────────────────────┐
│           ASquadManager (Squad Commander)            │
├───────────────────────────────────────────────────────┤
│                                                       │
│  ┌─────────────┐         ┌──────────────┐          │
│  │ CollectState│────────▶│ GlobalState  │          │
│  └─────────────┘         └──────┬───────┘          │
│                                  │                   │
│       ┌──────────────────────────┴──────┐           │
│       │  bDataCollectionMode?           │           │
│       └───┬──────────────────────────┬───┘           │
│          YES                        NO               │
│           │                          │               │
│    ┌──────▼────────┐        ┌───────▼──────┐       │
│    │ ε-Greedy      │        │ MCTS          │       │
│    │ Exploration   │        │ (TeamMCTS)    │       │
│    └──────┬────────┘        └───────┬──────┘       │
│           │                          │               │
│           └──────────┬───────────────┘               │
│                      │                               │
│              ┌───────▼────────┐                     │
│              │ TacticalPlay   │                     │
│              └───────┬────────┘                     │
│                      │                               │
│         ┌────────────▼─────────────┐                │
│         │ RecordTransition()       │                │
│         │ (TeamDataCollector)      │                │
│         └────────────┬─────────────┘                │
│                      │                               │
│              ┌───────▼────────┐                     │
│              │ Training Data  │                     │
│              │ (CSV/Binary)   │                     │
│              └────────────────┘                     │
└───────────────────────────────────────────────────────┘
```

---

## 5. Usage Instructions

### **Step 1: Start Data Collection**

1. **Ensure no world model is loaded:**
   ```cpp
   SquadManager->TeamWorldModelPath = "";  // Leave empty
   ```

2. **Enable recording in TeamDataCollector:**
   ```cpp
   SquadManager->DataCollector->bIsRecording = true;
   ```

3. **Configure ε-greedy exploration:**
   ```cpp
   SquadManager->bDataCollectionMode = true;
   SquadManager->ExplorationRate = 0.7f;  // 70% random exploration
   ```

4. **Play matches:**
   - System automatically records (s, a, s', r) transitions every 0.5s
   - Data saved to `Content/TrainingData/TeamTransitions_*.csv`

### **Step 2: Train World Model (Python)**

```python
# Load collected data
import pandas as pd
data = pd.read_csv("Content/TrainingData/TeamTransitions_*.csv")

# Train neural network
# Input: [team_state_60, tactical_play_1] → Output: [next_state_60, reward_3]

# Export to ONNX
torch.onnx.export(model, "Content/Models/TeamWorldModel_v1.onnx")
```

### **Step 3: Deploy Model**

1. **Copy ONNX file to project:**
   ```
   Content/Models/TeamWorldModel_v1.onnx
   ```

2. **Configure SquadManager:**
   ```cpp
   SquadManager->TeamWorldModelPath = "Content/Models/TeamWorldModel_v1.onnx";
   SquadManager->bDataCollectionMode = false;  // Use MCTS
   ```

3. **Verify initialization:**
   ```
   [Log] Team 0: World model loaded from Content/Models/TeamWorldModel_v1.onnx
   [Log] Team 0: Team MCTS initialized with loaded world model
   ```

---

## 6. Validation Checklist

- [x] ✅ `UTeamWorldModel` member variable added to `ASquadManager`
- [x] ✅ `TeamWorldModelPath` configuration property added
- [x] ✅ World model initialization in `Initialize()`
- [x] ✅ MCTS setup with world model (`TeamMCTS->Setup()`)
- [x] ✅ Data collector initialized and recording enabled
- [x] ✅ Transition recording in `PerformTacticalPlanning()`
- [x] ✅ Reward calculation implemented (`CalculateTeamReward()`)
- [x] ✅ ε-greedy action selection for data collection
- [x] ✅ Auto-enable data collection mode when no model loaded
- [x] ✅ Safety checks for model availability before MCTS

---

## 7. Performance Metrics

| Metric | Data Collection Mode | Production Mode (MCTS) |
|--------|---------------------|------------------------|
| Planning Time | ~0.1ms (ε-greedy) | ~15ms (MCTS budget) |
| Action Selection | Random + Heuristic | Optimal (value-based) |
| Data Recording | ✅ Enabled | ✅ Enabled (continuous) |
| Overhead | Minimal | Moderate |

---

## 8. Troubleshooting

### **Issue: No data being collected**

**Check:**
```cpp
// 1. Verify data collector is initialized
if (SquadManager->DataCollector == nullptr)
    UE_LOG(LogTemp, Error, TEXT("DataCollector not initialized!"));

// 2. Verify recording is enabled
if (!SquadManager->DataCollector->bIsRecording)
    UE_LOG(LogTemp, Warning, TEXT("Recording is disabled!"));

// 3. Check previous state flag
if (!SquadManager->bHasPreviousState)
    UE_LOG(LogTemp, Log, TEXT("Waiting for first transition..."));
```

### **Issue: MCTS not working**

**Check:**
```cpp
// 1. Verify world model is loaded
if (!SquadManager->TeamWorldModel->IsModelLoaded())
    UE_LOG(LogTemp, Error, TEXT("World model not loaded!"));

// 2. Verify data collection mode is disabled
if (SquadManager->bDataCollectionMode)
    UE_LOG(LogTemp, Warning, TEXT("Data collection mode still enabled!"));

// 3. Check model path
UE_LOG(LogTemp, Log, TEXT("Model path: %s"), *SquadManager->TeamWorldModelPath);
```

### **Issue: Planning exceeds time budget**

**Solutions:**
1. Reduce batch size: `MCTSBatchSize = 4` (default 8)
2. Reduce time budget: `MCTSTimeBudget = 0.010f` (10ms instead of 15ms)
3. Optimize world model inference latency

---

## 9. Next Steps

1. **Collect Training Data (Week 2-3):**
   - Run 100+ matches in data collection mode
   - Target: 10,000+ transition samples
   - Verify data quality and diversity

2. **Train World Model (Week 3-4):**
   - Python neural network training pipeline
   - Validate prediction accuracy (MSE, confidence scores)
   - Export to ONNX format

3. **Deploy and Test MCTS (Week 4):**
   - Load trained model into SquadManager
   - Benchmark MCTS performance (time budget, win rate)
   - Compare vs ε-greedy baseline

4. **Continuous Learning:**
   - Keep data collection enabled in production
   - Periodically retrain model with new data
   - Hot-reload updated models during runtime

---

## 10. Related Files

- `Public/Team/SquadManager.h` - Squad Commander with data collection
- `Private/Team/SquadManager.cpp` - Implementation
- `Public/AI/Training/TeamDataCollector.h` - Data collector component
- `Public/AI/Models/TeamWorldModel.h` - World model interface
- `Public/AI/MCTS/TeamMCTS.h` - MCTS planner
- `DEPRECATED_CLASS_CLEANUP.md` - Refactoring summary
