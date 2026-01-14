#include "Team/TeamLeaderComponent.h"
#include "Team/FollowerAgentComponent.h"
#include "Team/ObjectiveActor.h"  // v8.0: For durability-based objectives
#include "AI/MCTS/MCTS.h"
#include "AI/MCTS/MCTSAsyncTask.h"
#include "Observation/ObservationElement.h"
#include "RL/CurriculumManager.h"
#include "RL/RLTypes.h"  // v8.0: EStrategyType, FStrategyAssignment
#include "RL/RLPolicyNetwork.h"  // v8.0: For value estimates
#include "Core/SimulationManagerGameMode.h"
#include "DrawDebugHelpers.h"
#include "Async/Async.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "RL/RewardCalculator.h"
#include "Kismet/GameplayStatics.h"
#include "Combat/HealthComponent.h"

//==============================================================================
// v8.0: TEAM LEADER COMPONENT
// MCTS assigns Strategies directly (not Missions) → RL outputs tactical params → Rules execute
//==============================================================================

UTeamLeaderComponent::UTeamLeaderComponent()
	: AsyncMCTSTask(nullptr)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.5f;  // Update every 0.5s
}

void UTeamLeaderComponent::BeginPlay()
{
	Super::BeginPlay();

	// Initialize MCTS
	InitializeMCTS();

	// Initialize curriculum manager (v3.0 Sprint 3)
	if (!CurriculumManager)
	{
		CurriculumManager = NewObject<UCurriculumManager>(GetOwner());
		if (CurriculumManager)
		{
			UE_LOG(LogTemp, Log, TEXT("TeamLeaderComponent: CurriculumManager initialized for '%s'"), *TeamName);
		}
	}

	// Auto-register with SimulationManager (fix for missing team registration)
	if (bAutoRegisterWithSimManager)
	{
		ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
		if (SimManager)
		{
			bool InbRegistered = SimManager->RegisterTeam(TeamID, this, TeamName, TeamColor);
			if (InbRegistered)
			{
				UE_LOG(LogTemp, Warning, TEXT("✅ TeamLeader '%s': Registered with SimulationManager (TeamID: %d)"),
					*TeamName, TeamID);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("❌ TeamLeader '%s': Failed to register with SimulationManager (TeamID: %d)"),
					*TeamName, TeamID);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("⚠️ TeamLeader '%s': SimulationManager not found, team registration skipped"), *TeamName);
		}
	}

	// Auto-discover objectives from level (v8.0 - Strategy Assignment)
	DiscoverWorldObjectives();

	UE_LOG(LogTemp, Log, TEXT("TeamLeaderComponent: Initialized team '%s'"), *TeamName);
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

	// Update team observation (for next decision)
	if (Followers.Num() > 0)
	{
		CurrentTeamObservation = BuildTeamObservation();
	}

	//--------------------------------------------------------------------------
	// CONTINUOUS PLANNING (v3.0 Sprint 6)
	//--------------------------------------------------------------------------
	if (bContinuousPlanning)
	{
		TimeSinceLastPlanning += DeltaTime;

		// Check if we should run proactive planning
		if (TimeSinceLastPlanning >= ContinuousPlanningInterval &&
			!bMCTSRunning &&
			GetAliveFollowers().Num() > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("⏰ [CONTINUOUS PLANNING] '%s': Planning interval reached (%.2fs), triggering MCTS"),
				*TeamName, TimeSinceLastPlanning);

			TimeSinceLastPlanning = 0.0f;

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

	// Check if async MCTS task completed (v8.0)
	if (AsyncMCTSTask != nullptr && AsyncMCTSTask->IsDone())
	{
		// Get results from completed task (v8.0 API)
		TArray<FStrategyAssignment> Assignments = AsyncMCTSTask->GetTask().GetStrategyResults();
		float ExecutionTime = AsyncMCTSTask->GetTask().GetExecutionTime();

		UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v8.0] '%s': Async task completed in %.2fms - %d assignments"),
			*TeamName, ExecutionTime, Assignments.Num());

		// Update performance stats
		MCTSExecutionCount++;
		AverageMCTSExecutionTime = ((AverageMCTSExecutionTime * (MCTSExecutionCount - 1)) + ExecutionTime) / MCTSExecutionCount;

		// Performance warning if exceeding target
		const float TargetTime = 50.0f;
		if (ExecutionTime > TargetTime)
		{
			UE_LOG(LogTemp, Warning, TEXT("⚠️ [PERFORMANCE] '%s': MCTS took %.2fms (exceeds target of %.0fms) - Avg: %.2fms over %d runs"),
				*TeamName, ExecutionTime, TargetTime, AverageMCTSExecutionTime, MCTSExecutionCount);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("✓ [PERFORMANCE] '%s': MCTS took %.2fms (within target) - Avg: %.2fms over %d runs"),
				*TeamName, ExecutionTime, AverageMCTSExecutionTime, MCTSExecutionCount);
		}

		// Delete completed task (FAsyncTask requires manual cleanup)
		delete AsyncMCTSTask;
		AsyncMCTSTask = nullptr;

		// Process results on game thread (v8.0)
		ApplyStrategyAssignment(Assignments);
	}

	// Process pending events (can interrupt if critical and bAllowEventInterrupts=true)
	ProcessPendingEvents();

	// ============================================================================
	// PROXIMITY DIAGNOSIS: Log inter-agent distances every 2 seconds
	// ============================================================================
	TimeSinceLastFormationLog += DeltaTime;
	if (TimeSinceLastFormationLog >= 2.0f)
	{
		TimeSinceLastFormationLog = 0.0f;

		TArray<AActor*> AliveFollowers = GetAliveFollowers();
		if (AliveFollowers.Num() >= 2)
		{
			//UE_LOG(LogTemp, Warning, TEXT("[FORMATION] '%s': Inter-agent distances (%d agents):"), *TeamName, AliveFollowers.Num());

			// Calculate all pairwise distances
			float MinDistance = FLT_MAX;
			float MaxDistance = 0.0f;
			float TotalDistance = 0.0f;
			int32 PairCount = 0;

			for (int32 i = 0; i < AliveFollowers.Num(); ++i)
			{
				AActor* Agent1 = AliveFollowers[i];
				if (!Agent1) continue;

				for (int32 j = i + 1; j < AliveFollowers.Num(); ++j)
				{
					AActor* Agent2 = AliveFollowers[j];
					if (!Agent2) continue;

					float Distance = FVector::Dist(Agent1->GetActorLocation(), Agent2->GetActorLocation());

					/*UE_LOG(LogTemp, Warning, TEXT("[FORMATION]   '%s' <-> '%s': %.1f cm"),
						*Agent1->GetName(),
						*Agent2->GetName(),
						Distance);*/

					MinDistance = FMath::Min(MinDistance, Distance);
					MaxDistance = FMath::Max(MaxDistance, Distance);
					TotalDistance += Distance;
					PairCount++;
				}
			}

			if (PairCount > 0)
			{
				float AvgDistance = TotalDistance / PairCount;
				/*UE_LOG(LogTemp, Warning, TEXT("[FORMATION] '%s': Distance stats - Min: %.1f cm, Max: %.1f cm, Avg: %.1f cm"),
					*TeamName, MinDistance, MaxDistance, AvgDistance);*/
			}
		}
	}

	// Draw debug info if enabled
	if (bEnableDebugDrawing)
	{
		DrawDebugInfo();
	}
}

void UTeamLeaderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up any running async tasks
	if (AsyncMCTSTask != nullptr)
	{
		// FAsyncTask requires manual cleanup
		AsyncMCTSTask->EnsureCompletion();  // Wait for task to finish
		delete AsyncMCTSTask;
		AsyncMCTSTask = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

//------------------------------------------------------------------------------
// INITIALIZATION
//------------------------------------------------------------------------------

void UTeamLeaderComponent::InitializeMCTS()
{
	StrategicMCTS = NewObject<UMCTS>(this);
	if (StrategicMCTS)
	{
		// Initialize MCTS for team-level decisions
		StrategicMCTS->InitializeTeamMCTS(MCTSSimulations, 1.41f);

		// Also set properties directly for compatibility
		StrategicMCTS->MaxSimulations = MCTSSimulations;
		StrategicMCTS->ExplorationParameter = 1.41f;
		StrategicMCTS->DiscountFactor = 0.95f;

		UE_LOG(LogTemp, Log, TEXT("TeamLeaderComponent: MCTS initialized with %d simulations"), MCTSSimulations);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TeamLeaderComponent: Failed to create MCTS"));
	}
}

void UTeamLeaderComponent::DiscoverWorldObjectives()
{
	if (!GetWorld()) return;

	//==========================================================================
	// v8.0: ObjectiveActor Discovery for Strategy Assignment
	// MCTS now assigns strategies directly to objectives
	//==========================================================================

	// Find all ObjectiveActors in the world
	TArray<AActor*> FoundObjectives;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AObjectiveActor::StaticClass(), FoundObjectives);

	// Find friendly and hostile objectives
	for (AActor* Actor : FoundObjectives)
	{
		AObjectiveActor* Objective = Cast<AObjectiveActor>(Actor);
		if (!Objective) continue;

		// Check if friendly (same team)
		if (Objective->OwnerTeamID == TeamID)
		{
			FriendlyObjective = Objective;
			UE_LOG(LogTemp, Display, TEXT("✅ [v8.0 DISCOVERY] TeamLeader '%s' (TeamID=%d): Found friendly ObjectiveActor '%s' at %s"),
				*TeamName, TeamID,
				*FriendlyObjective->GetName(),
				*FriendlyObjective->GetActorLocation().ToString());
		}
		else
		{
			HostileObjective = Objective;
			UE_LOG(LogTemp, Display, TEXT("✅ [v8.0 DISCOVERY] TeamLeader '%s' (TeamID=%d): Found hostile ObjectiveActor '%s' at %s"),
				*TeamName, TeamID,
				*HostileObjective->GetName(),
				*HostileObjective->GetActorLocation().ToString());
		}
	}

	// Validate discovery
	if (!FriendlyObjective)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ [v8.0 DISCOVERY] TeamLeader '%s' (TeamID=%d): No friendly ObjectiveActor found!"),
			*TeamName, TeamID);
	}

	if (!HostileObjective)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ [v8.0 DISCOVERY] TeamLeader '%s' (TeamID=%d): No hostile ObjectiveActor found!"),
			*TeamName, TeamID);
	}

	if (FriendlyObjective && HostileObjective)
	{
		UE_LOG(LogTemp, Display, TEXT("✅ [v8.0 DISCOVERY] TeamLeader '%s': Ready for strategy assignment (Defend: %s, Assault: %s)"),
			*TeamName,
			*FriendlyObjective->GetName(),
			*HostileObjective->GetName());
	}
}

