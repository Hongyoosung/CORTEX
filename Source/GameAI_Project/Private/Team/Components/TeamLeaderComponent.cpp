#include "Team/Components/TeamLeaderComponent.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/ObjectiveActor.h"  // v8.0: For durability-based objectives
#include "AI/MCTS/MCTS.h"
#include "AI/MCTS/MCTSAsyncTask.h"

// Phase 3: Manager Component Includes (v9.0)
#include "Team/Components/SquadManagerComponent.h"
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
	// Phase 3: Resolve Manager Components (v9.0 Coordinator Pattern)
	//==========================================================================
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamLeader] BeginPlay: No owner actor!"));
		return;
	}

	SquadManager = Owner->FindComponentByClass<USquadManagerComponent>();
	IntelManager = Owner->FindComponentByClass<UIntelManagerComponent>();
	StrategicPlanner = Owner->FindComponentByClass<UStrategicPlannerComponent>();
	VisualLogger = Owner->FindComponentByClass<UVisualLoggerComponent>();

	// Verify required components exist
	if (!SquadManager || !IntelManager || !StrategicPlanner)
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamLeader] %s: Missing required manager components! Squad=%s Intel=%s Planner=%s"),
			*Owner->GetName(),
			SquadManager ? TEXT("OK") : TEXT("MISSING"),
			IntelManager ? TEXT("OK") : TEXT("MISSING"),
			StrategicPlanner ? TEXT("OK") : TEXT("MISSING"));
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("✅ [TeamLeader] '%s': All manager components resolved (Squad, Intel, Planner, VisualLogger=%s)"),
		*TeamName, VisualLogger ? TEXT("OK") : TEXT("MISSING"));

	//==========================================================================
	// Initialize Manager Components
	//==========================================================================

	// Initialize StrategicPlanner (replaces InitializeMCTS)
	StrategicPlanner->InitializeMCTS(MCTSSimulations);
	StrategicPlanner->OnPlanReady.AddDynamic(this, &UTeamLeaderComponent::OnPlanReady);
	UE_LOG(LogTemp, Log, TEXT("[TeamLeader] '%s': StrategicPlanner initialized with %d simulations"), *TeamName, MCTSSimulations);

	// Subscribe to SquadManager events
	SquadManager->OnFollowerRegistered.AddDynamic(this, &UTeamLeaderComponent::OnSquadFollowerRegistered);

	// Configure IntelManager
	IntelManager->TeamID = TeamID;

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

				// Process pending registrations (delegated to SquadManager)
				ProcessPendingRegistrations();
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

	UE_LOG(LogTemp, Log, TEXT("[TeamLeader] '%s': Initialized (objective discovery scheduled for 0.3s delay)"), *TeamName);
}

void UTeamLeaderComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Check if simulation is running
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
	if (SimManager && !SimManager->IsSimulationRunning())
	{
		return;
	}

	// Phase 3: Update team observation (delegated to IntelManager)
	if (SquadManager && SquadManager->GetFollowerCount() > 0)
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
			!StrategicPlanner->IsMCTSRunning() &&
			GetAliveFollowers().Num() > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("[CONTINUOUS PLANNING] '%s': Planning interval reached (%.2fs), triggering MCTS"),
				*TeamName, TimeSinceLastPlanning);

			TimeSinceLastPlanning = 0.0f;
			RunStrategyAssignmentAsync();
		}
	}

	// Phase 3: Poll StrategicPlanner for async task completion
	if (StrategicPlanner)
	{
		StrategicPlanner->PollAsyncTask();
		// Note: Results are delivered via OnPlanReady delegate (no manual processing needed)

		// Sync bMCTSRunning state with StrategicPlanner
		bMCTSRunning = StrategicPlanner->IsMCTSRunning();
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

void UTeamLeaderComponent::InitializeMCTS()
{
	// DEPRECATED: Use StrategicPlannerComponent->InitializeMCTS() instead
}

void UTeamLeaderComponent::DiscoverWorldObjectives()
{
	// Phase 3: Delegate to IntelManager
	if (IntelManager) IntelManager->DiscoverWorldObjectives();
}

void UTeamLeaderComponent::ProcessPendingRegistrations()
{
	// DEPRECATED: SquadManager handles this automatically
}

//------------------------------------------------------------------------------
// FOLLOWER MANAGEMENT
//------------------------------------------------------------------------------

bool UTeamLeaderComponent::RegisterFollower(AActor* Follower)
{
	// Phase 3: Delegate to SquadManager
	if (!SquadManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamLeader] '%s': SquadManager not initialized!"), *TeamName);
		return false;
	}

	bool bSuccess = false;

	// Check if already registered
	if (SquadManager->IsFollowerRegistered(Follower))
	{
		return false;
	}

	// Try registration
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());

	// If leader is registered with SimManager, register immediately
	if (SimManager && bIsRegisteredToManager)
	{
		bSuccess = SquadManager->RegisterFollower(Follower);
		if (bSuccess)
		{
			SimManager->RegisterTeamMember(TeamID, Follower);
			UE_LOG(LogTemp, Log, TEXT("[TeamLeader] '%s': Registered %s (Immediate)"), *TeamName, *Follower->GetName());
		}
	}
	else
	{
		// Queue for later registration
		SquadManager->QueueFollowerRegistration(Follower);
		UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] '%s': %s queued (Leader not registered yet)"), *TeamName, *Follower->GetName());
		bSuccess = true;
	}

	return bSuccess;
}

