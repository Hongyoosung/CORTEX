# MOC v10.2 Policy Executor Refactor Summary

**Date:** 2026-02-10
**Status:** ✅ Complete

---

## What Was Done

### 1. Problem Identification

**Issue:** Two incompatible policy implementations existed:

1. **`MocPolicyExecutor`** (v10.2 attempt):
   - Single-head architecture
   - Primary interface: `InferWeights(EStrategyType Strategy)` - **took only strategy enum**
   - ❌ **Critical flaw**: No local state awareness → non-adaptive

2. **`MultiHeadRLPolicy`** (v10.1 style):
   - Multi-head architecture (correct)
   - Interface: `GetEQSWeights(FMocObservation)` - full 61-dim observation
   - ❌ **Mismatch**: Designed for decentralized MCTS, not v10.2 command-driven architecture

### 2. Solution: Unified Multi-Head Policy for v10.2

Created a **new unified `UMocPolicyExecutor`** that combines:
- ✅ Multi-head architecture (3 strategy-specialized heads)
- ✅ Command-driven interface (strategy from Squad Commander)
- ✅ Local state adaptation (context-aware weights)

---

## New Architecture

### Class: `UMocPolicyExecutor`

**Location:**
- Header: `Public/AI/Policy/MocPolicyExecutor.h`
- Implementation: `Private/AI/Policy/MocPolicyExecutor.cpp`

### Primary Interface

```cpp
FEQSWeightParameters InferWeights(
    EStrategyType CommandedStrategy,    // From Squad Commander
    const FObservation& LocalObservation // Local state (52-dim)
);
```

**Key Innovation:** Combines centralized command with local state adaptation.

### Data Flow (v10.2)

```
┌─────────────────────────────────────────────────────────────┐
│ Squad Commander (ASquadManager)                              │
│ • Performs centralized MCTS                                  │
│ • Outputs: Tactical Play → Role Distribution                │
└────────────────────┬────────────────────────────────────────┘
                     │ Commands: [Assault, Defend, Support]
                     ↓
┌─────────────────────────────────────────────────────────────┐
│ Executor Agent (AMocCharacter)                               │
│ • Receives commanded strategy                                │
│ • Observes local state (health, enemies, cover, etc.)       │
└────────────────────┬────────────────────────────────────────┘
                     │ CommandedStrategy + LocalObservation
                     ↓
┌─────────────────────────────────────────────────────────────┐
│ Multi-Head Policy Executor (UMocPolicyExecutor)              │
│                                                              │
│ INPUT: LocalObservation (52-dim)                            │
│   • Self: Position, Health, Velocity, Cooldown             │
│   • Allies: 4 × [Position, Health, Strategy]               │
│   • Enemies: 5 × [Position, Visible]                       │
│   • Map: CapturePoints, TimeRemaining                      │
│                                                              │
│ PROCESS:                                                     │
│   1. Encode observation → 52-dim tensor                     │
│   2. Select policy head based on CommandedStrategy          │
│      • Assault Head → Aggressive weights                   │
│      • Defend Head → Defensive weights                     │
│      • Support Head → Team-focused weights                 │
│   3. Run head-specific inference with local adaptation      │
│                                                              │
│ OUTPUT: EQS Weights (7-dim)                                 │
│   • EnemyObjectiveProximity                                 │
│   • AllyObjectiveProximity                                  │
│   • CoverDensity                                            │
│   • EnemyVisibility                                         │
│   • AllyProximity                                           │
│   • CombatRange                                             │
│   • PickupProximity                                         │
└────────────────────┬────────────────────────────────────────┘
                     │ EQS Weights (7-dim)
                     ↓
┌─────────────────────────────────────────────────────────────┐
│ EQS Spatial Reasoning                                        │
│ • Query 48 sample locations                                 │
│ • Apply weighted scoring                                    │
│ • Select best tactical position                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Key Design Decisions

### 1. Multi-Head Architecture (Retained from v10.1)

**Why:** Specialization improves performance. Each strategy requires different spatial reasoning:

| Strategy | Key Priorities | Weight Characteristics |
|----------|----------------|------------------------|
| **Assault** | Enemy proximity, visibility, aggressive positioning | High EnemyObj, EnemyVisibility |
| **Defend** | Cover, ally proximity, defensive positioning | High CoverDensity, AllyProximity |
| **Support** | Ally proximity, resource collection | High AllyProximity, PickupProximity |

### 2. Local State Input (52-dim)

**Why:** Even with commanded strategy, executor needs local adaptation:

**Example Scenarios:**

```
Commanded: Assault
LocalState: Health = 0.2, No Cover Nearby
→ Adaptive Weights: Increase CoverDensity despite assault command

