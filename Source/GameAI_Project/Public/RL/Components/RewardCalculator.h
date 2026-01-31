#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RL/RLTypes.h"
#include "Observation/ObservationElement.h"
#include "RewardCalculator.generated.h"

class UFollowerAgentComponent;
class UHealthComponent;

/**
 * v8.10 Unified Reward Configuration (VF Collapse Fix)
 * Strategy-specific behavior emerges from weight profiles, not separate reward functions.
 * All strategies use same reward components with different weights.
 *
 * Key Benefits:
 * - Single source of truth for reward computation
 * - Easy hyperparameter tuning via weight profiles
 * - TensorBoard-compatible component breakdown
 * - No code duplication
 *
 * v8.10 Critical Fix:
 * - Reward normalization: Clamps total reward to [-10, 10] range
 * - Prevents value function collapse from multi-modal return distributions
 * - Required because strategy-specific weights create 100x variance in raw rewards
 *   (e.g., Assault combat spikes vs Support formation rewards)
 */
namespace RewardConfig {
	// === BASE REWARD COMPONENT VALUES ===
	// These are scaled by strategy-specific weights

	// Objective progress (v8.0: REBALANCED - prioritize objectives over combat)
	constexpr float OBJECTIVE_ADVANCE_REWARD = 0.5f;       // Moving closer per step (was 0.1f)
	constexpr float OBJECTIVE_VOLUME_REWARD = 1.0f;        // Inside volume per step (was 0.1f) - 10× increase
	constexpr float OBJECTIVE_CAPTURE_REWARD = 100.0f;     // NEW: Completing objective capture
	constexpr float OBJECTIVE_PROGRESS_REWARD = 50.0f;     // NEW: Incremental capture progress (per 1.0 delta)
	constexpr float MATCH_WIN_REWARD = 200.0f;             // NEW: Terminal reward for winning match
	constexpr float MATCH_LOSS_PENALTY = -100.0f;          // NEW: Terminal penalty for losing match

	// Combat effectiveness (REBALANCED - reduced to prevent combat spam)
	constexpr float DAMAGE_REWARD_SCALE = 0.05f;           // Per 1 damage dealt (was 0.1f) - 50% reduction
	constexpr float KILL_REWARD_BASE = 5.0f;               // Base kill reward (was 10.0f) - 50% reduction
	constexpr float KILL_REWARD_EFFICIENT = 6.0f;          // Efficient target selection (was 12.0f) - 50% reduction

	// Survival
	constexpr float DEATH_PENALTY = -10.0f;                // Base death penalty

	// Cover usage
	constexpr float COVER_BONUS = 0.1f;                    // Per step in cover

	// Team coordination (INCREASED - incentivize teamwork)
	constexpr float FORMATION_BONUS = 0.3f;                // Per step in formation (was 0.1f) - 3× increase
	constexpr float COMBINED_FIRE_REWARD = 20.0f;          // Combined fire on same target (was 10.0f in code) - 2× increase

	// v8.20: Support strategy specific rewards
	constexpr float SUPPORT_PROXIMITY_BONUS = 2.0f;        // Support approaching low-HP ally (scaled by proximity)
	constexpr float SUPPORT_CRITICAL_BONUS = 5.0f;         // Support reaching critically wounded ally (<30% HP, <200cm)

	// v8.0: Tactical parameter effectiveness (INCREASED - improve parameter learning)
	constexpr float TACTICAL_EFFECTIVENESS_BONUS = 0.5f;   // Per step when parameters match outcomes (was 0.15f) - 3.3× increase

	// === STRATEGY-SPECIFIC WEIGHT PROFILES ===
	// Mirrored from Python training_env/unified_reward.py

	struct FStrategyWeights
	{
		float ObjectiveProgress;
		float CombatEffectiveness;
		float Survival;
		float CoverUsage;
		float TeamCoordination;
		float TacticalEffectiveness;  // v8.0: Parameter-outcome alignment
	};

	// Assault: High aggression, push objectives
	constexpr FStrategyWeights ASSAULT_WEIGHTS = {
		1.0f,  // ObjectiveProgress: High
		0.8f,  // CombatEffectiveness: High
		0.6f,  // Survival: Medium (acceptable risk)
		0.3f,  // CoverUsage: Low (don't camp)
		0.4f,  // TeamCoordination: Medium (loose formation)
		0.8f   // TacticalEffectiveness: High (parameter alignment matters)
	};

