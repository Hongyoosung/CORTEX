#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TeamTypes.h"
#include "Observation/ObservationElement.h"
#include "RL/RLTypes.h"
#include "Observation/TeamObservation.h"
#include "Simulation/StateTransition.h"
#include "FollowerAgentComponent.generated.h"

// Forward declarations
class UTeamLeaderComponent;
class URLPolicyNetwork;
class URewardCalculator;
class UObjective;
struct FDamageEventData;
struct FDeathEventData;

/**
 * Delegate for follower events (v3.0)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnObjectiveReceived,
	UObjective*, Objective
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnEventSignaled,
	EStrategicEvent, Event,
	AActor*, Instigator,
	int32, Priority
);

/**
 * Follower Agent Component - Tactical Execution
 *
 * Responsibilities:
 * - Receive strategic commands from team leader
 * - Transition FSM based on commands
 * - Maintain local observation (71 features)
 * - Signal strategic events to leader
 * - Execute commands via Behavior Tree
 *
 * Usage:
 * 1. Attach to an AI-controlled Actor (e.g., AGameAICharacter)
 * 2. Set TeamLeader reference
 * 3. Component will automatically register with leader
 * 4. Leader will issue commands, component will execute
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UFollowerAgentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFollowerAgentComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;


	//--------------------------------------------------------------------------
	// TEAM LEADER COMMUNICATION
	//--------------------------------------------------------------------------

	/** Register with team leader */
	UFUNCTION(BlueprintCallable, Category = "Follower|Team")
	bool RegisterWithTeamLeader();

	/** Unregister from team leader */
	UFUNCTION(BlueprintCallable, Category = "Follower|Team")
	void UnregisterFromTeamLeader();

	/** Signal event to team leader */
	UFUNCTION(BlueprintCallable, Category = "Follower|Team")
	void SignalEventToLeader(
		EStrategicEvent Event,
		AActor* Instigator = nullptr,
		FVector Location = FVector::ZeroVector,
		int32 Priority = 5
	);



	//--------------------------------------------------------------------------
	// OBJECTIVE EXECUTION (v3.0)
	//--------------------------------------------------------------------------

	/** Get current objective assigned by leader */
	UFUNCTION(BlueprintPure, Category = "Follower|Objective")
	UObjective* GetCurrentObjective() const { return CurrentObjective; }

	/** Has active objective? */
	UFUNCTION(BlueprintPure, Category = "Follower|Objective")
	bool HasActiveObjective() const;

	//--------------------------------------------------------------------------
	// INDIVIDUAL STRATEGY (v5.0)
	//--------------------------------------------------------------------------

	/** Get current strategy assigned by leader (determines which policy head to use) */
	UFUNCTION(BlueprintPure, Category = "Follower|Strategy")
	EStrategyType GetCurrentStrategy() const { return CurrentStrategy; }

	/** Get ally context for support strategy (v5.0) */
	UFUNCTION(BlueprintPure, Category = "Follower|Strategy")
	FAllyContext GetAllyContext() const { return CachedAllyContext; }

	//--------------------------------------------------------------------------
	// STATE MANAGEMENT
	//--------------------------------------------------------------------------

	/** Mark follower as dead */
	UFUNCTION(BlueprintCallable, Category = "Follower|State")
	void MarkAsDead();

	/** Mark follower as alive (e.g., respawn) */
	UFUNCTION(BlueprintCallable, Category = "Follower|State")
	void MarkAsAlive();

	//--------------------------------------------------------------------------
	// OBSERVATION
	//--------------------------------------------------------------------------

	/** Update local observation */
	UFUNCTION(BlueprintCallable, Category = "Follower|Observation")
	void UpdateLocalObservation(const FObservationElement& NewObservation);

	/** Get local observation */
	UFUNCTION(BlueprintPure, Category = "Follower|Observation")
	FObservationElement GetLocalObservation() const { return LocalObservation; }

	/** Build observation from current actor state */
	UFUNCTION(BlueprintCallable, Category = "Follower|Observation")
	FObservationElement BuildLocalObservation();

	/** Build objective context from assigned objective (v6.0) */
	UFUNCTION(BlueprintCallable, Category = "Follower|Observation")
	FObjectiveContext BuildObjectiveContext(UObjective* Objective);

	/** Find nearest cover position relative to enemies */
	UFUNCTION(BlueprintCallable, Category = "Follower|Tactical")
	bool FindNearestCover(FVector& OutCoverLocation, float& OutDistance, const TArray<AActor*>& Enemies);

	//--------------------------------------------------------------------------
	// REINFORCEMENT LEARNING
	//--------------------------------------------------------------------------

	/** Provide reward feedback to RL policy */
	UFUNCTION(BlueprintCallable, Category = "Follower|RL")
	void ProvideReward(float Reward, bool bTerminal = false);

	/** Accumulate reward (alias for ProvideReward for compatibility) */
	UFUNCTION(BlueprintCallable, Category = "Follower|RL")
	void AccumulateReward(float Reward) { ProvideReward(Reward, false); }

	/** Get accumulated reward this episode */
	UFUNCTION(BlueprintPure, Category = "Follower|RL")
	float GetAccumulatedReward() const { return AccumulatedReward; }

	/** Reset episode (clear accumulated reward) */
	UFUNCTION(BlueprintCallable, Category = "Follower|RL")
	void ResetEpisode();

	/** Called when episode ends - assigns terminal reward and marks experiences */
	UFUNCTION(BlueprintCallable, Category = "Follower|RL")
	void OnEpisodeEnded(float EpisodeReward);

	/** Is tactical policy ready for queries? */
	UFUNCTION(BlueprintPure, Category = "Follower|RL")
	bool IsTacticalPolicyReady() const { return TacticalPolicy != nullptr; }

	/** Get tactical policy (nullptr if not set) */
	UFUNCTION(BlueprintPure, Category = "Follower|RL")
	URLPolicyNetwork* GetTacticalPolicy() const { return TacticalPolicy; }

	/** Set current objective for reward calculation (Sprint 4) */
	UFUNCTION(BlueprintCallable, Category = "Follower|RL")
	void SetCurrentObjective(UObjective* Objective);


	//--------------------------------------------------------------------------
	// UTILITY
	//--------------------------------------------------------------------------

	/** Get team leader */
	UFUNCTION(BlueprintPure, Category = "Follower|Team")
	UTeamLeaderComponent* GetTeamLeader() const { return TeamLeader; }

	/** Get reward calculator (Sprint 5) */
	UFUNCTION(BlueprintPure, Category = "Follower|RL")
	URewardCalculator* GetRewardCalculator() const { return RewardCalculator; }

	/** Is follower alive? */
	UFUNCTION(BlueprintPure, Category = "Follower|State")
	bool GetIsAlive() const { return bIsAlive; }

	/** Is registered with team leader? */
	UFUNCTION(BlueprintPure, Category = "Follower|Team")
	bool IsRegisteredWithLeader() const;

	/** Draw debug info */
	UFUNCTION(BlueprintCallable, Category = "Follower|Debug")
	void DrawDebugInfo();



