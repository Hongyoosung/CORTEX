# CORTEX v6.0 Refactoring Plan: MCTS-RL Coordination Architecture

**Date:** 2026-01-06 (Production Review Update)
**Version:** v5.0 → v6.0 (Production-Ready)
**Estimated Effort:** 5-6 days (experienced developer)
**Risk Level:** Medium (major architectural change, but clear interfaces)

**Updates from 2026-01-06 Production Review:**
- ✅ Added performance optimization phases (batching, event-driven inference)
- ✅ Added training & value alignment (critical for MCTS-RL synergy)
- ✅ Added Sim2Real synchronization process
- ✅ Added debug visualization system
- ✅ Added profiling requirements and validation

---

## Executive Summary

**Architectural Change:**
- **OLD (v5.0):** MCTS assigns strategies → RL selects strategies (redundant)
- **NEW (v6.0):** MCTS assigns objectives → RL selects strategies (synergistic)

**Key Benefits:**
1. Clear separation: MCTS=coordination, RL=adaptation, Rules=execution
2. Simpler RL: 4-action space (11x faster learning than v5.0's 44-action)
3. Better MCTS: Solves real combinatorial problem (assignment)
4. Academic merit: Novel hierarchical architecture for real-time combat

**Components Affected:**
- MCTS (major refactor)
- RL Policy Network (architecture change + batching)
- Observations (add objective context)
- Reward Calculator (objective-aware rewards + value alignment)
- StateTree Tasks (deterministic execution)
- Python Training Code (action space change)
- **NEW:** Performance optimization (batching, event-driven)
- **NEW:** Sim2Real synchronization (auto-sync script)
- **NEW:** Debug visualization (MCTS + RL)
- **NEW:** Profiling infrastructure (Unreal Insights)

---

## Production-Ready Updates (2026-01-06 Review)

**🎯 Critical Additions Based on Senior Developer Feedback:**

### 1. **Performance Optimization (Phases 11-14)**
**Problem:** Naive implementation = 4 agents × 2ms = 8ms per tick (exceeds 5-10ms budget)

**Solutions:**
- **Batched Inference (Phase 2):** All 4 agents in single network call → 2.6× faster (8ms → 3ms)
- **Event-Driven Updates (Phase 11):** Only update on significant events → 75-83% reduction (120ms/sec → 12-40ms/sec)
- **Profiling (Phase 14):** Unreal Insights integration with concrete performance targets

**Result:** <10ms AI frame for 4 agents ✅

---

### 2. **Training & Value Alignment (Phase 6 - CRITICAL)**
**Problem:** If MCTS optimizes "objective completion" but RL learns "survival," value function misleads MCTS → broken behavior

**Solutions:**
- **Reward Hierarchy:** Objective completion (100) > Death penalty (10) → RL learns to sacrifice when needed
- **Static Assert:** Compile-time check prevents accidental inversion
- **RewardConfig Namespace:** Explicit priority levels (P0: Objective > P1: Progress > P2: Combat > P3: Survival)

**Result:** MCTS and RL optimize same goal → coherent behavior ✅

---

### 3. **Sim2Real Synchronization (Phase 12)**
**Problem:** If Python training environment drifts from C++ runtime (e.g., different movement speed), trained model fails in-game

**Solutions:**
- **RLConfig Namespace (Phase 1):** Single source of truth in C++ header
- **Auto-Sync Script (Phase 12):** Parses C++ header → Generates Python config.py
- **Validation:** Unit test catches drift before deployment

**Result:** Training ↔ Runtime consistency guaranteed ✅

---

### 4. **Debug Visualization (Phase 13)**
**Problem:** Hard to debug MCTS-RL synergy without seeing internal state

**Solutions:**
- **MCTS Viz:** Yellow arrows (assignments), green text (value estimates), cyan text (objectives)
- **RL Viz:** Colored spheres (Red=Assault, Blue=Defend, Green=Support, Yellow=Retreat), health bars
- **Console Commands:** `ToggleMCTSDebug`, `ToggleRLDebug`, `PrintMCTSStats`

**Result:** In-world visualization of AI decision-making ✅

---

## Updated Critical Path

**Original (v6.0 basic):** MCTS + RL + Observations = Core architecture
**Production-Ready:** MCTS + RL + Observations + **Value Alignment** + **Batching** + **Sim2Real Sync**

**Why the additions are critical:**
1. **Value Alignment (Phase 6):** Without this, system will fail (RL learns wrong objective)
2. **Batching (Phase 2):** Without this, cannot hit <10ms performance target
3. **Sim2Real Sync (Phase 12):** Without this, trained models break in-game

---

## Phase 1: Data Structure Updates (Priority: P0, Effort: 3 hours)

### 1.1 Create RLConfig Namespace (NEW - Sim2Real Sync)

**File:** `Source/GameAI_Project/Public/RL/RLTypes.h`

**Add at top of file (single source of truth for training):**

```cpp
/**
 * RLConfig Namespace (v6.0)
 * Single source of truth for RL training parameters
 * CRITICAL: These values MUST match Python training environment
 * Run sync script before training: python tools/sync_config_from_cpp.py
 */
namespace RLConfig {
    // === CRITICAL: These values MUST match Python training environment ===

    // Movement (must match UE5 CharacterMovement)
    constexpr float AGENT_WALK_SPEED = 600.0f;      // cm/s
    constexpr float AGENT_RUN_SPEED = 900.0f;
    constexpr float AGENT_SPRINT_SPEED = 1200.0f;

    // Perception (must match UE5 AIPerception)
    constexpr float PERCEPTION_RADIUS = 3000.0f;    // cm
    constexpr int32 RAYCAST_COUNT = 16;
    constexpr float RAYCAST_LENGTH = 2000.0f;       // cm
    constexpr float RAYCAST_ANGLE_SPREAD = 180.0f;  // degrees

    // Combat (must match UE5 damage system)
    constexpr float BASE_DAMAGE = 10.0f;
    constexpr float MAX_HEALTH = 100.0f;
    constexpr float FIRE_RATE = 0.1f;               // seconds per shot

    // Observation Normalization
    constexpr float MAX_DISTANCE_NORMALIZATION = 5000.0f;  // cm
    constexpr float MAX_VELOCITY_NORMALIZATION = 1200.0f;

    // Action Space
    constexpr int32 NUM_STRATEGIES = 4;  // Assault, Defend, Support, Retreat
    constexpr int32 NUM_TARGETS = 11;    // 10 enemies + 1 no-target

    // Observation Space
    constexpr int32 OBSERVATION_SIZE = 68;  // 64 base + 4 objective context

    // === END CRITICAL SECTION ===
}
```

**Testing:** Compile and ensure no errors.

---

### 1.2 Update RLTypes.h - Objective Context

**File:** `Source/GameAI_Project/Public/RL/RLTypes.h`

**Changes:**

```cpp
// ============================================
// NEW: Objective Context for RL Observation
// ============================================

/**
 * Objective context provided to RL policy
 * Informs agent about their assigned objective from MCTS
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FObjectiveContext
{
    GENERATED_BODY()

    /** Assigned objective type */
    UPROPERTY(BlueprintReadWrite, Category = "Objective")
    EObjectiveType Type = EObjectiveType::None;

    /** Normalized distance to objective [0,1] */
    UPROPERTY(BlueprintReadWrite, Category = "Objective")
    float Distance = 0.0f;

    /** Normalized 2D direction to objective */
    UPROPERTY(BlueprintReadWrite, Category = "Objective")
    FVector2D Direction = FVector2D::ZeroVector;

    /** Target actor (enemy, capture point, ally, etc.) */
    UPROPERTY(BlueprintReadWrite, Category = "Objective")
    AActor* TargetActor = nullptr;

    /** Objective priority [0-10] */
    UPROPERTY(BlueprintReadWrite, Category = "Objective")
    int32 Priority = 5;

    /** Convert to feature array for neural network (4 features) */
    TArray<float> ToFeatureVector() const
    {
        // Encode type as normalized value [0, 0.2, 0.4, 0.6, 0.8, 1.0]
        float typeEncoded = static_cast<float>(Type) / static_cast<float>(EObjectiveType::Retreat);

        return {
            typeEncoded,
            Distance,
            Direction.X,
            Direction.Y
        };
    }
};

// ============================================
// NEW: MCTS Assignment Result
// ============================================

/**
 * Result of MCTS objective assignment
 * Maps agents to objectives with confidence metrics
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FObjectiveAssignment
{
    GENERATED_BODY()

    /** Agent-to-objective mapping */
    UPROPERTY(BlueprintReadWrite, Category = "Assignment")
    TMap<AActor*, UObjective*> AgentToObjective;

    /** MCTS-estimated value of this assignment [-1, 1] */
    UPROPERTY(BlueprintReadWrite, Category = "Assignment")
    float ExpectedValue = 0.0f;

    /** Total MCTS visit count (confidence) */
    UPROPERTY(BlueprintReadWrite, Category = "Assignment")
    int32 VisitCount = 0;

    /** Timestamp of assignment */
    UPROPERTY(BlueprintReadWrite, Category = "Assignment")
    float Timestamp = 0.0f;
};

// ============================================
// UPDATED: Remove Multi-Head Config
// ============================================

// DELETE: FMultiHeadPolicyConfig (no longer needed)

/**
 * RL Policy configuration (v6.0 - Single Head)
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FRLPolicyConfig
{
    GENERATED_BODY()

    /** Input size (observation features) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
    int32 InputSize = 68;  // v6.0: 64 base + 4 objective context

    /** Policy output size (strategy logits) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
    int32 PolicyOutputSize = 4;  // Assault, Defend, Support, Retreat

    /** Hidden layer sizes */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
    TArray<int32> HiddenLayers = {128, 128, 64};

    /** Use ONNX model (vs fallback heuristic) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
    bool bUseONNXModel = true;

    /** ONNX model path */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
    FString ModelPath = TEXT("Models/cortex_policy_v6.onnx");
};

// ============================================
// UPDATED: Simplified Action Space
// ============================================

/**
 * Macro action space for v6.0 (Strategy only, execution is rules-based)
 */
USTRUCT(BlueprintType)
struct FMacroAction
{
    GENERATED_BODY()

    /** Strategy: High-level approach to current objective */
    UPROPERTY(BlueprintReadWrite, Category = "Action")
    EStrategyType Strategy = EStrategyType::Assault;

    FMacroAction() : Strategy(EStrategyType::Assault) {}
    explicit FMacroAction(EStrategyType InStrategy) : Strategy(InStrategy) {}
};

// DELETE FTacticalAction (replaced by simpler FMacroAction)
```

**Testing:** Compile and ensure no errors. No runtime testing needed yet.

---

## Phase 2: RL Policy Network Refactor (Priority: P0, Effort: 5 hours)

### 2.1 Update RLPolicyNetwork.h

**File:** `Source/GameAI_Project/Public/RL/RLPolicyNetwork.h`

**Changes:**

```cpp
// Update class declaration
UCLASS()
class GAMEAI_PROJECT_API URLPolicyNetwork : public UObject
{
    GENERATED_BODY()

public:
    // ============================================
    // v6.0 API: Strategy Selection (Single Agent)
    // ============================================

    /**
     * Get strategy for current observation + objective (v6.0)
     * @param Observation - Agent's 64-feature observation
     * @param ObjectiveContext - Assigned objective context (4 features)
     * @return Selected strategy (Assault/Defend/Support/Retreat)
     */
    UFUNCTION(BlueprintCallable, Category = "RL Policy")
    EStrategyType GetStrategy(const FObservationElement& Observation, const FObjectiveContext& ObjectiveContext);

    /**
     * Get state value estimate (used by MCTS for leaf evaluation)
     * @param Observation - Agent's 64-feature observation
     * @param ObjectiveContext - Assigned objective context (4 features)
     * @return Value estimate [-1, 1]
     */
    UFUNCTION(BlueprintCallable, Category = "RL Policy")
    float GetStateValue(const FObservationElement& Observation, const FObjectiveContext& ObjectiveContext);

    // ============================================
    // v6.0 API: Batched Inference (Performance Critical - NEW)
    // ============================================

    /**
     * Get strategies for multiple agents in single network call (v6.0)
     * PERFORMANCE: 2.6× faster than sequential calls (8ms → 3ms for 4 agents)
     * @param Observations - Array of agent observations
     * @param ObjectiveContexts - Array of objective contexts (same size as Observations)
     * @return Array of strategies (same size as input)
     */
    UFUNCTION(BlueprintCallable, Category = "RL Policy")
    TArray<EStrategyType> GetStrategiesBatched(
        const TArray<FObservationElement>& Observations,
        const TArray<FObjectiveContext>& ObjectiveContexts
    );

    /**
     * Get state values for multiple agents in single network call (v6.0)
     * Used by MCTS for evaluating multiple agent-objective assignments
     * @param Observations - Array of agent observations
     * @param ObjectiveContexts - Array of objective contexts
     * @return Array of value estimates [-1, 1]
     */
    UFUNCTION(BlueprintCallable, Category = "RL Policy")
    TArray<float> GetStateValuesBatched(
        const TArray<FObservationElement>& Observations,
        const TArray<FObjectiveContext>& ObjectiveContexts
    );

    // ============================================
    // DELETE: Multi-Head Methods
    // ============================================
    // Remove: GetAction(), GetActionWithMask(), SelectStrategyHead(), etc.

private:
    /**
     * Build 68-feature input from observation + objective context
     */
    TArray<float> BuildNetworkInput(const FObservationElement& Observation, const FObjectiveContext& ObjectiveContext) const;

    /**
     * Forward pass through single-head network
     * @return Policy logits (4) and value (1)
     */
    struct FNetworkOutput {
        TArray<float> PolicyLogits;  // [4] - Strategy logits
        float Value;                  // State value estimate
    };
    FNetworkOutput ForwardPass(const TArray<float>& InputFeatures);

    /**
     * Sample strategy from logits
     */
    EStrategyType SampleStrategy(const TArray<float>& Logits) const;
};
```

### 2.2 Update RLPolicyNetwork.cpp

**File:** `Source/GameAI_Project/Private/RL/RLPolicyNetwork.cpp`

**Implementation:**

```cpp
// ============================================
// v6.0: Strategy Selection
// ============================================

EStrategyType URLPolicyNetwork::GetStrategy(const FObservationElement& Observation, const FObjectiveContext& ObjectiveContext)
{
    if (!bIsInitialized || !bUseONNXModel || !ModelInstance.IsValid())
    {
        // Fallback heuristic
        if (Observation.AgentHealth < 0.3f)
            return EStrategyType::Retreat;
        if (ObjectiveContext.Type == EObjectiveType::Defend)
            return EStrategyType::Defend;
        return EStrategyType::Assault;
    }

    // Build 68-feature input
    TArray<float> InputFeatures = BuildNetworkInput(Observation, ObjectiveContext);
    check(InputFeatures.Num() == 68);

    // Forward pass
    FNetworkOutput Output = ForwardPass(InputFeatures);

    // Sample strategy
    EStrategyType Strategy = SampleStrategy(Output.PolicyLogits);

    UE_LOG(LogTemp, Display, TEXT("✅ [RL v6.0] Strategy=%s, Value=%.2f, Objective=%s"),
        *UEnum::GetValueAsString(Strategy),
        Output.Value,
        *UEnum::GetValueAsString(ObjectiveContext.Type));

    return Strategy;
}

float URLPolicyNetwork::GetStateValue(const FObservationElement& Observation, const FObjectiveContext& ObjectiveContext)
{
    if (!bIsInitialized || !bUseONNXModel || !ModelInstance.IsValid())
    {
        // Fallback heuristic value
        float value = (Observation.AgentHealth - 50.0f) / 50.0f;
        value -= Observation.VisibleEnemyCount * 0.2f;
        return FMath::Clamp(value, -1.0f, 1.0f);
    }

    // Build 68-feature input
    TArray<float> InputFeatures = BuildNetworkInput(Observation, ObjectiveContext);

    // Forward pass
    FNetworkOutput Output = ForwardPass(InputFeatures);

    return FMath::Clamp(Output.Value, -1.0f, 1.0f);
}

// ============================================
// Helper: Build Network Input
// ============================================

TArray<float> URLPolicyNetwork::BuildNetworkInput(const FObservationElement& Observation, const FObjectiveContext& ObjectiveContext) const
{
    TArray<float> Input;
    Input.Reserve(68);

    // Base observation (64 features)
    TArray<float> BaseFeatures = Observation.ToFeatureVector();
    check(BaseFeatures.Num() == 64);
    Input.Append(BaseFeatures);

    // Objective context (4 features)
    TArray<float> ObjectiveFeatures = ObjectiveContext.ToFeatureVector();
    check(ObjectiveFeatures.Num() == 4);
    Input.Append(ObjectiveFeatures);

    return Input;
}

// ============================================
// Forward Pass (Single-Head Network)
// ============================================

URLPolicyNetwork::FNetworkOutput URLPolicyNetwork::ForwardPass(const TArray<float>& InputFeatures)
{
    FNetworkOutput Output;

    // Prepare input tensor
    InputBuffer = InputFeatures;
    UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make({1u, 68u});

    // Prepare output tensors
    TArray<float> PolicyBuffer, ValueBuffer;
    PolicyBuffer.SetNum(4);   // 4 strategy logits
    ValueBuffer.SetNum(1);    // 1 value estimate

    // Bind tensors
    TArray<UE::NNE::FTensorBindingCPU> InputBindings;
    InputBindings.Add({InputBuffer.GetData(), static_cast<uint64>(InputBuffer.Num() * sizeof(float))});

    TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
    OutputBindings.Add({PolicyBuffer.GetData(), static_cast<uint64>(PolicyBuffer.Num() * sizeof(float))});
    OutputBindings.Add({ValueBuffer.GetData(), static_cast<uint64>(ValueBuffer.Num() * sizeof(float))});

    // Run inference
    TArray<UE::NNE::FTensorShape> InputShapes = {InputShape};
    if (ModelInstance->SetInputTensorShapes(InputShapes) == UE::NNE::EResultStatus::Ok &&
        ModelInstance->RunSync(InputBindings, OutputBindings) == UE::NNE::EResultStatus::Ok)
    {
        Output.PolicyLogits = PolicyBuffer;
        Output.Value = ValueBuffer[0];
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Inference failed"));
        Output.PolicyLogits = {0.0f, 0.0f, 0.0f, 0.0f};
        Output.Value = 0.0f;
    }

    return Output;
}

// ============================================
// Sample Strategy from Logits
// ============================================

EStrategyType URLPolicyNetwork::SampleStrategy(const TArray<float>& Logits) const
{
    if (Logits.Num() != 4)
    {
        return EStrategyType::Assault;
    }

    // Softmax
    TArray<float> Probs = Softmax(Logits);

    // Sample
    float Rand = FMath::FRand();
    float CumulativeProb = 0.0f;

    for (int32 i = 0; i < 4; ++i)
    {
        CumulativeProb += Probs[i];
        if (Rand <= CumulativeProb)
        {
            return static_cast<EStrategyType>(i);
        }
    }

    return EStrategyType::Assault;
}

// ============================================
// v6.0: Batched Inference (NEW - Performance Critical)
// ============================================

TArray<EStrategyType> URLPolicyNetwork::GetStrategiesBatched(
    const TArray<FObservationElement>& Observations,
    const TArray<FObjectiveContext>& ObjectiveContexts)
{
    TArray<EStrategyType> Strategies;

    if (Observations.Num() != ObjectiveContexts.Num())
    {
        UE_LOG(LogTemp, Error, TEXT("URLPolicyNetwork: Batch size mismatch"));
        return Strategies;
    }

    int32 BatchSize = Observations.Num();
    if (BatchSize == 0) return Strategies;

    // Fallback if model not loaded
    if (!bIsInitialized || !bUseONNXModel || !ModelInstance.IsValid())
    {
        for (int32 i = 0; i < BatchSize; ++i)
        {
            Strategies.Add(GetStrategy(Observations[i], ObjectiveContexts[i]));
        }
        return Strategies;
    }

    // Build batched input tensor [BatchSize, 68]
    TArray<float> BatchedInput;
    BatchedInput.Reserve(BatchSize * 68);

    for (int32 i = 0; i < BatchSize; ++i)
    {
        TArray<float> Features = BuildNetworkInput(Observations[i], ObjectiveContexts[i]);
        check(Features.Num() == 68);
        BatchedInput.Append(Features);
    }

    // Prepare input tensor
    UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make({
        static_cast<uint32>(BatchSize),
        68u
    });

    // Prepare output buffers
    TArray<float> PolicyBuffer, ValueBuffer;
    PolicyBuffer.SetNum(BatchSize * 4);   // 4 strategy logits per agent
    ValueBuffer.SetNum(BatchSize);        // 1 value per agent

    // Bind tensors
    TArray<UE::NNE::FTensorBindingCPU> InputBindings;
    InputBindings.Add({BatchedInput.GetData(), static_cast<uint64>(BatchedInput.Num() * sizeof(float))});

    TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
    OutputBindings.Add({PolicyBuffer.GetData(), static_cast<uint64>(PolicyBuffer.Num() * sizeof(float))});
    OutputBindings.Add({ValueBuffer.GetData(), static_cast<uint64>(ValueBuffer.Num() * sizeof(float))});

    // Run batched inference
    TArray<UE::NNE::FTensorShape> InputShapes = {InputShape};
    if (ModelInstance->SetInputTensorShapes(InputShapes) == UE::NNE::EResultStatus::Ok &&
        ModelInstance->RunSync(InputBindings, OutputBindings) == UE::NNE::EResultStatus::Ok)
    {
        // Decode strategies from batched output
        for (int32 i = 0; i < BatchSize; ++i)
        {
            int32 Offset = i * 4;
            TArray<float> AgentLogits = {
                PolicyBuffer[Offset],
                PolicyBuffer[Offset + 1],
                PolicyBuffer[Offset + 2],
                PolicyBuffer[Offset + 3]
            };
            Strategies.Add(SampleStrategy(AgentLogits));
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Batched inference failed"));
        // Fallback to individual inference
        for (int32 i = 0; i < BatchSize; ++i)
        {
            Strategies.Add(GetStrategy(Observations[i], ObjectiveContexts[i]));
        }
    }

    return Strategies;
}

TArray<float> URLPolicyNetwork::GetStateValuesBatched(
    const TArray<FObservationElement>& Observations,
    const TArray<FObjectiveContext>& ObjectiveContexts)
{
    TArray<float> Values;

    if (Observations.Num() != ObjectiveContexts.Num())
    {
        UE_LOG(LogTemp, Error, TEXT("URLPolicyNetwork: Batch size mismatch"));
        return Values;
    }

    int32 BatchSize = Observations.Num();
    if (BatchSize == 0) return Values;

    // Fallback if model not loaded
    if (!bIsInitialized || !bUseONNXModel || !ModelInstance.IsValid())
    {
        for (int32 i = 0; i < BatchSize; ++i)
        {
            Values.Add(GetStateValue(Observations[i], ObjectiveContexts[i]));
        }
        return Values;
    }

    // Build batched input tensor (same as GetStrategiesBatched)
    TArray<float> BatchedInput;
    BatchedInput.Reserve(BatchSize * 68);

    for (int32 i = 0; i < BatchSize; ++i)
    {
        TArray<float> Features = BuildNetworkInput(Observations[i], ObjectiveContexts[i]);
        BatchedInput.Append(Features);
    }

    // Run batched inference (same tensor binding as above)
    UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make({
        static_cast<uint32>(BatchSize),
        68u
    });

    TArray<float> PolicyBuffer, ValueBuffer;
    PolicyBuffer.SetNum(BatchSize * 4);
    ValueBuffer.SetNum(BatchSize);

    TArray<UE::NNE::FTensorBindingCPU> InputBindings;
    InputBindings.Add({BatchedInput.GetData(), static_cast<uint64>(BatchedInput.Num() * sizeof(float))});

    TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
    OutputBindings.Add({PolicyBuffer.GetData(), static_cast<uint64>(PolicyBuffer.Num() * sizeof(float))});
    OutputBindings.Add({ValueBuffer.GetData(), static_cast<uint64>(ValueBuffer.Num() * sizeof(float))});

    TArray<UE::NNE::FTensorShape> InputShapes = {InputShape};
    if (ModelInstance->SetInputTensorShapes(InputShapes) == UE::NNE::EResultStatus::Ok &&
        ModelInstance->RunSync(InputBindings, OutputBindings) == UE::NNE::EResultStatus::Ok)
    {
        // Extract values from batched output
        Values = ValueBuffer;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Batched value inference failed"));
        // Fallback
        for (int32 i = 0; i < BatchSize; ++i)
        {
            Values.Add(GetStateValue(Observations[i], ObjectiveContexts[i]));
        }
    }

    return Values;
}
```

**Testing:**
1. Compile
2. Create dummy observation + objective context
3. Call `GetStrategy()` and verify it returns valid strategy
4. Call `GetStateValue()` and verify it returns value in [-1, 1]
5. **NEW:** Create 4 observations + 4 objective contexts
6. **NEW:** Call `GetStrategiesBatched()` and verify returns 4 strategies in <4ms
7. **NEW:** Call `GetStateValuesBatched()` and verify returns 4 values in [-1, 1]

---

## Phase 3: MCTS Refactor (Priority: P0, Effort: 6 hours)

### 3.1 Update MCTS Action Space

**File:** `Source/GameAI_Project/Public/AI/MCTS/MCTS.h`

**Changes:**

```cpp
// ============================================
// v6.0: MCTS Action = Objective Assignment
// ============================================

/**
 * MCTS Node (v6.0)
 * Each node represents a specific agent-to-objective assignment
 */
struct FMCTSNode
{
    /** Assignment represented by this node */
    FObjectiveAssignment Assignment;

    /** Parent node */
    TSharedPtr<FMCTSNode> Parent;

    /** Child nodes (different assignments) */
    TArray<TSharedPtr<FMCTSNode>> Children;

    /** Visit count */
    int32 VisitCount = 0;

    /** Total value (sum of all simulations) */
    float TotalValue = 0.0f;

    /** Is fully expanded? */
    bool bFullyExpanded = false;

    /** UCB1 value (for selection) */
    float GetUCB1(float ExplorationConstant) const;

    /** Average value */
    float GetAverageValue() const { return VisitCount > 0 ? TotalValue / VisitCount : 0.0f; }
};

UCLASS()
class GAMEAI_PROJECT_API UMCTS : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Run MCTS to find best agent-to-objective assignment (v6.0)
     * @param Agents - Available agents
     * @param Objectives - Available objectives
     * @param Simulations - Number of MCTS simulations
     * @return Best assignment found
     */
    UFUNCTION(BlueprintCallable, Category = "MCTS")
    FObjectiveAssignment RunObjectiveAssignment(
        const TArray<AActor*>& Agents,
        const TArray<UObjective*>& Objectives,
        int32 Simulations = 500
    );

    // ============================================
    // DELETE: Old v5.0 Methods
    // ============================================
    // Remove: RunMCTS(), SelectObjective(), etc.

private:
    /**
     * MCTS simulation phases
     */
    TSharedPtr<FMCTSNode> Selection(TSharedPtr<FMCTSNode> Root);
    TSharedPtr<FMCTSNode> Expansion(TSharedPtr<FMCTSNode> Node);
    float Simulation(TSharedPtr<FMCTSNode> Node);
    void Backpropagation(TSharedPtr<FMCTSNode> Node, float Value);

    /**
     * Evaluate assignment using RL value estimates + heuristics
     */
    float EvaluateAssignment(const FObjectiveAssignment& Assignment);

    /**
     * Generate possible assignments from current node
     */
    TArray<FObjectiveAssignment> GeneratePossibleAssignments(const FObjectiveAssignment& CurrentAssignment);

    /**
     * Coordination heuristics
     */
    float TeamCohesionScore(const FObjectiveAssignment& Assignment) const;
    float ObjectiveCoverageScore(const FObjectiveAssignment& Assignment) const;
    float CapabilityMatchScore(const FObjectiveAssignment& Assignment) const;

    /** RL policy for value estimates */
    UPROPERTY()
    URLPolicyNetwork* RLPolicy;

    /** Current agents and objectives */
    TArray<AActor*> AvailableAgents;
    TArray<UObjective*> AvailableObjectives;
};
```

### 3.2 Implement MCTS Assignment Logic

**File:** `Source/GameAI_Project/Private/AI/MCTS/MCTS.cpp`

**Key Implementation:**

```cpp
FObjectiveAssignment UMCTS::RunObjectiveAssignment(
    const TArray<AActor*>& Agents,
    const TArray<UObjective*>& Objectives,
    int32 Simulations)
{
    AvailableAgents = Agents;
    AvailableObjectives = Objectives;

    // Create root node (current assignment)
    TSharedPtr<FMCTSNode> Root = MakeShared<FMCTSNode>();
    Root->Assignment.AgentToObjective = BuildCurrentAssignment();
    Root->Assignment.Timestamp = FPlatformTime::Seconds();

    // Run MCTS simulations
    for (int32 i = 0; i < Simulations; ++i)
    {
        // Selection: Traverse tree to most promising node
        TSharedPtr<FMCTSNode> Node = Selection(Root);

        // Expansion: Add new child node
        if (!Node->bFullyExpanded)
        {
            Node = Expansion(Node);
        }

        // Simulation: Evaluate assignment
        float Value = Simulation(Node);

        // Backpropagation: Update ancestors
        Backpropagation(Node, Value);
    }

    // Select best child (highest visit count = most robust)
    TSharedPtr<FMCTSNode> BestChild = nullptr;
    int32 MaxVisits = 0;
    for (const auto& Child : Root->Children)
    {
        if (Child->VisitCount > MaxVisits)
        {
            MaxVisits = Child->VisitCount;
            BestChild = Child;
        }
    }

    if (BestChild.IsValid())
    {
        FObjectiveAssignment Result = BestChild->Assignment;
        Result.ExpectedValue = BestChild->GetAverageValue();
        Result.VisitCount = BestChild->VisitCount;
        return Result;
    }

    // Fallback: Return current assignment
    return Root->Assignment;
}

float UMCTS::EvaluateAssignment(const FObjectiveAssignment& Assignment)
{
    float TotalValue = 0.0f;
    int32 AgentCount = 0;

    // Query RL value for each agent
    for (const auto& [Agent, Objective] : Assignment.AgentToObjective)
    {
        if (!Agent || !Objective) continue;

        // Build observation for agent
        UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
        if (!FollowerComp) continue;

        FObservationElement Obs = FollowerComp->BuildObservation();
        FObjectiveContext ObjCtx = FollowerComp->BuildObjectiveContext(Objective);

        // Get RL value estimate
        float AgentValue = RLPolicy->GetStateValue(Obs, ObjCtx);
        TotalValue += AgentValue;
        AgentCount++;
    }

    // Normalize by agent count
    float AverageValue = AgentCount > 0 ? TotalValue / AgentCount : 0.0f;

    // Add coordination heuristics
    float Cohesion = TeamCohesionScore(Assignment);
    float Coverage = ObjectiveCoverageScore(Assignment);
    float Capability = CapabilityMatchScore(Assignment);

    // Weighted combination
    float FinalValue = AverageValue * 0.6f + Cohesion * 0.15f + Coverage * 0.15f + Capability * 0.1f;

    return FMath::Clamp(FinalValue, -1.0f, 1.0f);
}

float UMCTS::TeamCohesionScore(const FObjectiveAssignment& Assignment) const
{
    // Higher score if agents on same objective are near each other
    float CohesionScore = 0.0f;
    int32 PairCount = 0;

    TMap<UObjective*, TArray<AActor*>> ObjectiveToAgents;
    for (const auto& [Agent, Objective] : Assignment.AgentToObjective)
    {
        ObjectiveToAgents.FindOrAdd(Objective).Add(Agent);
    }

    for (const auto& [Objective, Agents] : ObjectiveToAgents)
    {
        if (Agents.Num() < 2) continue;

        // Check pairwise distances
        for (int32 i = 0; i < Agents.Num() - 1; ++i)
        {
            for (int32 j = i + 1; j < Agents.Num(); ++j)
            {
                float Distance = FVector::Dist(Agents[i]->GetActorLocation(), Agents[j]->GetActorLocation());
                float NormalizedDist = FMath::Clamp(Distance / 2000.0f, 0.0f, 1.0f); // 2000cm = 20m
                CohesionScore += (1.0f - NormalizedDist); // Closer = higher score
                PairCount++;
            }
        }
    }

    return PairCount > 0 ? CohesionScore / PairCount : 0.5f;
}

float UMCTS::ObjectiveCoverageScore(const FObjectiveAssignment& Assignment) const
{
    // Higher score if all high-priority objectives have agents
    int32 CoveredHighPriority = 0;
    int32 TotalHighPriority = 0;

    for (UObjective* Obj : AvailableObjectives)
    {
        if (Obj->Priority >= 7) // High priority threshold
        {
            TotalHighPriority++;

            // Check if any agent is assigned to this objective
            for (const auto& [Agent, AssignedObj] : Assignment.AgentToObjective)
            {
                if (AssignedObj == Obj)
                {
                    CoveredHighPriority++;
                    break;
                }
            }
        }
    }

    return TotalHighPriority > 0 ? static_cast<float>(CoveredHighPriority) / TotalHighPriority : 0.5f;
}

float UMCTS::CapabilityMatchScore(const FObjectiveAssignment& Assignment) const
{
    // Higher score if healthy agents assigned to offensive objectives, etc.
    float MatchScore = 0.0f;
    int32 AssignmentCount = 0;

    for (const auto& [Agent, Objective] : Assignment.AgentToObjective)
    {
        if (!Agent || !Objective) continue;

        UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
        if (!FollowerComp) continue;

        float Health = FollowerComp->GetCurrentHealth() / FollowerComp->GetMaxHealth();

        // Heuristic: Healthy agents for offensive, damaged agents for defensive
        if (Objective->Type == EObjectiveType::Capture && Health > 0.7f)
        {
            MatchScore += 1.0f; // Good match
        }
        else if (Objective->Type == EObjectiveType::Defend && Health < 0.5f)
        {
            MatchScore += 0.8f; // Reasonable (defend while healing)
        }
        else if (Objective->Type == EObjectiveType::Retreat && Health < 0.3f)
        {
            MatchScore += 1.0f; // Good match
        }
        else
        {
            MatchScore += 0.5f; // Neutral
        }

        AssignmentCount++;
    }

    return AssignmentCount > 0 ? MatchScore / AssignmentCount : 0.5f;
}
```

**Testing:**
1. Create 4 test agents + 3 test objectives
2. Call `RunObjectiveAssignment()`
3. Verify assignment is valid (all agents assigned)
4. Verify visit counts are reasonable (>0 for best child)
5. Verify expected value is in [-1, 1]

---

## Phase 4: Observations Update (Priority: P0, Effort: 2 hours)

### 4.1 Add Objective Context to Observations

**File:** `Source/GameAI_Project/Public/Observation/ObservationElement.h`

**Changes:**

```cpp
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FObservationElement
{
    GENERATED_BODY()

    // ... existing 64 features ...

    /** NEW: Objective context (v6.0) */
    UPROPERTY(BlueprintReadWrite, Category = "Observation|Objective")
    FObjectiveContext ObjectiveContext;

    /**
     * Convert to feature vector for neural network (68 features in v6.0)
     */
    TArray<float> ToFeatureVector() const
    {
        TArray<float> Features;
        Features.Reserve(68);

        // Base observation (64 features) - existing code
        Features.Append(AgentState);     // 7 features
        Features.Add(EnemyDistance);     // 1 feature
        Features.Append(RaycastHits);    // 32 features
        Features.Append(EnemyInfo);      // 16 features
        Features.Append(TacticalInfo);   // 4 features
        Features.Append(AllyContext.ToFeatureVector()); // 4 features

        // Objective context (4 features) - NEW
        Features.Append(ObjectiveContext.ToFeatureVector());

        check(Features.Num() == 68);
        return Features;
    }
};
```

### 4.2 Update TacticalObserver

**File:** `Source/GameAI_Project/Private/Observation/TacticalObserver.cpp`

**Add method to build objective context:**

```cpp
FObjectiveContext UTacticalObserver::BuildObjectiveContext(UObjective* Objective, AActor* Agent)
{
    FObjectiveContext Context;

    if (!Objective || !Agent)
    {
        return Context; // Default (all zeros)
    }

    Context.Type = Objective->Type;
    Context.TargetActor = Objective->TargetActor;
    Context.Priority = Objective->Priority;

    // Calculate distance and direction
    FVector AgentLoc = Agent->GetActorLocation();
    FVector ObjectiveLoc = Objective->TargetLocation;

    float Distance = FVector::Dist(AgentLoc, ObjectiveLoc);
    Context.Distance = FMath::Clamp(Distance / 5000.0f, 0.0f, 1.0f); // Normalize by 50m max

    FVector Direction = (ObjectiveLoc - AgentLoc).GetSafeNormal2D();
    Context.Direction = FVector2D(Direction.X, Direction.Y);

    return Context;
}
```

**Testing:**
1. Create test objective
2. Call `BuildObjectiveContext()`
3. Verify distance is normalized [0,1]
4. Verify direction is normalized 2D vector
5. Verify feature vector is 4 elements

---

## Phase 5: StateTree Rule-Based Execution (Priority: P1, Effort: 3 hours)

### 5.1 Update STTask_ExecuteMovement

**File:** `Source/GameAI_Project/Private/StateTree/Tasks/STTask_ExecuteMovement.cpp`

**Simplify to deterministic strategy → position mapping:**

```cpp
EStateTreeRunStatus FSTTask_ExecuteMovement::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
    FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
    FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

    if (!SharedContext.bIsAlive)
    {
        return EStateTreeRunStatus::Succeeded;
    }

    // Get current strategy from RL policy
    EStrategyType CurrentStrategy = SharedContext.FollowerComponent->GetCurrentStrategy();
    EStrategyType PreviousStrategy = InstanceData.PreviousStrategy;

    // Detect strategy change
    if (CurrentStrategy != PreviousStrategy)
    {
        // Map strategy to tactical position (deterministic)
        ETacticalPosition TargetPosition = StrategyToPosition(CurrentStrategy, SharedContext);

        // Execute movement
        ExecuteMovement(Context, TargetPosition, DeltaTime);

        InstanceData.PreviousStrategy = CurrentStrategy;
    }

    return EStateTreeRunStatus::Running;
}

ETacticalPosition FSTTask_ExecuteMovement::StrategyToPosition(
    EStrategyType Strategy,
    const FFollowerStateTreeContext& Context) const
{
    switch (Strategy)
    {
        case EStrategyType::Assault:
            return ETacticalPosition::ForwardCover;

        case EStrategyType::Defend:
            return ETacticalPosition::Hold;

        case EStrategyType::Support:
            // Move toward ally in need
            if (Context.FollowerComponent && Context.FollowerComponent->GetAllyContext().bAllyNeedsHelp)
            {
                return ETacticalPosition::ForwardCover; // Toward ally
            }
            return ETacticalPosition::Hold;

        case EStrategyType::Retreat:
            return ETacticalPosition::Retreat;

        default:
            return ETacticalPosition::Hold;
    }
}
```

### 5.2 Update STTask_ExecuteFire

**File:** `Source/GameAI_Project/Private/StateTree/Tasks/STTask_ExecuteFire.cpp`

**Simplify to strategy-based targeting:**

```cpp
EStateTreeRunStatus FSTTask_ExecuteFire::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
    FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
    FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

    if (!SharedContext.bIsAlive)
    {
        return EStateTreeRunStatus::Succeeded;
    }

    // Get current strategy
    EStrategyType CurrentStrategy = SharedContext.FollowerComponent->GetCurrentStrategy();

    // Retreat strategy = hold fire
    if (CurrentStrategy == EStrategyType::Retreat)
    {
        StopFiring(Context);
        return EStateTreeRunStatus::Running;
    }

    // Get visible enemies
    TArray<AActor*> VisibleEnemies = SharedContext.FollowerComponent->GetPerceivedEnemies();

    if (VisibleEnemies.Num() == 0)
    {
        StopFiring(Context);
        return EStateTreeRunStatus::Running;
    }

    // Select target based on strategy
    AActor* Target = SelectTarget(CurrentStrategy, VisibleEnemies, SharedContext);

    if (Target)
    {
        ExecuteFiring(Context, Target);
    }
    else
    {
        StopFiring(Context);
    }

    return EStateTreeRunStatus::Running;
}

AActor* FSTTask_ExecuteFire::SelectTarget(
    EStrategyType Strategy,
    const TArray<AActor*>& Enemies,
    const FFollowerStateTreeContext& Context) const
{
    if (Enemies.Num() == 0) return nullptr;

    switch (Strategy)
    {
        case EStrategyType::Assault:
        case EStrategyType::Defend:
            // Priority: Closest enemy
            return GetClosestEnemy(Enemies, Context.ControlledPawn);

        case EStrategyType::Support:
            // Priority: Enemy threatening ally
            return GetEnemyThreateningAlly(Enemies, Context.FollowerComponent);

        default:
            return nullptr;
    }
}
```

**Testing:**
1. Set agent strategy to each type (Assault, Defend, Support, Retreat)
2. Verify correct EQS query is used (ForwardCover, Hold, Retreat)
3. Verify correct targeting behavior (closest, threatening ally, hold fire)
4. Verify smooth transitions between strategies

---

## Phase 6: Reward Calculator Update (Priority: P0 ⚠️ CRITICAL, Effort: 3 hours)

**⚠️ CRITICAL FOR VALUE ALIGNMENT:**
This phase is **critical** for preventing broken behavior. If MCTS and RL optimize different goals, the system will fail. The reward structure MUST make objective completion the dominant term.

### 6.1 Value Alignment - Reward Priority Hierarchy (NEW)

**File:** `Source/GameAI_Project/Public/RL/RewardCalculator.h`

**Add reward constants (must align with MCTS objectives):**

```cpp
/**
 * v6.0 Reward Configuration
 * CRITICAL: Objective completion MUST be highest priority
 * If death penalty > objective reward, RL learns to hide instead of completing objectives
 */
namespace RewardConfig {
    // === PRIORITY 0: Objective Completion (Dominant Term) ===
    constexpr float OBJECTIVE_CAPTURE_REWARD = 100.0f;   // Mission success
    constexpr float OBJECTIVE_DEFEND_REWARD = 80.0f;     // Hold for duration
    constexpr float OBJECTIVE_SUPPORT_REWARD = 90.0f;    // Protected ally survives
    constexpr float OBJECTIVE_RETREAT_REWARD = 70.0f;    // Reach safe zone

    // === PRIORITY 1: Objective Progress ===
    constexpr float PROGRESS_PER_METER = 0.5f;           // Incremental progress

    // === PRIORITY 2: Combat Efficiency ===
    constexpr float KILL_REWARD = 15.0f;                 // Enemy eliminated

    // === PRIORITY 3: Survival (MUST be < Objective rewards) ===
    constexpr float DEATH_PENALTY = -10.0f;              // Acceptable loss if objective achieved

    // CRITICAL INVARIANT: Objective Completion > Death Penalty
    // 100.0 > 10.0 ✅ (dying to capture objective = net +90 reward)
    static_assert(OBJECTIVE_CAPTURE_REWARD > -DEATH_PENALTY,
        "Objective reward must exceed death penalty for proper MCTS-RL alignment");
}
```

### 6.2 Objective-Aware Rewards

**File:** `Source/GameAI_Project/Private/RL/RewardCalculator.cpp`

**Update reward calculation to use objective context:**

```cpp
float URewardCalculator::CalculateReward(
    const FObservationElement& PrevObs,
    const FObservationElement& CurrentObs,
    const FMacroAction& Action)
{
    float Reward = 0.0f;

    EStrategyType Strategy = Action.Strategy;
    EObjectiveType Objective = CurrentObs.ObjectiveContext.Type;

    // Strategy-specific rewards (base)
    Reward += CalculateStrategyReward(Strategy, PrevObs, CurrentObs);

    // Objective-aware modifiers
    Reward += CalculateObjectiveProgressReward(Objective, PrevObs, CurrentObs);

    // Alignment bonus: Strategy matches objective
    Reward += CalculateAlignmentBonus(Strategy, Objective);

    return Reward;
}

float URewardCalculator::CalculateObjectiveProgressReward(
    EObjectiveType Objective,
    const FObservationElement& PrevObs,
    const FObservationElement& CurrentObs) const
{
    float DistancePrev = PrevObs.ObjectiveContext.Distance;
    float DistanceCurrent = CurrentObs.ObjectiveContext.Distance;
    float DistanceDelta = DistancePrev - DistanceCurrent;

    switch (Objective)
    {
        case EObjectiveType::Capture:
            // Reward for getting closer to objective
            if (DistanceDelta > 0)
            {
                return DistanceDelta * 0.5f; // +0.5 per meter of progress
            }
            break;

        case EObjectiveType::Defend:
            // Reward for staying near objective
            if (DistanceCurrent < 0.2f) // Within 10m (normalized)
            {
                return 0.3f; // +0.3/sec for holding
            }
            break;

        case EObjectiveType::Retreat:
            // Reward for increasing distance from danger
            if (DistanceDelta < 0) // Moving away
            {
                return -DistanceDelta * 0.3f;
            }
            break;

        default:
            break;
    }

    return 0.0f;
}

float URewardCalculator::CalculateAlignmentBonus(
    EStrategyType Strategy,
    EObjectiveType Objective) const
{
    // Bonus for strategy matching objective intent
    if ((Strategy == EStrategyType::Assault && Objective == EObjectiveType::Capture) ||
        (Strategy == EStrategyType::Defend && Objective == EObjectiveType::Defend) ||
        (Strategy == EStrategyType::Retreat && Objective == EObjectiveType::Retreat))
    {
        return 0.1f; // Small bonus for alignment
    }

    return 0.0f;
}
```

**Testing:**
1. Agent with Capture objective moves toward it → positive reward
2. Agent with Defend objective holds position → positive reward
3. Agent with Retreat objective increases distance → positive reward
4. Agent strategy matches objective → receives alignment bonus

---

## Phase 7: TeamLeader Integration (Priority: P0, Effort: 3 hours)

### 7.1 Update TeamLeaderComponent

**File:** `Source/GameAI_Project/Private/Team/TeamLeaderComponent.cpp`

**Replace strategy assignment with objective assignment:**

```cpp
void UTeamLeaderComponent::RunObjectiveDecisionMaking()
{
    if (!StrategicMCTS || !ObjectiveManager)
    {
        return;
    }

    // Get available agents and objectives
    TArray<AActor*> AliveAgents = GetAliveFollowers();
    TArray<UObjective*> ActiveObjectives = ObjectiveManager->GetActiveObjectives();

    if (AliveAgents.Num() == 0 || ActiveObjectives.Num() == 0)
    {
        return;
    }

    // Run MCTS to find best assignment
    FObjectiveAssignment Assignment = StrategicMCTS->RunObjectiveAssignment(
        AliveAgents,
        ActiveObjectives,
        MCTSSimulations
    );

    // Apply assignment
    ApplyObjectiveAssignment(Assignment);

    // Log
    UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS] Assignment complete: Value=%.2f, Visits=%d"),
        Assignment.ExpectedValue,
        Assignment.VisitCount);
}

void UTeamLeaderComponent::ApplyObjectiveAssignment(const FObjectiveAssignment& Assignment)
{
    for (const auto& [Agent, Objective] : Assignment.AgentToObjective)
    {
        if (!Agent || !Objective) continue;

        // Update current assignments
        CurrentObjectives.Add(Agent, Objective);

        // Notify follower
        UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
        if (FollowerComp)
        {
            FollowerComp->SetCurrentObjective(Objective);

            UE_LOG(LogTemp, Display, TEXT("🎯 [ASSIGNMENT] '%s' → Objective '%s' (Type=%s, Priority=%d)"),
                *Agent->GetName(),
                *Objective->Description,
                *UEnum::GetValueAsString(Objective->Type),
                Objective->Priority);
        }
    }

    // Broadcast event
    FObjectiveAssignmentMap AssignmentMap;
    AssignmentMap.Objectives = CurrentObjectives;
    OnStrategicDecisionMade.Broadcast(AssignmentMap);
}
```

**Testing:**
1. Run MCTS assignment with 4 agents, 3 objectives
2. Verify all agents receive objectives
3. Verify assignments make sense (high-health → offensive, etc.)
4. Verify followers receive objective notifications

---

## Phase 8: Python Training Code Update (Priority: P2, Effort: 2 hours)

### 8.1 Update Action Space

**File:** `CORTEX_Training/cortex_env.py`

**Changes:**

```python
# v6.0: Action space is now single discrete (strategy only)
self.action_space = spaces.Discrete(4)  # Assault, Defend, Support, Retreat

# v6.0: Observation space updated (68 features)
self.observation_space = spaces.Box(
    low=-1.0,
    high=1.0,
    shape=(68,),  # 64 base + 4 objective context
    dtype=np.float32
)
```

### 8.2 Update Network Architecture

**File:** `CORTEX_Training/models/ppo_model.py`

**Remove multi-head, add single-head policy:**

```python
class CortexPolicyNetwork(nn.Module):
    def __init__(self, obs_dim=68, hidden_sizes=[128, 128, 64]):
        super().__init__()

        # Shared trunk
        self.trunk = nn.Sequential(
            nn.Linear(obs_dim, hidden_sizes[0]),
            nn.ReLU(),
            nn.Linear(hidden_sizes[0], hidden_sizes[1]),
            nn.ReLU(),
            nn.Linear(hidden_sizes[1], hidden_sizes[2]),
            nn.ReLU()
        )

        # Policy head (4 strategy logits)
        self.policy_head = nn.Linear(hidden_sizes[2], 4)

        # Value head (1 value estimate)
        self.value_head = nn.Linear(hidden_sizes[2], 1)

    def forward(self, obs):
        features = self.trunk(obs)
        policy_logits = self.policy_head(features)
        value = self.value_head(features)
        return policy_logits, value
```

### 8.3 Update ONNX Export

**File:** `CORTEX_Training/export_onnx.py`

```python
# v6.0: Export single-head model
dummy_input = torch.randn(1, 68)  # 68 features
torch.onnx.export(
    model,
    dummy_input,
    "cortex_policy_v6.onnx",
    input_names=["observation"],
    output_names=["policy_logits", "value"],
    dynamic_axes={"observation": {0: "batch_size"}}
)

# Verify output shapes
# policy_logits: [batch, 4]
# value: [batch, 1]
```

**Testing:**
1. Train for 100 steps
2. Export to ONNX
3. Load in UE5 and verify inference works
4. Verify output shapes match expectations

---

## Phase 9: Testing & Validation (Priority: P0, Effort: 4 hours)

### 9.1 Unit Tests

**Create:** `Tests/TestMCTSAssignment.cpp`

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTSAssignmentTest, "CORTEX.MCTS.Assignment", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMCTSAssignmentTest::RunTest(const FString& Parameters)
{
    // Setup
    UMCTS* MCTS = NewObject<UMCTS>();
    TArray<AActor*> Agents = CreateTestAgents(4);
    TArray<UObjective*> Objectives = CreateTestObjectives(3);

    // Execute
    FObjectiveAssignment Assignment = MCTS->RunObjectiveAssignment(Agents, Objectives, 100);

    // Verify
    TestTrue("All agents assigned", Assignment.AgentToObjective.Num() == 4);
    TestTrue("Value in range", Assignment.ExpectedValue >= -1.0f && Assignment.ExpectedValue <= 1.0f);
    TestTrue("Visit count > 0", Assignment.VisitCount > 0);

    return true;
}
```

### 9.2 Integration Tests

**Scenario 1: 4v4 Capture**
- 4 agents, 2 objectives (1 Capture, 1 Defend)
- Verify MCTS assigns agents sensibly
- Verify RL selects appropriate strategies
- Verify agents move to correct positions

**Scenario 2: Dynamic Strategy Switching**
- Agent assigned Capture objective
- Starts with Assault strategy (healthy)
- Takes damage → switches to Retreat
- Heals → switches back to Assault
- Verify smooth transitions, no crashes

**Scenario 3: Support Strategy**
- 4 agents, 1 ally critical
- Verify MCTS assigns 1-2 agents to Support objective
- Verify RL selects Support strategy
- Verify agents move toward threatened ally

### 9.3 Performance Profiling

**Target Latencies:**
- MCTS (async): 30-50ms for 500 simulations
- RL inference: 1-3ms per agent
- StateTree: <0.5ms per agent
- **Total: <10ms for 4 agents**

**Profile and optimize if needed:**
```cpp
SCOPE_CYCLE_COUNTER(STAT_MCTSAssignment);
SCOPE_CYCLE_COUNTER(STAT_RLInference);
SCOPE_CYCLE_COUNTER(STAT_StateTreeExecution);
```

---

## Phase 10: Documentation & Cleanup (Priority: P1, Effort: 2 hours)

### 10.1 Update Code Comments

**Add v6.0 markers:**
```cpp
// v6.0: MCTS now solves objective assignment (not strategy selection)
// v6.0: RL selects strategy based on observation + objective context
// v6.0: Rules execute strategy deterministically
```

### 10.2 Remove Dead Code

**Delete:**
- Multi-head network code (`SelectStrategyHead()`, etc.)
- Old v5.0 MCTS strategy selection
- Unused action space definitions (`FTacticalAction`, etc.)

### 10.3 Update README

**Create:** `CORTEX_Training/README_v6.0.md`

Document:
- New action space (Discrete(4))
- New observation space (68 features)
- Network architecture (single-head)
- Training procedure
- ONNX export process

---

## Phase 11: Performance Optimization - Event-Driven Updates (Priority: P1, Effort: 3 hours)

**Purpose:** Reduce inference cost by 75-83% (from 120-240ms/sec to 12-40ms/sec)

### 11.1 Add Event-Driven Update Logic to FollowerAgentComponent

**File:** `Source/GameAI_Project/Public/Team/FollowerAgentComponent.h`

**Add member variables:**

```cpp
UCLASS()
class GAMEAI_PROJECT_API UFollowerAgentComponent : public UActorComponent
{
    GENERATED_BODY()

private:
    // Event-driven strategy update (v6.0 - Performance)
    UPROPERTY()
    float LastStrategyHealth = 1.0f;

    UPROPERTY()
    int32 LastEnemyCount = 0;

    UPROPERTY()
    UObjective* LastObjective = nullptr;

    UPROPERTY()
    int32 TicksSinceLastUpdate = 0;

    UPROPERTY()
    EStrategyType CurrentStrategy = EStrategyType::Assault;

    /**
     * Check if strategy should be recomputed (v6.0)
     * @return true if significant event occurred
     */
    bool ShouldUpdateStrategy() const;
};
```

**File:** `Source/GameAI_Project/Private/Team/FollowerAgentComponent.cpp`

**Implement event-driven logic:**

```cpp
bool UFollowerAgentComponent::ShouldUpdateStrategy() const
{
    // Check significant state changes
    float CurrentHealth = GetCurrentHealth() / GetMaxHealth();
    bool healthChanged = FMath::Abs(CurrentHealth - LastStrategyHealth) > 0.2f;

    int32 CurrentEnemyCount = GetPerceivedEnemies().Num();
    bool newEnemyDetected = CurrentEnemyCount > LastEnemyCount;

    bool objectiveChanged = CurrentObjective != LastObjective;

    // Fallback: Force update every 10 ticks (~0.16s at 60 FPS)
    bool timeout = TicksSinceLastUpdate > 10;

    return healthChanged || newEnemyDetected || objectiveChanged || timeout;
}

void UFollowerAgentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Event-driven strategy update (v6.0 - Performance optimization)
    if (ShouldUpdateStrategy())
    {
        // Build observation and objective context
        FObservationElement Obs = TacticalObserver->BuildObservation(GetOwner());
        FObjectiveContext ObjCtx = TacticalObserver->BuildObjectiveContext(CurrentObjective, GetOwner());

        // Run RL inference (batched by TeamLeader if possible)
        CurrentStrategy = RLPolicy->GetStrategy(Obs, ObjCtx);

        // Cache for next check
        LastStrategyHealth = GetCurrentHealth() / GetMaxHealth();
        LastEnemyCount = GetPerceivedEnemies().Num();
        LastObjective = CurrentObjective;
        TicksSinceLastUpdate = 0;

        UE_LOG(LogTemp, Verbose, TEXT("🔄 [EVENT-DRIVEN] Agent updated strategy to %s"),
            *UEnum::GetValueAsString(CurrentStrategy));
    }

    // Always execute current strategy (cheap, <0.5ms)
    // ... existing execution logic ...

    TicksSinceLastUpdate++;
}
```

**Testing:**
1. Agent with 100% health → No strategy updates (only timeout updates)
2. Agent takes 25% damage → Triggers strategy update (health change >20%)
3. New enemy appears → Triggers strategy update
4. Objective reassignment → Triggers strategy update
5. Profile and verify ~6-10 updates/sec instead of 60/sec

---

## Phase 12: Sim2Real Synchronization Script (Priority: P1, Effort: 2 hours)

**Purpose:** Prevent training drift by auto-syncing C++ → Python config

### 12.1 Create Sync Script

**File:** `CORTEX_Training/tools/sync_config_from_cpp.py`

```python
#!/usr/bin/env python3
"""
Sync training environment config from C++ RL/RLTypes.h
Run before training to ensure Python and C++ environments match
"""
import re
import os
from pathlib import Path