void UTeamLeaderComponent::UnregisterFollower(AActor* Follower)
{
	// Phase 3: Delegate to SquadManager
	if (!SquadManager || !Follower)
	{
		return;
	}

	if (!SquadManager->IsFollowerRegistered(Follower))
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] '%s': Follower %s not registered"),
			*TeamName, *Follower->GetName());
		return;
	}

	// Unregister from squad
	SquadManager->UnregisterFollower(Follower);

	// Remove from current assignments
	CurrentAssignments.Remove(Follower);

	// Unregister from SimulationManager
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
	if (SimManager)
	{
		SimManager->UnregisterTeamMember(TeamID, Follower);
		UE_LOG(LogTemp, Log, TEXT("[TeamLeader] '%s': Unregistered follower %s from SimulationManager (TeamID: %d, %d remaining)"),
			*TeamName, *Follower->GetName(), TeamID, SquadManager->GetFollowerCount());
	}

	TotalFollowersLost++;

	// Broadcast event (note: SquadManager also broadcasts OnFollowerUnregistered)
	OnFollowerUnregistered.Broadcast(Follower, SquadManager->GetFollowerCount());

	// If all followers dead, trigger critical event
	if (SquadManager->GetAliveFollowers().Num() == 0 && SquadManager->GetFollowerCount() > 0)
	{
		ProcessStrategicEvent(EStrategicEvent::Custom, nullptr, FVector::ZeroVector, 10);
		UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] '%s': All followers eliminated!"), *TeamName);
	}
}

TArray<AActor*> UTeamLeaderComponent::GetFollowers() const
{
	return SquadManager ? SquadManager->GetFollowers() : TArray<AActor*>();
}


TArray<AActor*> UTeamLeaderComponent::GetAliveFollowers() const
{
	// Phase 3: Delegate to SquadManager
	if (SquadManager)
	{
		return SquadManager->GetAliveFollowers();
	}

	return TArray<AActor*>();
}

int32 UTeamLeaderComponent::GetFollowerCount() const
{
	return SquadManager ? SquadManager->GetFollowerCount() : 0;
}

bool UTeamLeaderComponent::IsFollowerRegistered(AActor* Follower) const
{
	return SquadManager ? SquadManager->IsFollowerRegistered(Follower) : false;
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

	UE_LOG(LogTemp, Warning, TEXT("[TEAM LEADER] '%s': MCTS trigger check: %s (bMCTSRunning=%s, IsCooldown=%s, Priority=%d >= Threshold=%d)"),
		*TeamName,
		bShouldTrigger ? TEXT("YES") : TEXT("NO"),
		bMCTSRunning ? TEXT("true") : TEXT("false"),
		IsMCTSOnCooldown() ? TEXT("true") : TEXT("false"),
		Context.Priority,
		EventPriorityThreshold);

	if (bShouldTrigger)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TEAM LEADER] '%s': TRIGGERING MCTS (Mode: %s)"),
			*TeamName,
			bAsyncMCTS ? TEXT("ASYNC") : TEXT("SYNC"));

		if (bAsyncMCTS)
		{
			RunStrategyAssignmentAsync();
		}
		else
		{
			RunStrategyAssignment();
		}
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[TEAM LEADER] '%s': MCTS not triggered, event will be processed later"),
			*TeamName);
	}

	OnEventProcessed.Broadcast(Context.EventType, bShouldTrigger);
}

