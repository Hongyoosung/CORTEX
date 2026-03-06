// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/SquadManager.h"
#include "Characters/MocCharacter.h"
#include "AI/MCTS/TeamMCTS.h"
#include "AI/Models/TeamWorldModel.h"
#include "AI/Training/TeamDataCollector.h"
#include "Types/RewardTypes.h"
#include "Actors/CapturePoint.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"


// ─────────────────────────────────────────────────────────────────────────────
// Debug helper — a module-local weak pointer to the most-recently created
// USquadManager (Team 0 by convention) for console commands.
// ─────────────────────────────────────────────────────────────────────────────

static TWeakObjectPtr<USquadManager> GDebugSquadManager;


// ─────────────────────────────────────────────────────────────────────────────
// Constructor — default role assignments + one-time console command registration
// ─────────────────────────────────────────────────────────────────────────────

USquadManager::USquadManager()
{
	CurrentRoleAssignments = {
		EStrategyType::Assault,
		EStrategyType::Assault,
		EStrategyType::Defend,
		EStrategyType::Defend,
		EStrategyType::Support
	};

	// ── Debug console commands ──────────────────────────────────────────────

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
				UE_LOG(LogTemp, Display, TEXT("  Planning Cycles: %d  (Event Replans: %d)"),
					SM->PlanningCycleCount, SM->EventDrivenReplanCount);

				for (int32 i = 0; i < SM->CurrentRoleAssignments.Num(); ++i)
				{
					UE_LOG(LogTemp, Display, TEXT("  Agent %d: %s"),
						i, *UEnum::GetValueAsString(SM->CurrentRoleAssignments[i]));
				}
				if (SM->TeamMCTSPlanner)
				{
					UE_LOG(LogTemp, Display, TEXT("  MCTS Last Iterations: %d"),
						SM->TeamMCTSPlanner->GetLastIterationCount());
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
		TEXT("Trigger immediate tactical replanning (uses cached state — no live agents required)"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (USquadManager* SM = GDebugSquadManager.Get())
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
			if (USquadManager* SM = GDebugSquadManager.Get())
			{
				UE_LOG(LogTemp, Display, TEXT("=== Timing Stats (Team %d) ==="), SM->TeamID);
				if (SM->TeamWorldModel)
				{
					UE_LOG(LogTemp, Display, TEXT("  World Model Avg Latency: %.2f ms"),
						SM->TeamWorldModel->GetAverageLatency());
				}
				else 
				{
					UE_LOG(LogTemp, Display, TEXT("  World Model: Not initialized"));
				}
					
				UE_LOG(LogTemp, Display, TEXT("  Last Planning Duration: %.2f ms"),
					SM->LastPlanningDurationMs);
			}
		})
	);
}


// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

void USquadManager::Initialize(int32 InTeamID)
{
	TeamID = InTeamID;

	// World model (ONNX) — path is pushed later via Configure().
	TeamWorldModel = NewObject<UTeamWorldModel>(this);

	// MCTS planner
	TeamMCTSPlanner = NewObject<UTeamMCTS>(this);
	if (TeamMCTSPlanner && TeamWorldModel)
	{
		FTeamMCTSConfig MCTSConfig;
		MCTSConfig.MaxIterations = 50;
		// TimeBudget and BatchSize are updated when Configure() is called.
		TeamMCTSPlanner->Setup(TeamWorldModel, MCTSConfig);

		UE_LOG(LogTemp, Log, TEXT("[SquadManager] Team %d: MCTS initialized (world model not yet loaded)"), TeamID);
	}

	// Training data collector
	DataCollector = NewObject<UTeamDataCollector>(this);
	UE_LOG(LogTemp, Log, TEXT("[SquadManager] Team %d: Data collector created"), TeamID);

	// Register as the debug target (last-created wins — fine for single-env sessions)
	GDebugSquadManager = this;
}

void USquadManager::Configure(const FSquadConfig& InConfig)
{
	Config = InConfig;

	// Re-apply time budget to MCTS if already constructed
	if (TeamMCTSPlanner)
	{
		FTeamMCTSConfig MCTSConfig;
		MCTSConfig.MaxIterations = 50;
		// Caller may expose BatchSize via Config in the future
		TeamMCTSPlanner->Setup(TeamWorldModel, MCTSConfig);
	}
}

