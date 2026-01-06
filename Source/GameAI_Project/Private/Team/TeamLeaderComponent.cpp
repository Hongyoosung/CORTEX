#include "Team/TeamLeaderComponent.h"
#include "Team/FollowerAgentComponent.h"
#include "Team/ObjectiveManager.h"
#include "Team/Objective.h"
#include "AI/MCTS/MCTS.h"
#include "AI/MCTS/MCTSAsyncTask.h"
#include "RL/CurriculumManager.h"
#include "RL/RLTypes.h"  // v5.0: EStrategyType
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
// v6.0: TEAM LEADER COMPONENT
// MCTS assigns objectives → RL selects strategies → Rules execute
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


	// Initialize objective manager (v3.0 Combat Refactoring)
	if (!ObjectiveManager)
	{
		ObjectiveManager = NewObject<UObjectiveManager>(GetOwner());
		if (ObjectiveManager)
		{
			ObjectiveManager->RegisterComponent();
		}
	}

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

	// Auto-discover objectives from level (v3.0 - Capture/Rescue support)
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
				RunObjectiveDecisionMakingAsync();
			}
			else
			{
				RunObjectiveDecisionMaking();
			}
		}
	}

	// Check if async MCTS task completed (v6.0)
	if (AsyncMCTSTask != nullptr && AsyncMCTSTask->IsDone())
	{
		// Get results from completed task (v6.0 API)
		FObjectiveAssignment Assignment = AsyncMCTSTask->GetTask().GetResults();
		float ExecutionTime = AsyncMCTSTask->GetTask().GetExecutionTime();

		UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v6.0] '%s': Async task completed in %.2fms - Value=%.2f, Visits=%d"),
			*TeamName, ExecutionTime, Assignment.ExpectedValue, Assignment.VisitCount);

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

		// Process results on game thread (v6.0)
		ApplyObjectiveAssignment(Assignment);
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
			UE_LOG(LogTemp, Warning, TEXT("[FORMATION] '%s': Inter-agent distances (%d agents):"), *TeamName, AliveFollowers.Num());

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

					UE_LOG(LogTemp, Warning, TEXT("[FORMATION]   '%s' <-> '%s': %.1f cm"),
						*Agent1->GetName(),
						*Agent2->GetName(),
						Distance);

					MinDistance = FMath::Min(MinDistance, Distance);
					MaxDistance = FMath::Max(MaxDistance, Distance);
					TotalDistance += Distance;
					PairCount++;
				}
			}

			if (PairCount > 0)
			{
				float AvgDistance = TotalDistance / PairCount;
				UE_LOG(LogTemp, Warning, TEXT("[FORMATION] '%s': Distance stats - Min: %.1f cm, Max: %.1f cm, Avg: %.1f cm"),
					*TeamName, MinDistance, MaxDistance, AvgDistance);
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
	if (!ObjectiveManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': Cannot discover objectives - ObjectiveManager not initialized"), *TeamName);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 ObjectivesDiscovered = 0;

	//==========================================================================
	// CAPTURE ZONES
	//==========================================================================
	TArray<AActor*> CaptureZones;
	UGameplayStatics::GetAllActorsWithTag(World, FName("CaptureZone"), CaptureZones);

	for (AActor* Zone : CaptureZones)
	{
		if (Zone && IsValid(Zone))
		{
			UObjective* CaptureObj = ObjectiveManager->CreateCaptureObjective(
				Zone->GetActorLocation(),
				8  // High priority
			);

			if (CaptureObj)
			{
				ObjectivesDiscovered++;
				UE_LOG(LogTemp, Log, TEXT("TeamLeader '%s': Discovered CaptureZone at %s"),
					*TeamName, *Zone->GetActorLocation().ToString());
			}
		}
	}

	//==========================================================================
	// RESCUE ZONES (wounded allies marked with "RescueTarget" tag)
	//==========================================================================
	TArray<AActor*> RescueTargets;
	UGameplayStatics::GetAllActorsWithTag(World, FName("RescueTarget"), RescueTargets);

	for (AActor* Target : RescueTargets)
	{
		if (Target && IsValid(Target))
		{
			// Check if target is actually wounded (health < 50%)
			UHealthComponent* HealthComp = Target->FindComponentByClass<UHealthComponent>();
			if (HealthComp && HealthComp->GetCurrentHealth() < HealthComp->GetMaxHealth() * 0.5f)
			{
				UObjective* RescueObj = ObjectiveManager->CreateRescueObjective(
					Target,
					7  // Medium-high priority
				);

				if (RescueObj)
				{
					ObjectivesDiscovered++;
					UE_LOG(LogTemp, Log, TEXT("TeamLeader '%s': Discovered RescueTarget '%s' at %s (Health: %.1f)"),
						*TeamName, *Target->GetName(), *Target->GetActorLocation().ToString(),
						HealthComp->GetCurrentHealth());
				}
			}
		}
	}

	//==========================================================================
	// DEFEND ZONES
	//==========================================================================
	TArray<AActor*> DefendZones;
	UGameplayStatics::GetAllActorsWithTag(World, FName("DefendZone"), DefendZones);

	for (AActor* Zone : DefendZones)
	{
		if (Zone && IsValid(Zone))
		{
			UObjective* DefendObj = ObjectiveManager->CreateDefendObjective(
				Zone->GetActorLocation(),
				7  // Medium-high priority
			);

			if (DefendObj)
			{
				ObjectivesDiscovered++;
				UE_LOG(LogTemp, Log, TEXT("TeamLeader '%s': Discovered DefendZone at %s"),
					*TeamName, *Zone->GetActorLocation().ToString());
			}
		}
	}

	UE_LOG(LogTemp, Display, TEXT("✅ TeamLeader '%s': Auto-discovered %d world objectives (MCTS will now consider these)"),
		*TeamName, ObjectivesDiscovered);
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
	CurrentObjectives.Remove(Follower);

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
			RunObjectiveDecisionMakingAsync();
		}
		else
		{
			RunObjectiveDecisionMaking();
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
			case EStrategicEvent::AmbushDetected:
			case EStrategicEvent::LowTeamHealth:
			case EStrategicEvent::ObjectiveComplete:
			case EStrategicEvent::ObjectiveFailed:
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
		case EStrategicEvent::AmbushDetected:
		case EStrategicEvent::LowTeamHealth:
		case EStrategicEvent::ObjectiveComplete:
		case EStrategicEvent::ObjectiveFailed:
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
			RunObjectiveDecisionMakingAsync();
		}
		else
		{
			RunObjectiveDecisionMaking();
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

	FTeamObservation TeamObs = FTeamObservation::BuildFromTeam(
		AliveFollowers,
		ObjectiveActor,
		Enemies
	);

	return TeamObs;
}





//==============================================================================
// v3.0 COMBAT REFACTORING: OBJECTIVE-BASED DECISION MAKING
//==============================================================================

//==============================================================================
// v6.0: OBJECTIVE ASSIGNMENT (SYNC)
//==============================================================================

void UTeamLeaderComponent::RunObjectiveDecisionMaking()
{
	if (bMCTSRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': MCTS already running"), *TeamName);
		return;
	}

	if (!StrategicMCTS || !ObjectiveManager)
	{
		UE_LOG(LogTemp, Error, TEXT("🎯 TeamLeader '%s': Missing MCTS or ObjectiveManager"), *TeamName);
		return;
	}

	TArray<AActor*> AliveAgents = GetAliveFollowers();
	TArray<UObjective*> ActiveObjectives = ObjectiveManager->GetActiveObjectives();

	if (AliveAgents.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': No alive agents, skipping MCTS"), *TeamName);
		return;
	}

	if (ActiveObjectives.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': No active objectives, skipping MCTS"), *TeamName);
		return;
	}

	bMCTSRunning = true;
	float StartTime = FPlatformTime::Seconds();
	LastMCTSTime = StartTime;

	UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v6.0] '%s': STARTED (SYNC) - %d agents, %d objectives"),
		*TeamName,
		AliveAgents.Num(),
		ActiveObjectives.Num());

	// Run MCTS to find best agent-to-objective assignment (v6.0)
	FObjectiveAssignment Assignment = StrategicMCTS->RunObjectiveAssignment(
		AliveAgents,
		ActiveObjectives,
		MCTSSimulations
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

	UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v6.0] '%s': COMPLETED in %.2fms - Value=%.2f, Visits=%d"),
		*TeamName,
		ExecutionTime,
		Assignment.ExpectedValue,
		Assignment.VisitCount);

	// Apply assignment (v6.0)
	ApplyObjectiveAssignment(Assignment);
}


