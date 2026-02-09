// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/SquadManager.h"
#include "Team/TeamManager.h"
#include "Characters/MocCharacter.h"
#include "AI/MCTS/TeamMCTS.h"
#include "AI/Models/MocAgentWorldModel.h"
#include "AI/Models/MocTeamWorldModel.h"
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

	// Initialize agent-level world model (existing infrastructure)
	AgentWorldModel = NewObject<UMocAgentWorldModel>(this);
	AgentWorldModel->InitModel(TEXT("Content/AI/Models/agent_world_model.onnx"));

	// Initialize team-level world model (NEW - v10.2 Week 2)
	TeamWorldModel = NewObject<UMocTeamWorldModel>(this);
	TeamWorldModel->Initialize(AgentWorldModel);

	// Initialize Team MCTS planner (NEW - v10.2 Week 3)
	TeamMCTSPlanner = NewObject<UTeamMCTS>(this);

	FTeamMCTSConfig MCTSConfig;
	MCTSConfig.TimeBudgetSeconds = MCTSTimeBudget;  // 0.015s from header
	MCTSConfig.BatchSize = MCTSBatchSize;            // 8 from header
	MCTSConfig.MaxIterations = 50;

	TeamMCTSPlanner->Setup(TeamWorldModel, MCTSConfig);

	UE_LOG(LogTemp, Log, TEXT("Squad Commander initialized with Team MCTS for Team %d"), TeamID);
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

void ASquadManager::PerformTacticalPlanning()
{
	if (!TeamManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("Squad Commander: TeamManager not set, cannot plan"));
		return;
	}

	// 1. Collect global team state
	FTeamState GlobalState = CollectTeamState();

	// 2. Run centralized MCTS (15ms budget)
	ETacticalPlay BestPlay = ETacticalPlay::StandardComp; // Default fallback
	float PlanningStartTime = FPlatformTime::Seconds();

	if (TeamMCTSPlanner && TeamWorldModel)
	{
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
		// Fallback heuristic if MCTS not available
		float AvgHealth = GlobalState.GetAverageHealth();
		BestPlay = (AvgHealth < 0.3f) ? ETacticalPlay::FortressDefense
			: (AvgHealth > 0.7f) ? ETacticalPlay::AggressivePush
			: ETacticalPlay::StandardComp;

		UE_LOG(LogTemp, Warning, TEXT("Team %d: MCTS not available, using fallback heuristic"), TeamID);
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

	// 6. Log for analytics
	LogPlanningDecision(BestPlay, PlanConfidence);
}

void ASquadManager::ReplanMCTSOnCriticalEvent(ECriticalEventType EventType, AActor* InstigatorActor)
{
	// Immediate replanning on high-volatility events
	UE_LOG(LogTemp, Warning, TEXT("Squad Commander: Critical event %s in &s - triggering replan"),
		*UEnum::GetValueAsString(EventType), InstigatorActor.GetName());

	PerformTacticalPlanning();
	EventDrivenReplanCount++;
	TimeSinceLastPlan = 0.0f; // Reset timer to avoid double-planning
}

FTeamState ASquadManager::CollectTeamState() const
{
	FTeamState State;

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

FTeamState ASquadManager::GetGlobalTeamState() const
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
