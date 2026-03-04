// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/TeamManager.h"
#include "RL/Rewards/MocRewardCalculator.h"
#include "Team/FogOfWarManager.h"
#include "Team/SquadManager.h"  
#include "Characters/MocCharacter.h"
#include "Combat/Components/HealthComponent.h"
#include "Schola/Components/ScholaMocAgent.h"
#include "Actors/SpawnArea.h"
#include "Data/TeamData.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"


ATeamManager::ATeamManager()
{
	PrimaryActorTick.bCanEverTick = true;

	// Initialize team states (colors will be synced in BeginPlay)
	RedTeamState.TeamID = 0;
	RedTeamState.TeamColor = FLinearColor::Red; // Updated in BeginPlay from config
	RedTeamState.Score = 0;

	BlueTeamState.TeamID = 1;
	BlueTeamState.TeamColor = FLinearColor::Blue; // Updated in BeginPlay from config
	BlueTeamState.Score = 0;
}

void ATeamManager::BeginPlay()
{
	Super::BeginPlay();

	// Create Squad Planners programmatically (one per team)
	RedTeamCommander = NewObject<USquadManager>(this);
	RedTeamCommander->Initialize(0, this);

	BlueTeamCommander = NewObject<USquadManager>(this);
	BlueTeamCommander->Initialize(1, this);
}

void ATeamManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Process respawn queue
	ProcessRespawnQueue(DeltaTime);

	// Tick Squad Planners
	if (RedTeamCommander)  RedTeamCommander->TickPlanner(DeltaTime);
	if (BlueTeamCommander) BlueTeamCommander->TickPlanner(DeltaTime);

}

void ATeamManager::SpawnTeams()
{
	UE_LOG(LogTemp, Log, TEXT("TeamManager: Spawning teams..."));

	// Spawn Red team
	SpawnTeam(0, AgentsPerTeam);

	// Spawn Blue team
	SpawnTeam(1, AgentsPerTeam);

	UE_LOG(LogTemp, Log, TEXT("TeamManager: Spawned %d total agents (%d per team)"),
		AllAgents.Num(), AgentsPerTeam);
}

void ATeamManager::SpawnTeam(int32 TeamID, int32 AgentCount)
{
	for (int32 i = 0; i < AgentCount; ++i)
	{
		AMocCharacter* Agent = SpawnAgent(TeamID, i);
		if (Agent)
		{
			// Add to appropriate team
			if (TeamID == 0)
			{
				RedTeamState.ActiveAgents.Add(Agent);
			}
			else if (TeamID == 1)
			{
				BlueTeamState.ActiveAgents.Add(Agent);
			}

			// Track globally
			AllAgents.Add(Agent);

			// Broadcast event
			OnAgentSpawned.Broadcast(TeamID, Agent);
		}
	}
}

