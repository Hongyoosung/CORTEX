// CombatChoiceActuator.h - v8.0 Schola actuator for combat decisions
// Discrete actuator outputting 1 discrete value with 2 choices for target priority

#pragma once

#include "CoreMinimal.h"
#include "Actuators/AbstractActuators.h"
#include "RL/RLTypes.h"
#include "CombatChoiceActuator.generated.h"

class UFollowerAgentComponent;

/**
 * v8.0 Schola actuator that receives combat decisions from Python.
 *
 * Action space: Discrete(2)
 * - [0]: Target Priority
 *   - 0 = Closest enemy
 *   - 1 = Lowest HP enemy
 *
 * v8.0 uses auto-aim (no learned aiming), only target priority is learned.
 * This allows the RL agent to develop combat tactics while keeping complexity low.
 */
UCLASS(BlueprintType, meta = (DisplayName = "v8.0 Combat Choice Actuator"))
class GAMEAI_PROJECT_API UCombatChoiceActuator : public UDiscreteActuator
{
	GENERATED_BODY()

public:
	UCombatChoiceActuator();

	// UDiscreteActuator interface
	virtual FDiscreteSpace GetActionSpace() override;
	virtual void TakeAction(const FDiscretePoint& Action) override;
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

	/** Get the last applied combat parameters */
	UFUNCTION(BlueprintPure, Category = "Actuator")
	FCombatParameters GetLastCombatParameters() const { return LastCombatParams; }

protected:
	/** Find follower agent component on owner */
	UFollowerAgentComponent* FindFollowerAgent() const;

	/** Last received combat parameters (cached for debugging) */
	FCombatParameters LastCombatParams;
};
