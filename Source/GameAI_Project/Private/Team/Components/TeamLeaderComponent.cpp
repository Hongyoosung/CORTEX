#include "Team/Components/TeamLeaderComponent.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/ObjectiveActor.h"  // v8.0: For durability-based objectives
#include "AI/MCTS/MCTS.h"
#include "AI/MCTS/MCTSAsyncTask.h"

// Phase 3: Manager Component Includes (v9.0)
#include "Team/Components/IntelManagerComponent.h"
#include "Team/Components/StrategicPlannerComponent.h"
#include "Util/Components/VisualLoggerComponent.h"
#include "Observation/ObservationElement.h"
#include "RL/RLTypes.h"  // v8.0: EStrategyType, FStrategyAssignment
#include "RL/RLPolicyNetwork.h"  // v8.0: For value estimates
#include "Core/SimulationManagerGameMode.h"
#include "Actor/LeaderCharacter.h"
#include "DrawDebugHelpers.h"
#include "Async/Async.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "RL/Components/RewardCalculator.h"
#include "Kismet/GameplayStatics.h"
#include "Combat/Components/HealthComponent.h"
#include "TimerManager.h"

//==============================================================================
// v8.0: TEAM LEADER COMPONENT
// MCTS assigns Strategies directly (not Missions) → RL outputs tactical params → Rules execute
//==============================================================================

UTeamLeaderComponent::UTeamLeaderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.5f;  // Update every 0.5s
}