AMocCharacter* ATeamManager::SpawnAgent(int32 TeamID, int32 AgentIndex)
{
	if (!CharacterClass)
	{
		UE_LOG(LogTemp, Error, TEXT("TeamManager: CharacterClass not set!"));
		return nullptr;
	}

	// Get spawn location and rotation
	FVector SpawnLocation;
	FRotator SpawnRotation;

	ASpawnArea* SpawnArea = (TeamID == 0) ? RedSpawnArea : BlueSpawnArea;
	if (SpawnArea)
	{
		SpawnLocation = SpawnArea->GetRandomSpawnPoint();
		SpawnRotation = SpawnArea->GetSpawnRotation();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamManager: No SpawnArea assigned for Team %d"), TeamID);
	}

	// Spawn parameters
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// Spawn character
	AMocCharacter* Agent = GetWorld()->SpawnActor<AMocCharacter>(
		CharacterClass,
		SpawnLocation,
		SpawnRotation,
		SpawnParams
	);

	if (Agent)
	{
		// Set team and environment IDs
		Agent->TeamID = TeamID;
		Agent->EnvID = this->EnvID;

		// Set TeamManager reference directly (multi-env support)
		Agent->SetTeamManager(this);

		// Get team configuration
		FTeamConfiguration TeamConfig = GetTeamConfiguration(TeamID);

		// v10.2: Apply Team from TeamData (preferred) or legacy config (fallback)
		UTeamData* TeamData = TeamConfig.TeamData;

		if (TeamData && Agent->GetMesh())
		{
			// Apply skeletal mesh
			if (TeamData->SkeletalMesh)
			{
				Agent->GetMesh()->SetSkeletalMesh(TeamData->SkeletalMesh);
				UE_LOG(LogTemp, Log, TEXT("TeamManager: Applied skeletal mesh '%s' to Team %d"),
					*TeamData->SkeletalMesh->GetName(), TeamID);
			}

			// Apply animation blueprint
			if (TeamData->AnimationBlueprint)
			{
				Agent->GetMesh()->SetAnimInstanceClass(TeamData->AnimationBlueprint);
				UE_LOG(LogTemp, Log, TEXT("TeamManager: Applied animation blueprint '%s' to Team %d"),
					*TeamData->AnimationBlueprint->GetName(), TeamID);
			}
		}

		// Update team color VFX (uses TeamState.TeamColor)
		Agent->UpdateTeamColorVFX();

		// Set agent name
		FString TeamName = TeamData ? TeamData->TeamName : (TeamID == 0 ? TEXT("Red") : TEXT("Blue"));
		FString AgentName = FString::Printf(TEXT("%s_Agent_%d"), *TeamName, AgentIndex);
#if WITH_EDITOR
		Agent->SetActorLabel(AgentName);
#endif

		UE_LOG(LogTemp, Log, TEXT("TeamManager: Spawned %s at %s"), *AgentName, *SpawnLocation.ToString());
	}

	return Agent;
}

void ATeamManager::ResetTeams()
{
	UE_LOG(LogTemp, Log, TEXT("[TeamManager] Resetting teams for new episode"));

	// 1. Reset team-level state (TeamManager's responsibility)
	RedTeamState.Score = 0;
	BlueTeamState.Score = 0;
	RedTeamState.RespawnQueue.Empty();
	BlueTeamState.RespawnQueue.Empty();
	TeamRespawnTimers[0] = -1.0f;
	TeamRespawnTimers[1] = -1.0f;

	// Ensure all agents are in ActiveAgents (not in respawn queue)
	RedTeamState.ActiveAgents.Empty();
	BlueTeamState.ActiveAgents.Empty();

	for (AMocCharacter* Agent : AllAgents)
	{
		if (Agent)
		{
			int32 TeamID = Agent->TeamID;
			if (TeamID == 0)
			{
				RedTeamState.ActiveAgents.Add(Agent);
			}
			else if (TeamID == 1)
			{
				BlueTeamState.ActiveAgents.Add(Agent);
			}
		}
	}

	// 2. Delegate agent resets (don't manipulate agent internals)
	for (AMocCharacter* Agent : AllAgents)
	{
		if (Agent)
		{
			// Reposition agent at team spawn location BEFORE reset
			// (so EQS queries during reset use the correct location)
			int32 TeamID = Agent->TeamID;
			FVector SpawnLocation = GetRandomSpawnPoint(GetTeamSpawnLocation(TeamID), SpawnRadius);
			Agent->SetActorLocation(SpawnLocation);

			// Each agent resets its own state
			// ResetCharacter() handles full reactivation (unhide, re-enable collision/tick, etc.)
			Agent->ResetCharacter();
		}
	}


	// 3. Reset Squad Commanders
	if (RedTeamCommander)
	{
		RedTeamCommander->Reset();
	}
	if (BlueTeamCommander)
	{
		BlueTeamCommander->Reset();
	}

	UE_LOG(LogTemp, Log, TEXT("[TeamManager] Team reset complete - %d active agents"), AllAgents.Num());
}

void ATeamManager::DestroyAllAgents()
{
	for (AMocCharacter* Agent : AllAgents)
	{
		if (Agent)
		{
			Agent->Destroy();
		}
	}

	AllAgents.Empty();
	RedTeamState.ActiveAgents.Empty();
	BlueTeamState.ActiveAgents.Empty();
	RedTeamState.RespawnQueue.Empty();
	BlueTeamState.RespawnQueue.Empty();

	UE_LOG(LogTemp, Log, TEXT("TeamManager: All agents destroyed"));
}

