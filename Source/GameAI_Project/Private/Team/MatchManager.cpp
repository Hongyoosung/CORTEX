// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/MatchManager.h"
#include "Team/SquadManager.h"
#include "Core/Subsystems/MocRewardSubsystem.h"
#include "Characters/MocCharacter.h"
#include "Combat/Components/HealthComponent.h"
#include "Schola/Components/ScholaMocAgent.h"
#include "Actors/SpawnArea.h"
#include "Actors/CapturePoint.h"
#include "Data/TeamData.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"



// ─────────────────────────────────────────────────────────────────────────────
// Constructor & UE lifecycle
// ─────────────────────────────────────────────────────────────────────────────

AMatchManager::AMatchManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AMatchManager::BeginPlay()
{
	Super::BeginPlay();

	// Initialise runtime state and create one USquadManager per configured team.
	for (const FTeamConfiguration& Config : TeamConfigs)
	{
		const int32 ID = Config.TeamID;

		FTeamState NewState;
		NewState.TeamID = ID;
		TeamStates.Add(ID, NewState);

		USquadManager* Commander = NewObject<USquadManager>(this);
		Commander->Initialize(ID);
		Commander->BindCapturePoints(EnvCapturePoints);
		Commander->Configure(MakeSquadConfig());
		// Configuration is pushed later by AScholaEnvironment via MakeSquadConfig().
		SquadCommanders.Add(ID, Commander);
	}
}

void AMatchManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ProcessRespawnQueue(DeltaTime);

	// Tick each squad planner, supplying up-to-date agent lists.
	for (auto& Pair : SquadCommanders)
	{
		if (USquadManager* Commander = Pair.Value)
		{
			const int32 EnemyTeamID = (Pair.Key == 0) ? 1 : 0;
			Commander->TickPlanner(DeltaTime,
			                       GetTeamAgents(Pair.Key),
			                       GetTeamAgents(EnemyTeamID));
		}
	}
}


// ─────────────────────────────────────────────────────────────────────────────
// Squad Commander configuration snapshot
// ─────────────────────────────────────────────────────────────────────────────

FSquadConfig AMatchManager::MakeSquadConfig() const
{
	FSquadConfig Out;
	Out.MCTSTimeBudget    = MCTSTimeBudget;
	Out.PlanningInterval  = PlanningInterval;
	Out.bDataCollectionMode = bDataCollectionMode;
	Out.ExplorationRate   = ExplorationRate;
	Out.bRLTrainingMode   = bRLTrainingMode;
	Out.bShowDebugInfo    = bShowDebugInfo;
	Out.RandomStream      = EnvRandomStream;
	return Out;
}


// ─────────────────────────────────────────────────────────────────────────────
// Team Spawning
// ─────────────────────────────────────────────────────────────────────────────

void AMatchManager::SpawnTeams()
{
	UE_LOG(LogTemp, Log, TEXT("[MatchManager] Spawning teams..."));

	SpawnTeam(0, AgentsPerTeam);
	SpawnTeam(1, AgentsPerTeam);

	UE_LOG(LogTemp, Log, TEXT("[MatchManager] Spawned %d total agents (%d per team)"),
		AllAgents.Num(), AgentsPerTeam);
}

void AMatchManager::SpawnTeam(int32 TeamID, int32 AgentCount)
{
	for (int32 i = 0; i < AgentCount; ++i)
	{
		AMocCharacter* Agent = SpawnAgent(TeamID, i);
		if (Agent)
		{
			TeamStates[TeamID].ActiveAgents.Add(Agent);
			AllAgents.Add(Agent);
			OnAgentSpawned.Broadcast(TeamID, Agent);
		}
	}
}

