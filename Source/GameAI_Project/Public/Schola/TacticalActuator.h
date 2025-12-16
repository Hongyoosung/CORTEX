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
 * v4.0 Action space: MultiDiscrete([6, N+1, 3, 3])
 * - [0]: Position choice: [Hold, Forward, Retreat, FlankL, FlankR, Advance] (6 options)
 * - [1]: Target index: [None=-1, Enemy_0, ..., Enemy_N] (N+1 options, dynamic)
 * - [2]: Fire mode: [HoldFire, Fire, Suppress] (3 options)
 * - [3]: Stance: [Stand, Crouch, Prone] (3 options)
 *
 * v3.x Legacy (DEPRECATED): 7-dimensional Box (continuous atomic actions)
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tactical Actuator"))
class GAMEAI_PROJECT_API UTacticalActuator : public UMultiDiscreteActuator
{
	GENERATED_BODY()

public:
	UTacticalActuator();

	// UMultiDiscreteActuator interface
	virtual FMultiDiscreteSpace GetActionSpace() override;
	virtual void TakeAction(const FMultiDiscretePoint& Action) override;
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

	/** Maximum number of enemies to track (for dynamic MultiDiscrete space sizing) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator|v4")
	int32 MaxEnemies = 10;

protected:
	/** Find follower agent component */
	UFollowerAgentComponent* FindFollowerAgent() const;

	/** Find state tree component */
	UFollowerStateTreeComponent* FindStateTreeComponent() const;

	/** Last received macro action (cached for debugging) */
	FMacroAction LastMacroAction;
};
