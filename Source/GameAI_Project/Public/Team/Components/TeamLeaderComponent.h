#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Team/TeamTypes.h"
#include "Observation/TeamObservation.h"
#include "TeamLeaderComponent.generated.h"

// Forward declarations
class UMCTS;
class AObjectiveActor;
class USquadManagerComponent;
class UIntelManagerComponent;
class UStrategicPlannerComponent;
class UVisualLoggerComponent;

/**
 * Delegate for strategic decision events (v8.0 - Strategy Assignment)
 */
USTRUCT(BlueprintType)
struct FStrategyAssignmentMap
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	TArray<FStrategyAssignment> Assignments;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnStrategicDecisionMade,
	FStrategyAssignmentMap, Assignments
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnEventProcessed,
	EStrategicEvent, Event,
	bool, bTriggeredMCTS
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnFollowerRegistered,
	AActor*, Follower,
	int32, TotalFollowers
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnFollowerUnregistered,
	AActor*, Follower,
	int32, RemainingFollowers
);

/**
 * Strategic experience data (for future offline analysis)
 */
USTRUCT(BlueprintType)
struct FStrategicExperience
{
	GENERATED_BODY()

	/** Team observation when decision was made */
	UPROPERTY()
	TArray<float> StateFeatures;

	/** Commands issued (encoded as action index per follower) */
	UPROPERTY()
	TArray<int32> ActionsTaken;

	/** Episode outcome reward (+1 win, -1 loss, 0 draw) */
	UPROPERTY()
	float EpisodeReward = 0.0f;

	/** Step number when decision was made */
	UPROPERTY()
	int32 StepNumber = 0;

	/** Timestamp */
	UPROPERTY()
	float Timestamp = 0.0f;
};

/**
 * Team Leader Component - Strategic Coordinator (v9.0 Phase 3)
 *
 * ARCHITECTURE (Phase 3 - Coordinator Pattern):
 * This component has been refactored from a monolithic class into a coordinator
 * that delegates to specialized manager components:
 *
 * - SquadManagerComponent: Follower roster management
 * - IntelManagerComponent: Enemy tracking, objective management, observations
 * - StrategicPlannerComponent: MCTS-based strategic planning (async)
 * - VisualLoggerComponent: Centralized debug visualization
 *
 * Responsibilities (Refactored):
 * - Coordinate manager components
 * - Process strategic events
 * - Broadcast strategy assignments to followers
 * - Track team performance metrics
 * - Handle episode lifecycle
 *
 * Usage:
 * 1. Attach to an Actor (team leader character)
 * 2. Ensure manager components are attached to the same actor
 * 3. Followers register via RegisterFollower() (delegates to SquadManager)
 * 4. Followers signal events via ProcessStrategicEvent()
 * 5. StrategicPlanner runs MCTS and fires OnPlanReady delegate
 * 6. Coordinator applies assignments via ApplyStrategyAssignment()
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UTeamLeaderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTeamLeaderComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	
	//--------------------------------------------------------------------------
	// FOLLOWER MANAGEMENT
	//--------------------------------------------------------------------------

	/** Register a follower */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|Followers")
	bool RegisterFollower(AActor* Follower);

	/** Unregister a follower */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|Followers")
	void UnregisterFollower(AActor* Follower);

	/** Get squad max capacity - Phase 3: Delegate to SquadManager */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Followers")
	int32 GetMaxFollowers() const;

	/** Is follower registered? - Phase 3: Delegate to SquadManager */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Followers")
	bool IsFollowerRegistered(AActor* Follower) const;

	//--------------------------------------------------------------------------
	// EVENT PROCESSING
	//--------------------------------------------------------------------------

	/** Process a strategic event */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|Events")
	void ProcessStrategicEvent(
		EStrategicEvent Event,
		AActor* Instigator = nullptr,
		FVector Location = FVector::ZeroVector,
		int32 Priority = 5
	);

	/** Process event with full context */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|Events")
	void ProcessStrategicEventWithContext(const FStrategicEventContext& Context);

	/** Should this event trigger MCTS? */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Events")
	bool ShouldTriggerMCTS(const FStrategicEventContext& Context) const;

	//--------------------------------------------------------------------------
	// MCTS EXECUTION
	//--------------------------------------------------------------------------

	/** Build team observation */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|Observation")
	FTeamObservation BuildTeamObservation();

	/** Apply strategy assignment to followers (v8.0) */
	void ApplyStrategyAssignment(const TArray<FStrategyAssignment>& Assignments);

	/** Is MCTS currently running? */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Debug")
	bool IsRunningMCTS() const;

	//--------------------------------------------------------------------------
	// ENEMY TRACKING - Phase 3: Delegate to IntelManager
	//--------------------------------------------------------------------------

	/** Register enemy for tracking (delegates to IntelManager) */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|Intel")
	void RegisterEnemy(AActor* Enemy);

	/** Unregister enemy from tracking (delegates to IntelManager) */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|Intel")
	void UnregisterEnemy(AActor* Enemy);

	//--------------------------------------------------------------------------
	// METRICS & DEBUGGING
	//--------------------------------------------------------------------------

	/** Get team performance metrics */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Metrics")
	FTeamMetrics GetTeamMetrics() const;

	/** Draw debug info */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|Debug")
	void DrawDebugInfo();

	/** Get pending events count */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Debug")
	int32 GetPendingEventsCount() const { return PendingEvents.Num(); }


	//--------------------------------------------------------------------------
	// v9.0: Follower
	//--------------------------------------------------------------------------
	


	//--------------------------------------------------------------------------
	// v9.0: OBJECTIVE ACCESSORS - Phase 3: Delegate to IntelManager
	//--------------------------------------------------------------------------

	/** Get friendly objective (for Defend strategy) */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Objectives")
	AObjectiveActor* GetFriendlyObjective() const;

	/** Get hostile objective (for Assault/Support strategies) */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Objectives")
	AObjectiveActor* GetHostileObjective() const;

	//--------------------------------------------------------------------------
	// EPISODE MANAGEMENT
	/** Handle episode start */
	UFUNCTION()
	void OnEpisodeStart(int32 EnvironmentID, int32 EpisodeNumber);

	/** Handle episode completion */
	UFUNCTION()
	void OnEpisodeComplete(int32 EnvironmentID, const FEpisodeResult& Result);



