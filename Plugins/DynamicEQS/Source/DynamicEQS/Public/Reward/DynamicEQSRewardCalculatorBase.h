// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Reward/DynamicEQSRewardData.h"

#include "DynamicEQSRewardCalculatorBase.generated.h"

/**
 * FDynamicEQSStepContext
 * Lightweight per-step data passed to the reward calculator.
 */
USTRUCT(BlueprintType)
struct DYNAMICEQS_API FDynamicEQSStepContext
{
	GENERATED_BODY()

	/** Flat observation vector collected this step. */
	UPROPERTY(BlueprintReadWrite, Category = "DynamicEQS|Reward")
	TArray<float> Observation;

	/** Elapsed time since the previous step (seconds). */
	UPROPERTY(BlueprintReadWrite, Category = "DynamicEQS|Reward")
	float DeltaTime = 0.0f;

	/** Zero-based step index within the current episode. */
	UPROPERTY(BlueprintReadWrite, Category = "DynamicEQS|Reward")
	int32 StepIndex = 0;
};

/**
 * UDynamicEQSRewardCalculatorBase
 * Abstract base for all reward calculators in the DynamicEQS plugin.
 * Subclass in Blueprint or C++ to implement domain-specific shaping.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Dynamic EQS Reward Calculator"))
class DYNAMICEQS_API UDynamicEQSRewardCalculatorBase : public UObject
{
	GENERATED_BODY()

public:
	/** Reward data asset providing scalar parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DynamicEQS|Reward")
	TObjectPtr<UDynamicEQSRewardData> RewardData;

	/**
	 * Compute the per-step shaped reward.
	 * @param Context  Observation and timing for the current step.
	 * @return         Scalar reward to add to the episode return.
	 */
	virtual float CalculateStepReward(const FDynamicEQSStepContext& Context)
		PURE_VIRTUAL(UDynamicEQSRewardCalculatorBase::CalculateStepReward, return 0.0f;);

	/**
	 * Compute the terminal reward when an episode ends.
	 * @param bWon  True if the agent achieved its objective.
	 * @return      Terminal reward.
	 */
	virtual float CalculateTerminalReward(bool bWon)
		PURE_VIRTUAL(UDynamicEQSRewardCalculatorBase::CalculateTerminalReward, return 0.0f;);

	/** Called at the start of each new episode; reset any accumulated state. */
	virtual void Reset();
};
