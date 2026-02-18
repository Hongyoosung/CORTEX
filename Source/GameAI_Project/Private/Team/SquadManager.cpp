// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/SquadManager.h"
#include "Team/TeamManager.h"
#include "Characters/MocCharacter.h"
#include "AI/MCTS/TeamMCTS.h"
#include "AI/Models/TeamWorldModel.h"
#include "AI/Training/TeamDataCollector.h"
#include "Types/RewardTypes.h"
#include "Core/MocGameMode.h"
#include "Actors/CapturePoint.h"
#include "Actors/PickupBase.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"

// Static reference for console commands (one per team, use Team 0 by default)
static TWeakObjectPtr<ASquadManager> GDebugSquadManager;

ASquadManager::ASquadManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // Check every 100ms

	// Initialize state
	TeamID = 0;
	TimeSinceLastPlan = 0.0f;
	ActiveTacticalPlay = ETacticalPlay::StandardComp;
	PlanConfidence = 0.5f;
	PlanningCycleCount = 0;
	EventDrivenReplanCount = 0;
	bHasPreviousState = false;
	bHealthCriticalTriggered = false;

	// Initialize role assignments (default: standard comp)
	CurrentRoleAssignments = {
		EStrategyType::Assault,
		EStrategyType::Assault,
		EStrategyType::Defend,
		EStrategyType::Defend,
		EStrategyType::Support
	};

	// Debug console commands
	static FAutoConsoleCommand CmdSquadState(
		TEXT("moc.debug.squadstate"),
		TEXT("Print current tactical play, role assignments, confidence, and MCTS stats"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (ASquadManager* SM = GDebugSquadManager.Get())
			{
				UE_LOG(LogTemp, Display, TEXT("=== Squad Commander State (Team %d) ==="), SM->TeamID);
				UE_LOG(LogTemp, Display, TEXT("  Tactical Play: %s"), *UEnum::GetValueAsString(SM->ActiveTacticalPlay));
				UE_LOG(LogTemp, Display, TEXT("  Confidence: %.2f"), SM->PlanConfidence);
				UE_LOG(LogTemp, Display, TEXT("  Planning Cycles: %d (Event Replans: %d)"), SM->PlanningCycleCount, SM->EventDrivenReplanCount);
				UE_LOG(LogTemp, Display, TEXT("  Data Collection Mode: %s"), SM->bDataCollectionMode ? TEXT("Yes") : TEXT("No"));
				for (int32 i = 0; i < SM->CurrentRoleAssignments.Num(); ++i)
				{
					UE_LOG(LogTemp, Display, TEXT("  Agent %d: %s"), i, *UEnum::GetValueAsString(SM->CurrentRoleAssignments[i]));
				}
				if (SM->TeamMCTSPlanner)
				{
					UE_LOG(LogTemp, Display, TEXT("  MCTS Last Iterations: %d"), SM->TeamMCTSPlanner->GetLastIterationCount());
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("No SquadManager available for debug"));
			}
		})
	);

	static FAutoConsoleCommand CmdForceReplan(
		TEXT("moc.debug.forcereplan"),
		TEXT("Trigger immediate tactical replanning"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (ASquadManager* SM = GDebugSquadManager.Get())
			{
				UE_LOG(LogTemp, Display, TEXT("Forcing replan for Team %d"), SM->TeamID);
				SM->PerformTacticalPlanning();
				SM->TimeSinceLastPlan = 0.0f;
			}
		})
	);

	static FAutoConsoleCommand CmdToggleViz(
		TEXT("moc.debug.toggleviz"),
		TEXT("Toggle 3D debug visualization"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (ASquadManager* SM = GDebugSquadManager.Get())
			{
				SM->bShowDebugInfo = !SM->bShowDebugInfo;
				SM->bDrawRoleAssignments = SM->bShowDebugInfo;
				UE_LOG(LogTemp, Display, TEXT("Debug visualization: %s"), SM->bShowDebugInfo ? TEXT("ON") : TEXT("OFF"));
			}
		})
	);

	static FAutoConsoleCommand CmdObserver(
		TEXT("moc.debug.observer"),
		TEXT("Print current 60-dim observation tensor"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (ASquadManager* SM = GDebugSquadManager.Get())
			{
				FTeamWorldState State = SM->CollectTeamState();
				TArray<float> Tensor = State.ToTensor();
				UE_LOG(LogTemp, Display, TEXT("=== Team State Tensor (%d dims) ==="), Tensor.Num());
				FString TensorStr;
				for (int32 i = 0; i < Tensor.Num(); ++i)
				{
					TensorStr += FString::Printf(TEXT("%.3f "), Tensor[i]);
					if ((i + 1) % 10 == 0)
					{
						UE_LOG(LogTemp, Display, TEXT("  [%d-%d]: %s"), i - 9, i, *TensorStr);
						TensorStr.Empty();
					}
				}
				if (!TensorStr.IsEmpty())
				{
					UE_LOG(LogTemp, Display, TEXT("  [%d+]: %s"), Tensor.Num() - (Tensor.Num() % 10), *TensorStr);
				}
			}
		})
	);

	static FAutoConsoleCommand CmdTiming(
		TEXT("moc.debug.timing"),
		TEXT("Print MCTS duration and world model latency"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (ASquadManager* SM = GDebugSquadManager.Get())
			{
				UE_LOG(LogTemp, Display, TEXT("=== Timing Stats (Team %d) ==="), SM->TeamID);
				UE_LOG(LogTemp, Display, TEXT("  MCTS Time Budget: %.1f ms"), SM->MCTSTimeBudget * 1000.0f);
				if (SM->TeamWorldModel)
				{
					UE_LOG(LogTemp, Display, TEXT("  World Model Avg Latency: %.2f ms"), SM->TeamWorldModel->GetAverageLatency());
				}
				else
				{
					UE_LOG(LogTemp, Display, TEXT("  World Model: Not initialized"));
				}
				UE_LOG(LogTemp, Display, TEXT("  Last Planning Duration: %.2f ms"), SM->LastPlanningDurationMs);
			}
		})
	);
}

