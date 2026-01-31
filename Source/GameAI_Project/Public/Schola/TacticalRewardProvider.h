// TacticalRewardProvider.h - Schola reward provider for strategy-specific rewards

#pragma once

#include "CoreMinimal.h"
#include "Common/AbstractInteractor.h"
#include "TacticalRewardProvider.generated.h"

class UFollowerAgentComponent;
class URewardCalculator;
class UObservationBuilderComponent;

/**
 * v9.0: Schola reward provider that integrates RewardCalculator.
 *
 * Provides strategy-specific, observation-based rewards:
 * - Assault: HostileObjectiveDistance rewards
 * - Defend: FriendlyObjectiveDistance rewards
 * - Support: AllyDistance rewards
 * - Retreat: EnemyDistance rewards
 * - Tactical parameter effectiveness (gradient-based)
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

	/** Find follower agent component */
	UFollowerAgentComponent* FindFollowerAgent() const;

	/** v9.0: Cached reward calculator (strategy-specific reward computation) */
	UPROPERTY()
	URewardCalculator* CachedRewardCalculator = nullptr;

	/** v9.0: Cached observation builder (for current/previous observations) */
	UPROPERTY()
	UObservationBuilderComponent* CachedObservationBuilder = nullptr;
};
