// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/SquadManager.h"
#include "Team/TeamManager.h"
#include "Characters/MocCharacter.h"
#include "AI/MCTS/TeamMCTS.h"
#include "AI/Models/TeamWorldModel.h"
#include "AI/Training/TeamDataCollector.h"
#include "Types/RewardTypes.h"
#include "Core/MocGameMode.h"
#include "Schola/ScholaEnvironment.h"
#include "Actors/CapturePoint.h"
#include "Actors/PickupBase.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"

// Static reference for console commands (Team 0 by default)
static TWeakObjectPtr<USquadManager> GDebugSquadManager;

USquadManager::USquadManager()
{
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
			if (USquadManager* SM = GDebugSquadManager.Get())
			{
				UE_LOG(LogTemp, Display, TEXT("=== Squad Commander State (Team %d) ==="), SM->TeamID);
				UE_LOG(LogTemp, Display, TEXT("  Tactical Play: %s"), *UEnum::GetValueAsString(SM->ActiveTacticalPlay));
				UE_LOG(LogTemp, Display, TEXT("  Confidence: %.2f"), SM->PlanConfidence);
				UE_LOG(LogTemp, Display, TEXT("  Planning Cycles: %d (Event Replans: %d)"), SM->PlanningCycleCount, SM->EventDrivenReplanCount);
				if (SM->TeamManager)
				{
					UE_LOG(LogTemp, Display, TEXT("  Data Collection Mode: %s"), SM->TeamManager->bDataCollectionMode ? TEXT("Yes") : TEXT("No"));
				}
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
			if (USquadManager* SM = GDebugSquadManager.Get())
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
			if (USquadManager* SM = GDebugSquadManager.Get())
			{
				if (SM->TeamManager)
				{
					SM->TeamManager->bShowDebugInfo = !SM->TeamManager->bShowDebugInfo;
					SM->TeamManager->bDrawRoleAssignments = SM->TeamManager->bShowDebugInfo;
					UE_LOG(LogTemp, Display, TEXT("Debug visualization: %s"), SM->TeamManager->bShowDebugInfo ? TEXT("ON") : TEXT("OFF"));
				}
			}
		})
	);

	static FAutoConsoleCommand CmdObserver(
		TEXT("moc.debug.observer"),
		TEXT("Print current 60-dim observation tensor"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (USquadManager* SM = GDebugSquadManager.Get())
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
			if (USquadManager* SM = GDebugSquadManager.Get())
			{
				UE_LOG(LogTemp, Display, TEXT("=== Timing Stats (Team %d) ==="), SM->TeamID);
				if (SM->TeamManager)
				{
					UE_LOG(LogTemp, Display, TEXT("  MCTS Time Budget: %.1f ms"), SM->TeamManager->MCTSTimeBudget * 1000.0f);
				}
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

UWorld* USquadManager::GetWorld() const
{
	if (TeamManager)
	{
		return TeamManager->GetWorld();
	}
	return nullptr;
}

void USquadManager::TickPlanner(float DeltaTime)
{
	TimeSinceLastPlan += DeltaTime;

	// Phase 1 RL Training: strategies are fixed per episode, skip all replanning
	if (!TeamManager || !TeamManager->bRLTrainingMode)
	{
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
	}

	// End-to-end validation logging (behind debug flag)
	if (TeamManager && TeamManager->bShowDebugInfo)
	{
		ValidationTickCounter += DeltaTime;
		if (ValidationTickCounter >= 2.0f)
		{
			ValidationTickCounter = 0.0f;

			FTeamWorldState ValidationState = CollectTeamState();
			TArray<float> Tensor = ValidationState.ToTensor();
			UE_LOG(LogTemp, Verbose, TEXT("[Validation] Team %d: Observer tensor dims=%d (expected ~60)"), TeamID, Tensor.Num());

			int32 AssignedCount = CurrentRoleAssignments.Num();
			if (AssignedCount != 5)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Validation] Team %d: Role assignments=%d (expected 5)"), TeamID, AssignedCount);
			}

			UE_LOG(LogTemp, Verbose, TEXT("[Validation] Team %d: LastPlanning=%.2fms, WorldModel=%.2fms"),
				TeamID, LastPlanningDurationMs,
				TeamWorldModel ? TeamWorldModel->GetAverageLatency() : 0.0f);
		}
	}

	// Draw debug visualization
	if (TeamManager && TeamManager->bShowDebugInfo && TeamManager->bDrawRoleAssignments)
	{
		DrawDebugVisualization();
	}
}

void USquadManager::Initialize(int32 InTeamID, ATeamManager* InTeamManager)
{
	TeamID = InTeamID;
	TeamManager = InTeamManager;

	// Initialize Team World Model
	TeamWorldModel = NewObject<UTeamWorldModel>(this);
	bool bModelLoaded = false;

	if (TeamWorldModel && !TeamManager->TeamWorldModelPath.IsEmpty())
	{
		bModelLoaded = TeamWorldModel->InitModel(TeamManager->TeamWorldModelPath);

		if (bModelLoaded)
		{
			UE_LOG(LogTemp, Log, TEXT("Team %d: World model loaded from %s"), TeamID, *TeamManager->TeamWorldModelPath);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Team %d: Failed to load world model from %s - using data collection mode"),
				TeamID, *TeamManager->TeamWorldModelPath);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Team %d: No world model path specified - using data collection mode"), TeamID);
	}

	// Initialize Team MCTS planner
	TeamMCTSPlanner = NewObject<UTeamMCTS>(this);

	if (TeamMCTSPlanner && TeamWorldModel)
	{
		FTeamMCTSConfig MCTSConfig;
		MCTSConfig.TimeBudgetSeconds = TeamManager->MCTSTimeBudget;
		MCTSConfig.BatchSize = TeamManager->MCTSBatchSize;
		MCTSConfig.MaxIterations = 50;

		TeamMCTSPlanner->Setup(TeamWorldModel, MCTSConfig);

		if (bModelLoaded)
		{
			UE_LOG(LogTemp, Log, TEXT("Team %d: Team MCTS initialized with loaded world model"), TeamID);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Team %d: Team MCTS initialized but world model not loaded - MCTS will not work until model is trained"), TeamID);
		}
	}

	// Initialize training data collector
	DataCollector = NewObject<UTeamDataCollector>(this);
	if (DataCollector)
	{
		DataCollector->bIsRecording = true;
		DataCollector->BeginRecording(FMath::RandRange(1000, 9999));
		UE_LOG(LogTemp, Log, TEXT("Team %d: Data collector initialized and recording started"), TeamID);
	}

	// Automatically enable data collection mode if no model is loaded
	if (!bModelLoaded)
	{
		TeamManager->bDataCollectionMode = true;
		UE_LOG(LogTemp, Display, TEXT("Team %d: Auto-enabled data collection mode (no world model available)"), TeamID);
	}

	// Wire critical event delegates
	TeamManager->OnAgentKilled.AddDynamic(this, &USquadManager::OnAgentKilledHandler);
	UE_LOG(LogTemp, Log, TEXT("Team %d: Bound OnAgentKilled delegate"), TeamID);

	// Capture point events are scoped per environment.
	// AScholaEnvironment calls BindCapturePoints() + SetScholaEnvironment() after Initialize().

	// Register as debug target
	GDebugSquadManager = this;

	UE_LOG(LogTemp, Log, TEXT("Squad Planner initialized for Team %d (MCTS=%s, DataCollection=%s)"),
		TeamID,
		bModelLoaded ? TEXT("Enabled") : TEXT("Disabled"),
		TeamManager->bDataCollectionMode ? TEXT("Enabled") : TEXT("Disabled"));
}

void USquadManager::SetScholaEnvironment(AScholaEnvironment* InEnvironment)
{
	OwningEnvironment = InEnvironment;
}

void USquadManager::BindCapturePoints(const TArray<ACapturePoint*>& CapturePoints)
{
	CachedCapturePoints = CapturePoints;

	for (ACapturePoint* Point : CapturePoints)
	{
		if (Point)
		{
			Point->OnPointCaptured.AddUniqueDynamic(this, &USquadManager::OnPointCapturedHandler);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Team %d: Bound OnPointCaptured to %d capture points (via BindCapturePoints)"), TeamID, CapturePoints.Num());
}

bool USquadManager::ShouldReplan() const
{
	return TimeSinceLastPlan >= TeamManager->PlanningInterval;
}

//========================================
// Episode Management
//========================================

void USquadManager::Reset()
{
	UE_LOG(LogTemp, Log, TEXT("[SquadManager] Resetting squad planner for Team %d"), TeamID);

	TimeSinceLastPlan = 0.0f;
	PlanConfidence = 0.0f;
	PlanningCycleCount = 0;
	EventDrivenReplanCount = 0;

	ActiveTacticalPlay = ETacticalPlay::StandardComp;

	CurrentRoleAssignments.Empty();
	CurrentRoleAssignments.Init(EStrategyType::Assault, 5);

	bHasPreviousState = false;
	PreviousTeamState = FTeamWorldState();
	PreviousTacticalPlay = ETacticalPlay::StandardComp;

	// Phase 1 RL Training: sample a random tactical play for the new episode
	if (TeamManager && TeamManager->bRLTrainingMode)
	{
		SampleRandomTacticalPlay();
	}

	UE_LOG(LogTemp, Log, TEXT("[SquadManager] Reset complete - default assignments restored"));
}

TArray<ETacticalPlay> USquadManager::GetFeasiblePlays(const FTeamWorldState& State) const
{
	// Precondition: Defend requires ≥1 friendly capture point
	bool bHasFriendlyPoint = false;
	for (int32 Ownership : State.CapturePointOwnership)
	{
		if (Ownership == 1) { bHasFriendlyPoint = true; break; }
	}

	// Precondition: Support requires ≥2 alive allies (support agent needs someone to support)
	int32 AliveCount = 0;
	for (bool bAlive : State.FriendlyAlive)
	{
		if (bAlive) AliveCount++;
	}
	const bool bCanSupport = (AliveCount >= 2);

	// Build feasible set — check each play's role composition
	static const ETacticalPlay AllPlays[10] = {
		ETacticalPlay::AllOutRush,     // 5A
		ETacticalPlay::AggressivePush, // 4A 1S
		ETacticalPlay::Phalanx,        // 2D 3S
		ETacticalPlay::StandardComp,   // 2A 2D 1S
		ETacticalPlay::FortressDefense,// 1A 4D
		ETacticalPlay::TurtleFormation,// 5D
		ETacticalPlay::BaitStrategy,   // 1A 4D
		ETacticalPlay::PincerManeuver, // 3A 2S
		ETacticalPlay::HealerComp,     // 2A 1D 2S
		ETacticalPlay::ResourceDeny,   // 2A 3S
	};

	TArray<ETacticalPlay> Feasible;
	for (ETacticalPlay Play : AllPlays)
	{
		TArray<EStrategyType> Roles = DecodeTacticalPlay(Play);

		bool bNeedsDefend  = Roles.Contains(EStrategyType::Defend);
		bool bNeedsSupport = Roles.Contains(EStrategyType::Support);

		if (bNeedsDefend  && !bHasFriendlyPoint) continue;
		if (bNeedsSupport && !bCanSupport)        continue;

		Feasible.Add(Play);
	}

	if (Feasible.IsEmpty())
	{
		// Ultimate fallback: pure assault is always valid
		UE_LOG(LogTemp, Warning, TEXT("[SquadManager] Team %d: No feasible plays — falling back to AllOutRush"), TeamID);
		Feasible.Add(ETacticalPlay::AllOutRush);
	}

	return Feasible;
}

void USquadManager::SampleRandomTacticalPlay()
{
	FTeamWorldState CurrentState = CollectTeamState();
	TArray<ETacticalPlay> FeasiblePlays = GetFeasiblePlays(CurrentState);

	// Weighted random within feasible set using the same per-play weights as epsilon-greedy
	static const float PlayWeights[10] = { 4.f, 6.f, 16.f, 11.f, 9.f, 7.f, 7.f, 13.f, 14.f, 13.f };

	float TotalWeight = 0.0f;
	for (ETacticalPlay Play : FeasiblePlays)
	{
		TotalWeight += PlayWeights[static_cast<int32>(Play)];
	}

	ETacticalPlay RandomPlay = FeasiblePlays[0];
	const float R = FMath::FRand() * TotalWeight;
	float Cumulative = 0.0f;
	for (ETacticalPlay Play : FeasiblePlays)
	{
		Cumulative += PlayWeights[static_cast<int32>(Play)];
		if (R < Cumulative) { RandomPlay = Play; break; }
	}

	TArray<EStrategyType> Roles = DecodeTacticalPlay(RandomPlay);
	DistributeRoles(Roles);

	ActiveTacticalPlay = RandomPlay;
	CurrentRoleAssignments = Roles;

	UE_LOG(LogTemp, Warning, TEXT("[SquadManager] Phase 1 RL: Sampled tactical play %s for episode (feasible set: %d plays)"),
		*UEnum::GetValueAsString(RandomPlay), FeasiblePlays.Num());
}

//========================================
// Centralized Planning
//========================================

void USquadManager::PerformTacticalPlanning()
{
	if (!TeamManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("Squad Planner: TeamManager not set, cannot plan"));
		return;
	}

	// 1. Collect global team state
	FTeamWorldState GlobalState = CollectTeamState();

	// Record transition from previous planning cycle (Data Collection)
	if (bHasPreviousState && DataCollector && DataCollector->bIsRecording)
	{
		FCompositeReward Reward = CalculateTeamReward(PreviousTeamState, GlobalState);
		DataCollector->RecordTransition(PreviousTeamState, PreviousTacticalPlay, GlobalState, Reward);
	}

	// 2. Select tactical play: ε-greedy (data collection) OR MCTS (production)
	ETacticalPlay BestPlay = ETacticalPlay::StandardComp;
	float PlanningStartTime = FPlatformTime::Seconds();

	// Pre-compute feasible plays once — shared by all selection paths
	TArray<ETacticalPlay> FeasiblePlays = GetFeasiblePlays(GlobalState);

	if (TeamManager->bDataCollectionMode)
	{
		BestPlay = SelectEpsilonGreedyAction(GlobalState, FeasiblePlays);

		float SelectionTime = (FPlatformTime::Seconds() - PlanningStartTime) * 1000.0f;

		if (TeamManager->bShowDebugInfo)
		{
			UE_LOG(LogTemp, Display, TEXT("Team %d ε-Greedy Selection: %s (%.3f ms, ExplorationRate=%.2f, feasible=%d)"),
				TeamID, *UEnum::GetValueAsString(BestPlay), SelectionTime, TeamManager->ExplorationRate, FeasiblePlays.Num());
		}
	}
	else if (TeamMCTSPlanner && TeamWorldModel && TeamWorldModel->IsModelLoaded())
	{
		BestPlay = TeamMCTSPlanner->FindBestTacticalPlay(GlobalState, FeasiblePlays);

		float PlanningTime = (FPlatformTime::Seconds() - PlanningStartTime) * 1000.0f;

		if (TeamManager->bShowDebugInfo)
		{
			UE_LOG(LogTemp, Display, TEXT("Team %d MCTS Planning: %.2f ms, %d iterations, feasible=%d"),
				TeamID, PlanningTime, TeamMCTSPlanner->GetLastIterationCount(), FeasiblePlays.Num());
		}

		if (PlanningTime > TeamManager->MCTSTimeBudget * 1000.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("Team %d MCTS exceeded budget: %.2f ms > %.2f ms"),
				TeamID, PlanningTime, TeamManager->MCTSTimeBudget * 1000.0f);
		}
	}
	else
	{
		// Fallback heuristic if MCTS not available — constrained to feasible set
		float AvgHealth = GlobalState.GetAverageHealth();
		ETacticalPlay Preferred = (AvgHealth < 0.3f) ? ETacticalPlay::FortressDefense
			: (AvgHealth > 0.7f) ? ETacticalPlay::AggressivePush
			: ETacticalPlay::StandardComp;

		BestPlay = FeasiblePlays.Contains(Preferred) ? Preferred : FeasiblePlays[0];

		if (TeamManager->bShowDebugInfo)
		{
			UE_LOG(LogTemp, Display, TEXT("Team %d: Using fallback heuristic (world model not loaded), feasible=%d"), TeamID, FeasiblePlays.Num());
		}
	}

	// 3. Convert to role distribution
	TArray<EStrategyType> NewRoles = DecodeTacticalPlay(BestPlay);

	// 4. Broadcast to agents
	DistributeRoles(NewRoles);

	// 5. Update state
	ActiveTacticalPlay = BestPlay;
	CurrentRoleAssignments = NewRoles;
	PlanConfidence = 0.8f;
	PlanningCycleCount++;

	// 6. Store current state for next transition recording
	PreviousTeamState = GlobalState;
	PreviousTacticalPlay = BestPlay;
	bHasPreviousState = true;

	LastPlanningDurationMs = (FPlatformTime::Seconds() - PlanningStartTime) * 1000.0f;

	LogPlanningDecision(BestPlay, PlanConfidence);
}

void USquadManager::ReplanMCTSOnCriticalEvent(ECriticalEventType EventType, AActor* InstigatorActor)
{
	// Phase 1 RL Training: no tactical replanning, but DO enforce feasibility.
	// If the current play has become impossible (e.g. all friendly points lost → Defend invalid),
	// resample within the feasible set so agents always have an achievable objective.
	if (TeamManager && TeamManager->bRLTrainingMode)
	{
		FTeamWorldState CurrentState = CollectTeamState();
		TArray<ETacticalPlay> FeasiblePlays = GetFeasiblePlays(CurrentState);

		if (!FeasiblePlays.Contains(ActiveTacticalPlay))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[SquadManager] Phase 1 RL Team %d: Active play %s is no longer feasible after event %s — resampling"),
				TeamID, *UEnum::GetValueAsString(ActiveTacticalPlay), *UEnum::GetValueAsString(EventType));
			SampleRandomTacticalPlay();
			EventDrivenReplanCount++;
		}
		return;
	}

	if (InstigatorActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("Squad Planner: Critical event %s by %s - triggering replan"),
			*UEnum::GetValueAsString(EventType), *InstigatorActor->GetName());
	}

	PerformTacticalPlanning();
	EventDrivenReplanCount++;
	TimeSinceLastPlan = 0.0f;
}