void ATeamManager::QueueRespawn(AMocCharacter* Agent, int32 TeamID)
{
	if (!Agent)
	{
		return;
	}

	// Remove from active agents and add to respawn queue
	FTeamState& TeamState = (TeamID == 0) ? RedTeamState : BlueTeamState;
	TeamState.ActiveAgents.Remove(Agent);
	TeamState.RespawnQueue.Add(Agent);

	// Deactivate the dead agent (hide and stop ticking)
	Agent->SetActorHiddenInGame(true);
	Agent->SetActorEnableCollision(false);
	Agent->SetActorTickEnabled(false);

	UE_LOG(LogTemp, Log, TEXT("TeamManager: Agent %s deactivated (Team %d, Active: %d, Queued: %d)"),
		*Agent->GetName(), TeamID, TeamState.ActiveAgents.Num(), TeamState.RespawnQueue.Num());

	// Only start respawn timer when ALL agents on the team are dead
	if (TeamState.ActiveAgents.Num() == 0)
	{
		TeamRespawnTimers[TeamID] = RespawnDelay;
		UE_LOG(LogTemp, Log, TEXT("TeamManager: Team %d fully eliminated! Group respawn in %.1fs (%d agents)"),
			TeamID, RespawnDelay, TeamState.RespawnQueue.Num());

		// Fire team-wipe penalty on every agent in the queue (the whole team is now dead).
		// Stacks with each agent's individual death penalty already queued this frame.
		for (AMocCharacter* DeadAgent : TeamState.RespawnQueue)
		{
			if (DeadAgent)
			{
				if (UMocRewardCalculator* RC = DeadAgent->FindComponentByClass<UMocRewardCalculator>())
				{
					RC->CalculateTeamWipePenalty(DeadAgent->GetCommandedStrategy());
				}
			}
		}
	}
}

void ATeamManager::ProcessRespawnQueue(float DeltaTime)
{
	// Process each team's group respawn timer
	for (int32 TeamID = 0; TeamID < 2; ++TeamID)
	{
		if (TeamRespawnTimers[TeamID] < 0.0f)
		{
			continue; // No pending respawn for this team
		}

		FTeamState& TeamState = (TeamID == 0) ? RedTeamState : BlueTeamState;
		if (TeamState.RespawnQueue.Num() == 0)
		{
			TeamRespawnTimers[TeamID] = -1.0f;
			continue;
		}

		TeamRespawnTimers[TeamID] -= DeltaTime;
		if (TeamRespawnTimers[TeamID] > 0.0f)
		{
			continue; // Still waiting
		}

		// Timer expired — respawn ALL queued agents for this team as a group
		TArray<AMocCharacter*> ToRespawn = TeamState.RespawnQueue;
		TeamState.RespawnQueue.Empty();
		TeamRespawnTimers[TeamID] = -1.0f;

		UE_LOG(LogTemp, Log, TEXT("TeamManager: Group respawning %d agents for Team %d"), ToRespawn.Num(), TeamID);

		for (AMocCharacter* Agent : ToRespawn)
		{
			if (!Agent)
			{
				continue;
			}

			TeamState.ActiveAgents.Add(Agent);

			// Reactivate the agent (undo deactivation from QueueRespawn)
			Agent->SetActorHiddenInGame(false);
			Agent->SetActorTickEnabled(true);

			// Reset agent state (health, alive flag, collision, movement, AI)
			Agent->ResetCharacter();

			// Respawn at team location
			FVector SpawnLoc = GetRandomSpawnPoint(GetTeamSpawnLocation(TeamID), SpawnRadius);
			Agent->SetActorLocation(SpawnLoc);

			// Grant respawn invincibility
			if (RespawnInvincibilityDuration > 0.0f && Agent->GetHealthComponent())
			{
				Agent->GetHealthComponent()->SetInvulnerable(true);
				FTimerHandle InvincibilityTimer;
				GetWorldTimerManager().SetTimer(InvincibilityTimer, [Agent]()
				{
					if (IsValid(Agent) && Agent->GetHealthComponent())
					{
						Agent->GetHealthComponent()->SetInvulnerable(false);
					}
				}, RespawnInvincibilityDuration, false);
			}

			// Broadcast event
			OnAgentSpawned.Broadcast(TeamID, Agent);
		}

		// After group respawn, trigger immediate replanning so agents get strategy commands
		USquadManager* Commander = GetSquadCommander(TeamID);
		if (Commander)
		{
			Commander->PerformTacticalPlanning();
		}
	}
}

