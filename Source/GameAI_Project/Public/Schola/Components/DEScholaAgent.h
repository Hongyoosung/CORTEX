
#pragma once

#include "CoreMinimal.h"
#include "Inference/InferenceComponent.h"
#include "Types/DEStrategyTypes.h"
#include "DEScholaAgent.generated.h"


/**
 * Agent Operation Mode
 * - Training: Python (RLlib) controls actions via Schola Policy
 * - Inference: Local ONNX models execute via Schola Policy
 */
UENUM(BlueprintType)
enum class EDEAgentMode : uint8
{
    Training    UMETA(DisplayName = "Training Mode (Python RLlib)"),
    Inference   UMETA(DisplayName = "Inference Mode (Local ONNX)")
};


/**
 * UDEScholaAgent - MOC v10.2 Schola Integration Component
 *
 * Purpose:
 * Thin integration layer between Schola framework and MOC v10.2 architecture.
 * Implements proper Segregation of Responsibilities by delegating work to
 * parent class components (Observers, Policy, Brain, Actuators).
 *
 * v10.2 Architecture Integration:
 * 1. DESquadManager performs centralized MCTS → assigns EDEStrategyType
 * 2. ADECharacter.SetCommandedStrategy() updates this agent
 * 3. This agent provides strategy to Observers (included in observation)
 * 4. Policy (Python/ONNX) converts (State + Strategy) → EQS Weights
 * 5. Actuators (DETacticalParameterActuator) apply weights to EQS system
 *
 * Responsibilities (What this class DOES):
 * - Store commanded strategy from DESquadManager
 * - Provide strategy to Observers (via GetCommandedStrategy)
 * - Support mode switching (Training vs Inference)
 * - Configure Schola components in BeginPlay
 *
 * Non-Responsibilities (What this class does NOT do):
 * - ✗ MCTS Planning (done by DESquadManager in v10.2)
 * - ✗ Data Logging (done by DEScholaEnvironment)
 * - ✗ Direct Action Execution (done by Actuators)
 * - ✗ Reward Calculation (done by Environment)
 * - ✗ World Model Management (separate system)
 *
 * Usage:
 * 1. Add to ADECharacter as component
 * 2. Configure Observers/Actuators in Blueprint
 * 3. Set CurrentMode (Training/Inference)
 * 4. DESquadManager calls UpdateCommandedStrategy()
 * 5. Schola's Think/Act cycle handles rest automatically
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UDEScholaAgent : public UInferenceComponent
{
    GENERATED_BODY()

public:
    UDEScholaAgent(const FObjectInitializer& ObjectInitializer);

    virtual void BeginPlay() override;

    //========================================
    // Command Reception (v10.2 Architecture)
    //========================================

    /**
     * Update commanded strategy from Squad Commander
     * Called by ADECharacter.SetCommandedStrategy()
     *
     * @param NewStrategy - Strategy assigned by centralized MCTS planner
     */
    UFUNCTION(BlueprintCallable, Category = "MOC|Commands")
    void UpdateCommandedStrategy(EDEStrategyType NewStrategy);

    /**
     * Get current commanded strategy
     * Used by Observers to include strategy in observation space
     *
     * Phase 1 Training Mode:
     * If bUseTrainingStrategyOverride is enabled, returns TrainingStrategyOverride
     * instead of the commanded strategy from DESquadManager.
     */
    UFUNCTION(BlueprintPure, Category = "MOC|Commands")
    EDEStrategyType GetCommandedStrategy() const
    {
        return bUseTrainingStrategyOverride ? TrainingStrategyOverride : CommandedStrategy;
    }

    //========================================
    // Episode Management
    //========================================

    /** Reset agent state for new episode */
    UFUNCTION(BlueprintCallable, Category = "MOC|Episode")
    void ResetAgent();

    //========================================
    // Mode Management
    //========================================

    /**
     * Current operation mode
     * - Training: Schola Policy connects to Python (RLlib)
     * - Inference: Schola Policy uses local ONNX models
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOC")
    EDEAgentMode CurrentMode = EDEAgentMode::Inference;

    //========================================
    // Phase 1 Training Configuration
    //========================================

    /**
     * Enable Training Strategy Override (Phase 1 Only)
     * When enabled, ignores DESquadManager commands and uses TrainingStrategyOverride instead.
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
    EDEStrategyType TrainingStrategyOverride = EDEStrategyType::Assault;

private:
    //========================================
    // Runtime State
    //========================================

    /**
     * Current strategy commanded by DESquadManager
     * Injected into observation space by Observers
     * Used by Policy to condition EQS weight output
     */
    EDEStrategyType CommandedStrategy = EDEStrategyType::Assault;
};