void UTeamLeaderComponent::BeginPlay()
{
	Super::BeginPlay();

	//==========================================================================
	// Phase 3-5: Resolve Manager Components (v9.0 Coordinator Pattern)
	// Phase 5: LeaderCharacter merged into LeaderCharacter
	//==========================================================================
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamLeader] BeginPlay: No owner actor!"));
		return;
	}

	// v9.0 PHASE 5: Verify owner is LeaderCharacter
	LeaderCharacter = Cast<ALeaderCharacter>(Owner);
	if (!LeaderCharacter)
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamLeader] %s: Owner is not a LeaderCharacter!"), *Owner->GetName());
		return;
	}

	IntelManager = Owner->FindComponentByClass<UIntelManagerComponent>();
	StrategicPlanner = Owner->FindComponentByClass<UStrategicPlannerComponent>();
	VisualLogger = Owner->FindComponentByClass<UVisualLoggerComponent>();

	// Verify required components exist
	if (!IntelManager || !StrategicPlanner)
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamLeader] %s: Missing required manager components! Intel=%s Planner=%s"),
			*Owner->GetName(),
			IntelManager ? TEXT("OK") : TEXT("MISSING"),
			StrategicPlanner ? TEXT("OK") : TEXT("MISSING"));
		return;
	}


	UE_LOG(LogTemp, Display, TEXT("✅ [TeamLeader Phase5] '%s': All manager components resolved"), *TeamName);

	//==========================================================================
	// Initialize Manager Components
	//==========================================================================

	IntelManager->OnObjectivesDiscovered.AddDynamic(this, &UTeamLeaderComponent::HandleObjectivesDiscovered);

	// Initialize StrategicPlanner with comprehensive config
	StrategicPlanner->InitializeMCTS();
	StrategicPlanner->OnPlanReady.AddDynamic(this, &UTeamLeaderComponent::OnPlanReady);

	// v9.0 PHASE 5: LeaderCharacter events removed (merged into LeaderCharacter)
	// Follower registration now handled directly by LeaderCharacter methods

	// Configure IntelManager
	IntelManager->TeamID = TeamID;


	//==========================================================================
	// Comprehensive Configuration Log (v9.0 Phase 5)
	//==========================================================================
	UE_LOG(LogTemp, Display, TEXT("✅ [TeamLeader Phase5] '%s' Configuration:"), *TeamName);
	UE_LOG(LogTemp, Display, TEXT("   ├─ Team: ID=%d, Color=(%s)"), TeamID, *TeamColor.ToString());
	UE_LOG(LogTemp, Display, TEXT("   ├─ Squad: CurrentFollowers=%d"), LeaderCharacter->GetFollowerCount());
	UE_LOG(LogTemp, Display, TEXT("   ├─ Intel: TeamID=%d"), IntelManager->TeamID);
	UE_LOG(LogTemp, Display, TEXT("   ├─ Planner: Simulations=%d, Async=%s"),
		StrategicPlanner->MCTSSimulations, StrategicPlanner->bAsyncMCTS ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogTemp, Display, TEXT("   ├─ Planning: Continuous=%s, Interval=%.1fs, AllowInterrupts=%s"),
		bContinuousPlanning ? TEXT("YES") : TEXT("NO"),
		ContinuousPlanningInterval,
		bAllowEventInterrupts ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogTemp, Display, TEXT("   └─ Policy: EventPriorityThreshold=%d"), EventPriorityThreshold);

	//==========================================================================
	// SimulationManager Registration (unchanged)
	//==========================================================================
	if (bAutoRegisterWithSimManager)
	{
		ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
		if (SimManager)
		{
			// Register team
			if (SimManager->RegisterTeam(TeamID, this, TeamName, TeamColor))
			{
				bIsRegisteredToManager = true;
				UE_LOG(LogTemp, Warning, TEXT("✅ TeamLeader '%s': Registered with SimulationManager"), *TeamName);

				// Register environment
				int32 EnvironmentID = TeamID / 2;
				SimManager->RegisterTeamEnvironment(TeamID, EnvironmentID);

				// Note: Pending follower registrations now handled automatically by LeaderCharacter
			}
			SimManager->OnEpisodeStarted.AddDynamic(this, &UTeamLeaderComponent::OnEpisodeStart);
			SimManager->OnEpisodeEnded.AddDynamic(this, &UTeamLeaderComponent::OnEpisodeComplete);
		}
	}

	//==========================================================================
	// Delayed Objective Discovery (delegated to IntelManager)
	//==========================================================================
	FTimerHandle DelayedDiscoveryTimer;
	GetWorld()->GetTimerManager().SetTimer(
		DelayedDiscoveryTimer,
		[this]()
		{
			if (IntelManager)
			{
				IntelManager->DiscoverWorldObjectives();
			}
		},
		0.3f,  // 0.3s delay (after GameMode's 0.2s delay)
		false  // No loop
	);
}

void UTeamLeaderComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	//==========================================================================
	// v9.0 PHASE 5: STRATEGIC COORDINATOR PATTERN
	//
	// TeamLeaderComponent is a COORDINATOR, not a thin wrapper like FollowerAgentComponent.
	// Its TickComponent logic is legitimately component-level (MCTS scheduling, event routing).
	//
	// Architecture:
	// - LeaderCharacter: Follower roster management (Phase 5)
	// - TeamLeaderComponent: Strategic coordination (MCTS, event processing)
	// - IntelManager: Enemy tracking, observations
	// - StrategicPlanner: Async MCTS execution
	// - VisualLogger: Debug visualization
	//==========================================================================

	// Check if simulation is running
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
	if (SimManager && !SimManager->IsSimulationRunning())
	{
		return;
	}

	// Phase 5: Update team observation (delegated to IntelManager)
	if (LeaderCharacter && LeaderCharacter->GetFollowerCount() > 0)
	{
		CurrentTeamObservation = BuildTeamObservation();
	}

	//--------------------------------------------------------------------------
	// CONTINUOUS PLANNING (v3.0 Sprint 6) - Phase 3: Use StrategicPlanner
	//--------------------------------------------------------------------------
	if (bContinuousPlanning && StrategicPlanner)
	{
		TimeSinceLastPlanning += DeltaTime;

		// Check if we should run proactive planning
		if (TimeSinceLastPlanning >= ContinuousPlanningInterval &&
			!StrategicPlanner->IsMCTSRunning())
		{
			UE_LOG(LogTemp, Display, TEXT("[CONTINUOUS PLANNING] '%s': Planning interval reached (%.2fs), triggering MCTS"),
				*TeamName, TimeSinceLastPlanning);

			TimeSinceLastPlanning = 0.0f;
		}
	}

	// Phase 3: Poll StrategicPlanner for async task completion
	if (StrategicPlanner)
	{
		StrategicPlanner->PollAsyncTask();
		// Note: Results are delivered via OnPlanReady delegate (no manual processing needed)
	}

	// Process pending events (can interrupt if critical and bAllowEventInterrupts=true)
	ProcessPendingEvents();

	// Phase 3: Formation diagnosis moved to VisualLoggerComponent
	// (VisualLogger->DrawFormationInfo handles this)

	// Draw debug info if enabled - Phase 3: Delegates to VisualLogger
	if (VisualLogger && VisualLogger->bEnableDebugDrawing)
	{
		DrawDebugInfo();
	}
}

void UTeamLeaderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Phase 3: Async task cleanup now handled automatically by StrategicPlanner's RAII (TUniquePtr)
	// No manual cleanup needed - StrategicPlanner's EndPlay handles it
	UE_LOG(LogTemp, Log, TEXT("[TeamLeader] '%s': EndPlay - cleanup delegated to manager components"), *TeamName);

	Super::EndPlay(EndPlayReason);
}

//------------------------------------------------------------------------------
// INITIALIZATION
//------------------------------------------------------------------------------
TArray<AObjectiveActor*> UTeamLeaderComponent::GetObjectivesArray() const
{
	TArray<AObjectiveActor*> Objectives;
	if (IntelManager)
	{
		if (AObjectiveActor* Friendly = IntelManager->GetFriendlyObjective())
		{
			Objectives.Add(Friendly);
		}
		if (AObjectiveActor* Hostile = IntelManager->GetHostileObjective())
		{
			Objectives.Add(Hostile);
		}
	}
	return Objectives;
}

//------------------------------------------------------------------------------
// FOLLOWER MANAGEMENT
//------------------------------------------------------------------------------

bool UTeamLeaderComponent::RegisterFollower(AActor* Follower)
{
	// Phase 3: Delegate to LeaderCharacter
	if (!LeaderCharacter)
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamLeader] '%s': LeaderCharacter not initialized!"), *TeamName);
		return false;
	}

	bool bSuccess = false;

	// Check if already registered
	if (LeaderCharacter->IsFollowerRegistered(Follower))
	{
		return false;
	}

	// Try registration
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());

	// If leader is registered with SimManager, register immediately
	if (SimManager && bIsRegisteredToManager)
	{
		bSuccess = LeaderCharacter->RegisterFollower(Follower);
		if (bSuccess)
		{
			SimManager->RegisterTeamMember(TeamID, Follower);
			UE_LOG(LogTemp, Log, TEXT("[TeamLeader] '%s': Registered %s (Immediate)"), *TeamName, *Follower->GetName());
		}
	}
	else
	{
		// Queue for later registration
		LeaderCharacter->QueueFollowerRegistration(Follower);
		UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] '%s': %s queued (Leader not registered yet)"), *TeamName, *Follower->GetName());
		bSuccess = true;
	}

	return bSuccess;
}