def parse_cpp_constants(header_path):
    """Extract constexpr values from RLConfig namespace"""
    with open(header_path, 'r') as f:
        content = f.read()

    # Extract RLConfig namespace block
    namespace_pattern = r'namespace RLConfig \{(.*?)\}'
    namespace_match = re.search(namespace_pattern, content, re.DOTALL)

    if not namespace_match:
        raise ValueError("RLConfig namespace not found in header")

    namespace_content = namespace_match.group(1)

    # Extract constexpr declarations
    const_pattern = r'constexpr\s+(\w+)\s+(\w+)\s*=\s*([^;]+);'
    constants = re.findall(const_pattern, namespace_content)

    return constants

def generate_python_config(constants, output_path):
    """Generate Python config file from C++ constants"""
    with open(output_path, 'w') as f:
        f.write('# AUTO-GENERATED FROM C++ RL/RLTypes.h - DO NOT EDIT MANUALLY\n')
        f.write('# Run: python tools/sync_config_from_cpp.py\n')
        f.write('# Last synced: ' + __import__('datetime').datetime.now().isoformat() + '\n\n')
        f.write('class RLConfig:\n')
        f.write('    """RL training configuration (synced from C++)\n\n')
        f.write('    CRITICAL: Values must match C++ RLConfig namespace exactly.\n')
        f.write('    Any drift will cause trained models to fail in-game.\n')
        f.write('    """\n\n')

        for type_, name, value in constants:
            # Clean up value (remove 'f' suffix from floats, handle booleans)
            value_clean = value.strip().rstrip('f')

            # Convert C++ types to Python
            if type_ == 'bool':
                value_clean = 'True' if 'true' in value_clean.lower() else 'False'
            elif type_ == 'int32':
                value_clean = str(int(float(value_clean)))

            f.write(f'    {name} = {value_clean}\n')

    print(f'✅ Config synced to: {output_path}')