AMocCharacter* AMatchManager::SpawnAgent(int32 TeamID, int32 AgentIndex)
{
	const FTeamConfiguration TeamConfig = GetTeamConfiguration(TeamID);
	UTeamData* TeamData = TeamConfig.TeamData;

	if (!TeamData)
	{
		UE_LOG(LogTemp, Error, TEXT("[MatchManager] TeamData is NULL for Team %d — cannot spawn agent."), TeamID);
		return nullptr;
	}

	if (!TeamData->CharacterClass || !TeamData->CharacterClass->IsChildOf(AMocCharacter::StaticClass()))
	{
		UE_LOG(LogTemp, Error, TEXT("[MatchManager] Invalid CharacterClass in TeamData for Team %d — must inherit from AMocCharacter."), TeamID);
		return nullptr;
	}

	// Resolve spawn transform via SpawnArea
	FVector   SpawnLocation = FVector::ZeroVector;
	FRotator  SpawnRotation = FRotator::ZeroRotator;

	if (TeamConfig.SpawnArea)
	{
		SpawnLocation = TeamConfig.SpawnArea->GetRandomSpawnPoint(EnvRandomStream);
		SpawnRotation = TeamConfig.SpawnArea->GetSpawnRotation();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MatchManager] No SpawnArea assigned for Team %d"), TeamID);
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AMocCharacter* Agent = GetWorld()->SpawnActor<AMocCharacter>(
		TeamData->CharacterClass, SpawnLocation, SpawnRotation, Params);

	if (!Agent)
	{
		return nullptr;
	}

	// Identity
	Agent->SetTeamID_Implementation(TeamID);
	Agent->SetEnvID_Implementation(EnvID);
	Agent->AssignedCapturePoints = EnvCapturePoints;

	// Appearance
	if (USkeletalMeshComponent* Mesh = Agent->GetMesh())
	{
		if (TeamData->SkeletalMesh)
			Mesh->SetSkeletalMesh(TeamData->SkeletalMesh);
		if (TeamData->AnimationBlueprint)
			Mesh->SetAnimInstanceClass(TeamData->AnimationBlueprint);
	}
	Agent->TeamColor = TeamData->TeamColor;

	// Death → respawn pipeline
	Agent->OnAgentDeathEvent_Delegate.AddDynamic(this, &AMatchManager::RegisterKill);

	const FString TeamName  = TeamData->TeamName.IsEmpty()
		? FString::Printf(TEXT("Team_%d"), TeamID)
		: TeamData->TeamName;
	const FString AgentName = FString::Printf(TEXT("%s_Agent_%d"), *TeamName, AgentIndex);

#if WITH_EDITOR
	Agent->SetActorLabel(AgentName);
#endif

	UE_LOG(LogTemp, Log, TEXT("[MatchManager] Spawned %s (Class: %s) at %s"),
		*AgentName, *TeamData->CharacterClass->GetName(), *SpawnLocation.ToString());

	return Agent;
}


// ─────────────────────────────────────────────────────────────────────────────
// Episode Reset
// ─────────────────────────────────────────────────────────────────────────────

void AMatchManager::ResetTeams()
{
	UE_LOG(LogTemp, Log, TEXT("[MatchManager] Resetting teams for new episode"));

	// 1. Clear runtime state and respawn timers
	for (const FTeamConfiguration& Conf : TeamConfigs)
	{
		FTeamState& State = TeamStates[Conf.TeamID];
		State.RespawnQueue.Empty();
		State.ActiveAgents.Empty();
		TeamRespawnTimers[Conf.TeamID] = -1.0f;
	}

	// 2. Reactivate all agents and redistribute them into ActiveAgents
	for (AMocCharacter* Agent : AllAgents)
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
	for (const FTeamConfiguration& Conf : TeamConfigs)
	{
		if (USquadManager* Commander = GetSquadCommander(Conf.TeamID))
		{
			Commander->Reset(GetTeamAgents(Conf.TeamID));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MatchManager] Reset complete — %d active agents"), AllAgents.Num());
}

void AMatchManager::DestroyAllAgents()
{
	for (AMocCharacter* Agent : AllAgents)
	{
		if (Agent) { Agent->Destroy(); }
	}
	AllAgents.Empty();

	for (const FTeamConfiguration& Conf : TeamConfigs)
	{
		TeamStates[Conf.TeamID].ActiveAgents.Empty();
		TeamStates[Conf.TeamID].RespawnQueue.Empty();
	}

	UE_LOG(LogTemp, Log, TEXT("[MatchManager] All agents destroyed"));
}


// ─────────────────────────────────────────────────────────────────────────────
// Respawn
// ─────────────────────────────────────────────────────────────────────────────

void AMatchManager::QueueRespawn(AMocCharacter* Agent, int32 TeamID)
{
	if (!Agent) { return; }

	if (!TeamStates.Contains(TeamID))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MatchManager] QueueRespawn: invalid TeamID %d for agent %s"),
			TeamID, *Agent->GetName());
		return;
	}

	FTeamState& State = TeamStates[TeamID];
	State.ActiveAgents.Remove(Agent);
	State.RespawnQueue.Add(Agent);
	Agent->Deactivate();

	UE_LOG(LogTemp, Log, TEXT("[MatchManager] Agent %s deactivated (Team %d, Active: %d, Queued: %d)"),
		*Agent->GetName(), TeamID, State.ActiveAgents.Num(), State.RespawnQueue.Num());

	// Start group respawn timer when the entire team is eliminated
	if (State.ActiveAgents.Num() == 0)
	{
		TeamRespawnTimers[TeamID] = RespawnDelay;
		UE_LOG(LogTemp, Log, TEXT("[MatchManager] Team %d fully eliminated — group respawn in %.1fs (%d agents)"),
			TeamID, RespawnDelay, State.RespawnQueue.Num());
	}
}

