
#pragma once

#include "CoreMinimal.h"
#include "Schola/DynamicEQSAgentComponent.h"
#include "Types/DEClassTypes.h"
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
 * 1. DESquadManager performs centralized MCTS → assigns EDEClassType
 * 2. ADEAgent.SetCommandedClass() updates this agent
 * 3. This agent provides class to Observers (included in observation)
 * 4. Policy (Python/ONNX) converts (State + Class) → EQS Weights
 * 5. Actuators (DETacticalParameterActuator) apply weights to EQS system
 *
 * Responsibilities (What this class DOES):
 * - Store commanded class from DESquadManager
 * - Provide class to Observers (via GetCommandedClass)
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
 * 1. Add to ADEAgent as component
 * 2. Configure Observers/Actuators in Blueprint
 * 3. Set AgentMode (Training/Inference) — inherited from UDynamicEQSAgentComponent
 * 4. DESquadManager calls UpdateCommandedClass()
 * 5. Schola's Think/Act cycle handles rest automatically
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class DE_API UDEScholaAgent : public UDynamicEQSAgentComponent
{
    GENERATED_BODY()

public:
    UDEScholaAgent();

    virtual void BeginPlay() override;


    //========================================
    // Command Reception 
    //========================================

    /**
     * Update commanded class from Squad Commander
     * Called by ADEAgent.SetCommandedClass()
     *
     * @param NewClass - Class assigned by centralized MCTS planner
     */
    UFUNCTION(BlueprintCallable, Category = "DE|Commands")
    void UpdateCommandedClass(EDEClassType NewClass);

    /**
     * Get current commanded class
     * Used by Observers to include class in observation space
     *
     * Phase 1 Training Mode:
     * If bUseTrainingClassOverride is enabled, returns TrainingClassOverride
     * instead of the commanded class from DESquadManager.
     */
    UFUNCTION(BlueprintPure, Category = "DE|Commands")
    EDEClassType GetCommandedClass() const
    {
        return CommandedClass;
    }


    //========================================
    // Episode Management
    //========================================

    /** Reset agent state for new episode */
    UFUNCTION(BlueprintCallable, Category = "DE|Episode")
    void ResetAgent();



public:

    //========================================
    // Per-Class ONNX Policies (Inference Mode)
    //========================================

    /**
     * Policy for Strike class.
     * Assign the Strike ONNX model asset here.
     * When CommandedClass == Strike, this policy drives the EQS weights.
     */
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Class|Policies")
    TObjectPtr<UObject> StrikePolicyObject;

    /**
     * Policy for Vanguard class.
     * Assign the Vanguard ONNX model asset here.
     */
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Class|Policies")
    TObjectPtr<UObject> VanguardPolicyObject;

    /**
     * Policy for Support class.
     * Assign the Support ONNX model asset here.
     */
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Class|Policies")
    TObjectPtr<UObject> SupportPolicyObject;

    //========================================
    // Runtime State
    //========================================

    /**
     * Current class commanded by DESquadManager
     * Injected into observation space by Observers
     * Used by Policy to condition EQS weight output
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class")
    EDEClassType CommandedClass = EDEClassType::Strike;

private:
    /** Returns the policy object corresponding to the given class. */
    UObject* GetPolicyForClass(EDEClassType Class) const;
};