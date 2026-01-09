#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RL/RLTypes.h"  // v6.0: EStrategyType, EMissionType, FMissionContext
#include "Observation/ObservationElement.h"
#include "RewardCalculator.generated.h"

class UFollowerAgentComponent;
class UHealthComponent;
class UMission;

/**
 * v6.0 Reward Configuration
 * CRITICAL: Mission completion MUST be highest priority
 * If death penalty > Mission reward, RL learns to hide instead of completing Missions
 * This ensures MCTS-RL value alignment for proper coordination
 */
namespace RewardConfig {
	// === PRIORITY 0: Mission Completion (Dominant Term) ===
	constexpr float OBJECTIVE_CAPTURE_REWARD = 100.0f;   // Mission success
	constexpr float OBJECTIVE_DEFEND_REWARD = 80.0f;     // Hold for duration
	constexpr float SUPPORT_REWARD = 90.0f;    // Protected ally survives
	constexpr float RETREAT_REWARD = 70.0f;    // Reach safe zone

	// === PRIORITY 1: Mission Progress ===
	constexpr float PROGRESS_PER_METER = 0.5f;           // Incremental progress

	// === PRIORITY 2: Combat Efficiency ===
	constexpr float KILL_REWARD = 15.0f;                 // Enemy eliminated

	// === PRIORITY 3: Survival (MUST be < Mission rewards) ===
	constexpr float DEATH_PENALTY = -10.0f;              // Acceptable loss if Mission achieved

	// CRITICAL INVARIANT: Mission Completion > Death Penalty
	// 100.0 > 10.0 ✅ (dying to capture objective = net +90 reward)
	static_assert(OBJECTIVE_CAPTURE_REWARD > -DEATH_PENALTY,
		"Objective reward must exceed death penalty for proper MCTS-RL alignment");
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
 * v6.0 Mission-Aware Reward System
 *
 * Calculates rewards based on Mission completion and progress, with proper MCTS-RL alignment.
 * The reward structure ensures Mission completion is the dominant term, preventing agents
 * from learning to prioritize survival over mission success.
 *
 * Key Features:
 * - Mission-aware rewards (rewards vary based on assigned Mission type)
 * - Strategy-Mission alignment bonuses
 * - Progress tracking (rewards for moving toward Mission)
 * - Proper value alignment with MCTS (Mission reward > death penalty)
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
	// CORE REWARD CALCULATION (v6.0)
	//--------------------------------------------------------------------------

	/**
	 * Calculate total reward (v6.0 - Mission-aware)
	 * @param PrevObs - Previous observation
	 * @param CurrentObs - Current observation (includes Mission context)
	 * @param Action - Action taken (strategy selection)
	 * @return Total reward for this step
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float CalculateReward(
		const FObservationElement& PrevObs,
		const FObservationElement& CurrentObs,
		const FMacroAction& Action
	);

	/**
	 * Calculate strategy-specific rewards (v6.0)
	 * Base rewards for combat events adjusted by current strategy
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float CalculateStrategyReward(
		EStrategyType Strategy,
		const FObservationElement& PrevObs,
		const FObservationElement& CurrentObs
	);

	/**
	 * Calculate Mission progress rewards (v6.0 - CRITICAL)
	 * Rewards for making progress toward assigned Mission
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float CalculateMissionProgressReward(
		EMissionType Mission,
		const FObservationElement& PrevObs,
		const FObservationElement& CurrentObs
	);

	/**
	 * Calculate alignment bonus (v6.0)
	 * Bonus for strategy matching Mission intent
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float CalculateAlignmentBonus(
		EStrategyType Strategy,
		EMissionType Mission
	);

	

	//--------------------------------------------------------------------------
	// EVENT TRACKING
	//--------------------------------------------------------------------------

	/** Track kill event */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnKillEnemy(AActor* Enemy);

	/** Track damage dealt */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnDealDamage(float Damage, AActor* Target);

	/** Track damage taken */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnTakeDamage(float Damage);

	/** Track death event */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnDeath();

	/** Track Mission completion */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnMissionComplete(UMission* Mission);

	/** Track Mission failure */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnMissionFailed(UMission* Mission);

	/** Set current Mission for reward tracking */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void SetCurrentMission(UMission* Mission);

	/** Set current strategy for reward calculation (v5.0) */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void SetCurrentStrategy(EStrategyType Strategy);

	/** Get current strategy */
	UFUNCTION(BlueprintPure, Category = "Reward")
	EStrategyType GetCurrentStrategy() const { return CurrentStrategy; }

	//--------------------------------------------------------------------------
	// COORDINATION TRACKING
	//--------------------------------------------------------------------------

	/** Check if agent is currently on Mission */
	UFUNCTION(BlueprintPure, Category = "Reward")
	bool IsOnMission() const;

	/** Check if agent is in formation with teammates */
	UFUNCTION(BlueprintPure, Category = "Reward")
	bool IsInFormation() const;

	/** Register combined fire event (multiple agents targeting same enemy) */
	void RegisterCombinedFire(AActor* Target);


	//--------------------------------------------------------------------------
	// CONFIGURATION (v6.0 - Simplified)
	//--------------------------------------------------------------------------

	/** Radius to consider "on Mission" (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Config")
	float MissionRadiusThreshold = 1000.0f;

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

	UPROPERTY()
	UMission* CurrentMission = nullptr;

	/** Current strategy (v5.0) - affects reward calculation */
	EStrategyType CurrentStrategy = EStrategyType::Assault;

	//--------------------------------------------------------------------------
	// REWARD ACCUMULATORS
	//--------------------------------------------------------------------------

	float AccumulatedIndividualReward = 0.0f;
	float AccumulatedCoordinationReward = 0.0f;
	float AccumulatedMissionReward = 0.0f;

	//--------------------------------------------------------------------------
	// EVENT TRACKERS
	//--------------------------------------------------------------------------

	int32 KillsSinceLastUpdate = 0;
	float DamageSinceLastUpdate = 0.0f;
	float DamageTakenSinceLastUpdate = 0.0f;
	float LastMissionProgress = 0.0f;

	bool bDisobeyedMission = false;

	/** Recent combined fire records */
	TArray<FCombinedFireRecord> RecentCombinedFires;

	//--------------------------------------------------------------------------
	// COVER TRACKING (Sprint 6)
	//--------------------------------------------------------------------------

	/** Was agent in cover last tick? */
	bool bWasInCover = false;

	/** Was agent taking damage last tick? */
	bool bWasUnderFire = false;

	/** Time spent in cover this episode */
	float TimeInCover = 0.0f;

	//--------------------------------------------------------------------------
	// STRATEGY-SPECIFIC TRACKING (v5.0)
	//--------------------------------------------------------------------------

	/** Last distance to Mission (for advance tracking) */
	float LastDistanceToMission = 0.0f;

	/** Last distance to nearest enemy (for retreat tracking) */
	float LastDistanceToEnemy = 0.0f;

	/** Protected ally (for Support strategy) */
	UPROPERTY()
	AActor* ProtectedAlly = nullptr;

	/** Protected ally's last health (to detect protection success) */
	float ProtectedAllyLastHealth = 1.0f;

	/** Was agent in safe zone last tick? (for Retreat strategy) */
	bool bWasInSafeZone = false;

	/** Was agent on Mission last tick? (for Defend strategy) */
	bool bWasOnMission = false;

	//--------------------------------------------------------------------------
	// HYBRID REWARD TRACKING (v6.0 Fix)
	//--------------------------------------------------------------------------

	/** Previous observation for calculating reward deltas */
	FObservationElement PreviousObservation;
};
