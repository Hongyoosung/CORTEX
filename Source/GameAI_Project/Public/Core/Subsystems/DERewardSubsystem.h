#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Types/DERewardTypes.h"
#include "Types/DEObservationTypes.h"
#include "DERewardSubsystem.generated.h"

class ADECharacter;
class UDERewardData;
struct FDEEQSWeightParameters;


UCLASS()
class GAMEAI_PROJECT_API UDERewardSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    //========================================
    // Data Asset Registration
    //========================================

    void SetRewardData(UDERewardData* Data) { CachedRewardData = Data; }
    UDERewardData* GetRewardData() const { return CachedRewardData; }


    //========================================
    // Event-Driven Sparse Rewards
    //========================================

    float CalculateDeathPenalty     (FDERewardState& InOutAgentState, EDEStrategyType ActiveStrategy, int32 AgentID);
    float CalculateKillReward       (FDERewardState& InOutAgentState, EDEStrategyType ActiveStrategy, int32 AgentID);
    float CalculateAssistReward     (FDERewardState& InOutState, EDEStrategyType ActiveStrategy, float DamageDealt, int32 AgentID);
    float CalculateTeamWipePenalty  (FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID);
    float CalculateCaptureReward    (FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID);
    float CalculateLosePointPenalty (FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID);
    float CalculateSurvivalReward   (FDERewardState& InOutState, EDEStrategyType ActiveStrategy, float CurrentHP, float MaxHP, int32 AgentID);
    float CalculateDistanceShaping  (FDERewardState& InOutState, EDEStrategyType ActiveStrategy, float DistanceToTarget, int32 AgentID);


    //========================================
    // Dense Per-Step Reward
    //========================================

    float ComputeStepReward(
        ADECharacter* Agent,
        FDERewardState& InOutAgentState,
        EDEStrategyType Strategy,
        const FDEObservation& Prev,
        const FDEObservation& Current,
        const FDEEQSWeightParameters& Action);

    /**
     * Compute cooperative base occupation shaping reward.
     * Adds spread incentive and penalises stacking / undefended bases.
     * Must be called inside ComputeStepReward() before clamping.
     *
     * @param Agent       The agent being evaluated
     * @param InOutState  Agent reward state (HasReachedAssignedBase flag stored here)
     * @return            Shaped reward value (not yet scaled by GlobalRewardScale)
     */
    float ComputeBaseCooperationReward(ADECharacter* Agent, FDERewardState& InOutState);

    float GetStrategyScale(EDEStrategyType Strategy, float AssaultScale, float DefendScale, float SupportScale) const;
    float DrainSparseReward(FDERewardState& InOutState, int32 AgentID);

    /**
     * Apply match-end win/loss reward to all agents.
     * Winning team agents receive MatchWinReward, losing team agents receive MatchLossReward.
     * On a tie, no terminal reward is applied.
     * The reward is queued into each agent's FDERewardState and drained on the next step.
     *
     * @param WinnerTeamID  Team ID of the winner, or -1 for a tie
     * @param AllAgents     All spawned agents in this environment
     */
    void ApplyMatchEndReward(int32 WinnerTeamID, const TArray<ADECharacter*>& AllAgents);



private:
    float ApplyAndLogReward(FDERewardState& InOutAgentState, EDERewardEventType EventType, EDEStrategyType Strategy, float RewardValue, int32 AgentID);

    UPROPERTY()
    UDERewardData* CachedRewardData = nullptr;
};
