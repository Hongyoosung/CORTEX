#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Types/RewardTypes.h"
#include "Types/ObservationTypes.h"
#include "MocRewardSubsystem.generated.h"

class AMocCharacter;
struct FEQSWeightParameters;





UCLASS()
class GAMEAI_PROJECT_API UMocRewardSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    float CalculateDeathPenalty(const URewardSettingsDataAsset* Settings, FRewardState& InOutAgentState, EStrategyType ActiveStrategy, int32 AgentID);
    float CalculateKillReward(const URewardSettingsDataAsset* Settings, FRewardState& InOutAgentState, EStrategyType ActiveStrategy, int32 AgentID);
    float CalculateAssistReward(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, float DamageDealt, int32 AgentID);
    float CalculateTeamWipePenalty(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, int32 AgentID);
    float CalculateCaptureReward(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, int32 AgentID);
    float CalculateLosePointPenalty(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, int32 AgentID);
    float CalculateSurvivalReward(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, float CurrentHP, float MaxHP, int32 AgentID);
    float CalculateDistanceShaping(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, float DistanceToTarget, int32 AgentID);
    float GetStrategyScale(EStrategyType Strategy, float AssaultScale, float DefendScale, float SupportScale) const;
    float DrainSparseReward(FRewardState& InOutState, const URewardSettingsDataAsset* Settings, int32 AgentID);
    
    float ComputeStepReward(
        AMocCharacter* Agent, 
        const URewardSettingsDataAsset* Settings, 
        FRewardState& InOutAgentState, 
        EStrategyType Strategy, 
        const FObservation& Prev, 
        const FObservation& Current, 
        const FEQSWeightParameters& Action);


private:
    float ApplyAndLogReward(FRewardState& InOutAgentState, ERewardEventType EventType, EStrategyType Strategy, float RewardValue, int32 AgentID);
};