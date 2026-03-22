// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/DEMatchManager.h"
#include "Team/DESquadManager.h"
#include "Core/Subsystems/DERewardSubsystem.h"
#include "Data/Reward/DERewardData.h"
#include "Characters/DEAgent.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GAS/DEGameplayTags.h"
#include "Schola/Components/DEScholaAgent.h"
#include "Actors/DESpawnArea.h"
#include "Actors/DECapturePoint.h"
#include "Data/DETeamData.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Player/DESpectatorController.h"



// ─────────────────────────────────────────────────────────────────────────────
// Constructor & UE lifecycle
// ─────────────────────────────────────────────────────────────────────────────

ADEMatchManager::ADEMatchManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADEMatchManager::BeginPlay()
{
	Super::BeginPlay();

	// Create per-environment reward calculator and configure it with this environment's reward data.
	RewardCalculator = NewObject<UDERewardSubsystem>(this);
	if (RewardCalculator)
	{
		RewardCalculator->RewardData = RewardData;
	}

	// Initialise runtime state and create one UDESquadManager per configured team.
	// Use FindOrAdd to avoid overwriting ActiveAgents that SpawnTeams() may have already
	// populated if ADEScholaEnvironment::BeginPlay() ran before this actor's BeginPlay.
	for (const FDETeamConfiguration& Config : TeamConfigs)
	{
		const int32 ID = Config.TeamID;

		FDETeamState& State = TeamStates.FindOrAdd(ID);
		State.TeamID = ID;

		UDESquadManager* Commander = NewObject<UDESquadManager>(this);
		Commander->Initialize(ID);
		Commander->BindCapturePoints(EnvCapturePoints);
		Commander->Configure(MakeSquadConfig());
		// Configuration is pushed later by ADEScholaEnvironment via MakeSquadConfig().
		SquadCommanders.Add(ID, Commander);
	}

	// Start timer-based gameplay loop (replaces Tick)
	GetWorld()->GetTimerManager().SetTimer(
		GameplayTimerHandle, this, &ADEMatchManager::GameplayTimerTick, 0.05f, true);
	GetWorld()->GetTimerManager().SetTimer(
		MatchConditionTimerHandle, this, &ADEMatchManager::MatchConditionTimerTick, 1.0f, true);

	// In standalone/inference mode there is no ADEScholaEnvironment to call
	// CapturePointInitialize(), so we do it here ourselves.
	if (bStandaloneMode)
	{
		CapturePointInitialize();
		UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Standalone mode — capture points auto-initialized (%d points)"),
			EnvCapturePoints.Num());

		// Register pre-placed ADEAgent actors that were not spawned by SpawnTeams().
		// We defer one tick so all characters have completed their own BeginPlay first.
		FTimerHandle DummyHandle;
		GetWorld()->GetTimerManager().SetTimerForNextTick([this]()
		{
			TArray<AActor*> FoundActors;
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADEAgent::StaticClass(), FoundActors);
			for (AActor* A : FoundActors)
			{
				ADEAgent* Agent = Cast<ADEAgent>(A);
				if (!Agent || AllAgents.Contains(Agent)) { continue; }

				const int32 TID = Agent->GetTeamID_Implementation();
				AllAgents.Add(Agent);
				FDETeamState& State = TeamStates.FindOrAdd(TID);
				State.TeamID = TID;
				State.ActiveAgents.AddUnique(Agent);

				UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Standalone: registered pre-placed agent %s (Team %d)"),
					*Agent->GetName(), TID);
			}

			// Assign base targets to all registered agents.
			for (const FDETeamConfiguration& Cfg : TeamConfigs)
			{
				AssignBasesToAgents(Cfg.TeamID);
			}

			UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Standalone: base assignment complete for %d pre-placed agents"),
				AllAgents.Num());
		});
	}
}

// Tick disabled — logic split into GameplayTimerTick (0.05s) and MatchConditionTimerTick (1.0s)

void ADEMatchManager::GameplayTimerTick()
{
	constexpr float DeltaTime = 0.05f;

	ProcessRespawnQueue(DeltaTime);

	for (auto& Pair : SquadCommanders)
	{
		if (UDESquadManager* Commander = Pair.Value)
		{
			const int32 EnemyTeamID = (Pair.Key == 0) ? 1 : 0;
			Commander->TickPlanner(DeltaTime,
			                       GetTeamAgents(Pair.Key),
			                       GetTeamAgents(EnemyTeamID));
		}
	}
}