void USquadManager::BindCapturePoints(const TArray<ACapturePoint*>& CapturePoints)
{
	CachedCapturePoints = CapturePoints;
	UE_LOG(LogTemp, Log, TEXT("[SquadManager] Team %d: Bound %d capture points"),
		TeamID, CapturePoints.Num());
}


// ─────────────────────────────────────────────────────────────────────────────
// Episode Management
// ─────────────────────────────────────────────────────────────────────────────

void USquadManager::Reset(const TArray<AMocCharacter*>& TeamAgents)
{
	UE_LOG(LogTemp, Log, TEXT("[SquadManager] Resetting planner for Team %d"), TeamID);

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
	PreviousTeamState     = FTeamWorldState();

	CurrentRoleAssignments.Empty();
	CurrentRoleAssignments.Init(EStrategyType::Assault, 5);

	EpisodeCount++;
	SampleRandomTacticalPlay(TeamAgents);

	UE_LOG(LogTemp, Log, TEXT("[SquadManager] Team %d reset complete"), TeamID);
}


// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void USquadManager::TickPlanner(float DeltaTime,
                                const TArray<AMocCharacter*>& TeamAgents,
                                const TArray<AMocCharacter*>& EnemyAgents)
{
	TimeSinceLastPlan += DeltaTime;

	if (ShouldReplan())
	{
		PerformTacticalPlanning(TeamAgents, EnemyAgents);
		TimeSinceLastPlan = 0.0f;
	}

	// Critical-health check
	const FTeamWorldState CurrentState = BuildTeamWorldState(TeamAgents, EnemyAgents);
	const bool bIsCritical = CurrentState.IsTeamHealthCritical();

	if (bIsCritical && !bHealthCriticalTriggered)
	{
		bHealthCriticalTriggered = true;
		ReplanOnCriticalEvent(ECriticalEventType::HealthCritical, nullptr, TeamAgents, EnemyAgents);
	}
	else if (!bIsCritical)
	{
		bHealthCriticalTriggered = false;
	}

	// Periodic validation log
	ValidationTickCounter += DeltaTime;
	if (ValidationTickCounter >= 2.0f)
	{
		ValidationTickCounter = 0.0f;

		const TArray<float> Tensor = CurrentState.ToTensor();
		UE_LOG(LogTemp, Verbose, TEXT("[Validation] Team %d: tensor dims=%d (expected ~60)"),
			TeamID, Tensor.Num());

		if (CurrentRoleAssignments.Num() != 5)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Validation] Team %d: role count=%d (expected 5)"),
				TeamID, CurrentRoleAssignments.Num());
		}
		UE_LOG(LogTemp, Verbose, TEXT("[Validation] Team %d: lastPlan=%.2f ms, worldModel=%.2f ms"),
			TeamID, LastPlanningDurationMs,
			TeamWorldModel ? TeamWorldModel->GetAverageLatency() : 0.0f);
	}
}

bool USquadManager::ShouldReplan() const
{
	return TimeSinceLastPlan >= Config.PlanningInterval;
}


// ─────────────────────────────────────────────────────────────────────────────
// Centralized Planning
// ─────────────────────────────────────────────────────────────────────────────

