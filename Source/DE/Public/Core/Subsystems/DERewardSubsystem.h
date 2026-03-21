#pragma once

#include "CoreMinimal.h"
#include "Reward/DynamicEQSRewardCalculatorBase.h"
#include "Types/DERewardTypes.h"
#include "Types/DEObservationTypes.h"
#include "DERewardSubsystem.generated.h"

class ADEAgent;
class UDERewardData;
struct FDEEQSWeightParameters;


UCLASS()
class DE_API UDERewardSubsystem : public UDynamicEQSRewardCalculatorBase
{
    GENERATED_BODY()

public:

    // -------------------------------------------------------------------------
    // UDynamicEQSRewardCalculatorBase overrides
    // -------------------------------------------------------------------------

    /**
     * Required override. Per-step reward from flat observation context.
     * Use ComputeStepReward() directly for full game-context reward computation.
     */
    virtual float CalculateStepReward(const FDynamicEQSStepContext& Context) override;

    /** Terminal reward: win => TerminalWinReward, loss => TerminalLossReward. */
    virtual float CalculateTerminalReward(bool bWon) override;

    virtual void Reset() override;


    //========================================
    // Event-Driven Sparse Rewards
    //========================================

    float CalculateDeathPenalty     (FDERewardState& InOutAgentState, EDEClassType ActiveClass, int32 AgentID);
    float CalculateKillReward       (FDERewardState& InOutAgentState, EDEClassType ActiveClass, int32 AgentID);
    float CalculateAssistReward     (FDERewardState& InOutState, EDEClassType ActiveClass, float DamageDealt, int32 AgentID);
    float CalculateTeamWipePenalty  (FDERewardState& InOutState, EDEClassType ActiveClass, int32 AgentID);
    float CalculateTeamWipeBonus   (FDERewardState& InOutState, EDEClassType ActiveClass, int32 AgentID);
    float CalculateCaptureReward    (FDERewardState& InOutState, EDEClassType ActiveClass, int32 AgentID);
    float CalculateLosePointPenalty (FDERewardState& InOutState, EDEClassType ActiveClass, int32 AgentID);
    float CalculateSurvivalReward   (FDERewardState& InOutState, EDEClassType ActiveClass, float CurrentHP, float MaxHP, int32 AgentID);
    float CalculateDistanceShaping  (FDERewardState& InOutState, EDEClassType ActiveClass, float DistanceToTarget, int32 AgentID);


    //========================================
    // Dense Per-Step Reward
    //========================================

    float ComputeStepReward(
        ADEAgent* Agent,
        FDERewardState& InOutAgentState,
        EDEClassType Class,
        const FDEAgentSnapshot& Prev,
        const FDEAgentSnapshot& Current,
        const FDEEQSWeightParameters& Action);

    /**
     * Compute cooperative base occupation shaping reward.
     * Adds spread incentive and penalises stacking / unvanguarded bases.
     * Must be called inside ComputeStepReward() before clamping.
     *
     * @param Agent       The agent being evaluated
     * @param InOutState  Agent reward state (HasReachedAssignedBase flag stored here)
     * @param Class    The current class of the agent (Strike, Vanguard, Support)
     * @return            Shaped reward value (not yet scaled by GlobalRewardScale)
     */
    float ComputeBaseCooperationReward(ADEAgent* Agent, FDERewardState& InOutState, EDEClassType Class);

    float GetClassScale(EDEClassType Class, float StrikeScale, float VanguardScale, float SupportScale) const;
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
    void ApplyMatchEndReward(int32 WinnerTeamID, const TArray<ADEAgent*>& AllAgents);



private:
    float ApplyAndLogReward(FDERewardState& InOutAgentState, EDERewardEventType EventType, EDEClassType Class, float RewardValue, int32 AgentID);

    /** Cast helper — returns the project-specific reward data asset. */
    const UDERewardData* GetSettings() const;
};
