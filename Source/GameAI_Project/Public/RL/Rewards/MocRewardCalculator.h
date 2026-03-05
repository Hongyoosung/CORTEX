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
class ATeamManager;


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

	/** Additional penalty when the entire team is wiped. Called by TeamManager when ActiveAgents reaches 0. */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateTeamWipePenalty(EStrategyType ActiveStrategy);

	/** Assault: +15, Defend: +20 (core objective), Support: +10 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateCaptureReward(EStrategyType ActiveStrategy);

	/** Assault: -25, Defend: -30 (critical failure), Support: -15 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateLosePointPenalty(EStrategyType ActiveStrategy);

	/** Only fires when HP < SurvivalHPThreshold */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateSurvivalReward(EStrategyType ActiveStrategy, float CurrentHP, float MaxHP);

	/** Dense distance shaping penalty (disabled when bUseDenseRewards=false) */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateDistanceShaping(EStrategyType ActiveStrategy, float DistanceToTarget);

	/** Match victory: +100 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Rewards")
	float CalculateVictoryBonus() const { return 10.0f; }

	/** Apply match end reward (win or loss). Called by OnMatchStateChanged handler. */
	void ApplyMatchEndReward(bool bTeamWon);

	/** Returns the last fully-computed individual step reward (before team mixing).
	 *  Used by teammates to compute team average with 1-step lag. */
	float GetLastIndividualStepReward() const { return LastIndividualStepReward; }

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
	void OnCapturePointCaptured(int32 EnvID, ECapturePointID PointID, ECapturePointOwnership PreviousOwner, ECapturePointOwnership NewOwner);

	/** Match state change handler. Bound to AMocGameMode::OnMatchStateChanged in BeginPlay. */
	UFUNCTION()
	void OnMatchStateChanged(EMocMatchState NewState);

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
	float CaptureRadiusSq_Cached = 250000.0f;

	// Momentum state (per-episode, reset in ResetEpisodeState)
	int32 PostCaptureMomentumStepsRemaining = 0;
	FVector LastCapturedPointLocation = FVector::ZeroVector;

	/** Assault zone-decay: steps since the nearest CP became friendly (for decaying zone bonus).
	 *  Reset when a non-friendly CP is nearest again. */
	int32 AssaultZoneStepsAfterCapture = 0;

	/** Number of steps over which zone presence bonus decays to 0 after CP becomes friendly.
	 *  Keep short (≤10) — a long decay lets agents linger in captured zones instead of
	 *  advancing to the next objective. PostCaptureMomentum is the intended push signal. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	float AssaultCapturedZoneDecaySteps = 5.0f;

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

	// B6: Per-step flag set by CalculateKillReward(), read and cleared in ComputeStepReward().
	// Fixes the RoleBreakPenalty bug where scanning the append-only EventLog caused the
	// penalty to fire every step after the first kill for the rest of the episode.
	bool bSparseKillFiredThisStep = false;

	bool bInFriendlyZone = false;

	/** Last fully-computed individual step reward (before team mixing).
	 *  Teammates read this with 1-step lag for team reward mixing. */
	float LastIndividualStepReward = 0.0f;

	// Isolation debounce: incremented each step all allies are dead, reset when any ally is alive.
	// Isolation mode only activates once this reaches IsolationDebounceSteps.
	int32 IsolatedConsecutiveSteps = 0;

	
};
