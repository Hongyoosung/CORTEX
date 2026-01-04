#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RL/RLTypes.h"  // v5.0: EStrategyType, FStrategyRewardWeights
#include "RewardCalculator.generated.h"

class UFollowerAgentComponent;
class UHealthComponent;
class UObjective;

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
 * Unified Hierarchical Reward System (Sprint 5)
 *
 * Combines individual tactical rewards with team coordination bonuses
 * and strategic objective rewards to align MCTS and RL objectives.
 *
 * Reward Structure:
 * - Individual rewards: Combat events (kill +10, damage +5, take damage -5, death -10)
 * - Coordination bonuses: Strategic kill +15, combined fire +10, formation +5, disobey -15
 * - Strategic rewards: Objective complete +50, enemy wipe +30, own wipe -30, objective lost -30
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

	/** Calculate total hierarchical reward (combines all reward sources) */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float CalculateTotalReward(float DeltaTime);

	/** Calculate individual combat rewards */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float CalculateIndividualReward();

	/** Calculate team coordination bonuses */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float CalculateCoordinationReward();

	/** Calculate objective-based rewards */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float CalculateObjectiveReward();

	/** Calculate efficiency penalties (time pressure, low health) */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float CalculateEfficiencyPenalty(float DeltaTime);

	/** Calculate cover usage rewards (Sprint 6) */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	float CalculateCoverReward();

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

	/** Track objective completion */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnObjectiveComplete(UObjective* Objective);

	/** Track objective failure */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void OnObjectiveFailed(UObjective* Objective);

	/** Set current objective for reward tracking */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void SetCurrentObjective(UObjective* Objective);

	/** Set current strategy for reward calculation (v5.0) */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void SetCurrentStrategy(EStrategyType Strategy);

	/** Get current strategy */
	UFUNCTION(BlueprintPure, Category = "Reward")
	EStrategyType GetCurrentStrategy() const { return CurrentStrategy; }

	//--------------------------------------------------------------------------
	// COORDINATION TRACKING
	//--------------------------------------------------------------------------

	/** Check if agent is currently on objective */
	UFUNCTION(BlueprintPure, Category = "Reward")
	bool IsOnObjective() const;

	/** Check if agent is in formation with teammates */
	UFUNCTION(BlueprintPure, Category = "Reward")
	bool IsInFormation() const;

	/** Register combined fire event (multiple agents targeting same enemy) */
	void RegisterCombinedFire(AActor* Target);

	/** Check if agent is complying with current objective */
	void CheckObjectiveCompliance();

	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** Reward weight for individual combat */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Weights")
	float IndividualRewardWeight = 1.0f;

	/** Reward weight for coordination bonuses */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Weights")
	float CoordinationRewardWeight = 1.0f;

	/** Reward weight for objectives */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Weights")
	float ObjectiveRewardWeight = 1.0f;

	//--------------------------------------------------------------------------
	// STRATEGY-SPECIFIC REWARD CONSTANTS (v5.0)
	//--------------------------------------------------------------------------

	// Assault Strategy Rewards
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Assault")
	float Assault_KillReward = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Assault")
	float Assault_DamageMultiplier = 0.01f;  // +0.1 per 10 damage

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Assault")
	float Assault_AdvanceReward = 0.5f;  // Per second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Assault")
	float Assault_ObjectiveReachReward = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Assault")
	float Assault_DeathPenalty = -8.0f;

	// Defend Strategy Rewards
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Defend")
	float Defend_KillReward = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Defend")
	float Defend_HoldPositionReward = 0.3f;  // Per second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Defend")
	float Defend_SuppressReward = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Defend")
	float Defend_LeavePositionPenalty = -2.0f;  // Per second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Defend")
	float Defend_DeathPenalty = -12.0f;

	// Support Strategy Rewards
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Support")
	float Support_AllyProtectedReward = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Support")
	float Support_KillThreatReward = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Support")
	float Support_MaintainDistanceReward = 0.2f;  // Per second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Support")
	float Support_AllyDeathPenalty = -20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Support")
	float Support_DrawFireReward = 5.0f;

	// Retreat Strategy Rewards
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Retreat")
	float Retreat_DistanceIncreaseReward = 0.3f;  // Per second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Retreat")
	float Retreat_SafeZoneReward = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Retreat")
	float Retreat_CoveringFireReward = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Retreat")
	float Retreat_SurvivalReward = 5.0f;  // Per episode

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Retreat")
	float Retreat_DeathPenalty = -15.0f;

	// Strategy-specific distance thresholds
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Support")
	float Support_MinDistance = 500.0f;  // 5m

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Support")
	float Support_MaxDistance = 1500.0f;  // 15m

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Strategy|Retreat")
	float Retreat_SafeDistance = 2000.0f;  // 20m from enemies

	/** Time window for combined fire detection (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Config")
	float CombinedFireWindow = 2.0f;

	/** Radius to consider "on objective" (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Config")
	float ObjectiveRadiusThreshold = 1000.0f;

	/** Distance threshold for formation detection (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Config")
	float FormationDistanceThreshold = 1500.0f;

	/** Reward for using cover when under fire (Sprint 6) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Cover")
	float CoverUnderFireReward = 10.0f;

	/** Penalty for being exposed when enemies are visible (Sprint 6) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Cover")
	float ExposedPenalty = -5.0f;

	/** Reward for crouching in cover (Sprint 6) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Cover")
	float CrouchInCoverReward = 5.0f;

	/** Distance to cover to consider "in cover" (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Cover")
	float CoverDistanceThreshold = 200.0f;

private:
	//--------------------------------------------------------------------------
	// COMPONENT REFERENCES
	//--------------------------------------------------------------------------

	UPROPERTY()
	UFollowerAgentComponent* FollowerComponent = nullptr;

	UPROPERTY()
	UHealthComponent* HealthComponent = nullptr;

	UPROPERTY()
	UObjective* CurrentObjective = nullptr;

	/** Current strategy (v5.0) - affects reward calculation */
	EStrategyType CurrentStrategy = EStrategyType::Assault;

	//--------------------------------------------------------------------------
	// REWARD ACCUMULATORS
	//--------------------------------------------------------------------------

	float AccumulatedIndividualReward = 0.0f;
	float AccumulatedCoordinationReward = 0.0f;
	float AccumulatedObjectiveReward = 0.0f;

	//--------------------------------------------------------------------------
	// EVENT TRACKERS
	//--------------------------------------------------------------------------

	int32 KillsSinceLastUpdate = 0;
	float DamageSinceLastUpdate = 0.0f;
	float DamageTakenSinceLastUpdate = 0.0f;
	float LastObjectiveProgress = 0.0f;

	bool bDisobeyedObjective = false;

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

	/** Last distance to objective (for advance tracking) */
	float LastDistanceToObjective = 0.0f;

	/** Last distance to nearest enemy (for retreat tracking) */
	float LastDistanceToEnemy = 0.0f;

	/** Protected ally (for Support strategy) */
	UPROPERTY()
	AActor* ProtectedAlly = nullptr;

	/** Protected ally's last health (to detect protection success) */
	float ProtectedAllyLastHealth = 1.0f;

	/** Was agent in safe zone last tick? (for Retreat strategy) */
	bool bWasInSafeZone = false;

	/** Was agent on objective last tick? (for Defend strategy) */
	bool bWasOnObjective = false;
};