//==============================================================================
// v6.0: OBJECTIVE ASSIGNMENT (ASYNC)
//==============================================================================

void UTeamLeaderComponent::RunObjectiveDecisionMakingAsync()
{
	if (bMCTSRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': MCTS already running"), *TeamName);
		return;
	}

	if (!StrategicMCTS || !ObjectiveManager)
	{
		UE_LOG(LogTemp, Error, TEXT("🎯 TeamLeader '%s': Missing MCTS or ObjectiveManager"), *TeamName);
		return;
	}

	TArray<AActor*> AliveAgents = GetAliveFollowers();
	TArray<UObjective*> ActiveObjectives = ObjectiveManager->GetActiveObjectives();

	if (AliveAgents.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': No alive agents, skipping MCTS"), *TeamName);
		return;
	}

	if (ActiveObjectives.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': No active objectives, skipping MCTS"), *TeamName);
		return;
	}

	bMCTSRunning = true;
	LastMCTSTime = FPlatformTime::Seconds();

	UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v6.0] '%s': STARTED (ASYNC) - %d agents, %d objectives"),
		*TeamName,
		AliveAgents.Num(),
		ActiveObjectives.Num());

	// Create async task (v6.0 API)
	AsyncMCTSTask = new FAsyncTask<FMCTSAsyncTask>(
		StrategicMCTS,
		AliveAgents,
		ActiveObjectives,
		MCTSSimulations
	);

	// Start background execution
	AsyncMCTSTask->StartBackgroundTask();

	UE_LOG(LogTemp, Verbose, TEXT("🎯 [MCTS v6.0] '%s': Async task started, will poll for completion in Tick"),
		*TeamName);
}