private:
	/** Combat event handlers */
	UFUNCTION()
	void OnDamageTakenEvent(const FDamageEventData& DamageEvent, float CurrentHealth);

	UFUNCTION()
	void OnDamageDealtEvent(AActor* Victim, float DamageAmount);

	UFUNCTION()
	void OnKillEvent(AActor* Victim, float TotalDamage);

	UFUNCTION()
	void OnDeathEvent(const FDeathEventData& DeathEvent);


public:
	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** Team leader actor (will find TeamLeaderComponent on this actor) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Config")
	AActor* TeamLeaderActor = nullptr;

	/** Team leader component reference (auto-set from TeamLeaderActor) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|Config")
	UTeamLeaderComponent* TeamLeader = nullptr;

	/** Automatically register with team leader on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Config")
	bool bAutoRegisterWithLeader = true;

	/** Auto-find team leader by tag (if TeamLeaderActor not set) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Config")
	FName TeamLeaderTag = TEXT("TeamLeader");

	/** RL policy for tactical action selection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Components")
	URLPolicyNetwork* TacticalPolicy = nullptr;

	/** Reward calculator for hierarchical rewards (Sprint 4) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|Components")
	URewardCalculator* RewardCalculator = nullptr;

	/** Enable RL policy (if false, uses rule-based fallback) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|RL")
	bool bUseRLPolicy = true;

	/** Collect experiences for offline training */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|RL")
	bool bCollectExperiences = true;

	/** Enable debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Debug")
	bool bEnableDebugDrawing = false;

	//--------------------------------------------------------------------------
	// COVER DETECTION CONFIG
	//--------------------------------------------------------------------------

	/** Cover search radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Cover")
	float CoverSearchRadius = 1000.0f;

	/** Cover search angular samples (rays around agent) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Cover")
	int32 CoverSearchSamples = 8;

	/** Minimum obstacle size to qualify as cover (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Cover")
	float MinCoverHeight = 100.0f;

	/** Cover query update interval (seconds) - cache results to avoid per-tick raycasts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Cover")
	float CoverQueryInterval = 0.5f;

	//--------------------------------------------------------------------------
	// STATE
	//--------------------------------------------------------------------------

	/** Current objective from leader (v3.0) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|State")
	TObjectPtr<UObjective> CurrentObjective = nullptr;

	/** Current strategy from leader (v5.0) - determines which policy head to use */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|State")
	EStrategyType CurrentStrategy = EStrategyType::Assault;

	/** Cached ally context for support strategy observation (v5.0) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|State")
	FAllyContext CachedAllyContext;

	/** Local observation (71 features) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|State")
	FObservationElement LocalObservation;

	/** Is follower alive? */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|State")
	bool bIsAlive = true;

	/** Time since last tactical action was taken (seconds) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|RL")
	float TimeSinceLastTacticalAction = 0.0f;

	/** Previous observation (for experience collection) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|RL")
	FObservationElement PreviousObservation;

	/** Last action taken (v6.0 - simplified to strategy only) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|RL")
	FMacroAction LastAction;

	/** Accumulated reward this episode */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|RL")
	float AccumulatedReward = 0.0f;

	//--------------------------------------------------------------------------
	// EVENTS
	//--------------------------------------------------------------------------

	/** Fired when objective is received from leader */
	UPROPERTY(BlueprintAssignable, Category = "Follower|Events")
	FOnObjectiveReceived OnObjectiveReceived;

	/** Fired when event is signaled to leader */
	UPROPERTY(BlueprintAssignable, Category = "Follower|Events")
	FOnEventSignaled OnEventSignaled;



