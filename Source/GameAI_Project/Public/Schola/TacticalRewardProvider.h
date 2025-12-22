// TacticalRewardProvider.h - Schola reward provider for combat events

#pragma once

#include "CoreMinimal.h"
#include "Common/AbstractInteractor.h"
#include "TacticalRewardProvider.generated.h"

class UFollowerAgentComponent;

/**
 * Schola reward provider that exposes rewards from combat events.
 *
 * Reward structure:
 * +10.0 = Kill enemy
 * +5.0  = Deal damage
 * -5.0  = Take damage
 * -10.0 = Death
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, meta = (DisplayName = "Tactical Reward Provider"))
class GAMEAI_PROJECT_API UTacticalRewardProvider : public UObject
{
	GENERATED_BODY()

public:
	UTacticalRewardProvider();

	/** Get accumulated reward since last query (resets after read) */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float GetReward();

	/** Check if episode has terminated (agent died) */
	UFUNCTION(BlueprintPure, Category = "Reward")
	bool IsTerminated() const { return bTerminated; }

	/** Reset reward state for new episode */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void Reset();

	/** The follower agent component to get rewards from */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward")
	UFollowerAgentComponent* FollowerAgent = nullptr;

	/** Auto-find follower agent on owner actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward")
	bool bAutoFindFollower = true;

	/** Initialize the reward provider */
	void Initialize();

protected:
	/** Last accumulated reward value (from FollowerAgentComponent) */
	float LastRewardValue = 0.0f;

	/** Has episode terminated? */
	bool bTerminated = false;

	/** Find follower agent component */
	UFollowerAgentComponent* FindFollowerAgent() const;
};