void ASquadManager::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("Squad Commander initialized for Team %d"), TeamID);
}

void ASquadManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	TimeSinceLastPlan += DeltaTime;

	// Check if replanning needed (interval-based)
	if (ShouldReplan())
	{
		PerformTacticalPlanning();
		TimeSinceLastPlan = 0.0f;
	}

	// Health-critical event detection
	if (TeamManager)
	{
		FTeamWorldState CurrentState = CollectTeamState();
		bool bIsCritical = CurrentState.IsTeamHealthCritical();

		if (bIsCritical && !bHealthCriticalTriggered)
		{
			bHealthCriticalTriggered = true;
			ReplanMCTSOnCriticalEvent(ECriticalEventType::HealthCritical, nullptr);
		}
		else if (!bIsCritical)
		{
			bHealthCriticalTriggered = false;
		}
	}

	// End-to-end validation logging (behind debug flag)
	if (bShowDebugInfo)
	{
		ValidationTickCounter += DeltaTime;
		if (ValidationTickCounter >= 2.0f) // Log every 2 seconds
		{
			ValidationTickCounter = 0.0f;

			// Verify observer output dimensionality
			FTeamWorldState ValidationState = CollectTeamState();
			TArray<float> Tensor = ValidationState.ToTensor();
			UE_LOG(LogTemp, Verbose, TEXT("[Validation] Team %d: Observer tensor dims=%d (expected ~60)"), TeamID, Tensor.Num());

			// Verify all 5 agents have role assignments
			int32 AssignedCount = CurrentRoleAssignments.Num();
			if (AssignedCount != 5)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Validation] Team %d: Role assignments=%d (expected 5)"), TeamID, AssignedCount);
			}

			// Log timing
			UE_LOG(LogTemp, Verbose, TEXT("[Validation] Team %d: LastPlanning=%.2fms, WorldModel=%.2fms"),
				TeamID, LastPlanningDurationMs,
				TeamWorldModel ? TeamWorldModel->GetAverageLatency() : 0.0f);
		}
	}

	// Draw debug visualization
	if (bShowDebugInfo && bDrawRoleAssignments)
	{
		DrawDebugVisualization();
	}
}

