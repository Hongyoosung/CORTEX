// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/SquadManager.h"
#include "Team/TeamManager.h"
#include "Characters/MocCharacter.h"
#include "AI/MCTS/TeamMCTS.h"
#include "AI/Models/TeamWorldModel.h"
#include "AI/Training/TeamDataCollector.h"
#include "Types/RewardTypes.h"
#include "DrawDebugHelpers.h"

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

	// Initialize role assignments (default: standard comp)
	CurrentRoleAssignments = {
		EStrategyType::Assault,
		EStrategyType::Assault,
		EStrategyType::Defend,
		EStrategyType::Defend,
		EStrategyType::Support
	};
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

	// 7. Log for analytics
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
			State.FriendlyHealths[i] = TeamAgents[i]->GetHealthPercentage();
			State.FriendlyCooldowns[i] = TeamAgents[i]->GetWeaponCooldown();
			State.FriendlyAlive[i] = TeamAgents[i]->IsAlive();

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
			State.EnemyHealths[i] = EnemyAgents[i]->GetHealthPercentage();
			State.EnemyAlive[i] = EnemyAgents[i]->IsAlive();
			State.EnemyConfidences[i] = 1.0f; // Placeholder: should use FoW decay
		}
	}

	// TODO: Collect capture point ownership
	// TODO: Collect pickup availability
	// TODO: Get match time remaining

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
		if (TeamAgents[i] && TeamAgents[i]->IsAlive())
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
		if (TeamAgents[i] && TeamAgents[i]->IsAlive())
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
