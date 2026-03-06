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
class MatchManager;


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

	/** Additional penalty when the entire team is wiped. Called by MatchManager when ActiveAgents reaches 0. */
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



	/** Match state change handler. Bound to AMocGameMode::OnMatchStateChanged in BeginPlay. */
	UFUNCTION()
	void OnMatchStateChanged(EMocMatchState NewState);

	// ==================== Cumulative Reward ====================

	UFUNCTION(BlueprintPure, Category = "MOC|Rewards")
	float GetCumulativeReward() const { return CumulativeReward; }



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

	
	// ==================== Common Base Rewards ====================
	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float TimePenalty = 0.001f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float MatchWinReward = 50.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float KillReward = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float DeathPenaltyReward = 100.0f;

	/** Terminal reward for losing the match (applied as sparse reward) */
	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float MatchLossReward = -50.0f;

	/** Fraction of team average reward mixed into individual reward.
	 *  FinalReward = (1-α)*Individual + α*TeamAvg. Uses 1-step lag to avoid ordering issues. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float TeamRewardMixingRatio = 0.2f;

	/** Fraction of KillReward awarded for an assist (scaled by normalized damage contribution) */
	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float AssistRewardScale = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float SurvivalReward = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float TeamWipePenalty = 50.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float CaptureReward = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float LossCaptureReward = -100.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float PenaltyPerMeter = -0.01f;

	// ==================== Tunable Thresholds ====================

	/** HP fraction below which CalculateSurvivalReward fires */
	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SurvivalHPThreshold = 0.3f;

	/** Assault: minimum health loss (normalized) to apply the per-step health penalty.
	 *  FIX: Lowered from 0.5 to 0.05 — at 0.5 the penalty almost never fired (only one-shots). */
	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float AssaultHealthLossThreshold = 0.05f;

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

	// ==================== Reward Normalization & Clamping ====================

	/** Global reward scale applied to all step rewards before clamping.
	 *  Reduces reward magnitude so the value function can fit discounted returns.
	 *  Raised from 0.01 to 0.05 so dense signals are stronger (baseline 0.03→0.0015). */
	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float GlobalRewardScale = 0.1f;

	/** Scale applied to sparse rewards (kills, deaths, captures) BEFORE they're added to dense step reward.
	 *  Reduces the magnitude gap between dense (~1.0) and sparse (~20-100) rewards so the
	 *  value function can fit both. Applied inside DrainSparseReward(). */
	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float SparseRewardScale = 0.2f;

	/** Minimum per-step reward (after scaling) */
	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float StepRewardClampMin = -2.0f;

	/** Maximum per-step reward (after scaling) */
	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float StepRewardClampMax = 2.0f;

	// ==================== Capture Loss Cooldown (B4) ====================

	/** Minimum seconds between loss penalties for the same capture point.
	 *  Prevents flip-flop scenarios from flooding sparse negative rewards. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Capture")
	float CaptureLossCooldownSeconds = 10.0f;

	// ==================== Strategy Baselines (B1/B2) ====================

	/** Unconditional per-step baseline for Assault so random policies don't diverge to -inf.
	 *  Reduced from 0.02: zone control reward now provides the primary per-step income
	 *  once bases are held, so this only serves as a safety floor for untrained agents. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	float AssaultBaselineReward = 0.01f;

	/** Unconditional per-step baseline for Defend.
	 *  Reduced from 0.03 for the same reason — zone control reward is the dominant signal. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	float DefendBaselineReward = 0.01f;

	/** Unconditional per-step baseline for Support. Same purpose as DefendBaselineReward. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	float SupportBaselineReward = 0.01f;

	// ==================== Zone Control Reward ====================

	/** Per-step reward = ZoneControlRewardPerBase × Scale × (FriendlyBases - EnemyBases).
	 *  Creates a continuous dense signal aligned with the domination objective:
	 *  positive when the team holds more bases, negative when the enemy dominates.
	 *  The negative case discourages passive corner-hiding by penalising inaction
	 *  while the enemy controls the map. */
	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlRewardPerBase = 0.2f;

	/** Assault scale: smaller because capture events and approach rewards are the
	 *  primary gradient; zone control is a secondary alignment signal. */
	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlAssaultScale = 0.1f;

	/** Defend scale: larger because retaining friendly zones is the core objective. */
	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlDefendScale = 1.5f;

	/** Support scale: neutral — zone state matters but is not the primary role objective. */
	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlSupportScale = 1.0f;

	// ==================== Isolation Mode (last survivor) ====================

	/** Multiplier on the objective approach reward when the agent is the last survivor
	 *  (all allies dead). Creates a stronger directional gradient toward capture points
	 *  to prevent corner-hiding. Applied to Assault and Support; Defend uses it for
	 *  the zone approach reward. Default 5× means ~0.05/cm instead of ~0.01/cm. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Isolation")
	float IsolationApproachMultiplier = 5.0f;

	/** Number of consecutive steps all allies must be dead before isolation mode activates.
	 *  Prevents false positives during normal respawn windows (typically 3-8 seconds).
	 *  At 10 steps/s a value of 30 ≈ 3 seconds of sustained isolation. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Isolation")
	int32 IsolationDebounceSteps = 30;

	


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
