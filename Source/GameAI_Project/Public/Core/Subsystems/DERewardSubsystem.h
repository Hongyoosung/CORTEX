#pragma once

#include "CoreMinimal.h"
#include "Reward/DynamicEQSRewardCalculatorBase.h"
#include "Types/DERewardTypes.h"
#include "Types/DEObservationTypes.h"
#include "DERewardSubsystem.generated.h"

class ADECharacter;
class UDERewardData;
struct FDEEQSWeightParameters;


UCLASS()
class GAMEAI_PROJECT_API UDERewardSubsystem : public UDynamicEQSRewardCalculatorBase
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
        const FDEAgentSnapshot& Prev,
        const FDEAgentSnapshot& Current,
        const FDEEQSWeightParameters& Action);

    /**
     * Compute cooperative base occupation shaping reward.
     * Adds spread incentive and penalises stacking / undefended bases.
     * Must be called inside ComputeStepReward() before clamping.
     *
     * @param Agent       The agent being evaluated
     * @param InOutState  Agent reward state (HasReachedAssignedBase flag stored here)
     * @param Strategy    The current strategy of the agent (Assault, Defend, Support)
     * @return            Shaped reward value (not yet scaled by GlobalRewardScale)
     */
    float ComputeBaseCooperationReward(ADECharacter* Agent, FDERewardState& InOutState, EDEStrategyType Strategy);

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

    /** Cast helper — returns the project-specific reward data asset. */
    const UDERewardData* GetSettings() const;
};