//------------------------------------------------------------------------------
// FOLLOWER MANAGEMENT
//------------------------------------------------------------------------------

bool UTeamLeaderComponent::RegisterFollower(AActor* Follower)
{
	if (!Follower)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': Cannot register null follower"), *TeamName);
		return false;
	}

	if (Followers.Num() >= MaxFollowers)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': Max followers reached (%d)"), *TeamName, MaxFollowers);
		return false;
	}

	if (Followers.Contains(Follower))
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': Follower %s already registered"),
			*TeamName, *Follower->GetName());
		return false;
	}

	Followers.Add(Follower);

	// Register with SimulationManager (fix for team ID detection)
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
	if (SimManager)
	{
		bool InbRegistered = SimManager->RegisterTeamMember(TeamID, Follower);
		if (InbRegistered)
		{
			UE_LOG(LogTemp, Log, TEXT("TeamLeader '%s': Registered follower %s with SimulationManager (TeamID: %d, %d/%d)"),
				*TeamName, *Follower->GetName(), TeamID, Followers.Num(), MaxFollowers);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': Failed to register follower %s with SimulationManager"),
				*TeamName, *Follower->GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': SimulationManager not found, follower %s not registered with team mapping"),
			*TeamName, *Follower->GetName());
	}

	// Broadcast event
	OnFollowerRegistered.Broadcast(Follower, Followers.Num());

	return true;
}

void UTeamLeaderComponent::UnregisterFollower(AActor* Follower)
{
	if (!Follower) return;

	if (!Followers.Contains(Follower))
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': Follower %s not registered"),
			*TeamName, *Follower->GetName());
		return;
	}

	Followers.Remove(Follower);
	CurrentAssignments.Remove(Follower);

	// Unregister from SimulationManager (fix for team ID detection)
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
	if (SimManager)
	{
		SimManager->UnregisterTeamMember(TeamID, Follower);
		UE_LOG(LogTemp, Log, TEXT("TeamLeader '%s': Unregistered follower %s from SimulationManager (TeamID: %d, %d remaining)"),
			*TeamName, *Follower->GetName(), TeamID, Followers.Num());
	}

	TotalFollowersLost++;

	// Broadcast event
	OnFollowerUnregistered.Broadcast(Follower, Followers.Num());

	// If all followers dead, trigger critical event
	if (GetAliveFollowers().Num() == 0 && Followers.Num() > 0)
	{
		ProcessStrategicEvent(EStrategicEvent::Custom, nullptr, FVector::ZeroVector, 10);
		UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': All followers eliminated!"), *TeamName);
	}
}