	// Defend: Hold position, maximize survival
	constexpr FStrategyWeights DEFEND_WEIGHTS = {
		0.2f,  // ObjectiveProgress: Low (stay near objective)
		0.6f,  // CombatEffectiveness: Medium (suppress threats)
		1.0f,  // Survival: High (must survive to hold)
		0.9f,  // CoverUsage: High (maximize cover)
		0.5f,  // TeamCoordination: Medium (defensive formation)
		0.8f   // TacticalEffectiveness: High (parameter alignment matters)
	};

	// Support: Stick with ally, balanced positioning
	constexpr FStrategyWeights SUPPORT_WEIGHTS = {
		0.5f,  // ObjectiveProgress: Medium (follow ally)
		0.4f,  // CombatEffectiveness: Medium-low (assist, don't solo)
		0.7f,  // Survival: Medium-high (stay alive to support)
		0.5f,  // CoverUsage: Medium (balanced)
		1.0f,  // TeamCoordination: High (stick with ally)
		0.9f   // TacticalEffectiveness: Highest (critical for support behavior)
	};

	// Retreat: Survive at all costs, avoid combat
	constexpr FStrategyWeights RETREAT_WEIGHTS = {
		0.8f,  // ObjectiveProgress: High (reach safe zone)
		0.0f,  // CombatEffectiveness: None (avoid combat)
		1.2f,  // Survival: Highest (survival paramount)
		0.7f,  // CoverUsage: High (use cover while retreating)
		0.3f,  // TeamCoordination: Low (self-preservation)
		0.7f   // TacticalEffectiveness: Medium-high (escape efficiency)
	};

	// Lookup helper
	inline const FStrategyWeights& GetWeightsForStrategy(EStrategyType Strategy)
	{
		switch (Strategy)
		{
			case EStrategyType::Assault:  return ASSAULT_WEIGHTS;
			case EStrategyType::Defend:   return DEFEND_WEIGHTS;
			case EStrategyType::Support:  return SUPPORT_WEIGHTS;
			case EStrategyType::Retreat:  return RETREAT_WEIGHTS;
			default:                       return ASSAULT_WEIGHTS;
		}
	}

	// === v9.0: PER-COMPONENT NORMALIZATION ===
	// Prevents value function collapse by normalizing each component before weighting

	struct FComponentNormalization
	{
		float Scale;      // Multiplicative scaling factor
		float Offset;     // Additive offset after scaling
		float ClipMin;    // Minimum value after normalization
		float ClipMax;    // Maximum value after normalization

		float Normalize(float RawValue) const
		{
			float Scaled = RawValue * Scale + Offset;
			return FMath::Clamp(Scaled, ClipMin, ClipMax);
		}
	};

	// Normalization parameters per component (tuned to balance component magnitudes)
	constexpr FComponentNormalization OBJECTIVE_NORM = { 0.02f, -0.2f, -1.0f, 3.0f };     // Objective progress
	constexpr FComponentNormalization COMBAT_NORM = { 0.04f, 0.0f, -0.5f, 2.0f };         // Combat effectiveness
	constexpr FComponentNormalization SURVIVAL_NORM = { 0.2f, 0.0f, -2.0f, 0.0f };        // Survival (death penalty)
	constexpr FComponentNormalization COVER_NORM = { 1.0f, 0.0f, 0.0f, 0.5f };            // Cover usage
	constexpr FComponentNormalization COORD_NORM = { 0.05f, 0.0f, 0.0f, 1.5f };           // Team coordination
	constexpr FComponentNormalization TACTICAL_NORM = { 1.0f, 0.0f, -0.5f, 1.5f };        // Tactical effectiveness
}

/**
 * Combined Fire Record - Tracks recent attacks on same target for coordination detection
 */
USTRUCT()
struct FCombinedFireRecord
{
	GENERATED_BODY()

	UPROPERTY()
	AActor* Target = nullptr;

	UPROPERTY()
	float Timestamp = 0.0f;
};

/**
 * v8.0 Reward Component Breakdown
 * For TensorBoard logging and debugging
 */
