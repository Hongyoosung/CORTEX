// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/RewardTypes.h"
#include "Types/ObservationTypes.h"
#include "Types/EQSTypes.h"
#include "Types/StrategyTypes.h"
#include "Actors/CapturePoint.h"
#include "MocRewardCalculator.generated.h"

class AMocCharacter;


/**
 * Strategy-conditioned reward calculator for MOC Arena.
 *
 * Responsibilities:
 * - Event-driven sparse rewards: kills, captures, deaths (called by MocCharacter events)
 * - Dense per-step rewards: movement shaping, health, objective progress, time penalty
 * - Momentum state for post-capture behaviour shaping
 *
 * Access pattern:
 *   MocTrainer → MocCharacter (forwarding methods) → UMocRewardCalculator
 *   MocTrainer never includes this header directly.
 */
UCLASS(ClassGroup=(MOC), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UMocRewardCalculator : public UActorComponent
{
	GENERATED_BODY()

public:
	UMocRewardCalculator();

	virtual void BeginPlay() override;

	// ==================== Event-Driven Sparse Rewards ====================

	/** Assault: +10, Defend: +5, Support: +3 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateKillReward(EStrategyType ActiveStrategy);

	/** Scales by damage contribution (0–100 normalized) */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateAssistReward(EStrategyType ActiveStrategy, float DamageDealt);

	/** Assault: -20, Defend: -15, Support: -10 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateDeathPenalty(EStrategyType ActiveStrategy);

	/** Assault: +15, Defend: +20 (core objective), Support: +10 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateCaptureReward(EStrategyType ActiveStrategy);

	/** Assault: -25, Defend: -30 (critical failure), Support: -15 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateLosePointPenalty(EStrategyType ActiveStrategy);

	/** Assault: +1, Defend: +1, Support: +2 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculatePickupDenyReward(EStrategyType ActiveStrategy);

	/** Only fires when HP < SurvivalHPThreshold */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateSurvivalReward(EStrategyType ActiveStrategy, float CurrentHP, float MaxHP);

	/** Dense distance shaping penalty (disabled when bUseDenseRewards=false) */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateDistanceShaping(EStrategyType ActiveStrategy, float DistanceToTarget);

	/** Match victory: +100 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateVictoryBonus() const { return 100.0f; }

	// ==================== Dense Per-Step Reward ====================

	/**
	 * Compute a single-step reward for the state transition (Prev → Current).
	 * Encapsulates all dense reward logic previously in MocTrainer::ComputeCommandedStrategyReward.
	 *
	 * Caller: MocCharacter::ComputeStepReward() — MocTrainer never calls this directly.
	 */
	float ComputeStepReward(
		EStrategyType Strategy,
		const FObservation& Prev,
		const FObservation& Current,
		const FEQSWeightParameters& Action);

	/**
	 * Decompose the last step's reward into labelled components (debug / GetInfo).
	 * Does NOT re-run full objective logic (ObjectiveComponent left 0 — needs world access).
	 */
	FRewardBreakdown ComputeRewardBreakdown(
		EStrategyType Strategy,
		const FObservation& Prev,
		const FObservation& Current) const;

	// ==================== Episode Management ====================

	/** Reset per-episode state: cumulative reward, event log, momentum counters */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	void ResetEpisodeState();

	/**
	 * Returns the accumulated sparse reward (kills, deaths, captures) since the last drain,
	 * then resets the bucket to zero. Called once per step by ComputeStepReward().
	 */
	float DrainSparseReward();

	/** Capture-point ownership-change handler. Bound to every ACapturePoint::OnPointCaptured in BeginPlay. */
	UFUNCTION()
	void OnCapturePointCaptured(ECapturePointID PointID, ECapturePointOwnership PreviousOwner, ECapturePointOwnership NewOwner);

	// ==================== Cumulative Reward ====================

	UFUNCTION(BlueprintPure, Category = "MOC|Rewards")
	float GetCumulativeReward() const { return CumulativeReward; }

	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	void ResetCumulativeReward() { CumulativeReward = 0.0f; EventLog.Empty(); }

	// ==================== Event Log ====================

	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	void LogRewardEvent(ERewardEventType EventType, EStrategyType Strategy, float Reward);

	UFUNCTION(BlueprintPure, Category = "MOC|Rewards")
	const TArray<FRewardEvent>& GetEventLog() const { return EventLog; }


private:
	float ApplyAndLogReward(ERewardEventType EventType, EStrategyType Strategy, float RewardValue);

	/** Returns the strategy-conditioned scale. Falls back to 1.0 for unknown strategies. */
	float GetStrategyScale(EStrategyType Strategy, float AssaultScale, float DefendScale, float SupportScale) const;

	void AddReward(float Value);

	/** Populate CachedCapturePoints from world. Called once in BeginPlay. */
	void CacheCapturePoints();


public:
	// ==================== Reward Shaping Parameters ====================
	// (Tunable in Blueprint / Details panel)

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float TimePenalty = 0.001f;

	/** Enable distance-shaping dense rewards */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rewards|Common")
	bool bUseDenseRewards = true;

	/** Fraction of KillReward awarded for an assist (scaled by normalized damage contribution) */
	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float AssistRewardScale = 0.5f;

	//==================== Common Base Rewards ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float SurvivalReward = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float DeathPenaltyReward = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float KillReward = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float CaptureReward = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float LossCaptureReward = -100.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float PickupDenyReward = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float PenaltyPerMeter = -0.01f;

	// ==================== Tunable Thresholds ====================

	/** HP fraction below which CalculateSurvivalReward fires */
	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SurvivalHPThreshold = 0.3f;

	/** Assault: minimum health loss (normalized) to apply the per-step health penalty */
	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float AssaultHealthLossThreshold = 0.5f;

	/** Assault: movement below this distance (cm) triggers idle penalty when not in zone */
	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float AssaultIdleMovementThreshold = 50.0f;

	/** Defend: movement below this distance (cm) awards the stationary position bonus */
	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float DefendStationaryThreshold = 200.0f;

	/** Support: minimum movement (cm) for position reward (lower bound) */
	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportMinMoveThreshold = 100.0f;

	/** Support: maximum movement (cm) for position reward (upper bound) */
	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportMaxMoveThreshold = 500.0f;

	/** Support: maximum distance (cm) from the most-injured ally to trigger AllyProximityBonus */
	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportAllyProximityThreshold = 1500.0f;

	/** Defend: health fraction above which the health bonus applies */
	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float DefendHealthThreshold = 0.7f;

	/** Support: health fraction above which the health bonus applies */
	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportHealthThreshold = 0.8f;

	// ==================== Reward Clamping (B3) ====================

	/** Minimum per-step reward (prevents catastrophic negative accumulation) */
	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float StepRewardClampMin = -10.0f;

	/** Maximum per-step reward */
	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float StepRewardClampMax = 10.0f;

	// ==================== Capture Loss Cooldown (B4) ====================

	/** Minimum seconds between loss penalties for the same capture point.
	 *  Prevents flip-flop scenarios from flooding sparse negative rewards. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Capture")
	float CaptureLossCooldownSeconds = 10.0f;

	// ==================== Strategy Baselines (B1/B2) ====================

	/** Unconditional per-step baseline for Defend so random policies don't diverge to -inf.
	 *  Offsets the expected distance penalty for an untrained agent. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	float DefendBaselineReward = 0.3f;

	/** Unconditional per-step baseline for Support. Same purpose as DefendBaselineReward. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	float SupportBaselineReward = 0.3f;


protected:
	//==================== Per-Strategy Reward Settings ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	FAssaultRewardSettings AssaultReward;

	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	FDefendRewardSettings DefendReward;

	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	FSupportRewardSettings SupportReward;



	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewards")
	float CumulativeReward = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewards")
	TArray<FRewardEvent> EventLog;

	UPROPERTY()
	AMocCharacter* OwnerCharacter;

	/** Cached capture point references (populated in BeginPlay) */
	UPROPERTY()
	TArray<TObjectPtr<ACapturePoint>> CachedCapturePoints;

	/** Shared capture radius (taken from first cached point; assumes uniform radius) */
	float CaptureRadius_Cached = 500.0f;

	// Momentum state (per-episode, reset in ResetEpisodeState)
	int32 PostCaptureMomentumStepsRemaining = 0;
	FVector LastCapturedPointLocation = FVector::ZeroVector;

	// FIX (Issue 3 — Support): Cache the target ally index so the approach-delta
	// is always computed against the SAME ally across consecutive steps.
	// Without this, if a different ally becomes most-injured between steps,
	// PrevAllyDist uses the old position of a different slot → noisy gradient.
	int32 CachedInjuredAllyIdx = -1;
	// Re-evaluate target ally every N steps (or on death: idx goes invalid)
	int32 InjuredAllyStalenessCounter = 0;
	static constexpr int32 InjuredAllyReevalInterval = 5;

	// B4: Per-capture-point cooldown for loss penalties (keyed by PointID)
	TMap<ECapturePointID, float> LastCaptureLossPenaltyTime;
};
