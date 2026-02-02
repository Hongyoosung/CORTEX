// TacticalParameterActuator.h - v8.0 Schola actuator for tactical parameters
// Box actuator outputting 4 continuous values [0,1] for EQS weight modulation

#pragma once

#include "CoreMinimal.h"
#include "Actuators/AbstractActuators.h"
#include "RL/RLTypes.h"
#include "TacticalParameterActuator.generated.h"

class UFollowerAgentComponent;

/**
 * v8.0 Schola actuator that receives tactical parameters from Python.
 *
 * Action space: Box([0,1]^4)
 * - [0]: Aggression     - 0.0 = passive, 1.0 = aggressive
 * - [1]: CoverPreference - 0.0 = ignore cover, 1.0 = prioritize cover
 * - [2]: SpreadDistance  - 0.0 = tight formation, 1.0 = spread out
 * - [3]: RiskTolerance   - 0.0 = retreat early, 1.0 = fight to death
 *
 * These parameters modulate EQS query weights for tactical positioning.
 * MCTS assigns the strategy, RL provides tactical parameter tuning.
 */
UCLASS(BlueprintType, meta = (DisplayName = "v8.0 Tactical Parameter Actuator"))
class GAMEAI_PROJECT_API UTacticalParameterActuator : public UBoxActuator
{
	GENERATED_BODY()

public:
	UTacticalParameterActuator();

	// UBoxActuator interface
	virtual FBoxSpace GetActionSpace() override;
	virtual void TakeAction(const FBoxPoint& Action) override;
	virtual void InitializeActuator() override;
	virtual void ResetActuator() override;

	/** The follower agent component to control */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	UFollowerAgentComponent* FollowerAgent = nullptr;

	/** Auto-find follower agent on owner actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	bool bAutoFindFollower = true;

	/** Enable debug logging */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	bool bDebugLogging = false;

	/** Get the last applied tactical parameters */
	UFUNCTION(BlueprintPure, Category = "Actuator")
	FTacticalParameters GetLastTacticalParameters() const { return LastTacticalParams; }

protected:
	/** Find follower agent component on owner */
	UFollowerAgentComponent* FindFollowerAgent() const;

	/** Last received tactical parameters (cached for debugging) */
	FTacticalParameters LastTacticalParams;
};