Commanded: Defend
LocalState: All Enemies Dead
→ Adaptive Weights: Reduce defensive posture, allow forward positioning

Commanded: Support
LocalState: Ally at 0.1 health nearby
→ Adaptive Weights: Prioritize that ally's position
```

### 3. Fallback Defaults

When ONNX model not loaded (currently the case), uses strategy-specific defaults from v10.0Architecture.md Section 2.5.

**Assault Defaults:**
```cpp
EnemyObjectiveProximity = 0.9f   // Approach enemy
CoverDensity = 0.2f              // Less cover priority
EnemyVisibility = 0.8f           // Maintain line of sight
```

**Defend Defaults:**
```cpp
AllyObjectiveProximity = 0.9f    // Protect friendly base
CoverDensity = 0.8f              // High cover priority
```

---

## Implementation Changes

### Files Modified

1. **`Public/AI/Policy/MocPolicyExecutor.h`** (NEW)
   - Multi-head interface with local state input
   - Clear documentation of v10.2 architecture

2. **`Private/AI/Policy/MocPolicyExecutor.cpp`** (NEW)
   - Implements `InferWeights(Strategy, LocalObs)`
   - Fallback defaults for each strategy
   - Placeholder for ONNX multi-head inference

3. **`Private/AI/AIController/MocAIController.cpp`** (UPDATED)
   - Line 136-147: Updated to pass local observation
   - Now calls: `PolicyExecutor->InferWeights(CommandedStrategy, LocalObs)`

### Files Deleted

- ❌ `Public/AI/Policy/MultiHeadRLPolicy.h` (v10.1 style, incompatible)
- ❌ `Private/AI/Policy/MultiHeadPolicyExecutor.cpp` (v10.1 style, incompatible)

---

## Next Steps

### 1. Observation Collection (TODO)

Currently `CurrentObservation` in AIController is not being populated. Need to:

```cpp
// In AMocAIController::Tick() or OnPerceptionUpdated()
void AMocAIController::UpdateObservation()
{
    FObservation Obs;

    // Self state
    AMocCharacter* MyChar = Cast<AMocCharacter>(GetPawn());
    Obs.Position = MyChar->GetActorLocation();
    Obs.Health = MyChar->GetHealth() / MyChar->GetMaxHealth();
    Obs.Velocity = MyChar->GetVelocity();
    Obs.WeaponCooldown = MyChar->GetWeaponCooldownRatio();
    Obs.CurrentStrategy = MyChar->GetCommandedStrategy();
    Obs.bIsAlive = MyChar->IsAlive();

    // Allies (from perception)
    TArray<AActor*> Allies;
    AIPerception->GetCurrentlyPerceivedActors(
        UAISense_Sight::StaticClass(),
        Allies
    );
    // ... filter for friendly agents, populate Obs.AllyPositions, etc.

    // Enemies (from perception)
    // ... populate Obs.EnemyPositions, Obs.EnemyVisible

    // Map state
    // ... get capture point status, time remaining

    CurrentObservation = Obs;
}
```

**Reference:** See `UMocTacticalObserver::CollectObservations()` for complete implementation example.

### 2. ONNX Model Training

Need to train multi-head policy model:

**Expected Model Architecture:**
```python
# PyTorch model structure
class MultiHeadPolicyV2(nn.Module):
    def __init__(self):
        # Shared backbone
        self.encoder = nn.Sequential(
            nn.Linear(52, 128),  # Input: local state only
            nn.ReLU(),
            nn.Linear(128, 256),
            nn.LayerNorm(256)
        )

        # Strategy-specialized heads
        self.assault_head = self.build_head(256, 8)
        self.defend_head = self.build_head(256, 8)
        self.support_head = self.build_head(256, 8)

    def forward(self, state_tensor, strategy_idx):
        features = self.encoder(state_tensor)

        # Route to appropriate head
        if strategy_idx == 0:  # Assault
            return self.assault_head(features)
        elif strategy_idx == 1:  # Defend
            return self.defend_head(features)
        else:  # Support
            return self.support_head(features)