void AMatchManager::ProcessRespawnQueue(float DeltaTime)
{
	for (const FTeamConfiguration& Conf : TeamConfigs)
	{
		const int32 TeamID = Conf.TeamID;
		float& Timer = TeamRespawnTimers[TeamID];

		if (Timer < 0.0f) { continue; }

		if (!TeamStates.Contains(TeamID) || TeamStates[TeamID].RespawnQueue.Num() == 0)
		{
			Timer = -1.0f;
			continue;
		}

		Timer -= DeltaTime;
		if (Timer > 0.0f) { continue; }

		// Timer expired — respawn the whole group
		TArray<AMocCharacter*> ToRespawn = TeamStates[TeamID].RespawnQueue;
		TeamStates[TeamID].RespawnQueue.Empty();
		Timer = -1.0f;

		UE_LOG(LogTemp, Log, TEXT("[MatchManager] Group respawning %d agents for Team %d"),
			ToRespawn.Num(), TeamID);

		for (AMocCharacter* Agent : ToRespawn)
		{
			if (!Agent) { continue; }

			Agent->Activate();
			TeamStates[TeamID].ActiveAgents.Add(Agent);

			const FVector SpawnLoc = GetRandomSpawnPoint(GetTeamSpawnLocation(TeamID), SpawnRadius);
			Agent->SetActorLocation(SpawnLoc);

			if (RespawnInvincibilityDuration > 0.0f && Agent->GetHealthComponent())
			{
				Agent->GetHealthComponent()->SetInvulnerable(true);
				FTimerHandle InvincibilityTimer;
				GetWorldTimerManager().SetTimer(InvincibilityTimer,
					[Agent]()
					{
						if (IsValid(Agent) && Agent->GetHealthComponent())
						{
							Agent->GetHealthComponent()->SetInvulnerable(false);
						}
					},
					RespawnInvincibilityDuration, false);
			}

			OnAgentSpawned.Broadcast(TeamID, Agent);
		}

		// Trigger immediate replanning so newly respawned agents get strategy commands
		if (USquadManager* Commander = GetSquadCommander(TeamID))
		{
			const int32 EnemyTeamID = (TeamID == 0) ? 1 : 0;
			Commander->PerformTacticalPlanning(GetTeamAgents(TeamID), GetTeamAgents(EnemyTeamID));
		}
	}
}


// ─────────────────────────────────────────────────────────────────────────────
// Team State Queries
// ─────────────────────────────────────────────────────────────────────────────

FTeamState AMatchManager::GetTeamState(int32 TeamID) const
{
	return TeamStates.Contains(TeamID) ? TeamStates[TeamID] : FTeamState();
}

FTeamConfiguration AMatchManager::GetTeamConfiguration(int32 TeamID) const
{
	for (const FTeamConfiguration& Config : TeamConfigs)
	{
		if (Config.TeamID == TeamID) { return Config; }
	}
	UE_LOG(LogTemp, Warning, TEXT("[MatchManager] Unknown TeamID %d"), TeamID);
	return FTeamConfiguration();
}

TArray<AMocCharacter*> AMatchManager::GetTeamAgents(int32 TeamID) const
{
	return TeamStates.Contains(TeamID)
		? TeamStates[TeamID].ActiveAgents
		: TArray<AMocCharacter*>();
}

