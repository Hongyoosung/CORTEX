// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/DESquadManager.h"
#include "Characters/DECharacter.h"
#include "Types/DERewardTypes.h"
#include "Actors/DECapturePoint.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"


// ─────────────────────────────────────────────────────────────────────────────
// Debug helper — a module-local weak pointer to the most-recently created
// UDESquadManager (Team 0 by convention) for console commands.
// ─────────────────────────────────────────────────────────────────────────────

static TWeakObjectPtr<UDESquadManager> GDebugSquadManager;


// ─────────────────────────────────────────────────────────────────────────────
// Constructor — default role assignments + one-time console command registration
// ─────────────────────────────────────────────────────────────────────────────

UDESquadManager::UDESquadManager()
{
	CurrentRoleAssignments = {
		EDEStrategyType::Assault,
		EDEStrategyType::Assault,
		EDEStrategyType::Defend,
		EDEStrategyType::Defend,
		EDEStrategyType::Support
	};

	// ── Debug console commands ──────────────────────────────────────────────

	static FAutoConsoleCommand CmdSquadState(
		TEXT("moc.debug.squadstate"),
		TEXT("Print current tactical play, role assignments, confidence, and MCTS stats"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UDESquadManager* SM = GDebugSquadManager.Get())
			{
				UE_LOG(LogTemp, Display, TEXT("=== Squad Commander State (Team %d) ==="), SM->TeamID);
				UE_LOG(LogTemp, Display, TEXT("  Tactical Play: %s"), *UEnum::GetValueAsString(SM->ActiveTacticalPlay));
				UE_LOG(LogTemp, Display, TEXT("  Confidence: %.2f"), SM->PlanConfidence);
				UE_LOG(LogTemp, Display, TEXT("  Planning Cycles: %d  (Event Replans: %d)"),
					SM->PlanningCycleCount, SM->EventDrivenReplanCount);

				for (int32 i = 0; i < SM->CurrentRoleAssignments.Num(); ++i)
				{
					UE_LOG(LogTemp, Display, TEXT("  Agent %d: %s"),
						i, *UEnum::GetValueAsString(SM->CurrentRoleAssignments[i]));
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("No DESquadManager available for debug"));
			}
		})
	);

	static FAutoConsoleCommand CmdForceReplan(
		TEXT("moc.debug.forcereplan"),
		TEXT("Trigger immediate tactical replanning (uses cached state — no live agents required)"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UDESquadManager* SM = GDebugSquadManager.Get())
			{
				UE_LOG(LogTemp, Display, TEXT("Forcing replan for Team %d"), SM->TeamID);
				// Called without live agent arrays for debug convenience; will use empty lists.
				SM->PerformTacticalPlanning({}, {});
				SM->TimeSinceLastPlan = 0.0f;
			}
		})
	);

	static FAutoConsoleCommand CmdTiming(
		TEXT("moc.debug.timing"),
		TEXT("Print MCTS duration and world model latency"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UDESquadManager* SM = GDebugSquadManager.Get())
			{
				UE_LOG(LogTemp, Display, TEXT("=== Timing Stats (Team %d) ==="), SM->TeamID);
					
				UE_LOG(LogTemp, Display, TEXT("  Last Planning Duration: %.2f ms"),
					SM->LastPlanningDurationMs);
			}
		})
	);
}


// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

void UDESquadManager::Initialize(int32 InTeamID)
{
	TeamID = InTeamID;

	// Register as the debug target (last-created wins — fine for single-env sessions)
	GDebugSquadManager = this;
}

void UDESquadManager::Configure(const FDESquadConfig& InConfig)
{
	Config = InConfig;
}

void UDESquadManager::BindCapturePoints(const TArray<ADECapturePoint*>& CapturePoints)
{
	CachedCapturePoints = CapturePoints;
	UE_LOG(LogTemp, Log, TEXT("[DESquadManager] Team %d: Bound %d capture points"),
		TeamID, CapturePoints.Num());
}


// ─────────────────────────────────────────────────────────────────────────────
// Episode Management
// ─────────────────────────────────────────────────────────────────────────────

