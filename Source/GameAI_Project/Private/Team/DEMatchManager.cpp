// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/DEMatchManager.h"
#include "Team/DESquadManager.h"
#include "Core/Subsystems/DERewardSubsystem.h"
#include "Characters/DECharacter.h"
#include "AbilitySystemComponent.h"
#include "GAS/DEGameplayTags.h"
#include "Schola/Components/DEScholaAgent.h"
#include "Actors/DESpawnArea.h"
#include "Actors/DECapturePoint.h"
#include "Data/DETeamData.h"
#include "Data/DERewardData.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"



// ─────────────────────────────────────────────────────────────────────────────
// Constructor & UE lifecycle
// ─────────────────────────────────────────────────────────────────────────────

ADEMatchManager::ADEMatchManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ADEMatchManager::BeginPlay()
{
	Super::BeginPlay();

	// Register reward data with the subsystem so all agents share one instance.
	if (UDERewardSubsystem* RS = GetWorld()->GetSubsystem<UDERewardSubsystem>())
	{
		RS->SetRewardData(RewardData);
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
}

void ADEMatchManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ProcessRespawnQueue(DeltaTime);

	// Tick each squad planner, supplying up-to-date agent lists.
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


// ─────────────────────────────────────────────────────────────────────────────
// Squad Commander configuration snapshot
// ─────────────────────────────────────────────────────────────────────────────

FDESquadConfig ADEMatchManager::MakeSquadConfig() const
{
	FDESquadConfig Out;
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

void ADEMatchManager::SpawnTeams()
{
	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Spawning teams..."));

	SpawnTeam(0, AgentsPerTeam);
	SpawnTeam(1, AgentsPerTeam);

	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Spawned %d total agents (%d per team)"),
		AllAgents.Num(), AgentsPerTeam);
}

void ADEMatchManager::SpawnTeam(int32 TeamID, int32 AgentCount)
{
	for (int32 i = 0; i < AgentCount; ++i)
	{
		ADECharacter* Agent = SpawnAgent(TeamID, i);
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

ADECharacter* ADEMatchManager::SpawnAgent(int32 TeamID, int32 AgentIndex)
{
	const FDETeamConfiguration TeamConfig = GetTeamConfiguration(TeamID);
	UDETeamData* TeamData = TeamConfig.TeamData;

	if (!TeamData)
	{
		UE_LOG(LogTemp, Error, TEXT("[DEMatchManager] TeamData is NULL for Team %d — cannot spawn agent."), TeamID);
		return nullptr;
	}

	if (!TeamData->CharacterClass || !TeamData->CharacterClass->IsChildOf(ADECharacter::StaticClass()))
	{
		UE_LOG(LogTemp, Error, TEXT("[DEMatchManager] Invalid CharacterClass in TeamData for Team %d — must inherit from ADECharacter."), TeamID);
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

	ADECharacter* Agent = GetWorld()->SpawnActor<ADECharacter>(
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

	// 1. Clear runtime state and respawn timers
	for (const FDETeamConfiguration& Conf : TeamConfigs)
	{
		FDETeamState& State = TeamStates[Conf.TeamID];
		State.RespawnQueue.Empty();
		State.ActiveAgents.Empty();
		TeamRespawnTimers[Conf.TeamID] = -1.0f;
	}

	// 2. Reactivate all agents and redistribute them into ActiveAgents
	for (ADECharacter* Agent : AllAgents)
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
	for (ADECharacter* Agent : AllAgents)
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

void ADEMatchManager::QueueRespawn(ADECharacter* Agent, int32 TeamID)
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

	UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Agent %s deactivated (Team %d, Active: %d, Queued: %d)"),
		*Agent->GetName(), TeamID, State.ActiveAgents.Num(), State.RespawnQueue.Num());

	// Start group respawn timer when the entire team is eliminated
	if (State.ActiveAgents.Num() == 0)
	{
		TeamRespawnTimers[TeamID] = RespawnDelay;
		UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Team %d fully eliminated — group respawn in %.1fs (%d agents)"),
			TeamID, RespawnDelay, State.RespawnQueue.Num());

		// Apply team wipe penalty to the last agent to die and all already-queued agents
		if (UDERewardSubsystem* RS = GetWorld()->GetSubsystem<UDERewardSubsystem>())
		{
			if (RS->GetRewardData())
			{
				RS->CalculateTeamWipePenalty(Agent->RewardState, Agent->GetCommandedStrategy(), Agent->AgentID);
				for (ADECharacter* Queued : State.RespawnQueue)
				{
					if (Queued)
						RS->CalculateTeamWipePenalty(Queued->RewardState, Queued->GetCommandedStrategy(), Queued->AgentID);
				}
			}
		}
	}
}

void ADEMatchManager::ProcessRespawnQueue(float DeltaTime)
{
	for (const FDETeamConfiguration& Conf : TeamConfigs)
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
		TArray<ADECharacter*> ToRespawn = TeamStates[TeamID].RespawnQueue;
		TeamStates[TeamID].RespawnQueue.Empty();
		Timer = -1.0f;

		UE_LOG(LogTemp, Log, TEXT("[DEMatchManager] Group respawning %d agents for Team %d"),
			ToRespawn.Num(), TeamID);

		for (ADECharacter* Agent : ToRespawn)
		{
			if (!Agent) { continue; }

			Agent->Activate();
			TeamStates[TeamID].ActiveAgents.Add(Agent);

			const FVector SpawnLoc = GetRandomSpawnPoint(GetTeamSpawnLocation(TeamID), SpawnRadius);
			Agent->SetActorLocation(SpawnLoc);

			if (RespawnInvincibilityDuration > 0.0f)
			{
				if (UAbilitySystemComponent* ASC = Agent->GetAbilitySystemComponent())
				{
					ASC->AddLooseGameplayTag(DEGameplayTags::State_Invulnerable);

					// Capture a weak ref so the timer is safe if the agent is destroyed
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
		}

		// Trigger immediate replanning so newly respawned agents get strategy commands
		if (!bRLTrainingMode)
		{
			if (UDESquadManager* Commander = GetSquadCommander(TeamID))
			{
				const int32 EnemyTeamID = (TeamID == 0) ? 1 : 0;
				Commander->PerformTacticalPlanning(GetTeamAgents(TeamID), GetTeamAgents(EnemyTeamID));
			}
		}
		
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

TArray<ADECharacter*> ADEMatchManager::GetTeamAgents(int32 TeamID) const
{
	return TeamStates.Contains(TeamID)
		? TeamStates[TeamID].ActiveAgents
		: TArray<ADECharacter*>();
}

TArray<ADECharacter*> ADEMatchManager::GetEnemyAgents(int32 TeamID) const
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
	ADECharacter* Victim = Cast<ADECharacter>(DeathEvent.DeadActor);
	ADECharacter* Killer = Cast<ADECharacter>(DeathEvent.Killer);

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

	UDERewardSubsystem* RS = GetWorld()->GetSubsystem<UDERewardSubsystem>();
	if (!RS || !RS->GetRewardData()) return;

	for (ADECharacter* Agent : GetTeamAgents(NewTeam))
	{
		if (Agent && Agent->IsAlive())
			RS->CalculateCaptureReward(Agent->RewardState, Agent->GetCommandedStrategy(), Agent->AgentID);
	}

	if (PreviousTeam != -1)
	{
		for (ADECharacter* Agent : GetTeamAgents(PreviousTeam))
		{
			if (Agent && Agent->IsAlive())
				RS->CalculateLosePointPenalty(Agent->RewardState, Agent->GetCommandedStrategy(), Agent->AgentID);
		}
	}
}

void ADEMatchManager::OnAgentDied(ADECharacter* DeadAgent, ADECharacter* Killer)
{
	UDERewardSubsystem* RS = GetWorld()->GetSubsystem<UDERewardSubsystem>();
	if (!RS || !RS->GetRewardData()) return;

	if (DeadAgent)
		RS->CalculateDeathPenalty(DeadAgent->RewardState, DeadAgent->GetCommandedStrategy(), DeadAgent->AgentID);

	if (Killer)
		RS->CalculateKillReward(Killer->RewardState, Killer->GetCommandedStrategy(), Killer->AgentID);

	// Assist rewards for non-killer contributors
	if (DeadAgent)
	{
		for (const auto& Pair : DeadAgent->GetDamageContributors())
		{
			ADECharacter* Contributor = Cast<ADECharacter>(Pair.Key);
			if (!Contributor || Contributor == Killer) continue;
			if (!Contributor->IsAlive()) continue;
			RS->CalculateAssistReward(Contributor->RewardState, Contributor->GetCommandedStrategy(), Pair.Value, Contributor->AgentID);
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