void ADEMatchManager::MatchConditionTimerTick()
{
	constexpr float DeltaTime = 1.0f;

	if (!bMatchActive) return;

	MatchTimer += DeltaTime;

	// ── Passive income (score tracking for winner determination at timeout) ──
	if (PassiveIncomeRate > 0.0f)
	{
		for (const ADECapturePoint* CP : EnvCapturePoints)
		{
			if (!CP) continue;
			const int32 OwnerTeam = CP->GetTeamID_Implementation();
			if (OwnerTeam >= 0)
			{
				AddTeamScore(OwnerTeam, FMath::RoundToInt(PassiveIncomeRate));
			}
		}
	}

	// Score-based and domination win conditions removed — fixed-length episodes only.
	// Score is still tracked for determining the winner at timeout.

	// ── Timeout ──
	if (MatchTimer >= MaxMatchDuration)
	{
		const int32 LeadTeam = GetWinnerTeamID();
		const EDEMatchState TimeoutState = (LeadTeam >= 0)
			? EDEMatchState::TeamWon
			: EDEMatchState::TimeExpired;

		UE_LOG(LogTemp, Warning,
			TEXT("[DEMatchManager] Env %d: Timeout — Scores [%d, %d], winner=%d"),
			EnvID, TeamScores[0], TeamScores[1], LeadTeam);
		StopMatchTimer();
		OnMatchConditionMet.Broadcast(TimeoutState, LeadTeam);
		return;
	}

	// ── Debug score display — only for the environment currently observed by the spectator ──
	{
		int32 ObservedEnvID = -1;
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (const ADESpectatorController* SC = Cast<ADESpectatorController>(PC))
			{
				ObservedEnvID = SC->GetObservedEnvID();
			}
		}

		// -1 means no agent is being watched — show all environments' scores.
		// Otherwise only draw for the environment that is currently being observed.
		if (ObservedEnvID == -1 || ObservedEnvID == EnvID)
		{
			for (const FDETeamConfiguration& Config : TeamConfigs)
			{
				if (!Config.DESpawnArea) continue;
				const FVector Loc  = Config.DESpawnArea->GetActorLocation() + FVector(0.0f, 0.0f, 300.0f);
				const FColor  Col  = Config.GetTeamColor().ToFColor(true);
				const FString Text = FString::Printf(TEXT("Team %d Score: %d  [%.0fs]"),
					Config.TeamID, TeamScores[Config.TeamID], GetTimeRemaining());
				// Duration matches the 1-second timer interval so the string stays
				// visible continuously instead of flashing once per second.
				DrawDebugString(GetWorld(), Loc, Text, nullptr, Col, 1.0f, true, 1.5f);
			}
		}
	}
}


// ─────────────────────────────────────────────────────────────────────────────
// Squad Commander configuration snapshot
// ─────────────────────────────────────────────────────────────────────────────

FDESquadConfig ADEMatchManager::MakeSquadConfig() const
{
	FDESquadConfig Out;
	Out.bRLTrainingMode   = bRLTrainingMode;
	Out.bShowDebugInfo    = bShowDebugInfo;
	Out.RandomStream      = EnvRandomStream;
	return Out;
}


// ─────────────────────────────────────────────────────────────────────────────
// Team Spawning
// ─────────────────────────────────────────────────────────────────────────────

void ADEMatchManager::SpawnTeams()
{
	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Spawning teams..."));

	SpawnTeam(0, AgentsPerTeam);
	SpawnTeam(1, AgentsPerTeam);

	// Initial base assignments for all teams
	for (const FDETeamConfiguration& Cfg : TeamConfigs)
	{
		AssignBasesToAgents(Cfg.TeamID);
	}

	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Spawned %d total agents (%d per team)"),
		AllAgents.Num(), AgentsPerTeam);
}

void ADEMatchManager::SpawnTeam(int32 TeamID, int32 AgentCount)
{
	for (int32 i = 0; i < AgentCount; ++i)
	{
		ADEAgent* Agent = SpawnAgent(TeamID, i);
		if (Agent)
		{
			FDETeamState& State = TeamStates.FindOrAdd(TeamID);
			State.TeamID = TeamID;
			State.ActiveAgents.Add(Agent);

			AllAgents.Add(Agent);
			OnAgentSpawned.Broadcast(TeamID, Agent);
		}
	}
}