FTeamWorldState USquadManager::CollectTeamState() const
{
	FTeamWorldState State;

	if (!TeamManager)
	{
		return State;
	}

	TArray<AMocCharacter*> TeamAgents = TeamManager->GetTeamAgents(TeamID);
	TArray<AMocCharacter*> EnemyAgents = TeamManager->GetEnemyAgents(TeamID);

	for (int32 i = 0; i < FMath::Min(5, TeamAgents.Num()); ++i)
	{
		if (TeamAgents[i])
		{
			State.FriendlyPositions[i] = TeamAgents[i]->GetActorLocation();
			State.FriendlyHealths[i] = TeamAgents[i]->GetHealthPercentage_Implementation();
			State.FriendlyCooldowns[i] = TeamAgents[i]->GetWeaponCooldown_Implementation();
			State.FriendlyAlive[i] = TeamAgents[i]->IsAlive_Implementation();
		}
	}

	for (int32 i = 0; i < FMath::Min(5, EnemyAgents.Num()); ++i)
	{
		if (EnemyAgents[i])
		{
			State.EnemyPositions[i] = EnemyAgents[i]->GetActorLocation();
			State.EnemyHealths[i] = EnemyAgents[i]->GetHealthPercentage_Implementation();
			State.EnemyAlive[i] = EnemyAgents[i]->IsAlive_Implementation();
			State.EnemyConfidences[i] = 1.0f;
		}
	}

	// Capture points are always provided by the owning ScholaEnvironment via CachedCapturePoints
	const TArray<ACapturePoint*>& CapturePoints = CachedCapturePoints;

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
				int32 OwningTeamID = CapturePoints[i]->GetOwningTeamID();
				State.CapturePointOwnership[i] = (OwningTeamID == TeamID) ? 1 : -1;
			}
		}
	}

	if (OwningEnvironment)
	{
		float MaxDuration = OwningEnvironment->MaxMatchDuration;
		if (MaxDuration > 0.0f)
		{
			State.TimeRemaining = FMath::Clamp(OwningEnvironment->GetTimeRemaining() / MaxDuration, 0.0f, 1.0f);
		}
	}

	return State;
}