void ASquadManager::Initialize(int32 InTeamID, ATeamManager* InTeamManager)
{
	TeamID = InTeamID;
	TeamManager = InTeamManager;

	// Initialize Team World Model (NEW - v10.2 Week 3)
	TeamWorldModel = NewObject<UTeamWorldModel>(this);
	bool bModelLoaded = false;

	if (TeamWorldModel && !TeamWorldModelPath.IsEmpty())
	{
		bModelLoaded = TeamWorldModel->InitModel(TeamWorldModelPath);

		if (bModelLoaded)
		{
			UE_LOG(LogTemp, Log, TEXT("Team %d: World model loaded from %s"), TeamID, *TeamWorldModelPath);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Team %d: Failed to load world model from %s - using data collection mode"),
				TeamID, *TeamWorldModelPath);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Team %d: No world model path specified - using data collection mode"), TeamID);
	}

	// Initialize Team MCTS planner (NEW - v10.2 Week 3)
	TeamMCTSPlanner = NewObject<UTeamMCTS>(this);

	if (TeamMCTSPlanner && TeamWorldModel)
	{
		FTeamMCTSConfig MCTSConfig;
		MCTSConfig.TimeBudgetSeconds = MCTSTimeBudget;  // 0.015s from header
		MCTSConfig.BatchSize = MCTSBatchSize;            // 8 from header
		MCTSConfig.MaxIterations = 50;

		TeamMCTSPlanner->Setup(TeamWorldModel, MCTSConfig);

		if (bModelLoaded)
		{
			UE_LOG(LogTemp, Log, TEXT("Team %d: Team MCTS initialized with loaded world model"), TeamID);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Team %d: Team MCTS initialized but world model not loaded - MCTS will not work until model is trained"),
				TeamID);
		}
	}

	// Initialize training data collector (NEW - v10.2 Week 2)
	DataCollector = NewObject<UTeamDataCollector>(this);
	if (DataCollector)
	{
		DataCollector->bIsRecording = true; // Enable by default (can be toggled in editor)
		DataCollector->BeginRecording(FMath::RandRange(1000, 9999));
		UE_LOG(LogTemp, Log, TEXT("Team %d: Data collector initialized and recording started"), TeamID);
	}

	// Automatically enable data collection mode if no model is loaded
	if (!bModelLoaded)
	{
		bDataCollectionMode = true;
		UE_LOG(LogTemp, Display, TEXT("Team %d: Auto-enabled data collection mode (no world model available)"), TeamID);
	}

	// Wire critical event delegates (v10.2 - Event-driven replanning)
	if (TeamManager)
	{
		// Kill events
		TeamManager->OnAgentKilled.AddDynamic(this, &ASquadManager::OnAgentKilledHandler);
		UE_LOG(LogTemp, Log, TEXT("Team %d: Bound OnAgentKilled delegate"), TeamID);

		// Capture point events - bind to all capture points via GameMode
		if (UWorld* World = GetWorld())
		{
			if (AMocGameMode* GameMode = Cast<AMocGameMode>(World->GetAuthGameMode()))
			{
				TArray<ACapturePoint*> CapturePoints = GameMode->GetAllCapturePoints();
				for (ACapturePoint* Point : CapturePoints)
				{
					if (Point)
					{
						Point->OnPointCaptured.AddDynamic(this, &ASquadManager::OnPointCapturedHandler);
					}
				}
				UE_LOG(LogTemp, Log, TEXT("Team %d: Bound OnPointCaptured to %d capture points"), TeamID, CapturePoints.Num());
			}
		}
	}

	// Register as debug target
	GDebugSquadManager = this;

	UE_LOG(LogTemp, Log, TEXT("Squad Commander initialized for Team %d (MCTS=%s, DataCollection=%s)"),
		TeamID,
		bModelLoaded ? TEXT("Enabled") : TEXT("Disabled"),
		bDataCollectionMode ? TEXT("Enabled") : TEXT("Disabled"));
}

bool ASquadManager::ShouldReplan() const
{
	// Interval-based replanning
	if (TimeSinceLastPlan >= PlanningInterval)
	{
		return true;
	}

	// Event-driven replanning handled via OnCriticalEvent
	return false;
}

//========================================
// Episode Management
//========================================

