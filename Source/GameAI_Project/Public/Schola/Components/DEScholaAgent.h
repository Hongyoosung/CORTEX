
#pragma once

#include "CoreMinimal.h"
#include "Schola/DynamicEQSAgentComponent.h"
#include "Types/DEStrategyTypes.h"
#include "DEScholaAgent.generated.h"


/**
 * UDEScholaAgent - Schola Integration Component
 *
 * Purpose:
 * Thin integration layer between Schola framework and architecture.
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
 * - Support mode switching (Training vs Inference) via inherited AgentMode
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
 * 3. Set AgentMode (Training/Inference) — inherited from UDynamicEQSAgentComponent
 * 4. DESquadManager calls UpdateCommandedStrategy()
 * 5. Schola's Think/Act cycle handles rest automatically
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UDEScholaAgent : public UDynamicEQSAgentComponent
{
    GENERATED_BODY()

public:
    UDEScholaAgent();

    virtual void BeginPlay() override;


    //========================================
    // Command Reception 
    //========================================

    /**
     * Update commanded strategy from Squad Commander
     * Called by ADECharacter.SetCommandedStrategy()
     *
     * @param NewStrategy - Strategy assigned by centralized MCTS planner
     */
    UFUNCTION(BlueprintCallable, Category = "DE|Commands")
    void UpdateCommandedStrategy(EDEStrategyType NewStrategy);

    /**
     * Get current commanded strategy
     * Used by Observers to include strategy in observation space
     *
     * Phase 1 Training Mode:
     * If bUseTrainingStrategyOverride is enabled, returns TrainingStrategyOverride
     * instead of the commanded strategy from DESquadManager.
     */
    UFUNCTION(BlueprintPure, Category = "DE|Commands")
    EDEStrategyType GetCommandedStrategy() const
    {
        return CommandedStrategy;
    }


    //========================================
    // Episode Management
    //========================================

    /** Reset agent state for new episode */
    UFUNCTION(BlueprintCallable, Category = "DE|Episode")
    void ResetAgent();



public:

    //========================================
    // Runtime State
    //========================================

    /**
     * Current strategy commanded by DESquadManager
     * Injected into observation space by Observers
     * Used by Policy to condition EQS weight output
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
    EDEStrategyType CommandedStrategy = EDEStrategyType::Assault;
};