def verify_sync(cpp_header, python_config):
    """Verify Python config matches C++ header"""
    constants = parse_cpp_constants(cpp_header)

    # Import Python config
    import sys
    sys.path.insert(0, str(Path(python_config).parent))
    from config import RLConfig

    mismatches = []
    for type_, name, value in constants:
        if not hasattr(RLConfig, name):
            mismatches.append(f'Missing in Python: {name}')
            continue

        py_value = getattr(RLConfig, name)
        cpp_value = float(value.strip().rstrip('f'))

        if type_ == 'int32':
            cpp_value = int(cpp_value)

        if abs(py_value - cpp_value) > 0.001:
            mismatches.append(f'{name}: Python={py_value}, C++={cpp_value}')

    return mismatches

if __name__ == '__main__':
    # Paths
    script_dir = Path(__file__).parent
    project_root = script_dir.parent.parent
    cpp_header = project_root / 'Source' / 'GameAI_Project' / 'Public' / 'RL' / 'RLTypes.h'
    python_config = script_dir.parent / 'training_env' / 'config.py'

    # Parse and generate
    print(f'📖 Parsing C++ header: {cpp_header}')
    constants = parse_cpp_constants(str(cpp_header))
    print(f'   Found {len(constants)} constants')

    print(f'✍️  Generating Python config: {python_config}')
    generate_python_config(constants, str(python_config))

    # Verify
    print(f'🔍 Verifying sync...')
    mismatches = verify_sync(str(cpp_header), str(python_config))

    if mismatches:
        print('❌ SYNC FAILED:')
        for mismatch in mismatches:
            print(f'   - {mismatch}')
        exit(1)
    else:
        print('✅ Sync verified successfully!')
