// TacticalRewardProvider.h - Schola reward provider for strategy-specific rewards

#pragma once

#include "CoreMinimal.h"
#include "Common/AbstractInteractor.h"
#include "TacticalRewardProvider.generated.h"

class AFollowerCharacter;
class URewardCalculator;
class UObservationBuilderComponent;

/** //============================================================
 * Schola reward provider that integrates RewardCalculator.
 *
 * Provides strategy-specific, observation-based rewards:
 * - Assault: HostileObjectiveDistance rewards
 * - Defend: FriendlyObjectiveDistance rewards
 * - Support: AllyDistance rewards
 * - Retreat: EnemyDistance rewards
 * - Tactical parameter effectiveness (gradient-based)
 */ //============================================================
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

	/** Initialize the reward provider */
	void Initialize(AFollowerCharacter* Follower);



protected:
	//============= COMPONENTS =================
	/** The follower agent component to get rewards from */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward")
	TObjectPtr<AFollowerCharacter>				FollowerAgent;

	UPROPERTY()
	TObjectPtr<URewardCalculator>				RewardCalculator;

	UPROPERTY()
	TObjectPtr<UObservationBuilderComponent>	ObservationBuilder;



	/** Last accumulated reward value (from FollowerAgentComponent) */
	float LastRewardValue;
};