void ASquadManager::Reset()
{
	UE_LOG(LogTemp, Log, TEXT("[SquadManager] Resetting squad commander for Team %d"), TeamID);

	// Reset planning state
	TimeSinceLastPlan = 0.0f;
	PlanConfidence = 0.0f;
	PlanningCycleCount = 0;
	EventDrivenReplanCount = 0;

	// Reset to default tactical play
	ActiveTacticalPlay = ETacticalPlay::StandardComp;

	// Reset role assignments to default (Assault)
	CurrentRoleAssignments.Empty();
	CurrentRoleAssignments.Init(EStrategyType::Assault, 5);

	// Reset previous state tracking (for data collection)
	bHasPreviousState = false;
	PreviousTeamState = FTeamWorldState();
	PreviousTacticalPlay = ETacticalPlay::StandardComp;

	// Note: Don't reset TeamMCTSPlanner or DataCollector as they maintain their own state
	// and will be reset by their own systems if needed

	UE_LOG(LogTemp, Log, TEXT("[SquadManager] Reset complete - default assignments restored"));
}

//========================================
// Centralized Planning
//========================================

void ASquadManager::PerformTacticalPlanning()
{
	if (!TeamManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("Squad Commander: TeamManager not set, cannot plan"));
		return;
	}

	// 1. Collect global team state
	FTeamWorldState GlobalState = CollectTeamState();

	// Record transition from previous planning cycle (v10.2 Week 2 - Data Collection)
	if (bHasPreviousState && DataCollector && DataCollector->bIsRecording)
	{
		// Calculate reward based on state transition
		FCompositeReward Reward = CalculateTeamReward(PreviousTeamState, GlobalState);

		// Record (s, a, s', r) transition
		DataCollector->RecordTransition(
			PreviousTeamState,
			PreviousTacticalPlay,
			GlobalState,
			Reward
		);
	}

	// 2. Select tactical play: ε-greedy (data collection) OR MCTS (production)
	ETacticalPlay BestPlay = ETacticalPlay::StandardComp; // Default fallback
	float PlanningStartTime = FPlatformTime::Seconds();

	if (bDataCollectionMode)
	{
		// DATA COLLECTION MODE: Use ε-greedy for fast, diverse data collection
		BestPlay = SelectEpsilonGreedyAction(GlobalState);

		float SelectionTime = (FPlatformTime::Seconds() - PlanningStartTime) * 1000.0f;

		if (bShowDebugInfo)
		{
			UE_LOG(LogTemp, Display, TEXT("Team %d ε-Greedy Selection: %s (%.3f ms, ExplorationRate=%.2f)"),
				TeamID, *UEnum::GetValueAsString(BestPlay), SelectionTime, ExplorationRate);
		}
	}
	else if (TeamMCTSPlanner && TeamWorldModel && TeamWorldModel->IsModelLoaded())
	{
		// PRODUCTION MODE: Use MCTS for optimal tactical planning (requires trained world model)
		BestPlay = TeamMCTSPlanner->FindBestTacticalPlay(GlobalState);

		float PlanningTime = (FPlatformTime::Seconds() - PlanningStartTime) * 1000.0f;

		if (bShowDebugInfo)
		{
			UE_LOG(LogTemp, Display, TEXT("Team %d MCTS Planning: %.2f ms, %d iterations"),
				TeamID, PlanningTime, TeamMCTSPlanner->GetLastIterationCount());
		}

		// Warn if exceeding budget
		if (PlanningTime > MCTSTimeBudget * 1000.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("Team %d MCTS exceeded budget: %.2f ms > %.2f ms"),
				TeamID, PlanningTime, MCTSTimeBudget * 1000.0f);
		}
	}
	else
	{
		// Fallback heuristic if MCTS not available (no world model loaded)
		// This is expected during initial data collection phase before model training
		float AvgHealth = GlobalState.GetAverageHealth();
		BestPlay = (AvgHealth < 0.3f) ? ETacticalPlay::FortressDefense
			: (AvgHealth > 0.7f) ? ETacticalPlay::AggressivePush
			: ETacticalPlay::StandardComp;

		if (bShowDebugInfo)
		{
			UE_LOG(LogTemp, Display, TEXT("Team %d: Using fallback heuristic (world model not loaded)"), TeamID);
		}
	}

	// 3. Convert to role distribution
	TArray<EStrategyType> NewRoles = DecodeTacticalPlay(BestPlay);

	// 4. Broadcast to agents
	DistributeRoles(NewRoles);

	// 5. Update state
	ActiveTacticalPlay = BestPlay;
	CurrentRoleAssignments = NewRoles;
	PlanConfidence = 0.8f; // Placeholder
	PlanningCycleCount++;

	// 6. Store current state for next transition recording (v10.2 Week 2)
	PreviousTeamState = GlobalState;
	PreviousTacticalPlay = BestPlay;
	bHasPreviousState = true;

	// 7. Track overall planning duration
	LastPlanningDurationMs = (FPlatformTime::Seconds() - PlanningStartTime) * 1000.0f;

	// 8. Log for analytics
	LogPlanningDecision(BestPlay, PlanConfidence);
}