void UTeamLeaderComponent::UnregisterFollower(AActor* Follower)
{
	// Phase 3: Delegate to LeaderCharacter
	if (!LeaderCharacter || !Follower)
	{
		return;
	}

	if (!LeaderCharacter->IsFollowerRegistered(Follower))
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] '%s': Follower %s not registered"),
			*TeamName, *Follower->GetName());
		return;
	}

	// Unregister from squad
	LeaderCharacter->UnregisterFollower(Follower);

	// Remove from current assignments
	CurrentAssignments.Remove(Follower);

	// Unregister from SimulationManager
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
	if (SimManager)
	{
		SimManager->UnregisterTeamMember(TeamID, Follower);
		UE_LOG(LogTemp, Log, TEXT("[TeamLeader] '%s': Unregistered follower %s from SimulationManager (TeamID: %d, %d remaining)"),
			*TeamName, *Follower->GetName(), TeamID, LeaderCharacter->GetFollowerCount());
	}

	TotalFollowersLost++;

	// Broadcast event (note: LeaderCharacter also broadcasts OnFollowerUnregistered)
	OnFollowerUnregistered.Broadcast(Follower, LeaderCharacter->GetFollowerCount());

	// If all followers dead, trigger critical event
	if (LeaderCharacter->GetAliveFollowers().Num() == 0 && LeaderCharacter->GetFollowerCount() > 0)
	{
		ProcessStrategicEvent(EStrategicEvent::Custom, nullptr, FVector::ZeroVector, 10);
		UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] '%s': All followers eliminated!"), *TeamName);
	}
}


int32 UTeamLeaderComponent::GetMaxFollowers() const
{
	// v9.0 PHASE 5: MaxFollowers is private in LeaderCharacter, return constant
	return 4;
}

bool UTeamLeaderComponent::IsFollowerRegistered(AActor* Follower) const
{
	return LeaderCharacter ? LeaderCharacter->IsFollowerRegistered(Follower) : false;
}

//------------------------------------------------------------------------------
// EVENT PROCESSING
//------------------------------------------------------------------------------

void UTeamLeaderComponent::ProcessStrategicEvent(
	EStrategicEvent Event,
	AActor* Instigator,
	FVector Location,
	int32 Priority)
{
	FStrategicEventContext Context;
	Context.EventType = Event;
	Context.Instigator = Instigator;
	Context.Location = Location;
	Context.Priority = Priority;

	ProcessStrategicEventWithContext(Context);
}

void UTeamLeaderComponent::ProcessStrategicEventWithContext(
	const FStrategicEventContext& Context)
{
	FString EventName = UEnum::GetValueAsString(Context.EventType);
	FString InstigatorName = Context.Instigator ? Context.Instigator->GetName() : TEXT("None");

	UE_LOG(LogTemp, Warning, TEXT("[TEAM LEADER] '%s': Received event %s from %s (Priority: %d, Location: %s)"),
		*TeamName,
		*EventName,
		*InstigatorName,
		Context.Priority,
		*Context.Location.ToString());

	// Add to pending queue
	PendingEvents.Add(Context);
	UE_LOG(LogTemp, Display, TEXT("[TEAM LEADER] '%s': Event queued (%d pending events)"),
		*TeamName,
		PendingEvents.Num());

	// Check if we should trigger MCTS immediately
	bool bShouldTrigger = ShouldTriggerMCTS(Context);

	UE_LOG(LogTemp, Warning, TEXT("[TEAM LEADER] '%s': MCTS trigger check: %s (bMCTSRunning=%s, Priority=%d >= Threshold=%d)"),
		*TeamName,
		bShouldTrigger ? TEXT("YES") : TEXT("NO"),
		StrategicPlanner->IsMCTSRunning() ? TEXT("true") : TEXT("false"),
		Context.Priority,
		EventPriorityThreshold);

	OnEventProcessed.Broadcast(Context.EventType, bShouldTrigger);
}