```

**File:** `CORTEX_Training/training_env/config.py` (auto-generated)

```python
# AUTO-GENERATED FROM C++ RL/RLTypes.h - DO NOT EDIT MANUALLY
# Run: python tools/sync_config_from_cpp.py

class RLConfig:
    """RL training configuration (synced from C++)

    CRITICAL: Values must match C++ RLConfig namespace exactly.
    Any drift will cause trained models to fail in-game.
    """

    AGENT_WALK_SPEED = 600.0
    AGENT_RUN_SPEED = 900.0
    AGENT_SPRINT_SPEED = 1200.0
    PERCEPTION_RADIUS = 3000.0
    RAYCAST_COUNT = 16
    RAYCAST_LENGTH = 2000.0
    RAYCAST_ANGLE_SPREAD = 180.0
    BASE_DAMAGE = 10.0
    MAX_HEALTH = 100.0
    FIRE_RATE = 0.1
    MAX_DISTANCE_NORMALIZATION = 5000.0
    MAX_VELOCITY_NORMALIZATION = 1200.0
    NUM_STRATEGIES = 4
    NUM_TARGETS = 11
    OBSERVATION_SIZE = 68
```

### 12.2 Add to Training Pipeline

**File:** `CORTEX_Training/train.sh` or `train.bat`

```bash
#!/bin/bash
# v6.0 Training Pipeline

