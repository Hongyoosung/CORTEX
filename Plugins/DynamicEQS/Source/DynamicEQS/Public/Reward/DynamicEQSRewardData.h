// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "DynamicEQSRewardData.generated.h"

/**
 * UDynamicEQSRewardData
 * Data asset holding scalar reward shaping parameters.
 * Create one per agent variant in the Content Browser and assign it
 * to UDynamicEQSTrainerBase::RewardData.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Dynamic EQS Reward Data"))
class DYNAMICEQS_API UDynamicEQSRewardData : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Global scale applied to every reward signal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DynamicEQS|Reward")
	float RewardScale = 1.0f;

	/** Small positive reward given each step the agent survives. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DynamicEQS|Reward")
	float SurvivalBonus = 0.01f;

	/** Small negative reward applied each step to encourage efficiency. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DynamicEQS|Reward")
	float StepPenalty = -0.001f;

	/** Terminal reward given when the episode ends with a win.
	 *  Raised to dominate over dense shaping — winning the game must be the strongest signal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DynamicEQS|Reward")
	float TerminalWinReward = 500.0f;

	/** Terminal reward given when the episode ends with a loss. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DynamicEQS|Reward")
	float TerminalLossReward = -500.0f;
};