ADEAgent* ADEMatchManager::SpawnAgent(int32 TeamID, int32 AgentIndex)
{
	const FDETeamConfiguration TeamConfig = GetTeamConfiguration(TeamID);
	UDETeamData* TeamData = TeamConfig.TeamData;

	if (!TeamData)
	{
		UE_LOG(LogTemp, Error, TEXT("[DEMatchManager] TeamData is NULL for Team %d — cannot spawn agent."), TeamID);
		return nullptr;
	}

	if (!TeamData->CharacterClass || !TeamData->CharacterClass->IsChildOf(ADEAgent::StaticClass()))
	{
		UE_LOG(LogTemp, Error, TEXT("[DEMatchManager] Invalid CharacterClass in TeamData for Team %d — must inherit from ADEAgent."), TeamID);
		return nullptr;
	}

	// Resolve spawn transform via DESpawnArea
	FVector   SpawnLocation = FVector::ZeroVector;
	FRotator  SpawnRotation = FRotator::ZeroRotator;

	if (TeamConfig.DESpawnArea)
	{
		SpawnLocation = TeamConfig.DESpawnArea->GetRandomSpawnPoint(EnvRandomStream);
		SpawnRotation = TeamConfig.DESpawnArea->GetSpawnRotation();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEMatchManager] No DESpawnArea assigned for Team %d"), TeamID);
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	ADEAgent* Agent = GetWorld()->SpawnActor<ADEAgent>(
		TeamData->CharacterClass, SpawnLocation, SpawnRotation, Params);

	if (!Agent)
	{
		return nullptr;
	}

	// Identity
	Agent->SetTeamID_Implementation(TeamID);
	Agent->SetEnvID_Implementation(EnvID);
	Agent->AssignedCapturePoints = EnvCapturePoints;

	// Store TeamData on the agent so ApplyClassAppearance() can reference it later.
	Agent->TeamData = TeamData;
	Agent->TeamColor = TeamData->TeamColor;

	// Apply team-level appearance defaults at spawn time.
	// These will be overridden per-class when SetCommandedClass() is called.
	if (USkeletalMeshComponent* Mesh = Agent->GetMesh())
	{
		if (TeamData->SkeletalMesh)
			Mesh->SetSkeletalMesh(TeamData->SkeletalMesh);
		if (TeamData->AnimationBlueprint)
			Mesh->SetAnimInstanceClass(TeamData->AnimationBlueprint);
	}

	// Death → respawn pipeline
	Agent->OnAgentDeathEvent_Delegate.AddDynamic(this, &ADEMatchManager::RegisterKill);
	Agent->OnAgentDied_Delegate.AddDynamic(this, &ADEMatchManager::OnAgentDied);

	const FString TeamName  = TeamData->TeamName.IsEmpty()
		? FString::Printf(TEXT("Team_%d"), TeamID)
		: TeamData->TeamName;
	const FString AgentName = FString::Printf(TEXT("%s_Agent_%d"), *TeamName, AgentIndex);

#if WITH_EDITOR
	Agent->SetActorLabel(AgentName);
#endif

	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Spawned %s (Class: %s) at %s"),
		*AgentName, *TeamData->CharacterClass->GetName(), *SpawnLocation.ToString());

	return Agent;
}


// ─────────────────────────────────────────────────────────────────────────────
// Match Timer
// ─────────────────────────────────────────────────────────────────────────────

void ADEMatchManager::StartMatchTimer()
{
	MatchTimer = 0.0f;
	PassiveIncomeAccumulator = 0.0f;
	bMatchActive = true;
	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Env %d: Match timer started"), EnvID);
}

void ADEMatchManager::StopMatchTimer()
{
	bMatchActive = false;
}


// ─────────────────────────────────────────────────────────────────────────────
// Episode Reset
// ─────────────────────────────────────────────────────────────────────────────

void ADEMatchManager::ResetScores()
{
	TeamScores[0] = 0;
	TeamScores[1] = 0;
	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Env %d: Team scores reset"), EnvID);
}

void ADEMatchManager::AddTeamScore(int32 TeamID, int32 Points)
{
	if (TeamID < 0 || TeamID > 1) return;
	TeamScores[TeamID] += Points;
	OnTeamScoreChanged.Broadcast(TeamID, TeamScores[TeamID]);
	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Env %d: Team %d score +%d → %d"),
		EnvID, TeamID, Points, TeamScores[TeamID]);
}