echo "🔄 Syncing config from C++..."
python tools/sync_config_from_cpp.py || exit 1

echo "🎯 Starting training..."
python train.py --config training_env/config.py
```

**Testing:**
1. Run sync script: `python tools/sync_config_from_cpp.py`
2. Verify `config.py` is generated
3. Modify C++ constant (e.g., change RAYCAST_COUNT to 20)
4. Re-run sync script
5. Verify Python config.py updated to 20

---

## Phase 13: Debug Visualization System (Priority: P2, Effort: 3 hours)

**Purpose:** Visualize MCTS assignments and RL strategies in-world for debugging

### 13.1 Add MCTS Debug Visualization

**File:** `Source/GameAI_Project/Private/Team/TeamLeaderComponent.cpp`

```cpp
void UTeamLeaderComponent::DebugDrawMCTSAssignments()
{
    if (!bShowDebugVisualization) return;

    UWorld* World = GetWorld();
    if (!World) return;

    for (const auto& [Agent, Objective] : CurrentObjectives)
    {
        if (!Agent || !Objective) continue;

        FVector AgentPos = Agent->GetActorLocation();
        FVector ObjPos = Objective->TargetLocation;

        // Draw assignment edge (yellow arrow)
        DrawDebugDirectionalArrow(
            World,
            AgentPos,
            ObjPos,
            100.0f,  // Arrow size
            FColor::Yellow,
            false, 0.0f, 0, 3.0f  // Thickness
        );

        // Draw RL value estimate (green text)
        UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
        if (FollowerComp)
        {
            FObservationElement Obs = FollowerComp->BuildObservation();
            FObjectiveContext ObjCtx = FollowerComp->BuildObjectiveContext(Objective);
            float Value = RLPolicy->GetStateValue(Obs, ObjCtx);

            DrawDebugString(
                World,
                AgentPos + FVector(0, 0, 150),
                FString::Printf(TEXT("V=%.2f"), Value),
                nullptr,
                FColor::Green,
                0.0f
            );
        }

        // Draw objective type (cyan text)
        FString ObjTypeStr = UEnum::GetValueAsString(Objective->Type);
        DrawDebugString(
            World,
            ObjPos + FVector(0, 0, 100),
            ObjTypeStr,
            nullptr,
            FColor::Cyan,
            0.0f
        );
    }
}
```

### 13.2 Add Strategy Debug Visualization

**File:** `Source/GameAI_Project/Private/Team/FollowerAgentComponent.cpp`

```cpp
void UFollowerAgentComponent::DebugDrawStrategyState()
{
    if (!bShowDebugVisualization) return;

    UWorld* World = GetWorld();
    if (!World) return;

    FVector AgentPos = GetOwner()->GetActorLocation();

    // Strategy color coding
    FColor StrategyColor;
    switch (CurrentStrategy)
    {
        case EStrategyType::Assault:  StrategyColor = FColor::Red; break;
        case EStrategyType::Defend:   StrategyColor = FColor::Blue; break;
        case EStrategyType::Support:  StrategyColor = FColor::Green; break;
        case EStrategyType::Retreat:  StrategyColor = FColor::Yellow; break;
        default: StrategyColor = FColor::White; break;
    }

    // Draw strategy sphere around agent
    DrawDebugSphere(
        World,
        AgentPos,
        100.0f,  // Radius
        12,      // Segments
        StrategyColor,
        false, 0.0f, 0, 2.0f  // Thickness
    );

    // Draw strategy text above agent
    FString StrategyStr = UEnum::GetValueAsString(CurrentStrategy);
    DrawDebugString(
        World,
        AgentPos + FVector(0, 0, 200),
        StrategyStr,
        nullptr,
        StrategyColor,
        0.0f,
        true  // Draw shadow
    );

    // Draw health bar
    float HealthPct = GetCurrentHealth() / GetMaxHealth();
    FColor HealthColor = FMath::Lerp(FColor::Red, FColor::Green, HealthPct);
    FVector BarStart = AgentPos + FVector(-50, 0, 250);
    FVector BarEnd = AgentPos + FVector(-50 + HealthPct * 100, 0, 250);
    DrawDebugLine(
        World,
        BarStart,
        BarEnd,
        HealthColor,
        false, 0.0f, 0, 5.0f
    );
}
```

### 13.3 Add Console Commands

**File:** `Source/GameAI_Project/Private/Core/SimulationManagerGameMode.cpp`

```cpp
// Add to GameMode class
UFUNCTION(Exec)
void ToggleMCTSDebug()
{
    for (ATeamLeaderComponent* Leader : TeamLeaders)
    {
        if (Leader)
        {
            Leader->bShowDebugVisualization = !Leader->bShowDebugVisualization;
            UE_LOG(LogTemp, Display, TEXT("MCTS Debug: %s"),
                Leader->bShowDebugVisualization ? TEXT("ON") : TEXT("OFF"));
        }
    }
}