bool UTeamLeaderComponent::ShouldTriggerMCTS(const FStrategicEventContext& Context) const
{
	// Don't trigger if MCTS already running
	if (bMCTSRunning)
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

	// Legacy event-driven mode
	// Don't trigger if on cooldown
	if (IsMCTSOnCooldown())
	{
		UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': MCTS on cooldown, event queued"), *TeamName);
		return false;
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

	// 3. 목표물 재탐색 (에피소드 리셋 후 객체 상태가 변경되었을 수 있으므로 안전장치)
	if (IntelManager && !IntelManager->AreObjectivesDiscovered())
	{
		DiscoverWorldObjectives();
	}

	// 4. MCTS 즉시 실행하여 초기 배치 할당 (이것이 없으면 CurrentBatchKey가 비어있게 됨)
	if (bAsyncMCTS)
	{
		RunStrategyAssignmentAsync();
	}
	else
	{
		RunStrategyAssignment();
	}
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
	if (bMCTSRunning) return;
	if (IsMCTSOnCooldown()) return;

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

	if (ShouldTriggerMCTS(TopEvent))
	{
		if (bAsyncMCTS)
		{
			RunStrategyAssignmentAsync();
		}
		else
		{
			RunStrategyAssignment();
		}
	}
}

bool UTeamLeaderComponent::IsMCTSOnCooldown() const
{
	float CurrentTime = FPlatformTime::Seconds();
	return (CurrentTime - LastMCTSTime) < MCTSCooldown;
}

//------------------------------------------------------------------------------
// MCTS EXECUTION
//------------------------------------------------------------------------------

FTeamObservation UTeamLeaderComponent::BuildTeamObservation()
{
	// Phase 3: Delegate to IntelManager
	if (IntelManager && SquadManager)
	{
		return IntelManager->BuildTeamObservation(SquadManager->GetFollowers());
	}

	return FTeamObservation();
}


//==============================================================================
// v8.0: STRATEGY ASSIGNMENT (SYNC)
//==============================================================================

void UTeamLeaderComponent::RunStrategyAssignment()
{
	// DEPRECATED: Use RunStrategyAssignmentAsync() instead
	RunStrategyAssignmentAsync();
}


//==============================================================================
// v8.0: STRATEGY ASSIGNMENT (ASYNC)
//==============================================================================

void UTeamLeaderComponent::RunStrategyAssignmentAsync()
{
	// Phase 3: Delegate to StrategicPlanner
	if (!StrategicPlanner)
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamLeader] '%s': StrategicPlanner not initialized"), *TeamName);
		return;
	}

	if (StrategicPlanner->IsMCTSRunning())
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] '%s': MCTS already running"), *TeamName);
		return;
	}

	// Get alive agents from SquadManager
	TArray<AActor*> AliveAgents = SquadManager ? SquadManager->GetAliveFollowers() : TArray<AActor*>();
	if (AliveAgents.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] '%s': No alive agents, skipping MCTS"), *TeamName);
		return;
	}

	// Get objectives from IntelManager
	if (!IntelManager || !IntelManager->AreObjectivesDiscovered())
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] '%s': Missing objectives, retrying discovery..."), *TeamName);

		// Retry objective discovery
		if (IntelManager)
		{
			IntelManager->DiscoverWorldObjectives();
		}

		// Check again after retry
		if (!IntelManager || !IntelManager->AreObjectivesDiscovered())
		{
			UE_LOG(LogTemp, Error, TEXT("[TeamLeader] '%s': Still missing objectives after retry!"), *TeamName);
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("✅ [TeamLeader] '%s': Objectives found after retry!"), *TeamName);
	}

	// Update state
	bMCTSRunning = true;
	LastMCTSTime = FPlatformTime::Seconds();

	// Build objectives array
	TArray<AObjectiveActor*> Objectives;
	if (IntelManager->GetFriendlyObjective()) Objectives.Add(IntelManager->GetFriendlyObjective());
	if (IntelManager->GetHostileObjective()) Objectives.Add(IntelManager->GetHostileObjective());

	// Delegate to StrategicPlanner
	StrategicPlanner->RunStrategyAssignmentAsync(AliveAgents, Objectives);

	UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v9.0] '%s': STARTED (ASYNC) - %d agents"),
		*TeamName, AliveAgents.Num());
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

	bMCTSRunning = false;

	UE_LOG(LogTemp, Warning, TEXT("🎯 [ASSIGNMENT v8.0] '%s': All assignments complete"),
		*TeamName);
}


