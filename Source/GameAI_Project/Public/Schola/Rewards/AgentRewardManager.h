// TacticalRewardProvider.h - Schola reward provider for strategy-specific rewards

#pragma once

#include "CoreMinimal.h"
#include "Common/AbstractInteractor.h"
#include "AgentRewardManager.generated.h"


class UObservationBuilderComponent;


/** //============================================================
 * Schola reward provider that integrates Agnet Reward Manager.
 *
 * Provides strategy-specific, observation-based rewards:
 * - Assault: HostileObjectiveDistance rewards
 * - Defend: FriendlyObjectiveDistance rewards
 * - Support: AllyDistance rewards
 * - Retreat: EnemyDistance rewards
 * - Tactical parameter effectiveness (gradient-based)
 */ //============================================================
UCLASS(BlueprintType, Blueprintable, EditInlineNew, meta = (DisplayName = "Reward Manager"))
class GAMEAI_PROJECT_API UAgentRewardManager : public UObject
{
	GENERATED_BODY()

public:
	UAgentRewardManager();

	/** Get accumulated reward since last query (resets after read) */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float GetReward();

	UFUNCTION(BlueprintCallable, Category = "Reward")
	void AccumulateReward(float Reward);

	/** Reset reward state for new episode */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void Reset();



protected:
	//============= COMPONENTS =================
	UPROPERTY()
	TObjectPtr<UObservationBuilderComponent>	ObservationBuilder;


	/** Last accumulated reward value (from FollowerAgentComponent) */
	float LastRewardValue;

	float CurrentReward;
};