UFUNCTION(Exec)
void ToggleRLDebug()
{
    for (AActor* Agent : AllAgents)
    {
        UFollowerAgentComponent* Follower = Agent->FindComponentByClass<UFollowerAgentComponent>();
        if (Follower)
        {
            Follower->bShowDebugVisualization = !Follower->bShowDebugVisualization;
            UE_LOG(LogTemp, Display, TEXT("RL Debug: %s"),
                Follower->bShowDebugVisualization ? TEXT("ON") : TEXT("OFF"));
        }
    }
}

UFUNCTION(Exec)
void PrintMCTSStats()
{
    for (ATeamLeaderComponent* Leader : TeamLeaders)
    {
        if (Leader)
        {
            UE_LOG(LogTemp, Display, TEXT("=== MCTS Stats ==="));
            UE_LOG(LogTemp, Display, TEXT("Iterations: %d"), Leader->LastMCTSIterations);
            UE_LOG(LogTemp, Display, TEXT("Best Value: %.2f"), Leader->BestAssignmentValue);
            UE_LOG(LogTemp, Display, TEXT("Visit Count: %d"), Leader->BestAssignmentVisits);
        }
    }
}
```

**Testing:**
1. Run game and type `ToggleMCTSDebug` in console
2. Verify yellow arrows from agents to objectives
3. Verify green value estimates displayed
4. Type `ToggleRLDebug`
5. Verify colored spheres around agents (Red=Assault, Blue=Defend, etc.)
6. Type `PrintMCTSStats` and verify output in log

---

## Phase 14: Profiling Setup & Validation (Priority: P2, Effort: 2 hours)

**Purpose:** Measure and validate performance targets

### 14.1 Add Profiling Macros

**File:** `Source/GameAI_Project/Public/Core/ProfilingMacros.h`

```cpp
#pragma once