int32 ADEMatchManager::GetTeamScore(int32 TeamID) const
{
	return (TeamID >= 0 && TeamID <= 1) ? TeamScores[TeamID] : 0;
}

int32 ADEMatchManager::GetWinnerTeamID() const
{
	if (TeamScores[0] > TeamScores[1]) return 0;
	if (TeamScores[1] > TeamScores[0]) return 1;
	return -1; // tie
}

void ADEMatchManager::ResetTeams()
{
	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Resetting teams for new episode"));

	ResetScores();
	StartMatchTimer();

	// 1. Clear runtime state and respawn timers
	AgentRespawnTimers.Empty();
	for (const FDETeamConfiguration& Conf : TeamConfigs)
	{
		FDETeamState& State = TeamStates[Conf.TeamID];
		State.RespawnQueue.Empty();
		State.ActiveAgents.Empty();
	}

	// 2. Reactivate all agents and redistribute them into ActiveAgents
	for (ADEAgent* Agent : AllAgents)
	{
		if (!IsValid(Agent)) { continue; }

		const int32 TeamID = Agent->GetTeamID_Implementation();
		Agent->Activate();
		TeamStates[TeamID].ActiveAgents.Add(Agent);

		const FVector SpawnLoc = GetRandomSpawnPoint(GetTeamSpawnLocation(TeamID), SpawnRadius);
		Agent->SetActorLocation(SpawnLoc);
		Agent->ResetCharacter();
	}

	// 3. Reset each planner with fresh agent lists
	for (const FDETeamConfiguration& Conf : TeamConfigs)
	{
		if (UDESquadManager* Commander = GetSquadCommander(Conf.TeamID))
		{
			Commander->Reset(GetTeamAgents(Conf.TeamID));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Reset complete — %d active agents"), AllAgents.Num());
}

void ADEMatchManager::DestroyAllAgents()
{
	for (ADEAgent* Agent : AllAgents)
	{
		if (Agent) { Agent->Destroy(); }
	}
	AllAgents.Empty();

	for (const FDETeamConfiguration& Conf : TeamConfigs)
	{
		TeamStates[Conf.TeamID].ActiveAgents.Empty();
		TeamStates[Conf.TeamID].RespawnQueue.Empty();
	}

	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] All agents destroyed"));
}


// ─────────────────────────────────────────────────────────────────────────────
// Respawn
// ─────────────────────────────────────────────────────────────────────────────

void ADEMatchManager::QueueRespawn(ADEAgent* Agent, int32 TeamID)
{
	if (!Agent) { return; }

	if (!TeamStates.Contains(TeamID))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEMatchManager] QueueRespawn: invalid TeamID %d for agent %s"),
			TeamID, *Agent->GetName());
		return;
	}

	FDETeamState& State = TeamStates[TeamID];
	State.ActiveAgents.Remove(Agent);
	State.RespawnQueue.Add(Agent);
	Agent->Deactivate();

	// Individual respawn timer
	AgentRespawnTimers.Add(Agent, RespawnDelay);

	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Agent %s queued for respawn in %.1fs (Team %d, Active: %d, Queued: %d)"),
		*Agent->GetName(), RespawnDelay, TeamID, State.ActiveAgents.Num(), State.RespawnQueue.Num());

	// Team wipe detection: all agents dead → penalty for wiped team, bonus for enemy team
	if (State.ActiveAgents.Num() == 0)
	{
		const int32 EnemyTeamID = (TeamID == 0) ? 1 : 0;

		UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Team %d fully wiped! Applying team wipe rewards."), TeamID);

		if (RewardCalculator && RewardCalculator->RewardData)
		{
			// Penalty to all agents on the wiped team
			for (ADEAgent* Queued : State.RespawnQueue)
			{
				if (Queued)
					RewardCalculator->CalculateTeamWipePenalty(Queued->RewardState, Queued->GetCommandedClass(), Queued->AgentID);
			}

			// Bonus to all living agents on the enemy team
			for (ADEAgent* Enemy : GetTeamAgents(EnemyTeamID))
			{
				if (Enemy && Enemy->IsAlive())
					RewardCalculator->CalculateTeamWipeBonus(Enemy->RewardState, Enemy->GetCommandedClass(), Enemy->AgentID);
			}
		}
	}
}