bool UTeamLeaderComponent::ShouldTriggerMCTS(const FStrategicEventContext& Context) const
{
	// Don't trigger if MCTS already running
	if (StrategicPlanner->IsMCTSRunning())
	{
		UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': MCTS already running, event queued"), *TeamName);
		return false;
	}

	// v3.0 Sprint 6: In continuous planning mode, only interrupt for critical events
	if (bContinuousPlanning)
	{
		if (!bAllowEventInterrupts)
		{
			// Event interrupts disabled, let continuous planning handle it
			UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': Continuous planning mode, event interrupts disabled"), *TeamName);
			return false;
		}

		// Only trigger on CRITICAL events (priority >= 9)
		bool bIsCritical = Context.Priority >= 9;

		// Critical event types
		switch (Context.EventType)
		{
			case EStrategicEvent::AllyKilled:
			case EStrategicEvent::EnemySpotted:
			case EStrategicEvent::EnemyKilled:
			case EStrategicEvent::UnderFire:

				bIsCritical = true;
				break;
			default:
				break;
		}

		if (!bIsCritical)
		{
			UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': Continuous planning mode, non-critical event queued for next cycle"), *TeamName);
			return false;
		}

		UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': CRITICAL EVENT interrupting continuous planning"), *TeamName);
		return true;
	}



	// Trigger if event priority exceeds threshold
	if (Context.Priority >= EventPriorityThreshold)
	{
		UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': Event priority %d >= threshold %d, triggering MCTS"),
			*TeamName, Context.Priority, EventPriorityThreshold);
		return true;
	}

	// High-priority events always trigger
	switch (Context.EventType)
	{
		case EStrategicEvent::AllyKilled:
		case EStrategicEvent::EnemySpotted:

			UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': Critical event type, triggering MCTS"), *TeamName);
			return true;
		default:
			break;
	}

	return false;
}


AObjectiveActor* UTeamLeaderComponent::GetFriendlyObjective() const
{
	return IntelManager ? IntelManager->GetFriendlyObjective() : nullptr;
}

AObjectiveActor* UTeamLeaderComponent::GetHostileObjective() const
{
	return IntelManager ? IntelManager->GetHostileObjective() : nullptr;
}

//------------------------------------------------------------------------------
// ENEMY TRACKING - Phase 3: Delegate to IntelManager
//------------------------------------------------------------------------------

void UTeamLeaderComponent::RegisterEnemy(AActor* Enemy)
{
	if (IntelManager)
	{
		IntelManager->RegisterEnemy(Enemy);
	}
}

void UTeamLeaderComponent::UnregisterEnemy(AActor* Enemy)
{
	if (IntelManager)
	{
		IntelManager->UnregisterEnemy(Enemy);
	}
}

void UTeamLeaderComponent::OnEpisodeStart(int32 EnvironmentID, int32 EpisodeNumber)
{
	// 1. 내 환경인지 확인 (EnvironmentID = TeamID / 2)
	int32 MyEnvironmentID = TeamID / 2;
	if (EnvironmentID != MyEnvironmentID)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] Env %d | Team %d | Episode %d Started - Triggering Initial MCTS"),
		EnvironmentID, TeamID, EpisodeNumber);

	// 2. 이전 에피소드 데이터 완전 초기화
	CurrentBatchKey.Empty();
	CurrentAssignments.Empty();
}

void UTeamLeaderComponent::OnEpisodeComplete(int32 EnvironmentID, const FEpisodeResult& Result)
{
	// Filter: Only process events for our environment
	int32 MyEnvironmentID = TeamID / 2;
	if (EnvironmentID != MyEnvironmentID) return;

	// Map global result to team-specific result
	ETeamEpisodeResult LocalResult = ETeamEpisodeResult::Draw;
	if (Result.WinningTeamID == TeamID) LocalResult = ETeamEpisodeResult::Win;
	else if (Result.LosingTeamID == TeamID) LocalResult = ETeamEpisodeResult::Loss;

	// Update MCTS cache - Phase 3: Delegate to StrategicPlanner
	if (StrategicPlanner && StrategicPlanner->GetMCTS() && !CurrentBatchKey.IsEmpty())
	{
		StrategicPlanner->GetMCTS()->UpdateBatchCache(CurrentAssignments, LocalResult);

		// Periodic cache persistence (every 10 episodes)
		static int32 EpisodeCounter = 0;
		if (++EpisodeCounter % 10 == 0)
		{
			FString CachePath = FPaths::ProjectSavedDir() + TEXT("MCTS/BatchCache.json");
			StrategicPlanner->GetMCTS()->SaveBatchCache(CachePath);
		}
	}

	// Reset for next episode
	CurrentBatchKey.Empty();
	CurrentAssignments.Empty();
}