USTRUCT(BlueprintType)
struct FRewardComponentBreakdown
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	float ObjectiveProgress = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float CombatEffectiveness = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float Survival = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float CoverUsage = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float TeamCoordination = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float TacticalEffectiveness = 0.0f;  // v8.0: Parameter-outcome alignment

	UPROPERTY(BlueprintReadOnly)
	float Total = 0.0f;

	// For debugging
	FString ToString() const
	{
		return FString::Printf(
			TEXT("Obj=%.2f, Combat=%.2f, Surv=%.2f, Cover=%.2f, Coord=%.2f, Tact=%.2f, Total=%.2f"),
			ObjectiveProgress, CombatEffectiveness, Survival, CoverUsage, TeamCoordination, TacticalEffectiveness, Total
		);
	}
};

/**
 * v8.0 Unified Reward System
 *
 * Calculates rewards using strategy-specific weight profiles applied to common reward components.
 * MCTS assigns strategies, RL outputs tactical parameters, rewards guide parameter learning.
 *
 * Key Features:
 * - Strategy-specific weight profiles (Assault, Defend, Support, Retreat)
 * - Component-based reward breakdown (Objective, Combat, Survival, Cover, Coordination, TacticalEffectiveness)
 * - Tactical parameter effectiveness rewards (parameter → outcome alignment)
 * - TensorBoard-compatible logging
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API URewardCalculator : public UActorComponent
{
	GENERATED_BODY()

public:
	URewardCalculator();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//--------------------------------------------------------------------------
	// CORE REWARD CALCULATION
	//--------------------------------------------------------------------------

	/**
	 * Calculate total reward (delegates to CalculateUnifiedReward)
	 * @param PrevObs - Previous observation
	 * @param CurrentObs - Current observation
	 * @param Action - Macro action (tactical + combat parameters)
	 * @return Total reward for this step
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float CalculateReward(
		const FObservationElement& PrevObs,
		const FObservationElement& CurrentObs,
		const FMacroAction& Action
	);

	/**
	 * v8.0 Unified Reward Calculation
	 * All strategies use same components, different weights
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	FRewardComponentBreakdown CalculateUnifiedReward(
		EStrategyType Strategy,
		const FObservationElement& PrevObs,
		const FObservationElement& CurrentObs
	);

	// ========================================================================
	// v8.0 REWARD COMPONENT CALCULATIONS (Strategy-Agnostic)
	// These return base values, scaled by strategy weights in CalculateUnifiedReward
	// ========================================================================

	/**
	 * Objective progress component (volume-based in v8.0)
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward|Components")
	float CalculateObjectiveProgressComponent(
		const FObservationElement& PrevObs,
		const FObservationElement& CurrentObs
	);

	/**
	 * Combat effectiveness component (kills, damage, target priority)
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward|Components")
	float CalculateCombatEffectivenessComponent(
		const FObservationElement& CurrentObs
	);

	/**
	 * Survival component (death penalty)
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward|Components")
	float CalculateSurvivalComponent(
		const FObservationElement& CurrentObs
	);

	/**
	 * Cover usage component (tactical positioning)
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward|Components")
	float CalculateCoverUsageComponent(
		const FObservationElement& CurrentObs
	);

	/**
	 * Team coordination component (formation, ally proximity)
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward|Components")
	float CalculateTeamCoordinationComponent(
		const FObservationElement& CurrentObs
	);

	/**
	 * v8.0: Tactical parameter effectiveness component
	 * Rewards alignment between tactical parameters and actual positioning outcomes
	 * Creates tight feedback loop: parameters → EQS → positioning → reward
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward|Components")
	float CalculateTacticalParameterEffectivenessComponent(
		const FObservationElement& CurrentObs,
		const FTacticalParameters& TacticalParams
	);

	/**
	 * v8.0: Set current tactical parameters for reward calculation
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void SetCurrentTacticalParameters(const FTacticalParameters& Params);

	// ========================================================================
	// v9.0: STRATEGY-SPECIFIC REWARD FUNCTIONS
	// Use observation fields (HostileObjectiveDistance, FriendlyObjectiveDistance, etc.)
	// ========================================================================

	/**
	 * Assault reward: Incentivize approaching and capturing hostile objective
	 */
	float CalculateAssaultReward(
		const FObservationElement& PrevObs,
		const FObservationElement& CurrentObs
	);

	/**
	 * Defend reward: Incentivize staying near friendly objective
	 */
	float CalculateDefendReward(
		const FObservationElement& PrevObs,
		const FObservationElement& CurrentObs
	);

	/**
	 * Support reward: Incentivize proximity to allies (uses AllyDistance)
	 */
	float CalculateSupportReward(
		const FObservationElement& PrevObs,
		const FObservationElement& CurrentObs
	);

	/**
	 * Retreat reward: Incentivize distance from enemies (uses DistanceToNearestEnemy)
	 */
	float CalculateRetreatReward(
		const FObservationElement& PrevObs,
		const FObservationElement& CurrentObs
	);


	//--------------------------------------------------------------------------
	// EVENT TRACKING
	//--------------------------------------------------------------------------

	/** Track kill event */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnKillEnemy(AActor* Enemy);

	/** Track kill event with target priority info (v8.0) */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnKillEnemyWithPriority(AActor* Enemy, bool bWasLowestHP);

	/** Track damage dealt */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnDealDamage(float Damage, AActor* Target);

	/** Track damage taken */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnTakeDamage(float Damage);

	/** Track death event */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnDeath();

	/** Set current strategy for reward calculation (from MCTS assignment) */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void SetCurrentStrategy(EStrategyType Strategy);

	/** Get current strategy */
	UFUNCTION(BlueprintPure, Category = "Reward")
	EStrategyType GetCurrentStrategy() const { return CurrentStrategy; }

	//--------------------------------------------------------------------------
	// COORDINATION TRACKING
	//--------------------------------------------------------------------------

	/** Check if agent is in formation with teammates */
	UFUNCTION(BlueprintPure, Category = "Reward")
	bool IsInFormation() const;

	/** Register combined fire event (multiple agents targeting same enemy) */
	void RegisterCombinedFire(AActor* Target);


	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** Time window for combined fire detection (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Config")
	float CombinedFireWindow = 2.0f;

	/** Distance threshold for formation detection (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Config")
	float FormationDistanceThreshold = 1500.0f;

private:
	//--------------------------------------------------------------------------
	// COMPONENT REFERENCES
	//--------------------------------------------------------------------------

	UPROPERTY()
	UFollowerAgentComponent* FollowerComponent = nullptr;

	UPROPERTY()
	UHealthComponent* HealthComponent = nullptr;

	/** Current strategy (from MCTS) - determines reward weight profile */
	EStrategyType CurrentStrategy = EStrategyType::Assault;

	//--------------------------------------------------------------------------
	// REWARD ACCUMULATORS
	//--------------------------------------------------------------------------

	float AccumulatedIndividualReward = 0.0f;
	float AccumulatedCoordinationReward = 0.0f;

	//--------------------------------------------------------------------------
	// EVENT TRACKERS
	//--------------------------------------------------------------------------

	int32 KillsSinceLastUpdate = 0;
	float DamageSinceLastUpdate = 0.0f;
	float DamageTakenSinceLastUpdate = 0.0f;

	/** Recent combined fire records */
	TArray<FCombinedFireRecord> RecentCombinedFires;

	//--------------------------------------------------------------------------
	// SUPPORT STRATEGY TRACKING
	//--------------------------------------------------------------------------

	/** Protected ally (for Support strategy coordination reward) */
	UPROPERTY()
	AActor* ProtectedAlly = nullptr;

	//--------------------------------------------------------------------------
	// OBSERVATION TRACKING
	//--------------------------------------------------------------------------

	/** Previous observation for calculating reward deltas */
	FObservationElement PreviousObservation;

	//--------------------------------------------------------------------------
	// v8.0 UNIFIED REWARD TRACKING
	//--------------------------------------------------------------------------

	/** Last reward component breakdown (for TensorBoard logging) */
	FRewardComponentBreakdown LastRewardBreakdown;

	/** Track if target was lowest HP for efficient kill bonus */
	bool bLastKillWasLowestHP = false;

	/** Current tactical parameters from RL (for effectiveness reward calculation) */
	FTacticalParameters CurrentTacticalParams;

	/** v9.0: Previous tactical parameters (for temporal consistency reward) */
	FTacticalParameters PreviousTacticalParams;

	/** v8.0 REBALANCED: Track objective capture progress for incremental rewards */
	float PreviousCaptureProgress = 0.0f;

	/** Track if capture completion reward was already given (prevent duplicate rewards) */
	bool bCaptureCompletionRewarded = false;

public:
	/** Get last reward breakdown for TensorBoard logging */
	UFUNCTION(BlueprintPure, Category = "Reward")
	FRewardComponentBreakdown GetLastRewardBreakdown() const { return LastRewardBreakdown; }
};
