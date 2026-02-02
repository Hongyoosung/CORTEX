// CombinedTacticalActuator.h - v8.0 Schola actuator for tactical params + combat choice
// Single Box actuator outputting 5 continuous values to avoid Dict action space issues

#pragma once

#include "CoreMinimal.h"
#include "Actuators/AbstractActuators.h"
#include "RL/RLTypes.h"
#include "CombinedTacticalActuator.generated.h"

class UFollowerAgentComponent;

/**
 * v8.0 Combined Schola actuator for tactical parameters AND combat choice.
 *
 * IMPORTANT: This replaces both TacticalParameterActuator and CombatChoiceActuator
 * to avoid Dict action space issues with RLlib. A single Box space is much simpler
 * for PPO to handle than a Dict{Box, Discrete} space.
 *
 * Action space: Box([0,1]^5)
 * - [0]: Aggression      - 0.0 = passive, 1.0 = aggressive
 * - [1]: CoverPreference - 0.0 = ignore cover, 1.0 = prioritize cover
 * - [2]: SpreadDistance  - 0.0 = tight formation, 1.0 = spread out
 * - [3]: RiskTolerance   - 0.0 = retreat early, 1.0 = fight to death
 * - [4]: CombatPriority  - <0.5 = Closest enemy, >=0.5 = LowestHP enemy
 *
 * The first 4 values modulate EQS query weights for tactical positioning.
 * The 5th value determines target priority selection.
 * MCTS assigns the strategy, RL provides tactical parameter + combat tuning.
 */
UCLASS(BlueprintType, meta = (DisplayName = "v8.0 Combined Tactical Actuator"))
class GAMEAI_PROJECT_API UCombinedTacticalActuator : public UBoxActuator
{
	GENERATED_BODY()

public:
	UCombinedTacticalActuator();

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

	/** Threshold for combat priority (default 0.5) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator")
	float CombatPriorityThreshold = 0.5f;

	/** Get the last applied tactical parameters */
	UFUNCTION(BlueprintPure, Category = "Actuator")
	FTacticalParameters GetLastTacticalParameters() const { return LastTacticalParams; }

	/** Get the last applied combat parameters */
	UFUNCTION(BlueprintPure, Category = "Actuator")
	FCombatParameters GetLastCombatParameters() const { return LastCombatParams; }

protected:
	/** Find follower agent component on owner */
	UFollowerAgentComponent* FindFollowerAgent() const;

	/** Last received tactical parameters (cached for debugging) */
	FTacticalParameters LastTacticalParams;

	/** Last received combat parameters (cached for debugging) */
	FCombatParameters LastCombatParams;
};