void ASquadManager::ReplanMCTSOnCriticalEvent(ECriticalEventType EventType, AActor* InstigatorActor)
{
	// Immediate replanning on high-volatility events
	if (InstigatorActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("Squad Commander: Critical event %s by %s - triggering replan"),
			*UEnum::GetValueAsString(EventType), *InstigatorActor->GetName());
	}
	

	PerformTacticalPlanning();
	EventDrivenReplanCount++;
	TimeSinceLastPlan = 0.0f; // Reset timer to avoid double-planning
}

FTeamWorldState ASquadManager::CollectTeamState() const
{
	FTeamWorldState State;

	if (!TeamManager)
	{
		return State; // Return default state
	}

	// Get team agents
	TArray<AMocCharacter*> TeamAgents = TeamManager->GetTeamAgents(TeamID);
	TArray<AMocCharacter*> EnemyAgents = TeamManager->GetEnemyAgents(TeamID);

	// Collect friendly unit data
	for (int32 i = 0; i < FMath::Min(5, TeamAgents.Num()); ++i)
	{
		if (TeamAgents[i])
		{
			State.FriendlyPositions[i] = TeamAgents[i]->GetActorLocation();
			State.FriendlyHealths[i] = TeamAgents[i]->GetHealthPercentage_Implementation();
			State.FriendlyCooldowns[i] = TeamAgents[i]->GetWeaponCooldown_Implementation();
			State.FriendlyAlive[i] = TeamAgents[i]->IsAlive_Implementation();

			// Get current strategy (if implemented)
			// State.FriendlyStrategies[i] = TeamAgents[i]->GetCommandedStrategy();
		}
	}

	// Collect enemy data (with fog of war)
	for (int32 i = 0; i < FMath::Min(5, EnemyAgents.Num()); ++i)
	{
		if (EnemyAgents[i])
		{
			// Use FogOfWarManager for last known positions
			State.EnemyPositions[i] = EnemyAgents[i]->GetActorLocation();
			State.EnemyHealths[i] = EnemyAgents[i]->GetHealthPercentage_Implementation();
			State.EnemyAlive[i] = EnemyAgents[i]->IsAlive_Implementation();
			State.EnemyConfidences[i] = 1.0f; // Placeholder: should use FoW decay
		}
	}

	// Collect capture point ownership
	if (UWorld* World = GetWorld())
	{
		if (AMocGameMode* GameMode = Cast<AMocGameMode>(World->GetAuthGameMode()))
		{
			// Capture points: -1 = enemy, 0 = neutral, +1 = friendly
			TArray<ACapturePoint*> CapturePoints = GameMode->GetAllCapturePoints();
			for (int32 i = 0; i < FMath::Min(5, CapturePoints.Num()); ++i)
			{
				if (CapturePoints[i])
				{
					ECapturePointOwnership Ownership = CapturePoints[i]->GetOwnership();
					if (Ownership == ECapturePointOwnership::Neutral)
					{
						State.CapturePointOwnership[i] = 0;
					}
					else
					{
						// Map team ownership relative to this squad's team
						int32 OwningTeamID = CapturePoints[i]->GetOwningTeamID();
						State.CapturePointOwnership[i] = (OwningTeamID == TeamID) ? 1 : -1;
					}
				}
			}

			// Pickup availability bitmask
			int32 PickupBitmask = 0;
			int32 BitIndex = 0;

			// Query all pickup actors in the world
			TArray<AActor*> PickupActors;
			UGameplayStatics::GetAllActorsOfClass(World, APickupBase::StaticClass(), PickupActors);
			for (int32 i = 0; i < FMath::Min(32, PickupActors.Num()); ++i)
			{
				if (APickupBase* Pickup = Cast<APickupBase>(PickupActors[i]))
				{
					if (Pickup->IsAvailable())
					{
						PickupBitmask |= (1 << BitIndex);
					}
					BitIndex++;
				}
			}
			State.PickupAvailability = PickupBitmask;

			// Time remaining (normalized 0-1)
			float MaxDuration = GameMode->MaxMatchDuration;
			if (MaxDuration > 0.0f)
			{
				State.TimeRemaining = FMath::Clamp(GameMode->GetTimeRemaining() / MaxDuration, 0.0f, 1.0f);
			}
		}
	}

	return State;
}

