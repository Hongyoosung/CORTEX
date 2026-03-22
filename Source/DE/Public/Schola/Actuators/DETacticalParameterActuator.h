// DETacticalParameterActuator.h - v10.2 Schola actuator for EQS weight outputs
// Box actuator outputting 7-dimensional EQS weights for spatial reasoning

#pragma once

#include "CoreMinimal.h"
#include "Schola/Base/DynamicEQSActuatorBase.h"
#include "Types/DEClassTypes.h"
#include "Types/DEEQSTypes.h"
#include "DETacticalParameterActuator.generated.h"


class ADEAgent;


/** //============================================================
 * Schola actuator - Pure action-to-weight translator.
 *
 * Action Space: Box([-1, 1]^7)
 * - [0]: EnemyObjectiveProximity  (-1=avoid, +1=approach)
 * - [1]: AllyObjectiveProximity   (-1=avoid, +1=vanguard)
 * - [2]: CoverDensity             (-1=ignore, +1=prioritize)
 * - [3]: EnemyVisibility          (-1=hide, +1=expose)
 * - [4]: AllyProximity            (-1=solo, +1=group)
 * - [5]: CombatRange              (normalized engagement distance)
 * - [6]: AssignedBaseProximity    (pull toward assigned base)
 *
 * Responsibility:
 * - Receives Box action from Schola framework
 * - Converts to FDEEQSWeightParameters
 * - Writes weights to DEAgent via UpdateTacticalWeights()
 * - Triggers movement via PerformTacticalAction() (unified for training + inference)
 */ //============================================================
UCLASS(BlueprintType, meta = (DisplayName = "DE Tactical Parameter Actuator"))
class DE_API UDETacticalParameterActuator : public UDynamicEQSActuatorBase
{
	GENERATED_BODY()

public:
	UDETacticalParameterActuator();

	//============================================================
	// UDynamicEQSActuatorBase interface
	//============================================================
	virtual FBoxSpace GetActionSpace() const override;
	virtual void TakeAction(const FBoxPoint& Action) override;
	virtual void InitializeActuator() override;
	virtual void ResetActuator() override;


	//============================================================
	// Commander Integration
	//============================================================

	/**
	 * Set the commanded class from Squad Commander.
	 * This provides context to the RL policy about the assigned role.
	 *
	 * @param CommandedClass - Role assigned by ASquadManager (Strike/Vanguard/Support)
	 */
	UFUNCTION(BlueprintCallable, Category = "Actuator|v10.2")
	void SetCommandedClass(EDEClassType CommandedClass);

	/**
	 * Get the last commanded class.
	 * Used by observer to include class context in observations.
	 */
	UFUNCTION(BlueprintPure, Category = "Actuator|v10.2")
	EDEClassType GetCommandedClass() const { return CurrentCommandedClass; }

	/**
	 * Get the last EQS weights output by the actuator.
	 * Used for debugging and visualization.
	 */
	UFUNCTION(BlueprintPure, Category = "Actuator|v10.2")
	FDEEQSWeightParameters GetLastEQSWeights() const { return LastEQSWeights; }

	/** Explicitly set the owning DEAgent (preferred over GetTypedOuter) */
	UFUNCTION(BlueprintCallable, Category = "Actuator")
	void SetOwnerCharacter(ADEAgent * InCharacter);


	//============================================================
	// Configuration
	//============================================================

	/** Auto-find DE agent on owner actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	bool bAutoFindDE = true;

	/** Enable debug logging for action outputs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	bool bDebugLogging = false;

	/** Clamp outputs to [-1, 1] range (safety check) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	bool bClampOutputs = true;



protected:
	/** Cached DE character reference */
	UPROPERTY()
	TObjectPtr<ADEAgent> DEAgent;

	/** Current commanded class from Squad Commander */
	UPROPERTY(BlueprintReadOnly, Category = "Actuator|v10.2")
	EDEClassType CurrentCommandedClass = EDEClassType::Strike;

	/** Last EQS weights sent to the character */
	UPROPERTY(BlueprintReadOnly, Category = "Debug")
	FDEEQSWeightParameters LastEQSWeights;

	/** Action received counter (for debugging) */
	UPROPERTY(VisibleAnywhere, Category = "Debug")
	int32 ActionCount = 0;



private:
	/**
	 * Find the owning DEAgent through all possible outer chains:
	 * 1. Direct outer (DEAgent)
	 * 2. ActuatorComponent on DEAgent
	 * 3. Trainer (AIController) → possessed pawn
	 */
	ADEAgent* FindDECharacter() const;

	/**
	 * Convert Box action to EQS weight parameters.
	 * Maps 7-dim Box([-1,1]) to FDEEQSWeightParameters struct.
	 */
	FDEEQSWeightParameters ActionToEQSWeights(const FBoxPoint& Action) const;

	/**
	 * Validate EQS weights are in valid range.
	 * Logs warnings if weights are out of expected bounds.
	 */
	bool ValidateEQSWeights(const FDEEQSWeightParameters& Weights) const;
};
