// FollowerAgentTrainer.h - Trainer wrapper for follower agents

#pragma once

#include "CoreMinimal.h"
#include "Training/AbstractTrainer.h"
#include "FollowerAgentTrainer.generated.h"


class UScholaAgentComponent;
class AFollowerCharacter;
class UHealthComponent;
class UAgentRewardMaanger;


/** //============================================================
 * Follower Agent Trainer
 *
 * Wraps a follower agent (with ScholaAgentComponent) for Schola RL training.
 * Bridges UE5 agent to Schola's training system via AAbstractTrainer interface.
 *
 * Architecture:
 * - Spawned by ScholaCombatEnvironment for each follower agent
 * - Delegates reward computation to TacticalRewardProvider
 * - Delegates observations/actions to ScholaAgentComponent
 * - Tracks episode termination (death, timeout, etc.)
 *
 * Usage:
 * - Created automatically by ScholaCombatEnvironment
 * - No manual setup required
 */ //============================================================
UCLASS()
class GAMEAI_PROJECT_API AFollowerAgentTrainer : public AAbstractTrainer
{
	GENERATED_BODY()

public:
	AFollowerAgentTrainer();

	//============================================================
	// INITIALIZATION
	//============================================================

	/** Initialize trainer with ScholaAgentComponent (possesses pawn automatically) */
	void Initialize(UScholaAgentComponent* InAgent);


	//============================================================
	// ABSTRACT TRAINER INTERFACE (REQUIRED)
	//============================================================

	virtual float ComputeReward() override;
	virtual EAgentTrainingStatus ComputeStatus() override;
	virtual void GetInfo(TMap<FString, FString>& Info) override;
	virtual void ResetTrainer() override;
	virtual void OnCompletion() override;


	//============================================================
	// AI CONTROLLER OVERRIDES (DIAGNOSTIC)
	//============================================================

	/** Track when possession happens */
	virtual void OnPossess(APawn* InPawn) override;

	/** Track when unpossession happens (THIS IS THE BUG!) */
	virtual void OnUnPossess() override;


private:
	//============= COMPONENTS =================

	/** Reference to ScholaAgentComponent */
	TObjectPtr<UScholaAgentComponent>	ScholaAgent;

	/** Reference to FollowerAgentComponent */
	TObjectPtr<AFollowerCharacter>		FollowerAgent;

	TObjectPtr<UHealthComponent>		AgentHealthComponent;

	/** Reference to TacticalRewardProvider */
	TObjectPtr<UAgentRewardManager>		RewardManager;


	/** Cumulative reward for current episode */
	UPROPERTY()
	float EpisodeReward = 0.0f;

	/** Step count for current episode */
	UPROPERTY()
	int32 EpisodeSteps = 0;
};