void ADEMatchManager::ProcessRespawnQueue(float DeltaTime)
{
	// Collect agents whose timers have expired this frame
	TArray<ADEAgent*> ToRespawn;

	for (auto It = AgentRespawnTimers.CreateIterator(); It; ++It)
	{
		It->Value -= DeltaTime;
		if (It->Value <= 0.0f)
		{
			ToRespawn.Add(It->Key);
			It.RemoveCurrent();
		}
	}

	for (ADEAgent* Agent : ToRespawn)
	{
		if (!Agent) { continue; }

		const int32 TeamID = Agent->GetTeamID_Implementation();

		if (TeamStates.Contains(TeamID))
		{
			TeamStates[TeamID].RespawnQueue.Remove(Agent);
			TeamStates[TeamID].ActiveAgents.Add(Agent);
		}

		Agent->Activate();

		const FVector SpawnLoc = GetRandomSpawnPoint(GetTeamSpawnLocation(TeamID), SpawnRadius);
		Agent->SetActorLocation(SpawnLoc);

		if (RespawnInvincibilityDuration > 0.0f)
		{
			if (UAbilitySystemComponent* ASC = Agent->GetAbilitySystemComponent())
			{
				ASC->AddLooseGameplayTag(DEGameplayTags::State_Invulnerable);

				TWeakObjectPtr<UAbilitySystemComponent> WeakASC(ASC);
				FTimerHandle TimerHandle;
				GetWorldTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda([WeakASC]()
				{
					if (UAbilitySystemComponent* StillValid = WeakASC.Get())
					{
						StillValid->RemoveLooseGameplayTag(DEGameplayTags::State_Invulnerable);
					}
				}), RespawnInvincibilityDuration, /*bLoop=*/false);
			}
		}

		OnAgentSpawned.Broadcast(TeamID, Agent);

		UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Respawned %s (Team %d) at %s"),
			*Agent->GetName(), TeamID, *SpawnLoc.ToString());
	}
}


// ─────────────────────────────────────────────────────────────────────────────
// Team State Queries
// ─────────────────────────────────────────────────────────────────────────────

FDETeamState ADEMatchManager::GetTeamState(int32 TeamID) const
{
	return TeamStates.Contains(TeamID) ? TeamStates[TeamID] : FDETeamState();
}

FDETeamConfiguration ADEMatchManager::GetTeamConfiguration(int32 TeamID) const
{
	for (const FDETeamConfiguration& Config : TeamConfigs)
	{
		if (Config.TeamID == TeamID) { return Config; }
	}
	UE_LOG(LogTemp, Warning, TEXT("[DEMatchManager] Unknown TeamID %d"), TeamID);
	return FDETeamConfiguration();
}

const TArray<ADEAgent*>& ADEMatchManager::GetTeamAgents(int32 TeamID) const
{
	if (const FDETeamState* State = TeamStates.Find(TeamID))
	{
		return State->ActiveAgents;
	}
	static const TArray<ADEAgent*> EmptyAgents;
	return EmptyAgents;
}

TArray<ADEAgent*> ADEMatchManager::GetEnemyAgents(int32 TeamID) const
{
	// For a 2-team game the enemy is whichever team ≠ TeamID.
	const int32 EnemyID = (TeamID == 0) ? 1 : 0;
	return GetTeamAgents(EnemyID);
}

int32 ADEMatchManager::GetTotalAgentCount() const
{
	return AllAgents.Num();
}

FVector ADEMatchManager::GetTeamSpawnLocation(int32 TeamID) const
{
	const FDETeamConfiguration Config = GetTeamConfiguration(TeamID);
	return Config.DESpawnArea
		? Config.DESpawnArea->GetRandomSpawnPoint(EnvRandomStream)
		: FVector::ZeroVector;
}

FVector ADEMatchManager::GetRandomSpawnPoint(FVector BaseLocation, float Radius) const
{
	const float RandX = EnvRandomStream.FRandRange(-Radius, Radius);
	const float RandY = EnvRandomStream.FRandRange(-Radius, Radius);
	return BaseLocation + FVector(RandX, RandY, 0.0f);
}


// ─────────────────────────────────────────────────────────────────────────────
// Kill / Score
// ─────────────────────────────────────────────────────────────────────────────