void USquadManager::PerformTacticalPlanning(const TArray<AMocCharacter*>& TeamAgents,
                                            const TArray<AMocCharacter*>& EnemyAgents)
{
	const float StartTime = FPlatformTime::Seconds();

	// 1. Build team state from caller-supplied agent lists
	const FTeamWorldState GlobalState = BuildTeamWorldState(TeamAgents, EnemyAgents);

	// 2. Record transition for data collection
	if (bHasPreviousState && DataCollector && DataCollector->bIsRecording)
	{
		const FCompositeReward Reward = CalculateTeamReward(PreviousTeamState, GlobalState);
		DataCollector->RecordTransition(PreviousTeamState, PreviousTacticalPlay, GlobalState, Reward);
	}

	// 3. Select tactical play
	const TArray<ETacticalPlay> FeasiblePlays = GetFeasiblePlays(GlobalState);
	ETacticalPlay BestPlay = ETacticalPlay::StandardComp;

	if (Config.bDataCollectionMode)
	{
		BestPlay = SelectEpsilonGreedyAction(GlobalState, FeasiblePlays);

		if (Config.bShowDebugInfo)
		{
			const float Elapsed = (FPlatformTime::Seconds() - StartTime) * 1000.0f;
			UE_LOG(LogTemp, Display,
				TEXT("[SquadManager] Team %d ε-Greedy: %s (%.3f ms, ε=%.2f, feasible=%d)"),
				TeamID, *UEnum::GetValueAsString(BestPlay), Elapsed,
				Config.ExplorationRate, FeasiblePlays.Num());
		}
	}
	else if (TeamMCTSPlanner && TeamWorldModel && TeamWorldModel->IsModelLoaded())
	{
		BestPlay = TeamMCTSPlanner->FindBestTacticalPlay(GlobalState, FeasiblePlays);

		if (Config.bShowDebugInfo)
		{
			const float Elapsed = (FPlatformTime::Seconds() - StartTime) * 1000.0f;
			UE_LOG(LogTemp, Display,
				TEXT("[SquadManager] Team %d MCTS: %.2f ms, %d iters, feasible=%d"),
				TeamID, Elapsed, TeamMCTSPlanner->GetLastIterationCount(), FeasiblePlays.Num());

			if (Elapsed > Config.MCTSTimeBudget * 1000.0f)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[SquadManager] Team %d MCTS exceeded budget: %.2f ms > %.2f ms"),
					TeamID, Elapsed, Config.MCTSTimeBudget * 1000.0f);
			}
		}
	}
	else
	{
		// Heuristic fallback: pick by average team health
		const float AvgHealth = GlobalState.GetAverageHealth();
		const ETacticalPlay Preferred =
			(AvgHealth < 0.3f) ? ETacticalPlay::FortressDefense :
			(AvgHealth > 0.7f) ? ETacticalPlay::AggressivePush :
			                     ETacticalPlay::StandardComp;

		BestPlay = FeasiblePlays.Contains(Preferred) ? Preferred : FeasiblePlays[0];

		if (Config.bShowDebugInfo)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[SquadManager] Team %d: Heuristic fallback (no model), feasible=%d"),
				TeamID, FeasiblePlays.Num());
		}
	}

	// 4. Decode play → role array and push to agents
	const TArray<EStrategyType> NewRoles = DecodeTacticalPlay(BestPlay);
	DistributeRoles(NewRoles, TeamAgents);

	// 5. Update state
	ActiveTacticalPlay      = BestPlay;
	CurrentRoleAssignments  = NewRoles;
	PlanConfidence          = 0.8f;
	PlanningCycleCount++;

	PreviousTeamState       = GlobalState;
	PreviousTacticalPlay    = BestPlay;
	bHasPreviousState       = true;
	LastPlanningDurationMs  = (FPlatformTime::Seconds() - StartTime) * 1000.0f;

	LogPlanningDecision(BestPlay, PlanConfidence);
}

FTeamWorldState USquadManager::BuildTeamWorldState(
	const TArray<AMocCharacter*>& TeamAgents,
	const TArray<AMocCharacter*>& EnemyAgents) const
{
	FTeamWorldState State;

	for (int32 i = 0; i < FMath::Min(5, TeamAgents.Num()); ++i)
	{
		if (AMocCharacter* A = TeamAgents[i])
		{
			State.FriendlyPositions[i] = A->GetActorLocation();
			State.FriendlyHealths[i]   = A->GetHealthPercentage_Implementation();
			State.FriendlyCooldowns[i] = A->GetWeaponCooldown_Implementation();
			State.FriendlyAlive[i]     = A->IsAlive_Implementation();
		}
	}

	for (int32 i = 0; i < FMath::Min(5, EnemyAgents.Num()); ++i)
	{
		if (AMocCharacter* A = EnemyAgents[i])
		{
			State.EnemyPositions[i]   = A->GetActorLocation();
			State.EnemyHealths[i]     = A->GetHealthPercentage_Implementation();
			State.EnemyAlive[i]       = A->IsAlive_Implementation();
			State.EnemyConfidences[i] = 1.0f;
		}
	}


	return State;
}