void UDESquadManager::Reset(const TArray<ADECharacter*>& TeamAgents)
{
	UE_LOG(LogTemp, Log, TEXT("[DESquadManager] Resetting planner for Team %d"), TeamID);

	TimeSinceLastPlan       = 0.0f;
	PlanConfidence          = 0.0f;
	PlanningCycleCount      = 0;
	EventDrivenReplanCount  = 0;
	bHasPreviousState       = false;
	bHealthCriticalTriggered = false;
	ValidationTickCounter   = 0.0f;
	LastPlanningDurationMs  = 0.0f;

	ActiveTacticalPlay    = ETacticalPlay::StandardComp;
	PreviousTacticalPlay  = ETacticalPlay::StandardComp;

	CurrentRoleAssignments.Empty();
	CurrentRoleAssignments.Init(EDEStrategyType::Assault, 5);

	EpisodeCount++;
	SampleRandomTacticalPlay(TeamAgents);

	UE_LOG(LogTemp, Log, TEXT("[DESquadManager] Team %d reset complete"), TeamID);
}


// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void UDESquadManager::TickPlanner(float DeltaTime,
                                const TArray<ADECharacter*>& TeamAgents,
                                const TArray<ADECharacter*>& EnemyAgents)
{
	TimeSinceLastPlan += DeltaTime;

	if (ShouldReplan())
	{
		PerformTacticalPlanning(TeamAgents, EnemyAgents);
		TimeSinceLastPlan = 0.0f;
	}



	// Periodic validation log
	ValidationTickCounter += DeltaTime;
	if (ValidationTickCounter >= 2.0f)
	{
		ValidationTickCounter = 0.0f;


		if (CurrentRoleAssignments.Num() != 5)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Validation] Team %d: role count=%d (expected 5)"),
				TeamID, CurrentRoleAssignments.Num());
		}

	}
}

bool UDESquadManager::ShouldReplan() const
{
	return TimeSinceLastPlan >= Config.PlanningInterval;
}


// ─────────────────────────────────────────────────────────────────────────────
// Centralized Planning
// ─────────────────────────────────────────────────────────────────────────────

void UDESquadManager::PerformTacticalPlanning(const TArray<ADECharacter*>& TeamAgents,
                                            const TArray<ADECharacter*>& EnemyAgents)
{
	// Phase 1 RL: strategy is fixed per episode by SampleRandomTacticalPlay at reset.
	// Mid-episode replanning would overwrite the round-robin assignment.
	if (Config.bRLTrainingMode)
	{
		return;
	}
}




EDEStrategyType UDESquadManager::GetAgentStrategy(int32 AgentIndex) const
{
	return (AgentIndex >= 0 && AgentIndex < CurrentRoleAssignments.Num())
		? CurrentRoleAssignments[AgentIndex]
		: EDEStrategyType::Assault;
}


// ─────────────────────────────────────────────────────────────────────────────
// Event-Driven Replanning
// ─────────────────────────────────────────────────────────────────────────────

void UDESquadManager::ReplanOnCriticalEvent(EDECriticalEventType EventType,
                                          AActor* InstigatorActor,
                                          const TArray<ADECharacter*>& TeamAgents,
                                          const TArray<ADECharacter*>& EnemyAgents)
{


	if (InstigatorActor)
	{
		UE_LOG(LogTemp, Log, TEXT("[DESquadManager] Team %d: Critical event %s by %s — replanning"),
			TeamID, *UEnum::GetValueAsString(EventType), *InstigatorActor->GetName());
	}

	PerformTacticalPlanning(TeamAgents, EnemyAgents);
	EventDrivenReplanCount++;
	TimeSinceLastPlan = 0.0f;
}


// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

