// TacticalParameterActuator.h - v10.2 Schola actuator for EQS weight outputs
// Box actuator outputting 7-dimensional EQS weights for spatial reasoning

#pragma once

#include "CoreMinimal.h"
#include "Actuators/AbstractActuators.h"
#include "Types/StrategyTypes.h"
#include "Types/EQSTypes.h"
#include "TacticalParameterActuator.generated.h"


class AMocCharacter;


/** //============================================================
 * v10.2 Schola actuator for EQS weight generation in Commander-Executor architecture.
 *
 * Action Space: Box([-1, 1]^7)
 * - [0]: EnemyObjectiveProximity - Approach enemy base (-1=avoid, +1=approach)
 * - [1]: AllyObjectiveProximity  - Defend friendly base (-1=avoid, +1=defend)
 * - [2]: CoverDensity           - Prioritize cover (-1=ignore, +1=prioritize)
 * - [3]: EnemyVisibility        - Maintain line-of-sight (-1=hide, +1=expose)
 * - [4]: AllyProximity          - Stay near teammates (-1=solo, +1=group)
 * - [5]: CombatRange            - Preferred engagement distance (normalized)
 * - [6]: PickupProximity        - Collect health/ammo (-1=ignore, +1=prioritize)
 *
 * Integration with v10.2 Architecture:
 * - Receives commanded strategy (Assault/Defend/Support) from Squad Commander
 * - Outputs EQS weights that modulate spatial reasoning tests
 * - No local MCTS - execution only layer in hierarchical system
 * - Works with UMocEQSExecutor for tactical positioning
 *
 * Key Changes from v8.0:
 * - 4-dim → 7-dim outputs (direct EQS weights vs abstract parameters)
 * - [0,1] → [-1,1] range (allows negative preferences)
 * - Strategy context integration (commanded role awareness)
 * - Removed local planning (centralized commander handles this)
 */ //============================================================
UCLASS(BlueprintType, meta = (DisplayName = "v10.2 Tactical Parameter Actuator"))
class GAMEAI_PROJECT_API UTacticalParameterActuator : public UBoxActuator
{
	GENERATED_BODY()

public:
	UTacticalParameterActuator();

	//============================================================
	// UBoxActuator interface
	//============================================================
	virtual FBoxSpace GetActionSpace() override;
	virtual void TakeAction(const FBoxPoint& Action) override;
	virtual void InitializeActuator() override;
	virtual void ResetActuator() override;


	//============================================================
	// v10.2 Commander Integration
	//============================================================

	/**
	 * Set the commanded strategy from Squad Commander.
	 * This provides context to the RL policy about the assigned role.
	 *
	 * @param CommandedStrategy - Role assigned by ASquadManager (Assault/Defend/Support)
	 */
	UFUNCTION(BlueprintCallable, Category = "Actuator|v10.2")
	void SetCommandedStrategy(EStrategyType CommandedStrategy);

	/**
	 * Get the last commanded strategy.
	 * Used by observer to include strategy context in observations.
	 */
	UFUNCTION(BlueprintPure, Category = "Actuator|v10.2")
	EStrategyType GetCommandedStrategy() const { return CurrentCommandedStrategy; }

	/**
	 * Get the last EQS weights output by the actuator.
	 * Used for debugging and visualization.
	 */
	UFUNCTION(BlueprintPure, Category = "Actuator|v10.2")
	FEQSWeightParameters GetLastEQSWeights() const { return LastEQSWeights; }


	//============================================================
	// Configuration
	//============================================================

	/** Auto-find Moc agent on owner actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	bool bAutoFindMoc = true;

	/** Enable debug logging for action outputs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	bool bDebugLogging = false;

	/** Clamp outputs to [-1, 1] range (safety check) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	bool bClampOutputs = true;


protected:
	/** Cached Moc character reference */
	UPROPERTY()
	TObjectPtr<AMocCharacter> MocAgent;

	/** Current commanded strategy from Squad Commander */
	UPROPERTY(BlueprintReadOnly, Category = "Actuator|v10.2")
	EStrategyType CurrentCommandedStrategy = EStrategyType::Assault;

	/** Last EQS weights sent to the character */
	UPROPERTY(BlueprintReadOnly, Category = "Debug")
	FEQSWeightParameters LastEQSWeights;

	/** Action received counter (for debugging) */
	UPROPERTY(VisibleAnywhere, Category = "Debug")
	int32 ActionCount = 0;

private:
	/**
	 * Convert Box action to EQS weight parameters.
	 * Maps 7-dim Box([-1,1]) to FEQSWeightParameters struct.
	 */
	FEQSWeightParameters ActionToEQSWeights(const FBoxPoint& Action) const;

	/**
	 * Validate EQS weights are in valid range.
	 * Logs warnings if weights are out of expected bounds.
	 */
	bool ValidateEQSWeights(const FEQSWeightParameters& Weights) const;
};
