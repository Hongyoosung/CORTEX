// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/MocTypes.h"
#include "MocRewardCalculator.generated.h"

class AMocCharacter;

/**
 * Reward event types for logging and analysis
 */
UENUM(BlueprintType)
enum class ERewardEventType : uint8
{
	Kill,
	Assist,
	Death,
	CapturePoint,
	LosePoint,
	RevealEnemy,
	PickupDeny,
	Survival,
	DistanceShaping,
	TeamVictory,
	StrategyDiversity
};

/**
 * Reward event log entry
 */
USTRUCT(BlueprintType)
struct FRewardEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	ERewardEventType EventType;

	UPROPERTY(BlueprintReadOnly)
	EStrategyType ActiveStrategy;

	UPROPERTY(BlueprintReadOnly)
	float RewardValue = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float Timestamp = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	int32 AgentID = -1;

	FRewardEvent() = default;

	FRewardEvent(ERewardEventType Type, EStrategyType Strategy, float Value, float Time, int32 ID)
		: EventType(Type)
		, ActiveStrategy(Strategy)
		, RewardValue(Value)
		, Timestamp(Time)
		, AgentID(ID)
	{}
};

/**
 * Strategy-conditioned reward calculator for MOC Arena.
 *
 * Implements reward structure from MocGameEnvSpecification.md Section 0.6.
 * Each strategy receives specialized rewards to encourage role specialization.
 *
 * Design Philosophy:
 * - Sparse rewards: Kills, captures, match outcome (training stability)
 * - Dense rewards: Distance shaping, damage dealt (early exploration)
 * - Curriculum: Phase 1a uses dense → Phase 1b transitions to sparse
 *
 * Example:
 * - Assault agent kills enemy: +10 reward
 * - Defend agent kills enemy: +5 reward (less important for role)
 */
UCLASS(ClassGroup=(MOC), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UMocRewardCalculator : public UActorComponent
{
	GENERATED_BODY()

public:
	UMocRewardCalculator();

	/**
	 * Calculate reward for enemy kill
	 * Assault: +10, Defend: +5, Support: +3
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateKillReward(EStrategyType ActiveStrategy);

	/**
	 * Calculate reward for assist (damage contribution)
	 * Assault: +3, Defend: +2, Support: +4
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateAssistReward(EStrategyType ActiveStrategy, float DamageDealt);

	/**
	 * Calculate penalty for death
	 * Assault: -20, Defend: -15, Support: -10
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateDeathPenalty(EStrategyType ActiveStrategy);

	/**
	 * Calculate reward for capturing point
	 * Assault: +15, Defend: +20 (core objective), Support: +10
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateCaptureReward(EStrategyType ActiveStrategy);

	/**
	 * Calculate penalty for losing point
	 * Assault: -25, Defend: -30 (critical failure), Support: -15
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateLosePointPenalty(EStrategyType ActiveStrategy);

	/**
	 * Calculate reward for revealing enemy (first sight)
	 * Assault: +2, Defend: +1, Support: +1,(core objective)
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateRevealEnemyReward(EStrategyType ActiveStrategy);

	/**
	 * Calculate reward for pickup denial (take before enemy)
	 * Assault: +1, Defend: +1, Support: +2, (resource control)
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculatePickupDenyReward(EStrategyType ActiveStrategy);

	/**
	 * Calculate reward for survival at low HP (per 5 seconds)
	 * Assault: -5, Defend: -3, Support: -2
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateSurvivalReward(EStrategyType ActiveStrategy, float CurrentHP, float MaxHP);

	/**
	 * Calculate distance shaping penalty
	 * All strategies: -0.01 to -0.03 per meter to assigned target
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateDistanceShaping(EStrategyType ActiveStrategy, float DistanceToTarget);

	/**
	 * Calculate team-level bonus for strategy diversity
	 * Shared reward: +2 if ≥3 distinct strategies active
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateStrategyDiversityBonus(int32 UniqueStrategiesCount);

	/**
	 * Calculate team-level bonus for objective majority
	 * Shared reward: +0.5 per step if controlling ≥3 points
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateObjectiveMajorityBonus(int32 ControlledPoints);

	/**
	 * Calculate match victory bonus
	 * All agents: +100 (distributed equally)
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateVictoryBonus() const { return 100.0f; }

	/**
	 * Get total cumulative reward for agent
	 */
	UFUNCTION(BlueprintPure, Category = "MOC|Rewards")
	float GetCumulativeReward() const { return CumulativeReward; }

	/**
	 * Reset cumulative reward (start of new episode)
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	void ResetCumulativeReward() { CumulativeReward = 0.0f; EventLog.Empty(); }

	/**
	 * Log reward event for analysis
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	void LogRewardEvent(ERewardEventType EventType, EStrategyType Strategy, float Reward);

	/**
	 * Get reward event history
	 */
	UFUNCTION(BlueprintPure, Category = "MOC|Rewards")
	const TArray<FRewardEvent>& GetEventLog() const { return EventLog; }

	/**
	 * Enable/disable dense rewards (curriculum learning)
	 * Phase 1a: Dense = true
	 * Phase 1b: Dense = false (sparse only)
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	void SetUseDenseRewards(bool bUseDense) { bUseDenseRewards = bUseDense; }

protected:
	/** Cumulative reward for current episode */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewards")
	float CumulativeReward = 0.0f;

	/** Reward event log for analysis */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewards")
	TArray<FRewardEvent> EventLog;

	/** Use dense rewards (distance shaping, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rewards")
	bool bUseDenseRewards = true;

	/** Owner character */
	UPROPERTY()
	AMocCharacter* OwnerCharacter;

private:
	/** Add reward to cumulative total */
	void AddReward(float Value);
};