void ADEMatchManager::RegisterKill(const FDEDeathEventData& DeathEvent)
{
	ADEAgent* Victim = Cast<ADEAgent>(DeathEvent.DeadActor);
	ADEAgent* Killer = Cast<ADEAgent>(DeathEvent.Killer);

	const int32 VictimTeamID = Victim ? Victim->GetTeamID_Implementation() : -1;
	const int32 KillerTeamID = Killer ? Killer->GetTeamID_Implementation() : -1;

	QueueRespawn(Victim, VictimTeamID);

	// Award match score to the killing team
	if (KillerTeamID >= 0 && KillerTeamID != VictimTeamID && KillScorePoints > 0)
	{
		AddTeamScore(KillerTeamID, KillScorePoints);
	}

	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Env %d: Kill — Killer Team %d → Victim Team %d | Scores: [%d, %d]"),
		EnvID, KillerTeamID, VictimTeamID, TeamScores[0], TeamScores[1]);
}


// ─────────────────────────────────────────────────────────────────────────────
// DECapturePoint Integration
// ─────────────────────────────────────────────────────────────────────────────

void ADEMatchManager::CapturePointInitialize()
{
	for (ADECapturePoint* CP : EnvCapturePoints)
	{
		if (!CP) { continue; }
		CP->SetEnvID_Implementation(EnvID);
		CP->SetMatchManager(this);
		CP->OnPointCaptured_Delegate.AddDynamic(this, &ADEMatchManager::OnPointCaptured);
		UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] CP %s bound — EnvID=%d, PointID=%d"),
			*CP->GetName(), CP->GetEnvID_Implementation(), static_cast<int32>(CP->PointID));
	}
}

void ADEMatchManager::ResetCapturePoint()
{
	for (ADECapturePoint* CP : EnvCapturePoints)
	{
		if (CP) { CP->ResetPoint(); }
	}
}

void ADEMatchManager::ResetEnvironment()
{
	ResetTeams();
	ResetCapturePoint();
}

void ADEMatchManager::OnPointCaptured(int32 PreviousTeam, int32 NewTeam)
{
	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Env %d: Capture point transferred Team %d → Team %d | Scores: [%d, %d]"),
		EnvID, PreviousTeam, NewTeam, TeamScores[0], TeamScores[1]);

	// Award match score to the capturing team
	if (NewTeam >= 0 && CaptureScorePoints > 0)
	{
		AddTeamScore(NewTeam, CaptureScorePoints);
	}

	if (!RewardCalculator || !RewardCalculator->RewardData) return;

	for (ADEAgent* Agent : GetTeamAgents(NewTeam))
	{
		if (Agent && Agent->IsAlive())
			RewardCalculator->CalculateCaptureReward(Agent->RewardState, Agent->GetCommandedClass(), Agent->AgentID);
	}

	if (PreviousTeam != -1)
	{
		for (ADEAgent* Agent : GetTeamAgents(PreviousTeam))
		{
			if (Agent && Agent->IsAlive())
				RewardCalculator->CalculateLosePointPenalty(Agent->RewardState, Agent->GetCommandedClass(), Agent->AgentID);
		}
	}

	// Re-assign bases on capture flip (both teams may need to adjust)
	AssignBasesToAgents(NewTeam);
	if (PreviousTeam != -1 && PreviousTeam != NewTeam)
	{
		AssignBasesToAgents(PreviousTeam);
	}
}

void ADEMatchManager::OnAgentDied(ADEAgent* DeadAgent, ADEAgent* Killer)
{
	if (!RewardCalculator || !RewardCalculator->RewardData) return;
	UDERewardSubsystem* RS = RewardCalculator;

	if (DeadAgent)
		RS->CalculateDeathPenalty(DeadAgent->RewardState, DeadAgent->GetCommandedClass(), DeadAgent->AgentID);

	if (Killer)
		RS->CalculateKillReward(Killer->RewardState, Killer->GetCommandedClass(), Killer->AgentID);

	// Assist rewards for non-killer contributors
	if (DeadAgent)
	{
		for (const auto& Pair : DeadAgent->GetDamageContributors())
		{
			ADEAgent* Contributor = Cast<ADEAgent>(Pair.Key);
			if (!Contributor || Contributor == Killer) continue;
			if (!Contributor->IsAlive()) continue;
			RS->CalculateAssistReward(Contributor->RewardState, Contributor->GetCommandedClass(), Pair.Value, Contributor->AgentID);
		}
	}
}




// ─────────────────────────────────────────────────────────────────────────────
// Squad Commander
// ─────────────────────────────────────────────────────────────────────────────