FTeamWorldState USquadManager::GetGlobalTeamState() const
{
	return CollectTeamState();
}

EStrategyType USquadManager::GetAgentStrategy(int32 AgentIndex) const
{
	if (AgentIndex >= 0 && AgentIndex < CurrentRoleAssignments.Num())
	{
		return CurrentRoleAssignments[AgentIndex];
	}

	return EStrategyType::Assault;
}

TArray<EStrategyType> USquadManager::DecodeTacticalPlay(ETacticalPlay Play) const
{
	TArray<EStrategyType> Roles;

	switch (Play)
	{
	case ETacticalPlay::AllOutRush:
		Roles = { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Assault };
		break;
	case ETacticalPlay::AggressivePush:
		Roles = { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Support };
		break;
	case ETacticalPlay::Phalanx:
		Roles = { EStrategyType::Defend, EStrategyType::Defend, EStrategyType::Support, EStrategyType::Support, EStrategyType::Support };
		break;
	case ETacticalPlay::StandardComp:
		Roles = { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Defend, EStrategyType::Defend, EStrategyType::Support };
		break;
	case ETacticalPlay::FortressDefense:
		Roles = { EStrategyType::Assault, EStrategyType::Defend, EStrategyType::Defend, EStrategyType::Defend, EStrategyType::Defend };
		break;
	case ETacticalPlay::TurtleFormation:
		Roles = { EStrategyType::Defend, EStrategyType::Defend, EStrategyType::Defend, EStrategyType::Defend, EStrategyType::Defend };
		break;
	case ETacticalPlay::BaitStrategy:
		Roles = { EStrategyType::Assault, EStrategyType::Defend, EStrategyType::Defend, EStrategyType::Defend, EStrategyType::Defend };
		break;
	case ETacticalPlay::PincerManeuver:
		Roles = { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Support, EStrategyType::Support };
		break;
	case ETacticalPlay::HealerComp:
		Roles = { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Defend, EStrategyType::Support, EStrategyType::Support };
		break;
	case ETacticalPlay::ResourceDeny:
		Roles = { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Support, EStrategyType::Support, EStrategyType::Support };
		break;
	default:
		Roles = { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Defend, EStrategyType::Defend, EStrategyType::Support };
		break;
	}

	return Roles;
}

