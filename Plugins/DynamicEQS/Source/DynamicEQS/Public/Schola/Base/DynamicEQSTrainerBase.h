// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Training/AbstractTrainer.h"
#include "Reward/DynamicEQSRewardData.h"

#include "DynamicEQSTrainerBase.generated.h"

/**
 * EDynamicEQSTerminationReason
 * Describes why an RL episode was terminated.
 */
UENUM(BlueprintType)
enum class EDynamicEQSTerminationReason : uint8
{
	None            UMETA(DisplayName = "None (ongoing)"),
	MaxStepsReached UMETA(DisplayName = "Max Steps Reached"),
	Win             UMETA(DisplayName = "Win"),
	Loss            UMETA(DisplayName = "Loss"),
	Draw            UMETA(DisplayName = "Draw"),
};

/**
 * ADynamicEQSTrainerBase
 * Abstract base that owns per-episode logic: reward computation,
 * termination checking, episode reset, and debug telemetry.
 * Inherits from AAbstractTrainer (AAIController) to integrate with Schola.
 * Subclass in C++ or Blueprint; place in the level and assign to the agent.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Dynamic EQS Trainer"))
class DYNAMICEQS_API ADynamicEQSTrainerBase : public AAbstractTrainer
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
	// AAbstractTrainer overrides
	// -------------------------------------------------------------------------

	/**
	 * Compute scalar reward for the current step.
	 * Called by Schola's training loop each step.
	 */
	virtual float ComputeReward() override
		PURE_VIRTUAL(ADynamicEQSTrainerBase::ComputeReward, return 0.0f;);

	/**
	 * Map EDynamicEQSTerminationReason to EAgentTrainingStatus for Schola.
	 * Calls ComputeTermination() internally — override ComputeTermination() in subclasses.
	 */
	virtual EAgentTrainingStatus ComputeStatus() override;

	/** Populates the info map from GetDebugInfo(). */
	virtual void GetInfo(TMap<FString, FString>& Info) override;

	/** Delegates to ResetEpisode(). */
	virtual void ResetTrainer() override;

	/** Default empty implementation. Override if episode-end cleanup is needed. */
	virtual void OnCompletion() override;

	// -------------------------------------------------------------------------
	// Episode interface (override in subclasses instead of AAbstractTrainer)
	// -------------------------------------------------------------------------

	/**
	 * Determine whether and why the episode should terminate.
	 * Return EDynamicEQSTerminationReason::None to continue.
	 */
	virtual EDynamicEQSTerminationReason ComputeTermination()
		PURE_VIRTUAL(ADynamicEQSTrainerBase::ComputeTermination,
			return EDynamicEQSTerminationReason::None;);

	/** Reset all episode-level state. Called at the start of each new episode. */
	virtual void ResetEpisode();

	/**
	 * Return a map of named debug scalars (e.g., distance to goal, step count).
	 * Used for logging and visualisation.
	 */
	virtual TMap<FString, float> GetDebugInfo();
};