void UTeamLeaderComponent::ProcessPendingEvents()
{
	if (PendingEvents.Num() == 0) return;
	if (StrategicPlanner->IsMCTSRunning()) return;

	UE_LOG(LogTemp, Warning, TEXT("TeamLeaderComponent: Tick - Processing strategic decisions for team '%s'"), *TeamName);


	// Sort by priority (highest first)
	PendingEvents.Sort([](const FStrategicEventContext& A, const FStrategicEventContext& B) {
		return A.Priority > B.Priority;
	});

	// Process highest priority event
	FStrategicEventContext TopEvent = PendingEvents[0];
	PendingEvents.RemoveAt(0);

	UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': Processing pending event %d (Priority: %d)"),
		*TeamName, static_cast<int32>(TopEvent.EventType), TopEvent.Priority);
}


//------------------------------------------------------------------------------
// MCTS EXECUTION
//------------------------------------------------------------------------------

FTeamObservation UTeamLeaderComponent::BuildTeamObservation()
{
	// Phase 3: Delegate to IntelManager
	if (IntelManager && LeaderCharacter)
	{
		return IntelManager->BuildTeamObservation(LeaderCharacter->GetFollowers());
	}

	return FTeamObservation();
}

//==============================================================================
// v8.0: APPLY STRATEGY ASSIGNMENT
//==============================================================================

void UTeamLeaderComponent::ApplyStrategyAssignment(const TArray<FStrategyAssignment>& Assignments)
{
	UE_LOG(LogTemp, Warning, TEXT("🎯 [ASSIGNMENT v8.0] '%s': Applying %d strategy assignments"),
		*TeamName,
		Assignments.Num());

	// Log strategy summary
	TMap<EStrategyType, int32> StrategyCounts;
	for (const FStrategyAssignment& Assignment : Assignments)
	{
		StrategyCounts.FindOrAdd(Assignment.Strategy, 0)++;
	}

	UE_LOG(LogTemp, Display, TEXT("🎯 [ASSIGNMENT v8.0] '%s': Strategy breakdown:"),
		*TeamName);
	for (const auto& CountPair : StrategyCounts)
	{
		UE_LOG(LogTemp, Display, TEXT("   - %s: %d agents"),
			*UEnum::GetValueAsString(CountPair.Key),
			CountPair.Value);
	}

	// v9.0 FIX: Clear previous assignments to prevent cache key corruption
	// When continuous planning triggers multiple MCTS runs per episode,
	// we must replace old assignments completely, not merge them
	FString OldBatchKey = CurrentBatchKey;
	int32 OldAssignmentCount = CurrentAssignments.Num();
	CurrentAssignments.Empty();

	if (OldAssignmentCount > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ASSIGNMENT v9.0 FIX] 🔄 Replacing previous batch: OldKey='%s' (%d assignments)"),
			*OldBatchKey, OldAssignmentCount);
	}

	// Apply assignments to followers
	for (const FStrategyAssignment& Assignment : Assignments)
	{
		AActor* Agent = Assignment.Agent;

		if (!Agent)
		{
			UE_LOG(LogTemp, Error, TEXT("🎯 [ASSIGNMENT v8.0] Skipping invalid assignment: Agent=NULL"));
			continue;
		}

		// Update current assignments
		CurrentAssignments.Add(Agent, Assignment);

		// Notify follower
		UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
		if (FollowerComp)
		{
			FollowerComp->SetStrategyAssignment(Assignment);

			// v9.0: Strategy-only assignment logging (no explicit objective)
			UE_LOG(LogTemp, Warning, TEXT("🎯 [ASSIGNMENT v9.0] Agent '%s' → Strategy '%s' (Priority=%d, Value=%.2f, Visits=%d)"),
				*Agent->GetName(),
				*UEnum::GetValueAsString(Assignment.Strategy),
				Assignment.Priority,
				Assignment.ExpectedValue,
				Assignment.VisitCount);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("🎯 [ASSIGNMENT v8.0] Agent '%s' has no FollowerAgentComponent!"),
				*Agent->GetName());
		}
	}

	// Phase 3: Get batch key from StrategicPlanner
	if (StrategicPlanner && StrategicPlanner->GetMCTS() && CurrentAssignments.Num() > 0)
	{
		FString NewBatchKey = StrategicPlanner->GetMCTS()->GetBatchKey(CurrentAssignments);

		if (NewBatchKey != OldBatchKey)
		{
			UE_LOG(LogTemp, Warning, TEXT("[✅ BATCH KEY UPDATED] %s: '%s' → '%s' (%d assignments)"),
				*TeamName, *OldBatchKey, *NewBatchKey, CurrentAssignments.Num());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[✅ BATCH KEY SET] %s: BatchKey='%s' (%d assignments)"),
				*TeamName, *NewBatchKey, CurrentAssignments.Num());
		}

		CurrentBatchKey = NewBatchKey;
	}

	// Broadcast event (v8.0)
	FStrategyAssignmentMap AssignmentMap;
	AssignmentMap.Assignments = Assignments;
	OnStrategicDecisionMade.Broadcast(AssignmentMap);

	UE_LOG(LogTemp, Warning, TEXT("🎯 [ASSIGNMENT v8.0] '%s': All assignments complete"),
		*TeamName);
}