FTeamWorldState ASquadManager::GetGlobalTeamState() const
{
	return CollectTeamState();
}

EStrategyType ASquadManager::GetAgentStrategy(int32 AgentIndex) const
{
	if (AgentIndex >= 0 && AgentIndex < CurrentRoleAssignments.Num())
	{
		return CurrentRoleAssignments[AgentIndex];
	}

	return EStrategyType::Assault; // Default fallback
}

TArray<EStrategyType> ASquadManager::DecodeTacticalPlay(ETacticalPlay Play) const
{
	TArray<EStrategyType> Roles;

	switch (Play)
	{
	case ETacticalPlay::AllOutRush:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Assault
		};
		break;

	case ETacticalPlay::AggressivePush:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Support
		};
		break;

	case ETacticalPlay::Phalanx:
		Roles = {
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Support,
			EStrategyType::Support,
			EStrategyType::Support
		};
		break;

	case ETacticalPlay::StandardComp:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Support
		};
		break;

	case ETacticalPlay::FortressDefense:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend
		};
		break;

	case ETacticalPlay::TurtleFormation:
		Roles = {
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend
		};
		break;

	case ETacticalPlay::BaitStrategy:
		Roles = {
			EStrategyType::Assault, // Bait unit
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend
		};
		break;

	case ETacticalPlay::PincerManeuver:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Support,
			EStrategyType::Support
		};
		break;

	case ETacticalPlay::HealerComp:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Defend,
			EStrategyType::Support,
			EStrategyType::Support
		};
		break;

	case ETacticalPlay::ResourceDeny:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Support,
			EStrategyType::Support,
			EStrategyType::Support
		};
		break;

	default:
		// Fallback: Standard composition
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Support
		};
		break;
	}

	return Roles;
}

void ASquadManager::DistributeRoles(const TArray<EStrategyType>& Roles)
{
	if (!TeamManager)
	{
		return;
	}

	TArray<AMocCharacter*> TeamAgents = TeamManager->GetTeamAgents(TeamID);

	for (int32 i = 0; i < FMath::Min(Roles.Num(), TeamAgents.Num()); ++i)
	{
		if (TeamAgents[i] && TeamAgents[i]->IsAlive_Implementation())
		{
			// Direct command interface (v10.2)
			TeamAgents[i]->SetCommandedStrategy(Roles[i]);

			if (bShowDebugInfo)
			{
				UE_LOG(LogTemp, Log, TEXT("Team %d Agent %d assigned: %s"),
					TeamID, i, *UEnum::GetValueAsString(Roles[i]));
			}
		}
	}
}

void ASquadManager::LogPlanningDecision(ETacticalPlay Play, float Confidence) const
{
	if (bShowDebugInfo)
	{
		UE_LOG(LogTemp, Display,
			TEXT("Squad Commander Team %d: Tactical Play=%s, Confidence=%.2f, Cycle=%d"),
			TeamID,
			*UEnum::GetValueAsString(Play),
			Confidence,
			PlanningCycleCount);
	}
}