TArray<AActor*> UTeamLeaderComponent::GetAliveFollowers() const
{
	TArray<AActor*> Alive;

	for (AActor* Follower : Followers)
	{
		if (!Follower) continue;

		// Simple alive check - can be extended with health component check
		if (!Follower->IsPendingKillPending())
		{
			Alive.Add(Follower);
		}
	}

	return Alive;
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
			case EStrategicEvent::MissionComplete:
			case EStrategicEvent::MissionFailed:
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
		case EStrategicEvent::MissionComplete:
		case EStrategicEvent::MissionFailed:
			UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': Critical event type, triggering MCTS"), *TeamName);
			return true;
		default:
			break;
	}

	return false;
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
	// Gather all follower observations
	TArray<AActor*> AliveFollowers = GetAliveFollowers();
	TArray<AActor*> Enemies = GetKnownEnemies();

	// v8.0: Use discovered friendly objective
	AActor* FriendlyObj = FriendlyObjective;

	FTeamObservation TeamObs = FTeamObservation::BuildFromTeam(
		AliveFollowers,
		FriendlyObj,
		Enemies
	);

	return TeamObs;
}


//==============================================================================
// v8.0: STRATEGY ASSIGNMENT (SYNC)
//==============================================================================

void UTeamLeaderComponent::RunStrategyAssignment()
{
	if (bMCTSRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': MCTS already running"), *TeamName);
		return;
	}

	if (!StrategicMCTS)
	{
		UE_LOG(LogTemp, Error, TEXT("🎯 TeamLeader '%s': Missing MCTS"), *TeamName);
		return;
	}

	TArray<AActor*> AliveAgents = GetAliveFollowers();

	if (AliveAgents.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': No alive agents, skipping MCTS"), *TeamName);
		return;
	}

	if (!FriendlyObjective || !HostileObjective)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': Missing objectives, skipping MCTS"), *TeamName);
		return;
	}

	bMCTSRunning = true;
	float StartTime = FPlatformTime::Seconds();
	LastMCTSTime = StartTime;

	UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v8.0] '%s': STARTED (SYNC) - %d agents"),
		*TeamName,
		AliveAgents.Num());

	// v8.0: Pre-cache observations for thread safety
	TMap<AActor*, FObservationElement> CachedObservations;
	for (AActor* Agent : AliveAgents)
	{
		if (!Agent) continue;

		UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
		if (FollowerComp)
		{
			FObservationElement Obs = FollowerComp->BuildLocalObservation();
			CachedObservations.Add(Agent, Obs);
		}
	}

	// Run MCTS to find best strategy assignments (v8.0)
	TArray<FStrategyAssignment> Assignments = StrategicMCTS->RunStrategyAssignment(
		AliveAgents,
		FriendlyObjective,
		HostileObjective,
		MCTSSimulations,
		CachedObservations
	);

	float ExecutionTime = (FPlatformTime::Seconds() - StartTime) * 1000.0f; // ms

	// Update rolling average (Performance Profiling)
	MCTSExecutionCount++;
	AverageMCTSExecutionTime = ((AverageMCTSExecutionTime * (MCTSExecutionCount - 1)) + ExecutionTime) / MCTSExecutionCount;

	// Performance warning if exceeding target
	const float TargetTime = 50.0f; // Target: 30-50ms
	if (ExecutionTime > TargetTime)
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ [PERFORMANCE] '%s': MCTS took %.2fms (exceeds target of %.0fms) - Avg: %.2fms over %d runs"),
			*TeamName,
			ExecutionTime,
			TargetTime,
			AverageMCTSExecutionTime,
			MCTSExecutionCount);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("✓ [PERFORMANCE] '%s': MCTS took %.2fms (within target) - Avg: %.2fms over %d runs"),
			*TeamName,
			ExecutionTime,
			AverageMCTSExecutionTime,
			MCTSExecutionCount);
	}

	UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v8.0] '%s': COMPLETED in %.2fms - %d assignments"),
		*TeamName,
		ExecutionTime,
		Assignments.Num());

	// Apply assignment (v8.0)
	ApplyStrategyAssignment(Assignments);
}