//------------------------------------------------------------------------------
// METRICS
//------------------------------------------------------------------------------

FTeamMetrics UTeamLeaderComponent::GetTeamMetrics() const
{
	FTeamMetrics Metrics;

	// Phase 3: Get follower counts from LeaderCharacter
	Metrics.TotalFollowers = LeaderCharacter ? LeaderCharacter->GetFollowerCount() : 0;
	Metrics.AverageHealth = CurrentTeamObservation.AverageTeamHealth;
	Metrics.EnemiesEliminated = TotalEnemiesEliminated;
	Metrics.FollowersLost = TotalFollowersLost;
	Metrics.CommandsIssued = TotalCommandsIssued;

	// Calculate K/D ratio
	if (TotalFollowersLost > 0)
	{
		Metrics.KillDeathRatio = static_cast<float>(TotalEnemiesEliminated) / static_cast<float>(TotalFollowersLost);
	}
	else
	{
		Metrics.KillDeathRatio = static_cast<float>(TotalEnemiesEliminated);
	}

	return Metrics;
}

//------------------------------------------------------------------------------
// DEBUG VISUALIZATION (v6.0 Phase 13)
//------------------------------------------------------------------------------

void UTeamLeaderComponent::DrawDebugInfo()
{
	// Phase 3: Delegate to VisualLoggerComponent
	if (!VisualLogger || !GetOwner()) return;

	FVector LeaderPos = GetOwner()->GetActorLocation();
	int32 TotalCount = LeaderCharacter ? LeaderCharacter->GetFollowerCount() : 0;
	bool bMCTSActive = StrategicPlanner ? StrategicPlanner->IsMCTSRunning() : false;
	float AvgHealth = CurrentTeamObservation.AverageTeamHealth;

	// Draw formation info
	if (LeaderCharacter)
	{
		VisualLogger->DrawFormationInfo(CurrentTeamObservation.TeamCentroid, LeaderCharacter->GetAliveFollowers());
	}

	// Draw objective markers
	if (IntelManager)
	{
		VisualLogger->DrawObjectiveMarkers(
			IntelManager->GetFriendlyObjective(),
			IntelManager->GetHostileObjective()
		);
	}
}