UDESquadManager* ADEMatchManager::GetSquadCommander(int32 TeamID) const
{
	if (const UDESquadManager* const* Found = SquadCommanders.Find(TeamID))
	{
		return const_cast<UDESquadManager*>(*Found);
	}
	return nullptr;
}


void ADEMatchManager::AssignBasesToAgents(int32 TeamID)
{
	if (EnvCapturePoints.Num() == 0) return;

	TArray<ADEAgent*> Agents = GetTeamAgents(TeamID);
	if (Agents.Num() == 0) return;

	// Track which base indices are already claimed by a Vanguard agent (uniqueness constraint)
	TSet<int32> VanguardClaimed;

	// Helper: find nearest uncontested (by this team) or un-owned base
	auto FindBestBase = [&](ADEAgent* Agent, bool bVanguardUniqueOnly) -> int32
	{
		float BestDist = FLT_MAX;
		int32 BestIdx  = -1;
		const FVector AgentPos = Agent->GetActorLocation();

		for (int32 i = 0; i < EnvCapturePoints.Num(); ++i)
		{
			ADECapturePoint* CP = EnvCapturePoints[i];
			if (!CP) continue;

			// Vanguard agents must claim unique bases
			if (bVanguardUniqueOnly && VanguardClaimed.Contains(i)) continue;

			const float Dist = FVector::Dist(AgentPos, CP->GetActorLocation());
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestIdx  = i;
			}
		}
		return BestIdx;
	};

	// Pass 1 — assign Vanguard agents first (uniqueness enforced, nearest base — no ownership bias)
	for (ADEAgent* Agent : Agents)
	{
		if (!Agent || Agent->GetCommandedClass() != EDEClassType::Vanguard) continue;

		const int32 BestIdx = FindBestBase(Agent, /*bVanguardUniqueOnly=*/true);
		if (BestIdx >= 0)
		{
			VanguardClaimed.Add(BestIdx);
			Agent->AssignedBaseIndex = BestIdx;
		}
	}

	// Pass 2 — assign remaining roles (Strike → nearest non-friendly; Support → nearest injured ally's base)
	for (ADEAgent* Agent : Agents)
	{
		if (!Agent) continue;
		const EDEClassType InRole = Agent->GetCommandedClass();
		if (InRole == EDEClassType::Vanguard) continue; // already done

		int32 BestIdx = -1;
		const FVector AgentPos = Agent->GetActorLocation();
		float BestDist = FLT_MAX;

		for (int32 i = 0; i < EnvCapturePoints.Num(); ++i)
		{
			ADECapturePoint* CP = EnvCapturePoints[i];
			if (!CP) continue;

			const int32 InOwner = CP->GetTeamID_Implementation();
			const float Dist  = FVector::Dist(AgentPos, CP->GetActorLocation());

			if (InRole == EDEClassType::Strike)
			{
				// Prefer neutral/enemy bases
				const float Bias = (InOwner != TeamID) ? 0.0f : 3000.0f;
				if ((Dist + Bias) < BestDist) { BestDist = Dist + Bias; BestIdx = i; }
			}
			else // Support
			{
				// Prefer nearest base overall (follow the fight)
				if (Dist < BestDist) { BestDist = Dist; BestIdx = i; }
			}
		}

		Agent->AssignedBaseIndex = BestIdx >= 0 ? BestIdx : 0;
	}

	// Write to Blackboard for all agents (EQS context reads BB key "AssignedBaseIndex")
	for (ADEAgent* Agent : Agents)
	{
		if (!Agent) continue;

		AAIController* AIC = Cast<AAIController>(Agent->GetController());
		if (AIC)
		{
			if (UBlackboardComponent* BB = AIC->GetBlackboardComponent())
			{
				BB->SetValueAsInt(TEXT("AssignedBaseIndex"), Agent->AssignedBaseIndex);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] AssignBasesToAgents team=%d: %d agents assigned across %d bases"),
		TeamID, Agents.Num(), EnvCapturePoints.Num());
}


// ─────────────────────────────────────────────────────────────────────────────
// FDETeamConfiguration helpers
// ─────────────────────────────────────────────────────────────────────────────

FLinearColor FDETeamConfiguration::GetTeamColor() const
{
	return TeamData ? TeamData->TeamColor : FLinearColor::White;
}

void FDETeamConfiguration::SetTeamColor(const FLinearColor& NewColor)
{
	if (TeamData) { TeamData->TeamColor = NewColor; }
}