void UDESquadManager::SampleRandomTacticalPlay(const TArray<ADECharacter*>& TeamAgents)
{
	// Round-robin strategy wheel: agent i → StrategyWheel[(i + EpisodeCount) % 3].
	// Over every 3 episodes each agent rotates through all 3 strategies
	// for balanced RL training signal.
	static const EDEStrategyType StrategyWheel[3] = {
		EDEStrategyType::Assault,
		EDEStrategyType::Defend,
		EDEStrategyType::Support
	};

	TArray<EDEStrategyType> Roles;
	Roles.SetNum(5);
	int32 nA = 0, nD = 0, nS = 0;
	for (int32 i = 0; i < 5; i++)
	{
		const EDEStrategyType S = StrategyWheel[(i + EpisodeCount) % 3];
		Roles[i] = S;
		if      (S == EDEStrategyType::Assault) nA++;
		else if (S == EDEStrategyType::Defend)  nD++;
		else                                   nS++;
	}

	DistributeRoles(Roles, TeamAgents);
	ActiveTacticalPlay     = ETacticalPlay::StandardComp;
	CurrentRoleAssignments = Roles;

	UE_LOG(LogTemp, Log,
		TEXT("[DESquadManager] Phase 1 RL: Episode %d round-robin → %dA %dD %dS"),
		EpisodeCount, nA, nD, nS);
}



TArray<EDEStrategyType> UDESquadManager::DecodeTacticalPlay(ETacticalPlay Play) const
{
	switch (Play)
	{
	case ETacticalPlay::AllOutRush:      return { EDEStrategyType::Assault, EDEStrategyType::Assault, EDEStrategyType::Assault, EDEStrategyType::Assault, EDEStrategyType::Assault };
	case ETacticalPlay::AggressivePush:  return { EDEStrategyType::Assault, EDEStrategyType::Assault, EDEStrategyType::Assault, EDEStrategyType::Assault, EDEStrategyType::Support };
	case ETacticalPlay::Phalanx:         return { EDEStrategyType::Defend,  EDEStrategyType::Defend,  EDEStrategyType::Support, EDEStrategyType::Support, EDEStrategyType::Support };
	case ETacticalPlay::StandardComp:    return { EDEStrategyType::Assault, EDEStrategyType::Assault, EDEStrategyType::Defend,  EDEStrategyType::Defend,  EDEStrategyType::Support };
	case ETacticalPlay::FortressDefense: return { EDEStrategyType::Assault, EDEStrategyType::Defend,  EDEStrategyType::Defend,  EDEStrategyType::Defend,  EDEStrategyType::Defend  };
	case ETacticalPlay::TurtleFormation: return { EDEStrategyType::Defend,  EDEStrategyType::Defend,  EDEStrategyType::Defend,  EDEStrategyType::Defend,  EDEStrategyType::Defend  };
	case ETacticalPlay::BaitStrategy:    return { EDEStrategyType::Assault, EDEStrategyType::Defend,  EDEStrategyType::Defend,  EDEStrategyType::Defend,  EDEStrategyType::Defend  };
	case ETacticalPlay::PincerManeuver:  return { EDEStrategyType::Assault, EDEStrategyType::Assault, EDEStrategyType::Assault, EDEStrategyType::Support, EDEStrategyType::Support };
	case ETacticalPlay::HealerComp:      return { EDEStrategyType::Assault, EDEStrategyType::Assault, EDEStrategyType::Defend,  EDEStrategyType::Support, EDEStrategyType::Support };
	case ETacticalPlay::ResourceDeny:    return { EDEStrategyType::Assault, EDEStrategyType::Assault, EDEStrategyType::Support, EDEStrategyType::Support, EDEStrategyType::Support };
	default:                             return { EDEStrategyType::Assault, EDEStrategyType::Assault, EDEStrategyType::Defend,  EDEStrategyType::Defend,  EDEStrategyType::Support };
	}
}

void UDESquadManager::DistributeRoles(const TArray<EDEStrategyType>& Roles,
                                    const TArray<ADECharacter*>& TeamAgents) const
{
	for (int32 i = 0; i < FMath::Min(Roles.Num(), TeamAgents.Num()); ++i)
	{
		ADECharacter* Agent = TeamAgents[i];
		if (Agent && Agent->IsAlive())
		{
			Agent->SetCommandedStrategy(Roles[i]);

			if (Config.bShowDebugInfo)
			{
				UE_LOG(LogTemp, Log, TEXT("[DESquadManager] Team %d Agent %d → %s"),
					TeamID, i, *UEnum::GetValueAsString(Roles[i]));
			}
		}
	}
}