void USquadManager::DistributeRoles(const TArray<EStrategyType>& Roles)
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
			TeamAgents[i]->SetCommandedStrategy(Roles[i]);

			if (TeamManager->bShowDebugInfo)
			{
				UE_LOG(LogTemp, Log, TEXT("Team %d Agent %d assigned: %s"),
					TeamID, i, *UEnum::GetValueAsString(Roles[i]));
			}
		}
	}
}

void USquadManager::LogPlanningDecision(ETacticalPlay Play, float Confidence) const
{
	if (TeamManager && TeamManager->bShowDebugInfo)
	{
		UE_LOG(LogTemp, Display,
			TEXT("Squad Planner Team %d: Tactical Play=%s, Confidence=%.2f, Cycle=%d"),
			TeamID,
			*UEnum::GetValueAsString(Play),
			Confidence,
			PlanningCycleCount);
	}
}

void USquadManager::DrawDebugVisualization() const
{
	if (!GetWorld() || !TeamManager)
	{
		return;
	}

	TArray<AMocCharacter*> TeamAgents = TeamManager->GetTeamAgents(TeamID);

	for (int32 i = 0; i < FMath::Min(CurrentRoleAssignments.Num(), TeamAgents.Num()); ++i)
	{
		if (TeamAgents[i] && TeamAgents[i]->IsAlive_Implementation())
		{
			FVector LabelLocation = TeamAgents[i]->GetActorLocation() + FVector(0, 0, 150);

			FColor RoleColor = FColor::White;
			switch (CurrentRoleAssignments[i])
			{
			case EStrategyType::Assault: RoleColor = FColor::Red;   break;
			case EStrategyType::Defend:  RoleColor = FColor::Blue;  break;
			case EStrategyType::Support: RoleColor = FColor::Green; break;
			}

			DrawDebugString(GetWorld(), LabelLocation, UEnum::GetValueAsString(CurrentRoleAssignments[i]),
				nullptr, RoleColor, 0.0f, true);
		}
	}

	DrawDebugString(GetWorld(),
		TeamManager->GetActorLocation() + FVector(0, 0, 300),
		FString::Printf(TEXT("Tactical Play: %s"), *UEnum::GetValueAsString(ActiveTacticalPlay)),
		nullptr, FColor::Yellow, 0.0f, true);
}