private:
	/** Process pending events */
	void ProcessPendingEvents();

	/** Get objectives array for MCTS (Phase 3: Delegate to IntelManager) */
	TArray<AObjectiveActor*> GetObjectivesArray() const;

	/** Phase 3: Handler for StrategicPlanner completion (v9.0) */
	UFUNCTION()
	void OnPlanReady(const TArray<FStrategyAssignment>& Assignments, float ExecutionTimeMs, FString BatchKey);

	/** Phase 3: Handler for SquadManager follower registration (v9.0) */
	UFUNCTION()
	void OnSquadFollowerRegistered(AActor* Follower);

	UFUNCTION()
	void HandleObjectivesDiscovered();

	UFUNCTION()
	void BroadcastTacticalContext();

	UFUNCTION()
	void OnNewFollowerJoined(AActor* NewFollower);

	void PushContextToFollower(AActor* Follower, AObjectiveActor* Friendly, AObjectiveActor* Hostile);

public:
	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------
	/** Event priority threshold to trigger MCTS (0-10) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Config")
	int32 EventPriorityThreshold = 5;

	/** Enable continuous planning (time-sliced MCTS) instead of event-driven */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Planning")
	bool bContinuousPlanning = true;

	/** Interval for continuous planning (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Planning")
	float ContinuousPlanningInterval = 30.0f;

	/** Allow critical events to interrupt continuous planning */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Planning")
	bool bAllowEventInterrupts = true;

	/** Team ID for SimulationManager registration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Config")
	int32 TeamID = 0;

	/** Team name/ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Config")
	FString TeamName = TEXT("Alpha Team");

	/** Team color (for visualization) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Config")
	FLinearColor TeamColor = FLinearColor::Blue;

	/** Enable debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Debug")
	bool bEnableDebugDrawing = false;

	/** Auto-register with SimulationManager on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Config")
	bool bAutoRegisterWithSimManager = true;

	// v8.0: Objectives discovered dynamically in DiscoverWorldObjectives()

	//--------------------------------------------------------------------------
	// STATE (Phase 3: Coordinator-specific state only)
	//--------------------------------------------------------------------------

	/** Current strategy assignments for each follower (v8.0) */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|State")
	TMap<AActor*, FStrategyAssignment> CurrentAssignments;

	/** Pending events (queued for processing) */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|State")
	TArray<FStrategicEventContext> PendingEvents;

	/** Current team observation */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|State")
	FTeamObservation CurrentTeamObservation;

	//--------------------------------------------------------------------------
	// COMPONENTS
	//--------------------------------------------------------------------------

	/** Phase 3: Manager Components (v9.0 Coordinator Pattern) */

	/** Squad management (follower roster) */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|Components")
	USquadManagerComponent* SquadManager = nullptr;

	/** Intelligence gathering (enemy tracking, objectives, observations) */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|Components")
	UIntelManagerComponent* IntelManager = nullptr;

	/** Strategic planning (MCTS-based decision making) */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|Components")
	UStrategicPlannerComponent* StrategicPlanner = nullptr;

	/** Debug visualization (centralized drawing) */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|Components")
	UVisualLoggerComponent* VisualLogger = nullptr;


	//--------------------------------------------------------------------------
	// EVENTS
	//--------------------------------------------------------------------------

	/** Fired when strategic decision is made */
	UPROPERTY(BlueprintAssignable, Category = "Team Leader|Events")
	FOnStrategicDecisionMade OnStrategicDecisionMade;

	/** Fired when event is processed */
	UPROPERTY(BlueprintAssignable, Category = "Team Leader|Events")
	FOnEventProcessed OnEventProcessed;

	/** Fired when follower is registered */
	UPROPERTY(BlueprintAssignable, Category = "Team Leader|Events")
	FOnFollowerRegistered OnFollowerRegistered;

	/** Fired when follower is unregistered */
	UPROPERTY(BlueprintAssignable, Category = "Team Leader|Events")
	FOnFollowerUnregistered OnFollowerUnregistered;


private:
	// Phase 3: REMOVED - AsyncMCTSTask now in StrategicPlanner (TUniquePtr with RAII)

	/** Statistics tracking */
	int32 TotalCommandsIssued = 0;
	int32 TotalEnemiesEliminated = 0;
	int32 TotalFollowersLost = 0;

	/** Time since last formation distance log (for proximity diagnosis) */
	float TimeSinceLastFormationLog = 0.0f;

	/** Time since last planning cycle (v3.0 Sprint 6 - Continuous Planning) */
	float TimeSinceLastPlanning = 0.0f;

	/** Rolling average of MCTS execution times (v3.0 Sprint 6 - Performance Profiling) */
	float AverageMCTSExecutionTime = 0.0f;

	/** Count of MCTS executions for averaging */
	int32 MCTSExecutionCount = 0;

	/** SimulationManager�� ������ ���������� ��ϵǾ����� ���� */
	bool bIsRegisteredToManager = false;

	// Phase 3: REMOVED - PendingFollowerRegistration now in SquadManager

	FString CurrentBatchKey;
};