void UDESquadManager::LogPlanningDecision(ETacticalPlay Play, float Confidence) const
{
	if (Config.bShowDebugInfo)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[DESquadManager] Team %d: Play=%s, Confidence=%.2f, Cycle=%d"),
			TeamID, *UEnum::GetValueAsString(Play), Confidence, PlanningCycleCount);
	}
}

void UDESquadManager::DrawDebugVisualization(const TArray<ADECharacter*>& TeamAgents) const
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	for (int32 i = 0; i < FMath::Min(CurrentRoleAssignments.Num(), TeamAgents.Num()); ++i)
	{
		ADECharacter* Agent = TeamAgents[i];
		if (!Agent || !Agent->IsAlive()) { continue; }

		FColor RoleColor = FColor::White;
		switch (CurrentRoleAssignments[i])
		{
		case EDEStrategyType::Assault: RoleColor = FColor::Red;   break;
		case EDEStrategyType::Defend:  RoleColor = FColor::Blue;  break;
		case EDEStrategyType::Support: RoleColor = FColor::Green; break;
		}

		DrawDebugString(World,
			Agent->GetActorLocation() + FVector(0, 0, 150),
			UEnum::GetValueAsString(CurrentRoleAssignments[i]),
			nullptr, RoleColor, 0.0f, true);
	}
}




// ─────────────────────────────────────────────────────────────────────────────
// Epsilon-Greedy Action Selection
// ─────────────────────────────────────────────────────────────────────────────

ETacticalPlay UDESquadManager::SelectEpsilonGreedyAction(
	const TArray<ETacticalPlay>& FeasiblePlays) const
{
	// Sampling weights (per play index, matching ETacticalPlay enum order)
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
	static constexpr float PlayWeights[10] = { 4.f, 6.f, 16.f, 11.f, 9.f, 7.f, 7.f, 13.f, 14.f, 13.f };

	// Helper: first feasible play from an ordered preference list
	auto PickFeasible = [&](TArrayView<const ETacticalPlay> Prefs) -> ETacticalPlay
	{
		for (ETacticalPlay P : Prefs)
		{
			if (FeasiblePlays.Contains(P)) return P;
		}
		return FeasiblePlays[0]; // GetFeasiblePlays guarantees non-empty
	};

	const float RandValue = Config.RandomStream.FRand();

	if (RandValue < Config.ExplorationRate)
	{
		// Exploration: weighted random within feasible set
		float TotalWeight = 0.0f;
		for (ETacticalPlay Play : FeasiblePlays)
			TotalWeight += PlayWeights[static_cast<int32>(Play)];

		const float R = Config.RandomStream.FRand() * TotalWeight;
		float Cumulative = 0.0f;
		for (ETacticalPlay Play : FeasiblePlays)
		{
			Cumulative += PlayWeights[static_cast<int32>(Play)];
			if (R < Cumulative) return Play;
		}
		return FeasiblePlays[0];
	}


	// shuffle the preferred candidate
	static const ETacticalPlay MidCandidates[4] = {
		ETacticalPlay::StandardComp, ETacticalPlay::HealerComp,
		ETacticalPlay::PincerManeuver, ETacticalPlay::BaitStrategy
	};
	const int32 Pick = Config.RandomStream.RandRange(0, 3);
	const ETacticalPlay Prefs[] = { MidCandidates[Pick], ETacticalPlay::StandardComp, ETacticalPlay::AllOutRush };
	return PickFeasible(Prefs);
	
}


// ─────────────────────────────────────────────────────────────────────────────
// Capture Point Event Handler
// ─────────────────────────────────────────────────────────────────────────────

void UDESquadManager::OnPointCapturedHandler(ECapturePointID PointID,
                                            int32 PreviousTeamID,
                                            int32 NewTeamID)
{


	EDECriticalEventType EventType;
	if      (NewTeamID      == TeamID) EventType = EDECriticalEventType::ObjectiveCaptured;
	else if (PreviousTeamID == TeamID) EventType = EDECriticalEventType::ObjectiveLost;
	else                                    return; // Not relevant to this team

	// No live agent array available from a delegate; pass empty lists.
	// DEMatchManager will push a full replan via TickPlanner next frame.
	ReplanOnCriticalEvent(EventType, nullptr, {}, {});
}