EStrategyType USquadManager::GetAgentStrategy(int32 AgentIndex) const
{
	return (AgentIndex >= 0 && AgentIndex < CurrentRoleAssignments.Num())
		? CurrentRoleAssignments[AgentIndex]
		: EStrategyType::Assault;
}


// ─────────────────────────────────────────────────────────────────────────────
// Event-Driven Replanning
// ─────────────────────────────────────────────────────────────────────────────

void USquadManager::ReplanOnCriticalEvent(ECriticalEventType EventType,
                                          AActor* InstigatorActor,
                                          const TArray<AMocCharacter*>& TeamAgents,
                                          const TArray<AMocCharacter*>& EnemyAgents)
{
	if (Config.bRLTrainingMode)
	{
		// Phase 1 RL: don't replan — but check feasibility and resample if needed.
		const FTeamWorldState CurrentState = BuildTeamWorldState(TeamAgents, EnemyAgents);
		if (!GetFeasiblePlays(CurrentState).Contains(ActiveTacticalPlay))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[SquadManager] Phase 1 RL Team %d: play %s no longer feasible after event %s — resampling"),
				TeamID, *UEnum::GetValueAsString(ActiveTacticalPlay),
				*UEnum::GetValueAsString(EventType));
			SampleRandomTacticalPlay(TeamAgents);
			EventDrivenReplanCount++;
		}
		return;
	}

	if (InstigatorActor)
	{
		UE_LOG(LogTemp, Log, TEXT("[SquadManager] Team %d: Critical event %s by %s — replanning"),
			TeamID, *UEnum::GetValueAsString(EventType), *InstigatorActor->GetName());
	}

	PerformTacticalPlanning(TeamAgents, EnemyAgents);
	EventDrivenReplanCount++;
	TimeSinceLastPlan = 0.0f;
}


// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

void USquadManager::SampleRandomTacticalPlay(const TArray<AMocCharacter*>& TeamAgents)
{
	// Round-robin strategy wheel: agent i → StrategyWheel[(i + EpisodeCount) % 3].
	// Over every 3 episodes each agent rotates through all 3 strategies
	// for balanced RL training signal.
	static const EStrategyType StrategyWheel[3] = {
		EStrategyType::Assault,
		EStrategyType::Defend,
		EStrategyType::Support
	};

	TArray<EStrategyType> Roles;
	Roles.SetNum(5);
	int32 nA = 0, nD = 0, nS = 0;
	for (int32 i = 0; i < 5; i++)
	{
		const EStrategyType S = StrategyWheel[(i + EpisodeCount) % 3];
		Roles[i] = S;
		if      (S == EStrategyType::Assault) nA++;
		else if (S == EStrategyType::Defend)  nD++;
		else                                   nS++;
	}

	DistributeRoles(Roles, TeamAgents);
	ActiveTacticalPlay     = ETacticalPlay::StandardComp;
	CurrentRoleAssignments = Roles;

	UE_LOG(LogTemp, Log,
		TEXT("[SquadManager] Phase 1 RL: Episode %d round-robin → %dA %dD %dS"),
		EpisodeCount, nA, nD, nS);
}

TArray<ETacticalPlay> USquadManager::GetFeasiblePlays(const FTeamWorldState& State) const
{
	// Count alive allies
	int32 AliveCount = 0;
	for (bool bAlive : State.FriendlyAlive) { if (bAlive) AliveCount++; }
	const bool bCanSupport = (AliveCount >= 2);

	static const ETacticalPlay AllPlays[10] = {
		ETacticalPlay::AllOutRush,
		ETacticalPlay::AggressivePush,
		ETacticalPlay::Phalanx,
		ETacticalPlay::StandardComp,
		ETacticalPlay::FortressDefense,
		ETacticalPlay::TurtleFormation,
		ETacticalPlay::BaitStrategy,
		ETacticalPlay::PincerManeuver,
		ETacticalPlay::HealerComp,
		ETacticalPlay::ResourceDeny,
	};

	TArray<ETacticalPlay> Feasible;
	for (ETacticalPlay Play : AllPlays)
	{
		// Support-role plays require ≥2 alive allies
		if (DecodeTacticalPlay(Play).Contains(EStrategyType::Support) && !bCanSupport)
		{
			continue;
		}
		Feasible.Add(Play);
	}

	if (Feasible.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[SquadManager] Team %d: No feasible plays — falling back to AllOutRush"), TeamID);
		Feasible.Add(ETacticalPlay::AllOutRush);
	}
	return Feasible;
}

