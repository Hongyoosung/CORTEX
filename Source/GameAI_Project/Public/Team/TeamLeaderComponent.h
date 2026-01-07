#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TeamTypes.h"
#include "Observation/TeamObservation.h"
#include "TeamLeaderComponent.generated.h"

// Forward declarations
class UTeamCommunicationManager;
class UMCTS;
class UObjectiveManager;
class UObjective;

/**
 * Delegate for strategic decision events (v3.0)
 */
USTRUCT(BlueprintType)
struct FObjectiveAssignmentMap
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	TMap<AActor*, UObjective*> Objectives;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnStrategicDecisionMade,
	FObjectiveAssignmentMap, Objectives
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
 * Team Leader Component - Strategic Decision Making
 *
 * Responsibilities:
 * - Manage team of follower agents
 * - Process strategic events
 * - Run event-driven MCTS for team-level decisions
 * - Issue commands to followers
 * - Track team performance
 *
 * Usage:
 * 1. Attach to an Actor (player, AI, or dedicated manager)
 * 2. Register followers via RegisterFollower()
 * 3. Followers signal events via ProcessStrategicEvent()
 * 4. Leader runs MCTS and issues commands to followers
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

	/** Get all followers */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Followers")
	TArray<AActor*> GetFollowers() const { return Followers; }


	/** Get alive followers */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|Followers")
	TArray<AActor*> GetAliveFollowers() const;

	/** Get follower count */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Followers")
	int32 GetFollowerCount() const { return Followers.Num(); }

	/** Is follower registered? */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Followers")
	bool IsFollowerRegistered(AActor* Follower) const { return Followers.Contains(Follower); }

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


	/** Run objective-based decision-making (sync) - v6.0 MCTS Coordination */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|MCTS")
	void RunObjectiveDecisionMaking();

	/** Run objective-based decision-making (async) - v6.0 MCTS Coordination */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|MCTS")
	void RunObjectiveDecisionMakingAsync();

	/** Apply objective assignment to followers (v6.0) */
	void ApplyObjectiveAssignment(const FObjectiveAssignment& Assignment);

	UFUNCTION(BlueprintCallable, Category = "Team Leader|MCTS")
	bool IsMCTSRunning() const { return bMCTSRunning; }

	UFUNCTION(BlueprintCallable, Category = "Team Leader|MCTS")
	float GetLastMCTSDecisionTime() const { return LastMCTSTime; }

	//--------------------------------------------------------------------------
	// ENEMY TRACKING
	//--------------------------------------------------------------------------

	/** Register an enemy actor */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|Enemies")
	void RegisterEnemy(AActor* Enemy);

	/** Unregister an enemy actor */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|Enemies")
	void UnregisterEnemy(AActor* Enemy);

	/** Get known enemies */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Enemies")
	TArray<AActor*> GetKnownEnemies() const;

	/** Clear all known enemies (e.g., on episode reset) */
	UFUNCTION(BlueprintCallable, Category = "Team Leader|Enemies")
	void ClearKnownEnemies();


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

	/** Is MCTS currently running? */
	UFUNCTION(BlueprintPure, Category = "Team Leader|Debug")
	bool IsRunningMCTS() const { return bMCTSRunning; }

	/**
	 * 태그를 기반으로 액터를 찾고 목표를 등록하는 공용 헬퍼 함수
	 * @param TagName 검색할 액터의 태그
	 * @param ObjectiveCreator 각 액터에 대해 목표를 생성하는 로직을 담은 람다 함수 (nullptr 반환 시 스킵)
	 * @return 발견 및 등록된 목표의 수
	 */
	int32 FindAndRegisterObjectives(FName TagName, TFunctionRef<UObjective* (AActor*)> ObjectiveCreator);


private:
	/** Process pending events */
	void ProcessPendingEvents();

	/** Check if MCTS cooldown has expired */
	bool IsMCTSOnCooldown() const;

	/** Initialize MCTS engine */
	void InitializeMCTS();

	/** Discover objectives from tagged actors in the level (v3.0) */
	void DiscoverWorldObjectives();


public:
	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** Maximum number of followers this leader can command */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Config")
	int32 MaxFollowers = 4;

	/** MCTS simulations per strategic decision */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|MCTS")
	int32 MCTSSimulations = 500;

	/** Run MCTS asynchronously (recommended) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|MCTS")
	bool bAsyncMCTS = true;

	/** Minimum time between MCTS runs (seconds, prevents spam) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|MCTS")
	float MCTSCooldown = 2.0f;

	/** Event priority threshold to trigger MCTS (0-10) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Config")
	int32 EventPriorityThreshold = 5;

	//--------------------------------------------------------------------------
	// CONTINUOUS PLANNING (v3.0 Sprint 6)
	//--------------------------------------------------------------------------

	/** Enable continuous planning (time-sliced MCTS) instead of event-driven */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Planning")
	bool bContinuousPlanning = true;

	/** Interval for continuous planning (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Planning")
	float ContinuousPlanningInterval = 1.5f;

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

	/** Objective actor for the team */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Leader|Config")
	AActor* ObjectiveActor = nullptr;

	//--------------------------------------------------------------------------
	// STATE
	//--------------------------------------------------------------------------

	/** Registered followers */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|State")
	TArray<AActor*> Followers;

	/** Current objectives for each follower (v3.0) */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|State")
	TMap<AActor*, UObjective*> CurrentObjectives;

	/** Is MCTS currently running? */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|State")
	bool bMCTSRunning = false;

	/** Time of last MCTS execution */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|State")
	float LastMCTSTime = 0.0f;

	/** Pending events (queued for processing) */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|State")
	TArray<FStrategicEventContext> PendingEvents;

	/** Current team observation */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|State")
	FTeamObservation CurrentTeamObservation;

	/** Known enemy actors */
	UPROPERTY(BlueprintReadOnly, Category = "Team Leader|State")
	TSet<AActor*> KnownEnemies;

	//--------------------------------------------------------------------------
	// COMPONENTS
	//--------------------------------------------------------------------------

	/** MCTS decision engine */
	UPROPERTY()
	UMCTS* StrategicMCTS;

	/** Objective manager (v3.0 Combat Refactoring) */
	UPROPERTY(BlueprintReadWrite, Category = "Team Leader|Components")
	UObjectiveManager* ObjectiveManager;

	/** Curriculum manager for MCTS-guided RL training (v3.0 Sprint 3) */
	UPROPERTY(BlueprintReadWrite, Category = "Team Leader|Components")
	class UCurriculumManager* CurriculumManager;

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
	/** Async task for MCTS (using FAsyncTask to check completion and get results) */
	FAsyncTask<class FMCTSAsyncTask>* AsyncMCTSTask;

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
};