FVector ATeamManager::GetTeamSpawnLocation(int32 TeamID) const
{
	// Use SpawnArea if assigned (parallel environment setup)
	if (TeamID == 0 && RedSpawnArea)
	{
		return RedSpawnArea->GetRandomSpawnPoint();
	}
	if (TeamID == 1 && BlueSpawnArea)
	{
		return BlueSpawnArea->GetRandomSpawnPoint();
	}

	return FVector3d::ZeroVector;
}

FVector ATeamManager::GetRandomSpawnPoint(FVector BaseLocation, float Radius) const
{
	FVector RandomOffset = FVector(
		FMath::RandRange(-Radius, Radius),
		FMath::RandRange(-Radius, Radius),
		0.0f
	);
	return BaseLocation + RandomOffset;
}

FTeamState ATeamManager::GetTeamState(int32 TeamID) const
{
	return TeamID == 0 ? RedTeamState : BlueTeamState;
}

FTeamConfiguration ATeamManager::GetTeamConfiguration(int32 TeamID) const
{
	return TeamID == 0 ? RedTeamConfig : BlueTeamConfig;
}

TArray<AMocCharacter*> ATeamManager::GetTeamAgents(int32 TeamID) const
{
	return TeamID == 0 ? RedTeamState.ActiveAgents : BlueTeamState.ActiveAgents;
}

TArray<AMocCharacter*> ATeamManager::GetEnemyAgents(int32 TeamID) const
{
	// Return opposite team's agents
	return TeamID == 0 ? BlueTeamState.ActiveAgents : RedTeamState.ActiveAgents;
}

int32 ATeamManager::GetTeamScore(int32 TeamID) const
{
	return TeamID == 0 ? RedTeamState.Score : BlueTeamState.Score;
}

int32 ATeamManager::GetTotalAgentCount() const
{
	return AllAgents.Num();
}

void ATeamManager::AddTeamScore(int32 TeamID, int32 Amount)
{
	if (TeamID == 0)
	{
		RedTeamState.Score += Amount;
		OnTeamScoreChanged.Broadcast(TeamID, RedTeamState.Score);
	}
	else if (TeamID == 1)
	{
		BlueTeamState.Score += Amount;
		OnTeamScoreChanged.Broadcast(TeamID, BlueTeamState.Score);
	}

	UE_LOG(LogTemp, Log, TEXT("TeamManager: Team %d score +%d (Total: %d)"),
		TeamID, Amount, GetTeamScore(TeamID));
}

void ATeamManager::SubtractTeamScore(int32 TeamID, int32 Amount)
{
	AddTeamScore(TeamID, -Amount);
}

void ATeamManager::RegisterKill(int32 KillerTeamID, int32 VictimTeamID, AMocCharacter* Victim)
{
	// Broadcast kill event
	OnAgentKilled.Broadcast(VictimTeamID, KillerTeamID, Victim);

	// Queue victim for respawn
	QueueRespawn(Victim, VictimTeamID);

	UE_LOG(LogTemp, Log, TEXT("TeamManager: Kill registered - Killer Team: %d, Victim Team: %d"),
		KillerTeamID, VictimTeamID);
}


//========================================
// v10.2 Squad Commander Access
//========================================

USquadManager* ATeamManager::GetSquadCommander(int32 TeamID) const
{
	if (TeamID == 0)
	{
		return RedTeamCommander;
	}
	else if (TeamID == 1)
	{
		return BlueTeamCommander;
	}

	return nullptr;
}


FLinearColor FTeamConfiguration::GetTeamColor() const
{
	return TeamData.Get()->TeamColor;
}