//==============================================================================
// v8.0: STRATEGY ASSIGNMENT (ASYNC)
//==============================================================================

void UTeamLeaderComponent::RunStrategyAssignmentAsync()
{
	if (bMCTSRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': MCTS already running"), *TeamName);
		return;
	}

	if (!StrategicMCTS)
	{
		UE_LOG(LogTemp, Error, TEXT("🎯 TeamLeader '%s': Missing MCTS"), *TeamName);
		return;
	}

	TArray<AActor*> AliveAgents = GetAliveFollowers();

	if (AliveAgents.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': No alive agents, skipping MCTS"), *TeamName);
		return;
	}

	if (!FriendlyObjective || !HostileObjective)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': Missing objectives, skipping MCTS"), *TeamName);
		return;
	}

	bMCTSRunning = true;
	LastMCTSTime = FPlatformTime::Seconds();

	UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v8.0] '%s': STARTED (ASYNC) - %d agents"),
		*TeamName,
		AliveAgents.Num());

	// v8.0: Pre-cache observations on game thread for thread-safe async execution
	TMap<AActor*, FObservationElement> CachedObservations;
	for (AActor* Agent : AliveAgents)
	{
		if (!Agent) continue;

		UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
		if (FollowerComp)
		{
			// Build observation NOW on the game thread (safe to call GetWorld() here)
			FObservationElement Obs = FollowerComp->BuildLocalObservation();
			CachedObservations.Add(Agent, Obs);
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("🎯 [MCTS v8.0] '%s': Cached %d observations for async execution"),
		*TeamName,
		CachedObservations.Num());

	// Create async task (v8.0 API)
	AsyncMCTSTask = new FAsyncTask<FMCTSAsyncTask>(
		StrategicMCTS,
		AliveAgents,
		FriendlyObjective,
		HostileObjective,
		MCTSSimulations,
		CachedObservations // Pass cached observations for thread safety
	);

	// Start background execution
	AsyncMCTSTask->StartBackgroundTask();

	UE_LOG(LogTemp, Verbose, TEXT("🎯 [MCTS v8.0] '%s': Async task started, will poll for completion in Tick"),
		*TeamName);
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

			// v8.0: Enhanced logging
			FString ObjectiveName = Assignment.TargetObjective ? Assignment.TargetObjective->GetName() : TEXT("None");

			UE_LOG(LogTemp, Warning, TEXT("🎯 [ASSIGNMENT v8.0] Agent '%s' → Strategy '%s' (Objective=%s, Priority=%d, Value=%.2f, Visits=%d)"),
				*Agent->GetName(),
				*UEnum::GetValueAsString(Assignment.Strategy),
				*ObjectiveName,
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
	if (!Enemy) return;

	if (!KnownEnemies.Contains(Enemy))
	{
		KnownEnemies.Add(Enemy);
		UE_LOG(LogTemp, Warning, TEXT("[TEAM LEADER] '%s': Registered NEW enemy: %s (Total enemies: %d)"),
			*TeamName, *Enemy->GetName(), KnownEnemies.Num());
	}
}

void UTeamLeaderComponent::UnregisterEnemy(AActor* Enemy)
{
	if (!Enemy) return;

	if (KnownEnemies.Contains(Enemy))
	{
		KnownEnemies.Remove(Enemy);
		TotalEnemiesEliminated++;

		UE_LOG(LogTemp, Log, TEXT("TeamLeader '%s': Enemy %s eliminated (Remaining: %d)"),
			*TeamName, *Enemy->GetName(), KnownEnemies.Num());

		// Trigger event
		ProcessStrategicEvent(EStrategicEvent::EnemyEliminated, Enemy, Enemy->GetActorLocation(), 6);
	}
}

TArray<AActor*> UTeamLeaderComponent::GetKnownEnemies() const
{
	return KnownEnemies.Array();
}

void UTeamLeaderComponent::ClearKnownEnemies()
{
	int32 ClearedCount = KnownEnemies.Num();
	KnownEnemies.Empty();

	UE_LOG(LogTemp, Warning, TEXT("[TEAM LEADER] '%s': Cleared %d known enemies (episode reset)"),
		*TeamName, ClearedCount);
}

//------------------------------------------------------------------------------
// METRICS
//------------------------------------------------------------------------------

FTeamMetrics UTeamLeaderComponent::GetTeamMetrics() const
{
	FTeamMetrics Metrics;

	Metrics.TotalFollowers = Followers.Num();
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
	if (!GetOwner()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FVector LeaderPos = GetOwner()->GetActorLocation();

	// Draw team centroid
	if (CurrentTeamObservation.AliveFollowers > 0)
	{
		DrawDebugSphere(World, CurrentTeamObservation.TeamCentroid, 100.0f, 12,
			TeamColor.ToFColor(true), false, 0.5f, 0, 3.0f);
	}

	// ============================================
	// v8.0: MCTS Strategy Assignment Visualization
	// ============================================
	if (bEnableDebugDrawing)
	{
		for (const auto& Pair : CurrentAssignments)
		{
			AActor* Agent = Pair.Key;
			const FStrategyAssignment& Assignment = Pair.Value;

			if (!Agent) continue;

			FVector AgentPos = Agent->GetActorLocation();

			// Get objective position
			FVector ObjectivePos = FVector::ZeroVector;
			if (Assignment.TargetObjective && IsValid(Assignment.TargetObjective))
			{
				ObjectivePos = Assignment.TargetObjective->GetActorLocation();
			}

			// Draw MCTS assignment arrow (yellow arrow: Agent → Objective)
			if (!ObjectivePos.IsZero())
			{
				DrawDebugDirectionalArrow(
					World,
					AgentPos,
					ObjectivePos,
					100.0f,  // Arrow size
					FColor::Yellow,
					false, 0.0f, 0, 3.0f  // Thickness
				);
			}

			// Draw strategy and value estimate (green text above agent)
			FString InfoText = FString::Printf(TEXT("%s | V=%.2f"),
				*UEnum::GetValueAsString(Assignment.Strategy),
				Assignment.ExpectedValue);

			DrawDebugString(
				World,
				AgentPos + FVector(0, 0, 150),
				InfoText,
				nullptr,
				FColor::Green,
				0.0f,
				true  // Draw shadow
			);

			// Draw objective type (cyan text above objective)
			if (Assignment.TargetObjective && !ObjectivePos.IsZero())
			{
				FString ObjectiveText = Assignment.TargetObjective->GetName();
				DrawDebugString(
					World,
					ObjectivePos + FVector(0, 0, 100),
					ObjectiveText,
					nullptr,
					FColor::Cyan,
					0.0f,
					true  // Draw shadow
				);
			}
		}
	}
	// ============================================
	// End v8.0 MCTS Visualization
	// ============================================

	// Draw lines to each follower (legacy)
	for (AActor* Follower : GetAliveFollowers())
	{
		if (!Follower) continue;

		FVector FollowerPos = Follower->GetActorLocation();
		DrawDebugLine(World, LeaderPos, FollowerPos, TeamColor.ToFColor(true), false, 0.5f, 0, 2.0f);

		// Draw strategy type above follower (v8.0 legacy - redundant with v8.0 visualization)
		if (!bEnableDebugDrawing)
		{
			if (const FStrategyAssignment* AssignmentPtr = CurrentAssignments.Find(Follower))
			{
				FString StrategyText = UEnum::GetValueAsString(AssignmentPtr->Strategy);
				DrawDebugString(World, FollowerPos + FVector(0, 0, 150), StrategyText, nullptr, FColor::White, 0.5f, true);
			}
		}
	}

	// Draw enemy indicators
	for (AActor* Enemy : KnownEnemies)
	{
		if (!Enemy) continue;

		FVector EnemyPos = Enemy->GetActorLocation();
		DrawDebugSphere(World, EnemyPos, 50.0f, 8, FColor::Red, false, 0.5f);
	}

	// Draw team info
	FString TeamInfo = FString::Printf(TEXT("%s\nFollowers: %d/%d\nHealth: %.1f%%\nEnemies: %d"),
		*TeamName,
		GetAliveFollowers().Num(),
		Followers.Num(),
		CurrentTeamObservation.AverageTeamHealth,
		KnownEnemies.Num());

	DrawDebugString(World, LeaderPos + FVector(0, 0, 200), TeamInfo, nullptr, TeamColor.ToFColor(true), 0.5f, true);
}