void ASquadManager::DrawDebugVisualization() const
{
	if (!GetWorld() || !TeamManager)
	{
		return;
	}

	TArray<AMocCharacter*> TeamAgents = TeamManager->GetTeamAgents(TeamID);

	// Draw role labels above agents
	for (int32 i = 0; i < FMath::Min(CurrentRoleAssignments.Num(), TeamAgents.Num()); ++i)
	{
		if (TeamAgents[i] && TeamAgents[i]->IsAlive_Implementation())
		{
			FVector AgentLocation = TeamAgents[i]->GetActorLocation();
			FVector LabelLocation = AgentLocation + FVector(0, 0, 150);

			// Color based on strategy
			FColor RoleColor = FColor::White;
			switch (CurrentRoleAssignments[i])
			{
			case EStrategyType::Assault:
				RoleColor = FColor::Red;
				break;
			case EStrategyType::Defend:
				RoleColor = FColor::Blue;
				break;
			case EStrategyType::Support:
				RoleColor = FColor::Green;
				break;
			}

			DrawDebugString(
				GetWorld(),
				LabelLocation,
				UEnum::GetValueAsString(CurrentRoleAssignments[i]),
				nullptr,
				RoleColor,
				0.0f, // No duration (draw every frame)
				true // Draw shadow
			);
		}
	}

	// Draw tactical play name at commander location
	FVector CommanderLocation = GetActorLocation();
	FVector TacticalPlayLabelLocation = CommanderLocation + FVector(0, 0, 300);

	DrawDebugString(
		GetWorld(),
		TacticalPlayLabelLocation,
		FString::Printf(TEXT("Tactical Play: %s"), *UEnum::GetValueAsString(ActiveTacticalPlay)),
		nullptr,
		FColor::Yellow,
		0.0f,
		true
	);
}

FCompositeReward ASquadManager::CalculateTeamReward(const FTeamWorldState& OldState, const FTeamWorldState& NewState) const
{
	FCompositeReward Reward;

	// 1. Calculate team health delta (sum of individual health changes)
	float HealthDeltaSum = 0.0f;
	int32 AliveCount = 0;

	for (int32 i = 0; i < 5; ++i)
	{
		if (OldState.FriendlyAlive[i] || NewState.FriendlyAlive[i])
		{
			float OldHealth = OldState.FriendlyAlive[i] ? OldState.FriendlyHealths[i] : 0.0f;
			float NewHealth = NewState.FriendlyAlive[i] ? NewState.FriendlyHealths[i] : 0.0f;
			HealthDeltaSum += (NewHealth - OldHealth);
		}

		if (NewState.FriendlyAlive[i])
		{
			AliveCount++;
		}
	}

	Reward.HealthDelta = HealthDeltaSum;

	// 2. Calculate win probability estimate based on team advantage
	// Factors: alive count difference, health advantage, objective control
	int32 EnemyAliveCount = 0;
	float EnemyHealthSum = 0.0f;

	for (int32 i = 0; i < 5; ++i)
	{
		if (NewState.EnemyAlive[i])
		{
			EnemyAliveCount++;
			EnemyHealthSum += NewState.EnemyHealths[i];
		}
	}

	float TeamHealthSum = 0.0f;
	for (int32 i = 0; i < 5; ++i)
	{
		if (NewState.FriendlyAlive[i])
		{
			TeamHealthSum += NewState.FriendlyHealths[i];
		}
	}

	// Simple win probability heuristic
	float AliveRatio = EnemyAliveCount > 0 ? float(AliveCount) / float(EnemyAliveCount + AliveCount) : 1.0f;
	float HealthRatio = (EnemyHealthSum + TeamHealthSum) > 0.0f
		? TeamHealthSum / (EnemyHealthSum + TeamHealthSum)
		: 0.5f;

	Reward.WinProb = FMath::Clamp((AliveRatio * 0.6f + HealthRatio * 0.4f), 0.0f, 1.0f);

	// 3. Calculate objective score based on capture point control
	float ObjScore = 0.0f;
	int32 FriendlyPoints = 0;
	int32 EnemyPoints = 0;
	int32 NeutralPoints = 0;

	for (int32 i = 0; i < 5; ++i)
	{
		if (NewState.CapturePointOwnership[i] == 1)
		{
			FriendlyPoints++;
		}
		else if (NewState.CapturePointOwnership[i] == -1)
		{
			EnemyPoints++;
		}
		else
		{
			NeutralPoints++;
		}
	}

	// Reward for controlling more points
	ObjScore = (FriendlyPoints - EnemyPoints) / 5.0f; // Normalized [-1, 1]
	Reward.ObjectiveScore = ObjScore;

	return Reward;
}