TArray<AMocCharacter*> AMatchManager::GetEnemyAgents(int32 TeamID) const
{
	// For a 2-team game the enemy is whichever team ≠ TeamID.
	const int32 EnemyID = (TeamID == 0) ? 1 : 0;
	return GetTeamAgents(EnemyID);
}

int32 AMatchManager::GetTotalAgentCount() const
{
	return AllAgents.Num();
}

FVector AMatchManager::GetTeamSpawnLocation(int32 TeamID) const
{
	const FTeamConfiguration Config = GetTeamConfiguration(TeamID);
	return Config.SpawnArea
		? Config.SpawnArea->GetRandomSpawnPoint(EnvRandomStream)
		: FVector::ZeroVector;
}

FVector AMatchManager::GetRandomSpawnPoint(FVector BaseLocation, float Radius) const
{
	const float RandX = EnvRandomStream.FRandRange(-Radius, Radius);
	const float RandY = EnvRandomStream.FRandRange(-Radius, Radius);
	return BaseLocation + FVector(RandX, RandY, 0.0f);
}


// ─────────────────────────────────────────────────────────────────────────────
// Kill / Score
// ─────────────────────────────────────────────────────────────────────────────

void AMatchManager::RegisterKill(const FDeathEventData& DeathEvent)
{
	AMocCharacter* Victim = Cast<AMocCharacter>(DeathEvent.DeadActor);
	AMocCharacter* Killer = Cast<AMocCharacter>(DeathEvent.Killer);

	const int32 VictimTeamID = Victim ? Victim->GetTeamID_Implementation() : -1;
	const int32 KillerTeamID = Killer ? Killer->GetTeamID_Implementation() : -1;

	QueueRespawn(Victim, VictimTeamID);

	UE_LOG(LogTemp, Log, TEXT("[MatchManager] Env %d: Kill — Killer Team %d → Victim Team %d"),
		EnvID, KillerTeamID, VictimTeamID);
}


// ─────────────────────────────────────────────────────────────────────────────
// CapturePoint Integration
// ─────────────────────────────────────────────────────────────────────────────

void AMatchManager::CapturePointInitialize()
{
	for (ACapturePoint* CP : EnvCapturePoints)
	{
		if (!CP) { continue; }
		CP->SetEnvID_Implementation(EnvID);
		CP->OnPointCaptured_Delegate.AddDynamic(this, &AMatchManager::OnPointCaptured);
		UE_LOG(LogTemp, Log, TEXT("[MatchManager] CP %s bound — EnvID=%d, PointID=%d"),
			*CP->GetName(), CP->GetEnvID_Implementation(), static_cast<int32>(CP->PointID));
	}
}

void AMatchManager::ResetCapturePoint()
{
	for (ACapturePoint* CP : EnvCapturePoints)
	{
		if (CP) { CP->ResetPoint(); }
	}
}

void AMatchManager::ResetEnvironment()
{
	ResetTeams();
	ResetCapturePoint();
}

void AMatchManager::OnPointCaptured(int32 PreviousTeam, int32 NewTeam)
{
	UE_LOG(LogTemp, Log, TEXT("[MatchManager] Env %d: Capture point transferred Team %d → Team %d"),
		EnvID, PreviousTeam, NewTeam);

	// TODO: push territory-change reward to MocRewardSubsystem
}




// ─────────────────────────────────────────────────────────────────────────────
// Squad Commander
// ─────────────────────────────────────────────────────────────────────────────

USquadManager* AMatchManager::GetSquadCommander(int32 TeamID) const
{
	if (const USquadManager* const* Found = SquadCommanders.Find(TeamID))
	{
		return const_cast<USquadManager*>(*Found);
	}
	return nullptr;
}


// ─────────────────────────────────────────────────────────────────────────────
// FTeamConfiguration helpers
// ─────────────────────────────────────────────────────────────────────────────

FLinearColor FTeamConfiguration::GetTeamColor() const
{
	return TeamData ? TeamData->TeamColor : FLinearColor::White;
}

void FTeamConfiguration::SetTeamColor(const FLinearColor& NewColor)
{
	if (TeamData) { TeamData->TeamColor = NewColor; }
}
