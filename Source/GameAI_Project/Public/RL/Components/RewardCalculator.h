#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RL/RLTypes.h"
#include "Observation/ObservationElement.h"
#include "RewardCalculator.generated.h"

class UFollowerAgentComponent;
class UHealthComponent;

/**
 * v8.0 Unified Reward Configuration
 * Strategy-specific behavior emerges from weight profiles, not separate reward functions.
 * All strategies use same reward components with different weights.
 *
 * Key Benefits:
 * - Single source of truth for reward computation
 * - Easy hyperparameter tuning via weight profiles
 * - TensorBoard-compatible component breakdown
 * - No code duplication
 */
namespace RewardConfig {
	// === BASE REWARD COMPONENT VALUES ===
	// These are scaled by strategy-specific weights

	// Objective progress (v8.0: volume-based)
	constexpr float OBJECTIVE_ADVANCE_REWARD = 0.1f;       // Moving closer per step
	constexpr float OBJECTIVE_VOLUME_REWARD = 0.1f;        // Inside volume per step

	// Combat effectiveness
	constexpr float DAMAGE_REWARD_SCALE = 0.1f;            // Per 1 damage dealt
	constexpr float KILL_REWARD_BASE = 10.0f;              // Base kill reward
	constexpr float KILL_REWARD_EFFICIENT = 12.0f;         // Efficient target selection

	// Survival
	constexpr float DEATH_PENALTY = -10.0f;                // Base death penalty

	// Cover usage
	constexpr float COVER_BONUS = 0.1f;                    // Per step in cover

	// Team coordination
	constexpr float FORMATION_BONUS = 0.1f;                // Per step in formation

	// v8.0: Tactical parameter effectiveness
	constexpr float TACTICAL_EFFECTIVENESS_BONUS = 0.15f;  // Per step when parameters match outcomes

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

public:
	/** Get last reward breakdown for TensorBoard logging */
	UFUNCTION(BlueprintPure, Category = "Reward")
	FRewardComponentBreakdown GetLastRewardBreakdown() const { return LastRewardBreakdown; }
};