ETacticalPlay ASquadManager::SelectEpsilonGreedyAction(const FTeamWorldState& TeamState) const
{
	// ε-greedy policy for data collection
	// Provides diverse exploration without MCTS overhead

	float RandomValue = FMath::FRand();

	if (RandomValue < ExplorationRate)
	{
		// EXPLORATION: Random tactical play (uniform distribution)
		int32 RandomIndex = FMath::RandRange(0, 9);

		switch (RandomIndex)
		{
		case 0: return ETacticalPlay::AllOutRush;
		case 1: return ETacticalPlay::AggressivePush;
		case 2: return ETacticalPlay::Phalanx;
		case 3: return ETacticalPlay::StandardComp;
		case 4: return ETacticalPlay::FortressDefense;
		case 5: return ETacticalPlay::TurtleFormation;
		case 6: return ETacticalPlay::BaitStrategy;
		case 7: return ETacticalPlay::PincerManeuver;
		case 8: return ETacticalPlay::HealerComp;
		case 9: return ETacticalPlay::ResourceDeny;
		default: return ETacticalPlay::StandardComp;
		}
	}
	else
	{
		// EXPLOITATION: Heuristic-based selection

		float AvgHealth = TeamState.GetAverageHealth();
		int32 AliveCount = 0;

		for (int32 i = 0; i < 5; ++i)
		{
			if (TeamState.FriendlyAlive[i])
			{
				AliveCount++;
			}
		}

		// Heuristic selection based on team state
		if (AvgHealth < 0.25f)
		{
			// Critical health: Defensive plays
			return (FMath::RandBool()) ? ETacticalPlay::FortressDefense : ETacticalPlay::TurtleFormation;
		}
		else if (AvgHealth < 0.5f)
		{
			// Low health: Balanced plays
			return (FMath::RandBool()) ? ETacticalPlay::StandardComp : ETacticalPlay::Phalanx;
		}
		else if (AvgHealth > 0.75f)
		{
			// High health: Aggressive plays
			return (FMath::RandBool()) ? ETacticalPlay::AggressivePush : ETacticalPlay::AllOutRush;
		}
		else
		{
			// Medium health: Mixed strategies
			int32 RandomStrategy = FMath::RandRange(0, 3);

			switch (RandomStrategy)
			{
			case 0: return ETacticalPlay::StandardComp;
			case 1: return ETacticalPlay::HealerComp;
			case 2: return ETacticalPlay::PincerManeuver;
			case 3: return ETacticalPlay::BaitStrategy;
			default: return ETacticalPlay::StandardComp;
			}
		}
	}
}

//========================================
// Critical Event Handlers
//========================================

void ASquadManager::OnAgentKilledHandler(int32 VictimTeamID, int32 KillerTeamID, AMocCharacter* Victim)
{
	if (VictimTeamID == TeamID)
	{
		ReplanMCTSOnCriticalEvent(ECriticalEventType::AllyKilled, Victim);
	}
	else
	{
		ReplanMCTSOnCriticalEvent(ECriticalEventType::EnemyKilled, Victim);
	}
}

void ASquadManager::OnPointCapturedHandler(ECapturePointID PointID, ECapturePointOwnership PreviousOwner, ECapturePointOwnership NewOwner)
{
	// Determine if this is a gain or loss for our team
	ECapturePointOwnership OurOwnership = (TeamID == 0) ? ECapturePointOwnership::RedTeam : ECapturePointOwnership::BlueTeam;

	if (NewOwner == OurOwnership)
	{
		ReplanMCTSOnCriticalEvent(ECriticalEventType::ObjectiveCaptured, nullptr);
	}
	else if (PreviousOwner == OurOwnership)
	{
		ReplanMCTSOnCriticalEvent(ECriticalEventType::ObjectiveLost, nullptr);
	}
}