//------------------------------------------------------------------------------
// ENEMY TRACKING
//------------------------------------------------------------------------------

void UTeamLeaderComponent::RegisterEnemy(AActor* Enemy)
{
	// Phase 3: Delegate to IntelManager
	if (!IntelManager || !Enemy) return;

	// Filter out Leader characters - they should not be registered as enemies
	if (Enemy->IsA<ALeaderCharacter>())
	{
		return;
	}

	IntelManager->RegisterEnemy(Enemy);
}

void UTeamLeaderComponent::UnregisterEnemy(AActor* Enemy)
{
	// Phase 3: Delegate to IntelManager
	if (!IntelManager || !Enemy) return;

	// Check if enemy was registered before updating stats
	TArray<AActor*> KnownEnemies = IntelManager->GetKnownEnemies();
	if (KnownEnemies.Contains(Enemy))
	{
		IntelManager->UnregisterEnemy(Enemy);
		TotalEnemiesEliminated++;

		UE_LOG(LogTemp, Log, TEXT("[TeamLeader] '%s': Enemy %s eliminated (Remaining: %d)"),
			*TeamName, *Enemy->GetName(), IntelManager->GetKnownEnemyCount());

		// Trigger event
		ProcessStrategicEvent(EStrategicEvent::EnemyEliminated, Enemy, Enemy->GetActorLocation(), 6);
	}
}

TArray<AActor*> UTeamLeaderComponent::GetKnownEnemies() const
{
	// Phase 3: Delegate to IntelManager
	if (IntelManager)
	{
		return IntelManager->GetKnownEnemies();
	}

	return TArray<AActor*>();
}

void UTeamLeaderComponent::ClearKnownEnemies()
{
	// Phase 3: Delegate to IntelManager
	if (IntelManager)
	{
		int32 ClearedCount = IntelManager->GetKnownEnemyCount();
		IntelManager->ClearKnownEnemies();

		UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] '%s': Cleared %d known enemies (episode reset)"),
			*TeamName, ClearedCount);
	}
}

//------------------------------------------------------------------------------
// METRICS
//------------------------------------------------------------------------------

FTeamMetrics UTeamLeaderComponent::GetTeamMetrics() const
{
	FTeamMetrics Metrics;

	// Phase 3: Get follower counts from SquadManager
	Metrics.TotalFollowers = SquadManager ? SquadManager->GetFollowerCount() : 0;
	Metrics.AliveFollowers = GetAliveFollowers().Num();
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
	int32 AliveCount = GetAliveFollowers().Num();
	int32 TotalCount = SquadManager ? SquadManager->GetFollowerCount() : 0;
	bool bMCTSActive = StrategicPlanner ? StrategicPlanner->IsMCTSRunning() : false;
	float AvgHealth = CurrentTeamObservation.AverageTeamHealth;

	// Draw team leader state
	VisualLogger->DrawTeamLeaderState(LeaderPos, AliveCount, TotalCount, bMCTSActive, AvgHealth);

	// Draw formation info
	if (SquadManager)
	{
		VisualLogger->DrawFormationInfo(CurrentTeamObservation.TeamCentroid, SquadManager->GetAliveFollowers());
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
	if (SquadManager)
	{
		OnFollowerRegistered.Broadcast(Follower, SquadManager->GetFollowerCount());
	}
}