private:
	//--------------------------------------------------------------------------
	// STATE TRANSITION LOGGING (Sprint 2)
	//--------------------------------------------------------------------------

	/** Enable state transition logging */
	bool bLogStateTransitions = false;

	/** Previous team observation (for transition logging) */
	FTeamObservation PreviousTeamObservation;

	/** Logged state transitions */
	TArray<FStateTransitionSample> LoggedTransitions;

	/** Time of last state log */
	float LastStateLogTime = 0.0f;

	/** Minimum time between state logs (seconds) */
	float StateLogInterval = 1.0f;

	//--------------------------------------------------------------------------
	// COVER CACHE (Sprint 6)
	//--------------------------------------------------------------------------

	/** Cached cover location */
	FVector CachedCoverLocation = FVector::ZeroVector;

	/** Cached cover distance */
	float CachedCoverDistance = 9999.0f;

	/** Is cached cover valid? */
	bool bHasCachedCover = false;

	/** Last cover query time */
	float LastCoverQueryTime = 0.0f;

	//--------------------------------------------------------------------------
	// PERFORMANCE PROFILING (Sprint 6)
	//--------------------------------------------------------------------------

	/** Time spent building observations this episode (ms) */
	float TotalObservationTime = 0.0f;

	/** Time spent on cover queries this episode (ms) */
	float TotalCoverQueryTime = 0.0f;

	/** Number of cover queries this episode */
	int32 CoverQueriesThisEpisode = 0;
};