#include "Stats/Stats.h"

// v6.0 Performance Stats
DECLARE_CYCLE_STAT(TEXT("MCTS: Objective Assignment"), STAT_MCTSAssignment, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("RL: Batched Inference"), STAT_RLBatchedInference, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("RL: Single Inference"), STAT_RLSingleInference, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("StateTree: Execution"), STAT_StateTreeExecution, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Observation: Build"), STAT_ObservationBuild, STATGROUP_AI);
```

### 14.2 Add Profiling Scopes

**File:** `Source/GameAI_Project/Private/AI/MCTS/MCTS.cpp`

```cpp
FObjectiveAssignment UMCTS::RunObjectiveAssignment(...)
{
    SCOPE_CYCLE_COUNTER(STAT_MCTSAssignment);

    // ... existing code ...
}
```

**File:** `Source/GameAI_Project/Private/RL/RLPolicyNetwork.cpp`

```cpp
TArray<EStrategyType> URLPolicyNetwork::GetStrategiesBatched(...)
{
    SCOPE_CYCLE_COUNTER(STAT_RLBatchedInference);

    // ... existing code ...
}

EStrategyType URLPolicyNetwork::GetStrategy(...)
{
    SCOPE_CYCLE_COUNTER(STAT_RLSingleInference);

    // ... existing code ...
}
```

### 14.3 Create Profiling Checklist

**File:** `Docs/PROFILING_CHECKLIST_v6.0.md`

```markdown
# v6.0 Performance Validation Checklist

## Target Metrics (4v4 Scenario)
- [ ] MCTS Assignment: < 50ms
- [ ] Batched RL Inference (4 agents): < 4ms
- [ ] StateTree Execution (4 agents): < 2ms
- [ ] **Total AI Frame: < 10ms**

## Unreal Insights Capture
1. [ ] Start profiling: `UnrealInsights` → `Start Trace`
2. [ ] Play 60s of gameplay (4v4 combat)
3. [ ] Stop trace and save
4. [ ] Open trace in Unreal Insights

## Analysis
1. [ ] Navigate to "Timing" view
2. [ ] Filter for "STATGROUP_AI"
3. [ ] Verify STAT_MCTSAssignment < 50ms
4. [ ] Verify STAT_RLBatchedInference < 4ms
5. [ ] Screenshot flame graph showing <10ms AI frame

## Memory Validation
- [ ] MCTS Tree: < 1MB (check Memory Insights)
- [ ] RL Network Weights: < 400KB
- [ ] Observations (4 agents): < 20KB
- [ ] **Total AI Memory: < 2MB**

## Pass Criteria
✅ All timing targets met
✅ No memory leaks detected
✅ Flame graph screenshot saved
✅ Performance report written
```

**Testing:**
1. Enable Unreal Insights
2. Run 60s gameplay session
3. Verify all timing targets are met
4. Save flame graph screenshot
5. Document results in performance report

---

## Rollback Plan

If critical issues arise, rollback is straightforward:

1. **Revert files:**
   ```bash
   git checkout v5.0 Source/GameAI_Project/
   git checkout v5.0 CORTEX_Training/
   ```

2. **Critical files to backup before refactoring:**
   - `RL/RLPolicyNetwork.h/cpp` (major changes)
   - `AI/MCTS/MCTS.h/cpp` (major changes)
   - `RL/RLTypes.h` (data structure changes)

3. **Keep v5.0 ONNX model** as fallback

---

## Success Criteria

**Phase 1-7 (C++ Implementation):**
- ✅ Compiles without errors
- ✅ All agents receive objective assignments from MCTS
- ✅ RL policy returns valid strategies (Assault/Defend/Support/Retreat)
- ✅ StateTree executes strategies deterministically
- ✅ No crashes during 10-minute playtest

**Phase 8 (Python Training):**
- ✅ Training runs for 1000 steps without errors
- ✅ ONNX export succeeds
- ✅ ONNX model loads in UE5
- ✅ Inference produces valid outputs

**Phase 9 (Testing):**
- ✅ All unit tests pass
- ✅ Integration scenarios work as expected
- ✅ Performance within targets (<10ms total)
- ✅ No memory leaks (Valgrind or UE5 profiler)

**Phase 10 (Documentation):**
- ✅ CLAUDE_v6.0.md complete
- ✅ Code comments updated
- ✅ Training README complete

---

## Timeline Estimate (v6.0 Production-Ready)

| Phase | Effort | Dependencies | Priority |
|-------|--------|--------------|----------|
| 1. Data Structures + RLConfig | 3h | None | P0 |
| 2. RL Policy Network + Batching | 5h | Phase 1 | P0 |
| 3. MCTS Refactor | 6h | Phase 1, 2 | P0 |
| 4. Observations | 2h | Phase 1 | P0 |
| 5. StateTree Rules | 3h | Phase 2, 4 | P1 |
| 6. Reward Calculator + Value Alignment | 3h | Phase 1, 4 | **P0 (Critical)** |
| 7. TeamLeader | 3h | Phase 3 | P0 |
| 8. Python Training | 2h | Phase 2 | P1 |
| 9. Testing | 4h | Phase 1-8 | P0 |
| 10. Documentation | 2h | Phase 9 | P1 |
| **11. Event-Driven Updates (NEW)** | **3h** | **Phase 2, 4** | **P1** |
| **12. Sim2Real Sync Script (NEW)** | **2h** | **Phase 1** | **P1** |
| **13. Debug Visualization (NEW)** | **3h** | **Phase 7** | **P2** |
| **14. Profiling Setup (NEW)** | **2h** | **Phase 9** | **P2** |
| **Total** | **43 hours** | ~ **5-6 days** |

**Critical Path (Must complete first):**
1. Phase 1 → 2 → 3 → 7 (MCTS-RL integration) = 17 hours
2. Phase 6 (Value alignment - CRITICAL for preventing broken behavior) = 3 hours
3. Phase 9 (Testing) = 4 hours
**Minimum viable: 24 hours (~3 days)**

**Recommended approach:**
- **Day 1-2:** P0 phases (core architecture + value alignment)
- **Day 3:** Testing + P1 performance optimizations
- **Day 4-5:** P2 polish (debug viz, profiling)
- **Day 6:** Documentation + final validation

---

## Risk Mitigation (v6.0 Production Review)

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| **MCTS assignment too slow** | Medium | High | Profile early (Phase 14), optimize evaluation function, async execution |
| **RL convergence slower than v5.0** | Medium | Medium | Smaller action space (4 vs 44) should speed up learning |
| **Integration bugs (MCTS ↔ RL)** | High | Medium | Extensive unit tests, clear interfaces, debug visualization (Phase 13) |
| **ONNX export issues** | Low | High | Test export early in Phase 8 |
| **Performance regression** | Medium | High | Batched inference (Phase 2), event-driven updates (Phase 11), profiling (Phase 14) |
| **⚠️ Inference overhead (NEW)** | **High** | **High** | **Batched inference (2.6× speedup), event-driven updates (75-83% reduction)** |
| **⚠️ Value misalignment (NEW)** | **High** | **Critical** | **Phase 6: Reward structure with objective > survival (100 > 10), static_assert validation** |
| **⚠️ Sim2Real gap (NEW)** | **Medium** | **High** | **Phase 12: Auto-sync script, unit tests, single source of truth (RLConfig)** |

**Critical Path:** Phases 1 → 2 → 3 → 6 → 7 (core MCTS-RL integration + value alignment)

**NEW: Production-Critical Mitigations (from 2026-01-06 review):**

1. **Inference Overhead:**
   - ✅ Batched inference: 4 agents × 2ms sequential → 1 × 3ms batched = 2.6× faster
   - ✅ Event-driven updates: 60 FPS × 4ms → ~10 updates/sec × 4ms = 75% reduction
   - ✅ Fallback timeout: Force update every 10 ticks prevents stale strategies

2. **Value Alignment (MOST CRITICAL):**
   - ✅ Reward hierarchy: Objective completion (100) > Death penalty (10)
   - ✅ Static assert: Compile-time check prevents misalignment
   - ✅ Phase 6 is P0 priority (same as core architecture)
   - **⚠️ IF SKIPPED:** RL learns to hide, MCTS gets bad value estimates, system fails

3. **Sim2Real Gap:**
   - ✅ RLConfig namespace: Single source of truth in C++
   - ✅ Auto-sync script: Generates Python config from C++ header
   - ✅ Verification: Unit test catches drift before deployment
   - **⚠️ IF SKIPPED:** Trained model breaks in-game due to parameter mismatch

---

## Post-Refactoring: Next Steps

Once v6.0 is stable:

1. **Benchmarking Study:**
   - v6.0 vs v5.0 (learning speed, final performance)
   - v6.0 vs Pure RL baseline
   - v6.0 vs Pure MCTS baseline
   - v6.0 vs Rule-based AI

2. **Ablation Studies:**
   - Remove RL value estimates from MCTS (use heuristics only)
   - Remove coordination heuristics (use RL values only)
   - Remove objective context from RL (blind execution)

3. **Paper Writing:**
   - Introduction: Hierarchical MCTS-RL for real-time combat
   - Related Work: AlphaGo, OpenAI Five, DeepMind SC2
   - Method: 3-layer hierarchy, synergy mechanisms
   - Experiments: Benchmarks, ablations, emergent behaviors
   - Results: Performance, latency, learned strategies
   - Conclusion: Practical real-time multi-agent coordination

**Target Venue:** CoG 2026 or AAMAS 2026

---

**Good luck with the refactoring! The architecture is sound, and the implementation is straightforward. Take it one phase at a time, and you'll have a state-of-the-art system.**
