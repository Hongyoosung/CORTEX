
#pragma once

#include "CoreMinimal.h"
#include "Inference/InferenceComponent.h"
#include "Types/MocTypes.h"
#include "ScholaMocAgent.generated.h"


/**
 * Agent Operation Mode
 * - Training: Python (RLlib) controls actions via Schola Policy
 * - Inference: Local ONNX models execute via Schola Policy
 */
UENUM(BlueprintType)
enum class EAgentMode : uint8
{
    Training    UMETA(DisplayName = "Training Mode (Python RLlib)"),
    Inference   UMETA(DisplayName = "Inference Mode (Local ONNX)")
};


/**
 * UScholaMocAgent - MOC v10.2 Schola Integration Component
 *
 * Purpose:
 * Thin integration layer between Schola framework and MOC v10.2 architecture.
 * Implements proper Segregation of Responsibilities by delegating work to
 * parent class components (Observers, Policy, Brain, Actuators).
 *
 * v10.2 Architecture Integration:
 * 1. SquadManager performs centralized MCTS → assigns EStrategyType
 * 2. AMocCharacter.SetCommandedStrategy() updates this agent
 * 3. This agent provides strategy to Observers (included in observation)
 * 4. Policy (Python/ONNX) converts (State + Strategy) → EQS Weights
 * 5. Actuators (TacticalParameterActuator) apply weights to EQS system
 *
 * Responsibilities (What this class DOES):
 * - Store commanded strategy from SquadManager
 * - Provide strategy to Observers (via GetCommandedStrategy)
 * - Support mode switching (Training vs Inference)
 * - Configure Schola components in BeginPlay
 *
 * Non-Responsibilities (What this class does NOT do):
 * - ✗ MCTS Planning (done by SquadManager in v10.2)
 * - ✗ Data Logging (done by ScholaEnvironment)
 * - ✗ Direct Action Execution (done by Actuators)
 * - ✗ Reward Calculation (done by Environment)
 * - ✗ World Model Management (separate system)
 *
 * Usage:
 * 1. Add to AMocCharacter as component
 * 2. Configure Observers/Actuators in Blueprint
 * 3. Set CurrentMode (Training/Inference)
 * 4. SquadManager calls UpdateCommandedStrategy()
 * 5. Schola's Think/Act cycle handles rest automatically
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UScholaMocAgent : public UInferenceComponent
{
    GENERATED_BODY()

public:
    UScholaMocAgent(const FObjectInitializer& ObjectInitializer);

    virtual void BeginPlay() override;

    //========================================
    // Command Reception (v10.2 Architecture)
    //========================================

    /**
     * Update commanded strategy from Squad Commander
     * Called by AMocCharacter.SetCommandedStrategy()
     *
     * @param NewStrategy - Strategy assigned by centralized MCTS planner
     */
    UFUNCTION(BlueprintCallable, Category = "MOC|Commands")
    void UpdateCommandedStrategy(EStrategyType NewStrategy);

    /**
     * Get current commanded strategy
     * Used by Observers to include strategy in observation space
     *
     * Phase 1 Training Mode:
     * If bUseTrainingStrategyOverride is enabled, returns TrainingStrategyOverride
     * instead of the commanded strategy from SquadManager.
     */
    UFUNCTION(BlueprintPure, Category = "MOC|Commands")
    EStrategyType GetCommandedStrategy() const
    {
        return bUseTrainingStrategyOverride ? TrainingStrategyOverride : CommandedStrategy;
    }

    //========================================
    // Mode Management
    //========================================

    /**
     * Current operation mode
     * - Training: Schola Policy connects to Python (RLlib)
     * - Inference: Schola Policy uses local ONNX models
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOC")
    EAgentMode CurrentMode = EAgentMode::Inference;

    //========================================
    // Phase 1 Training Configuration
    //========================================

    /**
     * Enable Training Strategy Override (Phase 1 Only)
     * When enabled, ignores SquadManager commands and uses TrainingStrategyOverride instead.
     * This allows training separate policies per strategy (Assault, Defend, Support).
     *
     * IMPORTANT: Disable this in Phase 3 when testing centralized planning!
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOC|Phase1Training",
        meta = (DisplayName = "Override Strategy (Phase 1 Training)"))
    bool bUseTrainingStrategyOverride = false;

    /**
     * Training Strategy Override (Phase 1 Only)
     * The strategy to train when bUseTrainingStrategyOverride is enabled.
     * Set all agents to the same strategy to train a single policy.
     *
     * Training Plan:
     * - Day 1: Set to Assault, train assault policy
     * - Day 2: Set to Defend, train defend policy
     * - Day 3: Set to Support, train support policy
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOC|Phase1Training",
        meta = (DisplayName = "Training Strategy", EditCondition = "bUseTrainingStrategyOverride"))
    EStrategyType TrainingStrategyOverride = EStrategyType::Assault;

private:
    //========================================
    // Runtime State
    //========================================

    /**
     * Current strategy commanded by SquadManager
     * Injected into observation space by Observers
     * Used by Policy to condition EQS weight output
     */
    EStrategyType CommandedStrategy = EStrategyType::Assault;
};