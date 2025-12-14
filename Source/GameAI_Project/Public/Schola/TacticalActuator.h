// TacticalActuator.h - Schola actuator for receiving actions from Python

#pragma once

#include "CoreMinimal.h"
#include "Actuators/AbstractActuators.h"
#include "RL/RLTypes.h"
#include "TacticalActuator.generated.h"

class UFollowerAgentComponent;
class UFollowerStateTreeComponent;

/**
 * Schola actuator that receives actions from Python and applies them to follower agents.
 *
 * Action space: 7-dimensional Box (continuous)
 * - [0-1]: move_direction (continuous [-1, 1])
 * - [2]:   move_speed (continuous [0, 1])
 * - [3-4]: look_direction (continuous [-1, 1])
 * - [5]:   fire (continuous [0, 1], interpreted as binary: <0.5 = false, >=0.5 = true)
 * - [6]:   crouch (continuous [0, 1], interpreted as binary)
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tactical Actuator"))
class GAMEAI_PROJECT_API UTacticalActuator : public UBoxActuator
{
	GENERATED_BODY()

public:
	UTacticalActuator();

	// UBoxActuator interface
	virtual FBoxSpace GetActionSpace() override;
	virtual void TakeAction(const FBoxPoint& Action) override;
	virtual void InitializeActuator() override;

	/** The follower agent component to control */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	UFollowerAgentComponent* FollowerAgent = nullptr;

	/** Auto-find follower agent on owner actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	bool bAutoFindFollower = true;

	/** Enable debug logging */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	bool bDebugLogging = true;

	/** Enable firing mask (disable fire when no enemies detected) - for early training curriculum */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator|Training")
	bool bEnableFiringMask = true;

	/** Enable look direction smoothing (prevent spinning) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator|Training")
	bool bEnableLookSmoothing = true;

	/** Look smoothing factor [0-1] (0 = no smoothing, 1 = full smoothing) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator|Training", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LookSmoothingFactor = 0.3f;

protected:
	/** Find follower agent component */
	UFollowerAgentComponent* FindFollowerAgent() const;

	/** Find state tree component */
	UFollowerStateTreeComponent* FindStateTreeComponent() const;

	/** Last received action (cached for debugging) */
	FTacticalAction LastAction;
};
