// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Reward/DynamicEQSRewardData.h"
#include "ScholaTraining\Public\TrainingDataTypes\EnvironmentState.h"

#include "DynamicEQSTrainerBase.generated.h"


/**
 * ADynamicEQSTrainerBase
 * Abstract base that owns per-episode logic: reward computation,
 * termination checking, episode reset, and debug telemetry.
 * Inherits from AAIController to keep per-agent pairing semantics.
 * Subclass in C++ or Blueprint; place in the level and assign to the agent.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Dynamic EQS Trainer"))
class DYNAMICEQS_API ADynamicEQSTrainerBase : public AAIController
{
	GENERATED_BODY()

public:
	// -------------------------------------------------------------------------
	// Configuration
	// -------------------------------------------------------------------------

	/** Maximum number of steps per episode before forced termination. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Trainer")
	int32 MaxEpisodeSteps = 3000;

	/** If true, each (s, a, s', r) transition is written to TransitionLogPath. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Trainer")
	bool bLogTransitions = false;

	/** Filesystem path for the transition log CSV (used when bLogTransitions = true). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Trainer",
		meta = (EditCondition = "bLogTransitions"))
	FString TransitionLogPath;

	/** Reward shaping parameters loaded from a data asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DynamicEQS|Trainer")
	TObjectPtr<UDynamicEQSRewardData> RewardData;

	// -------------------------------------------------------------------------
	// Episode interface (override in subclasses)
	// -------------------------------------------------------------------------

	/**
	 * Compute scalar reward for the current step.
	 * Called by the environment each step.
	 */
	virtual float ComputeReward()
		PURE_VIRTUAL(ADynamicEQSTrainerBase::ComputeReward, return 0.0f;);

	/**
	 * Evaluate the current termination state.
	 * Calls ComputeTermination() internally — override ComputeTermination() in subclasses.
	 * Use the returned reason to set FAgentState::bTerminated / bTruncated in the environment actor.
	 */
	virtual EAgentTrainingStatus ComputeStatus();

	/** Populates the info map from GetDebugInfo(). */
	virtual void GetInfo(TMap<FString, FString>& Info);

	/** Delegates to ResetEpisode(). */
	virtual void ResetTrainer();

	/** Default empty implementation. Override if episode-end cleanup is needed. */
	virtual void OnCompletion();

	/**
	 * Determine whether and why the episode should terminate.
	 * Return EDynamicEQSTerminationReason::None to continue.
	 */
	virtual EAgentTrainingStatus ComputeTermination()
		PURE_VIRTUAL(ADynamicEQSTrainerBase::ComputeTermination,
			return EAgentTrainingStatus::Truncated;);

	/** Reset all episode-level state. Called at the start of each new episode. */
	virtual void ResetEpisode();

	/**
	 * Return a map of named debug scalars (e.g., distance to goal, step count).
	 * Used for logging and visualisation.
	 */
	virtual TMap<FString, float> GetDebugInfo();
};