TArray<EStrategyType> USquadManager::DecodeTacticalPlay(ETacticalPlay Play) const
{
	switch (Play)
	{
	case ETacticalPlay::AllOutRush:      return { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Assault };
	case ETacticalPlay::AggressivePush:  return { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Support };
	case ETacticalPlay::Phalanx:         return { EStrategyType::Defend,  EStrategyType::Defend,  EStrategyType::Support, EStrategyType::Support, EStrategyType::Support };
	case ETacticalPlay::StandardComp:    return { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Defend,  EStrategyType::Defend,  EStrategyType::Support };
	case ETacticalPlay::FortressDefense: return { EStrategyType::Assault, EStrategyType::Defend,  EStrategyType::Defend,  EStrategyType::Defend,  EStrategyType::Defend  };
	case ETacticalPlay::TurtleFormation: return { EStrategyType::Defend,  EStrategyType::Defend,  EStrategyType::Defend,  EStrategyType::Defend,  EStrategyType::Defend  };
	case ETacticalPlay::BaitStrategy:    return { EStrategyType::Assault, EStrategyType::Defend,  EStrategyType::Defend,  EStrategyType::Defend,  EStrategyType::Defend  };
	case ETacticalPlay::PincerManeuver:  return { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Support, EStrategyType::Support };
	case ETacticalPlay::HealerComp:      return { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Defend,  EStrategyType::Support, EStrategyType::Support };
	case ETacticalPlay::ResourceDeny:    return { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Support, EStrategyType::Support, EStrategyType::Support };
	default:                             return { EStrategyType::Assault, EStrategyType::Assault, EStrategyType::Defend,  EStrategyType::Defend,  EStrategyType::Support };
	}
}

void USquadManager::DistributeRoles(const TArray<EStrategyType>& Roles,
                                    const TArray<AMocCharacter*>& TeamAgents) const
{
	for (int32 i = 0; i < FMath::Min(Roles.Num(), TeamAgents.Num()); ++i)
	{
		AMocCharacter* Agent = TeamAgents[i];
		if (Agent && Agent->IsAlive_Implementation())
		{
			Agent->SetCommandedStrategy(Roles[i]);

			if (Config.bShowDebugInfo)
			{
				UE_LOG(LogTemp, Log, TEXT("[SquadManager] Team %d Agent %d → %s"),
					TeamID, i, *UEnum::GetValueAsString(Roles[i]));
			}
		}
	}
}

void USquadManager::LogPlanningDecision(ETacticalPlay Play, float Confidence) const
{
	if (Config.bShowDebugInfo)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[SquadManager] Team %d: Play=%s, Confidence=%.2f, Cycle=%d"),
			TeamID, *UEnum::GetValueAsString(Play), Confidence, PlanningCycleCount);
	}
}

void USquadManager::DrawDebugVisualization(const TArray<AMocCharacter*>& TeamAgents) const
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	for (int32 i = 0; i < FMath::Min(CurrentRoleAssignments.Num(), TeamAgents.Num()); ++i)
	{
		AMocCharacter* Agent = TeamAgents[i];
		if (!Agent || !Agent->IsAlive_Implementation()) { continue; }

		FColor RoleColor = FColor::White;
		switch (CurrentRoleAssignments[i])
		{
		case EStrategyType::Assault: RoleColor = FColor::Red;   break;
		case EStrategyType::Defend:  RoleColor = FColor::Blue;  break;
		case EStrategyType::Support: RoleColor = FColor::Green; break;
		}

		DrawDebugString(World,
			Agent->GetActorLocation() + FVector(0, 0, 150),
			UEnum::GetValueAsString(CurrentRoleAssignments[i]),
			nullptr, RoleColor, 0.0f, true);
	}
}