bool UTeamLeaderComponent::IsRunningMCTS() const
{
	return StrategicPlanner->IsMCTSRunning();
}

//------------------------------------------------------------------------------
// Phase 3: Manager Component Handlers (v9.0)
//------------------------------------------------------------------------------

void UTeamLeaderComponent::OnPlanReady(const TArray<FStrategyAssignment>& Assignments, float ExecutionTimeMs, FString BatchKey)
{
	UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v9.0] '%s': Plan ready in %.2fms - %d assignments (Batch: %s)"),
		*TeamName, ExecutionTimeMs, Assignments.Num(), *BatchKey);

	// Update performance stats
	MCTSExecutionCount++;
	AverageMCTSExecutionTime = ((AverageMCTSExecutionTime * (MCTSExecutionCount - 1)) + ExecutionTimeMs) / MCTSExecutionCount;

	// Performance warning if exceeding target
	const float TargetTime = 50.0f;
	if (ExecutionTimeMs > TargetTime)
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ [PERFORMANCE] '%s': MCTS took %.2fms (exceeds target of %.0fms) - Avg: %.2fms over %d runs"),
			*TeamName, ExecutionTimeMs, TargetTime, AverageMCTSExecutionTime, MCTSExecutionCount);
	}

	// Store batch key
	CurrentBatchKey = BatchKey;

	// Apply strategy assignments to followers
	ApplyStrategyAssignment(Assignments);
}

void UTeamLeaderComponent::OnSquadFollowerRegistered(AActor* Follower)
{
	// Propagate to our own OnFollowerRegistered delegate for backwards compatibility
	if (LeaderCharacter)
	{
		OnFollowerRegistered.Broadcast(Follower, LeaderCharacter->GetFollowerCount());
	}
}

void UTeamLeaderComponent::HandleObjectivesDiscovered()
{
	// 1. 데이터 확보
	AObjectiveActor* Friendly = IntelManager ? IntelManager->GetFriendlyObjective() : nullptr;
	AObjectiveActor* Hostile = IntelManager ? IntelManager->GetHostileObjective() : nullptr;

	// 2. 모든 팔로워에게 Push
	if (LeaderCharacter)
	{
		for (AActor* Follower : LeaderCharacter->GetAliveFollowers())
		{
			PushContextToFollower(Follower, Friendly, Hostile);
		}
	}
}

void UTeamLeaderComponent::BroadcastTacticalContext()
{
	if (!IntelManager || !LeaderCharacter) return;

	AObjectiveActor* Friendly = IntelManager->GetFriendlyObjective();
	AObjectiveActor* Hostile = IntelManager->GetHostileObjective();

	// 모든 살아있는 팔로워에게 Push
	for (AActor* Follower : LeaderCharacter->GetAliveFollowers())
	{
		PushContextToFollower(Follower, Friendly, Hostile);
	}
}

void UTeamLeaderComponent::OnNewFollowerJoined(AActor* NewFollower)
{
	// IntelManager가 없거나 아직 목표를 모르면 패스 (나중에 Discovered 이벤트에서 처리됨)
	if (!IntelManager || !IntelManager->AreObjectivesDiscovered()) return;

	PushContextToFollower(NewFollower,
		IntelManager->GetFriendlyObjective(),
		IntelManager->GetHostileObjective());
}

void UTeamLeaderComponent::PushContextToFollower(AActor* Follower, AObjectiveActor* Friendly, AObjectiveActor* Hostile)
{
	if (!Follower) return;

	UFollowerAgentComponent* Agent = Follower->FindComponentByClass<UFollowerAgentComponent>();
	if (Agent)
	{
		// v9.0 FIX: Pass CurrentTeamObservation as third parameter (ally intel for support strategy)
		Agent->UpdateTacticalContext(Friendly, Hostile, CurrentTeamObservation);
	}
}