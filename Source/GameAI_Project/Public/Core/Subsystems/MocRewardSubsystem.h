#pragma once

#include "CoreMinimal.hpp"
#include "Subsystems/WorldSubsystem.h"
#include "Types/RewardTypes.h"        
#include "MocRewardSubsystem.generated.h"

class AMocCharacter;
struct FEQSWeightParameters;

UCLASS()
class GAMEAI_PROJECT_API UMocRewardSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    float CalculateDeathPenalty(const URewardSettingsDataAsset* Settings, FRewardState& InOutAgentState, EStrategyType ActiveStrategy);
    float CalculateKillReward(const URewardSettingsDataAsset* Settings, FRewardState& InOutAgentState, EStrategyType ActiveStrategy);
    
    float ComputeStepReward(
        AMocCharacter* Agent, 
        const URewardSettingsDataAsset* Settings, 
        FRewardState& InOutAgentState, 
        EStrategyType Strategy, 
        const FObservation& Prev, 
        const FObservation& Current, 
        const FEQSWeightParameters& Action);

private:
    float ApplyAndLogReward(FRewardState& InOutAgentState, ERewardEventType EventType, EStrategyType Strategy, float RewardValue);
};