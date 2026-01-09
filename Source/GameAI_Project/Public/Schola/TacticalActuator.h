// TacticalActuator.h - Schola actuator for receiving actions from Python

#pragma once

#include "CoreMinimal.h"
#include "Actuators/AbstractActuators.h"
#include "RL/RLTypes.h"
#include "TacticalActuator.generated.h"

class UFollowerAgentComponent;
class UFollowerStateTreeComponent;

/**
 * Schola actuator that receives macro actions from Python and applies them to follower agents.
 *
 * v4.0 Action space: MultiDiscrete([4, N+1, 3]) - Stance removed for simpler learning
 * - [0]: Position choice: [Hold, ForwardCover, Retreat, Advance] (4 options)
 * - [1]: Target index: [None=-1, Enemy_0, ..., Enemy_N] (N+1 options, dynamic based on MaxEnemies)
 * - [2]: Fire mode: [HoldFire, Fire, Suppress] (3 options)
 *
 * Note: FlankLeft/FlankRight and Stance removed in v4.0 to simplify action space for faster learning
 *
 * Integration:
 * - Python RLlib environment → Schola gRPC → TacticalActuator::TakeAction()
 * - Parses discrete indices into FMacroAction struct
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tactical Actuator"))
class GAMEAI_PROJECT_API UTacticalActuator : public UDiscreteActuator
{
	GENERATED_BODY()

public:
	UTacticalActuator();

	// UMultiDiscreteActuator interface
	virtual FDiscreteSpace GetActionSpace() override;
	virtual void TakeAction(const FDiscretePoint& Action) override;
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