FCompositeReward USquadManager::CalculateTeamReward(const FTeamWorldState& OldState, const FTeamWorldState& NewState) const
{
	FCompositeReward Reward;

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
		if (NewState.FriendlyAlive[i]) AliveCount++;
	}

	Reward.HealthDelta = HealthDeltaSum;

	int32 EnemyAliveCount = 0;
	float EnemyHealthSum = 0.0f;
	for (int32 i = 0; i < 5; ++i)
	{
		if (NewState.EnemyAlive[i]) { EnemyAliveCount++; EnemyHealthSum += NewState.EnemyHealths[i]; }
	}

	float TeamHealthSum = 0.0f;
	for (int32 i = 0; i < 5; ++i)
	{
		if (NewState.FriendlyAlive[i]) TeamHealthSum += NewState.FriendlyHealths[i];
	}

	float AliveRatio = EnemyAliveCount > 0 ? float(AliveCount) / float(EnemyAliveCount + AliveCount) : 1.0f;
	float HealthRatio = (EnemyHealthSum + TeamHealthSum) > 0.0f ? TeamHealthSum / (EnemyHealthSum + TeamHealthSum) : 0.5f;
	Reward.WinProb = FMath::Clamp((AliveRatio * 0.6f + HealthRatio * 0.4f), 0.0f, 1.0f);

	int32 FriendlyPoints = 0, EnemyPoints = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		if (NewState.CapturePointOwnership[i] == 1)  FriendlyPoints++;
		else if (NewState.CapturePointOwnership[i] == -1) EnemyPoints++;
	}
	Reward.ObjectiveScore = (FriendlyPoints - EnemyPoints) / 5.0f;

	return Reward;
}

