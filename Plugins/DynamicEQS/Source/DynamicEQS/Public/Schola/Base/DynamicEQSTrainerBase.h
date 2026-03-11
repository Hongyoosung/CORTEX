// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
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
 * UDynamicEQSTrainerBase
 * Abstract base that owns per-episode logic: reward computation,
 * termination checking, episode reset, and debug telemetry.
 * Subclass in C++ or Blueprint; assign to UDynamicEQSAgentComponent::Trainer.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Dynamic EQS Trainer"))
class DYNAMICEQS_API UDynamicEQSTrainerBase : public UObject
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
	// Pure-virtual episode interface
	// -------------------------------------------------------------------------

	/**
	 * Compute the scalar reward for the current step.
	 * Called once per step by the agent component.
	 */
	virtual float ComputeReward()
		PURE_VIRTUAL(UDynamicEQSTrainerBase::ComputeReward, return 0.0f;);

	/**
	 * Determine whether and why the episode should terminate.
	 * Return EDynamicEQSTerminationReason::None to continue.
	 */
	virtual EDynamicEQSTerminationReason ComputeTermination()
		PURE_VIRTUAL(UDynamicEQSTrainerBase::ComputeTermination,
			return EDynamicEQSTerminationReason::None;);

	// -------------------------------------------------------------------------
	// Virtual (overridable) helpers
	// -------------------------------------------------------------------------

	/** Reset all episode-level state. Called at the start of each new episode. */
	virtual void ResetEpisode();

	/**
	 * Return a map of named debug scalars (e.g., distance to goal, step count).
	 * Used by the environment actor for logging and visualisation.
	 */
	virtual TMap<FString, float> GetDebugInfo();
};