//==============================================================================
// v6.0: APPLY OBJECTIVE ASSIGNMENT
//==============================================================================

void UTeamLeaderComponent::ApplyObjectiveAssignment(const FObjectiveAssignment& Assignment)
{
	UE_LOG(LogTemp, Warning, TEXT("🎯 [ASSIGNMENT v6.0] '%s': Applying assignment - %d agents, Value=%.2f, Visits=%d"),
		*TeamName,
		Assignment.AgentToObjective.Num(),
		Assignment.ExpectedValue,
		Assignment.VisitCount);

	// Log objective summary
	TMap<EObjectiveType, int32> ObjectiveCounts;
	for (const auto& Pair : Assignment.AgentToObjective)
	{
		if (Pair.Value)
		{
			ObjectiveCounts.FindOrAdd(Pair.Value->Type, 0)++;
		}
	}

	UE_LOG(LogTemp, Display, TEXT("🎯 [ASSIGNMENT v6.0] '%s': Objective breakdown:"),
		*TeamName);
	for (const auto& CountPair : ObjectiveCounts)
	{
		UE_LOG(LogTemp, Display, TEXT("   - %s: %d agents"),
			*UEnum::GetValueAsString(CountPair.Key),
			CountPair.Value);
	}

	// Apply assignments to followers
	for (const auto& Pair : Assignment.AgentToObjective)
	{
		AActor* Agent = Pair.Key;
		UObjective* Objective = Pair.Value;

		if (!Agent || !Objective)
		{
			UE_LOG(LogTemp, Error, TEXT("🎯 [ASSIGNMENT v6.0] Skipping invalid pair: Agent=%s, Objective=%s"),
				Agent ? *Agent->GetName() : TEXT("NULL"),
				Objective ? TEXT("Valid") : TEXT("NULL"));
			continue;
		}

		// Update current assignments
		CurrentObjectives.Add(Agent, Objective);

		// Notify follower
		UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
		if (FollowerComp)
		{
			FollowerComp->SetCurrentObjective(Objective);

			// v6.0: Build objective description from type and target
			FString ObjectiveDesc = FString::Printf(TEXT("%s"), *UEnum::GetValueAsString(Objective->Type));
			if (Objective->TargetActor)
			{
				ObjectiveDesc += FString::Printf(TEXT(" (%s)"), *Objective->TargetActor->GetName());
			}
			else if (!Objective->TargetLocation.IsZero())
			{
				ObjectiveDesc += FString::Printf(TEXT(" at (%.0f,%.0f,%.0f)"),
					Objective->TargetLocation.X, Objective->TargetLocation.Y, Objective->TargetLocation.Z);
			}

			UE_LOG(LogTemp, Warning, TEXT("🎯 [ASSIGNMENT v6.0] Agent '%s' → Objective '%s' (Priority=%d)"),
				*Agent->GetName(),
				*ObjectiveDesc,
				Objective->Priority);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("🎯 [ASSIGNMENT v6.0] Agent '%s' has no FollowerAgentComponent!"),
				*Agent->GetName());
		}
	}

	// Broadcast event (v6.0: Convert TObjectPtr to raw pointers)
	FObjectiveAssignmentMap AssignmentMap;
	for (const auto& Pair : Assignment.AgentToObjective)
	{
		AssignmentMap.Objectives.Add(Pair.Key.Get(), Pair.Value.Get());
	}
	OnStrategicDecisionMade.Broadcast(AssignmentMap);

	bMCTSRunning = false;

	UE_LOG(LogTemp, Warning, TEXT("🎯 [ASSIGNMENT v6.0] '%s': All assignments complete"),
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
// DEBUG VISUALIZATION
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

	// Draw lines to each follower
	for (AActor* Follower : GetAliveFollowers())
	{
		if (!Follower) continue;

		FVector FollowerPos = Follower->GetActorLocation();
		DrawDebugLine(World, LeaderPos, FollowerPos, TeamColor.ToFColor(true), false, 0.5f, 0, 2.0f);

		// Draw objective type above follower (v3.0)
		if (UObjective* const* ObjectivePtr = CurrentObjectives.Find(Follower))
		{
			if (*ObjectivePtr)
			{
				FString ObjectiveText = UEnum::GetValueAsString((*ObjectivePtr)->Type);
				DrawDebugString(World, FollowerPos + FVector(0, 0, 150), ObjectiveText, nullptr, FColor::White, 0.5f, true);
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