ETacticalPlay USquadManager::SelectEpsilonGreedyAction(const FTeamWorldState& TeamState, const TArray<ETacticalPlay>& FeasiblePlays) const
{
	// Play             Weight  |  A  D  S
	// AllOutRush          4    |  5  0  0
	// AggressivePush      6    |  4  0  1
	// Phalanx            16    |  0  2  3
	// StandardComp       11    |  2  2  1
	// FortressDefense     9    |  1  4  0
	// TurtleFormation     7    |  0  5  0
	// BaitStrategy        7    |  1  4  0
	// PincerManeuver     13    |  3  0  2
	// HealerComp         14    |  2  1  2
	// ResourceDeny       13    |  2  0  3  (sum = 100)
	static const float PlayWeights[10] = { 4.f, 6.f, 16.f, 11.f, 9.f, 7.f, 7.f, 13.f, 14.f, 13.f };

	// Helper: pick first feasible play from an ordered preference array
	auto PickFeasible = [&](TArrayView<const ETacticalPlay> Preferences) -> ETacticalPlay
	{
		for (ETacticalPlay P : Preferences)
		{
			if (FeasiblePlays.Contains(P)) return P;
		}
		return FeasiblePlays[0]; // guaranteed non-empty by GetFeasiblePlays
	};

	float RandomValue = FMath::FRand();

	if (RandomValue < TeamManager->ExplorationRate)
	{
		// Weighted random within feasible set
		float TotalWeight = 0.0f;
		for (ETacticalPlay Play : FeasiblePlays)
		{
			TotalWeight += PlayWeights[static_cast<int32>(Play)];
		}

		const float R = FMath::FRand() * TotalWeight;
		float Cumulative = 0.0f;
		for (ETacticalPlay Play : FeasiblePlays)
		{
			Cumulative += PlayWeights[static_cast<int32>(Play)];
			if (R < Cumulative) return Play;
		}
		return FeasiblePlays[0];
	}
	else
	{
		float AvgHealth = TeamState.GetAverageHealth();

		if (AvgHealth < 0.25f)
		{
			const ETacticalPlay Prefs[] = { ETacticalPlay::FortressDefense, ETacticalPlay::TurtleFormation, ETacticalPlay::BaitStrategy, ETacticalPlay::AllOutRush };
			return PickFeasible(Prefs);
		}
		else if (AvgHealth < 0.5f)
		{
			const ETacticalPlay Prefs[] = { ETacticalPlay::StandardComp, ETacticalPlay::Phalanx, ETacticalPlay::HealerComp, ETacticalPlay::AllOutRush };
			return PickFeasible(Prefs);
		}
		else if (AvgHealth > 0.75f)
		{
			const ETacticalPlay Prefs[] = { ETacticalPlay::AggressivePush, ETacticalPlay::AllOutRush, ETacticalPlay::PincerManeuver, ETacticalPlay::StandardComp };
			return PickFeasible(Prefs);
		}
		else
		{
			// Mid-health: shuffle the preferred candidate, then fall back in order
			const ETacticalPlay MidCandidates[4] = {
				ETacticalPlay::StandardComp, ETacticalPlay::HealerComp,
				ETacticalPlay::PincerManeuver, ETacticalPlay::BaitStrategy
			};
			const ETacticalPlay Prefs[] = { MidCandidates[FMath::RandRange(0, 3)], ETacticalPlay::StandardComp, ETacticalPlay::AllOutRush };
			return PickFeasible(Prefs);
		}
	}
}

//========================================
// Critical Event Handlers
//========================================

void USquadManager::OnAgentKilledHandler(int32 VictimTeamID, int32 KillerTeamID, AMocCharacter* Victim)
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

void USquadManager::OnPointCapturedHandler(ECapturePointID PointID, ECapturePointOwnership PreviousOwner, ECapturePointOwnership NewOwner)
{
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