// ─────────────────────────────────────────────────────────────────────────────
// Reward
// ─────────────────────────────────────────────────────────────────────────────

FCompositeReward USquadManager::CalculateTeamReward(const FTeamWorldState& OldState,
                                                    const FTeamWorldState& NewState) const
{
	FCompositeReward Reward;

	// Health delta sum
	float HealthDeltaSum = 0.0f;
	int32 AliveCount = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		if (OldState.FriendlyAlive[i] || NewState.FriendlyAlive[i])
		{
			const float OldH = OldState.FriendlyAlive[i] ? OldState.FriendlyHealths[i] : 0.0f;
			const float NewH = NewState.FriendlyAlive[i] ? NewState.FriendlyHealths[i] : 0.0f;
			HealthDeltaSum += (NewH - OldH);
		}
		if (NewState.FriendlyAlive[i]) AliveCount++;
	}
	Reward.HealthDelta = HealthDeltaSum;

	// Win probability (alive ratio × 0.6 + health ratio × 0.4)
	int32 EnemyAliveCount = 0;
	float EnemyHealthSum = 0.0f, TeamHealthSum = 0.0f;
	for (int32 i = 0; i < 5; ++i)
	{
		if (NewState.EnemyAlive[i])   { EnemyAliveCount++; EnemyHealthSum  += NewState.EnemyHealths[i]; }
		if (NewState.FriendlyAlive[i])                     TeamHealthSum   += NewState.FriendlyHealths[i];
	}
	const float AliveRatio  = EnemyAliveCount > 0
		? float(AliveCount) / float(EnemyAliveCount + AliveCount) : 1.0f;
	const float HealthRatio = (EnemyHealthSum + TeamHealthSum) > 0.0f
		? TeamHealthSum / (EnemyHealthSum + TeamHealthSum) : 0.5f;
	Reward.WinProb = FMath::Clamp(AliveRatio * 0.6f + HealthRatio * 0.4f, 0.0f, 1.0f);

	// Objective score (friendly points − enemy points, normalised to [-1, 1])
	int32 FriendlyPoints = 0, EnemyPoints = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		if      (NewState.CapturePointOwnership[i] ==  1) FriendlyPoints++;
		else if (NewState.CapturePointOwnership[i] == -1) EnemyPoints++;
	}
	Reward.ObjectiveScore = (FriendlyPoints - EnemyPoints) / 5.0f;

	return Reward;
}


// ─────────────────────────────────────────────────────────────────────────────
// Epsilon-Greedy Action Selection
// ─────────────────────────────────────────────────────────────────────────────

ETacticalPlay USquadManager::SelectEpsilonGreedyAction(
	const FTeamWorldState& TeamState,
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

	// Exploitation: health-based heuristic
	const float AvgHealth = TeamState.GetAverageHealth();
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
		// Mid-health: shuffle the preferred candidate
		static const ETacticalPlay MidCandidates[4] = {
			ETacticalPlay::StandardComp, ETacticalPlay::HealerComp,
			ETacticalPlay::PincerManeuver, ETacticalPlay::BaitStrategy
		};
		const int32 Pick = Config.RandomStream.RandRange(0, 3);
		const ETacticalPlay Prefs[] = { MidCandidates[Pick], ETacticalPlay::StandardComp, ETacticalPlay::AllOutRush };
		return PickFeasible(Prefs);
	}
}


// ─────────────────────────────────────────────────────────────────────────────
// Capture Point Event Handler
// ─────────────────────────────────────────────────────────────────────────────

void USquadManager::OnPointCapturedHandler(ECapturePointID PointID,
                                            int32 PreviousTeamID,
                                            int32 NewTeamID)
{


	ECriticalEventType EventType;
	if      (NewTeamID      == TeamID) EventType = ECriticalEventType::ObjectiveCaptured;
	else if (PreviousTeamID == TeamID) EventType = ECriticalEventType::ObjectiveLost;
	else                                    return; // Not relevant to this team

	// No live agent array available from a delegate; pass empty lists.
	// MatchManager will push a full replan via TickPlanner next frame.
	ReplanOnCriticalEvent(EventType, nullptr, {}, {});
}