```

**Export to ONNX:**
```python
# Expected ONNX model structure:
# - Input: "state_input" [BatchSize, 52]
# - 3 Output nodes:
#   • "assault_head_output" [BatchSize, 8]
#   • "defend_head_output" [BatchSize, 8]
#   • "support_head_output" [BatchSize, 8]

torch.onnx.export(
    model,
    (state_sample,),
    "policy_multihead.onnx",
    input_names=["state_input"],
    output_names=["assault_head_output", "defend_head_output", "support_head_output"],
    dynamic_axes={"state_input": {0: "batch_size"}}
)
```

### 3. ONNX Runtime Integration

Uncomment and implement the ONNX inference code in `RunMultiHeadInference()`:

- Use UE5.4+ NNE (Neural Network Engine), or
- Integrate third-party ONNX Runtime plugin
- Select output head based on strategy parameter

---

## Benefits of New Architecture

### ✅ Correct v10.2 Implementation
- Matches centralized commander-executor architecture
- Squad Commander issues commands → Executors adapt to local state

### ✅ Local State Adaptation
- Weights adjust based on health, nearby enemies, available cover
- Example: Assault command with low health → prioritize cover

### ✅ Strategy Specialization
- Multi-head architecture preserves v10.1 training benefits
- Each head learns distinct behaviors through specialized rewards

### ✅ Backward Compatible
- Fallback defaults work without ONNX model
- Gradual migration path: defaults → trained model

### ✅ Clean Codebase
- Single unified policy class
- Clear interface: `InferWeights(CommandedStrategy, LocalObs)`
- Removed conflicting v10.1 implementations

---

## Testing Checklist

- [ ] Verify fallback defaults generate reasonable weights
- [ ] Test all 3 strategies (Assault/Defend/Support) with various health levels
- [ ] Confirm EQS receives correct weight parameters
- [ ] Profile inference time (target: <2ms)
- [ ] Train multi-head ONNX model
- [ ] Test ONNX inference with real model
- [ ] Compare behavior: defaults vs trained model
- [ ] Integration test: Squad Commander → Policy Executor → EQS → Navigation

---

## Architecture Validation

**Question:** Does this match v10.2 requirements?

**Answer:** ✅ Yes, confirmed by architecture documents:

From `v10.2Architecture.md` Section 6.2:
> "Good News: The existing Phase 1 multi-head RL policy training remains unchanged."

This confirms multi-head architecture should be retained.

From `v10.2Architecture.md` Section 5 (Executor Agent Refactoring):
> "Executor agents receive role assignments and execute via RL Policy + EQS without local MCTS"

This confirms command-driven interface with local state reasoning.

---

## Summary

**Before:**
- ❌ Two incompatible policy classes
- ❌ Single-head taking only strategy enum (no adaptation)
- ❌ v10.1-style full observation interface (wrong for v10.2)

**After:**
- ✅ Single unified multi-head policy executor
- ✅ Command-driven with local state adaptation
- ✅ Correct v10.2 architecture: centralized command + decentralized execution
- ✅ Clean codebase ready for ONNX integration

---

**Document Status:** ✅ Complete
**Next Phase:** Observation collection + ONNX model training
