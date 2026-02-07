// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/TeamManager.h"
#include "Team/FogOfWarManager.h"
#include "Team/SquadManager.h"
#include "Characters/MocCharacter.h"
#include "Schola/Components/ScholaMocAgent.h"
#include "Actors/PickupBase.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"

ATeamManager::ATeamManager()
{
	PrimaryActorTick.bCanEverTick = true;

	// Initialize team states
	RedTeamState.TeamID = 0;
	RedTeamState.TeamColor = FLinearColor::Red;
	RedTeamState.Score = 0;

	BlueTeamState.TeamID = 1;
	BlueTeamState.TeamColor = FLinearColor::Blue;
	BlueTeamState.Score = 0;
}

void ATeamManager::BeginPlay()
{
	Super::BeginPlay();

	// Find or spawn FogOfWarManager
	if (!FogOfWarManager)
	{
		// Try to find existing manager in level
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFogOfWarManager::StaticClass(), FoundActors);

		if (FoundActors.Num() > 0)
		{
			FogOfWarManager = Cast<AFogOfWarManager>(FoundActors[0]);
			UE_LOG(LogTemp, Log, TEXT("TeamManager: Found existing FogOfWarManager"));
		}
		else
		{
			// Spawn new manager
			FActorSpawnParameters SpawnParams;
			SpawnParams.Name = FName("FogOfWarManager");
			FogOfWarManager = GetWorld()->SpawnActor<AFogOfWarManager>(
				AFogOfWarManager::StaticClass(),
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				SpawnParams
			);

			UE_LOG(LogTemp, Log, TEXT("TeamManager: Spawned new FogOfWarManager"));
		}
	}

	if (!FogOfWarManager)
	{
		UE_LOG(LogTemp, Error, TEXT("TeamManager: Failed to create FogOfWarManager!"));
	}
}

void ATeamManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Process respawn queue
	ProcessRespawnQueue(DeltaTime);

	// Debug visualization
	if (bShowDebugInfo)
	{
		// Draw spawn locations
		DrawDebugSphere(GetWorld(), RedTeamSpawnLocation, SpawnRadius, 16, FColor::Red, false, -1.0f, 0, 2.0f);
		DrawDebugSphere(GetWorld(), BlueTeamSpawnLocation, SpawnRadius, 16, FColor::Blue, false, -1.0f, 0, 2.0f);

		// Draw team info
		FString RedInfo = FString::Printf(TEXT("Red Team\nScore: %d\nActive: %d\nRespawning: %d"),
			RedTeamState.Score, RedTeamState.ActiveAgents.Num(), RedTeamState.RespawnQueue.Num());
		FString BlueInfo = FString::Printf(TEXT("Blue Team\nScore: %d\nActive: %d\nRespawning: %d"),
			BlueTeamState.Score, BlueTeamState.ActiveAgents.Num(), BlueTeamState.RespawnQueue.Num());

		DrawDebugString(GetWorld(), RedTeamSpawnLocation + FVector(0, 0, 300), RedInfo, nullptr, FColor::Red, 0.0f, true);
		DrawDebugString(GetWorld(), BlueTeamSpawnLocation + FVector(0, 0, 300), BlueInfo, nullptr, FColor::Blue, 0.0f, true);
	}
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

	// Get spawn location
	FVector BaseLocation = GetTeamSpawnLocation(TeamID);
	FVector SpawnLocation = GetRandomSpawnPoint(BaseLocation, SpawnRadius);
	FRotator SpawnRotation = FRotator::ZeroRotator;

	// Face towards center of map
	if (TeamID == 0) // Red team
	{
		SpawnRotation.Yaw = 0.0f; // Face right
	}
	else // Blue team
	{
		SpawnRotation.Yaw = 180.0f; // Face left
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
		// Set team ID via actor tag
		FName TeamTag = FName(*FString::Printf(TEXT("Team_%d"), TeamID));
		Agent->Tags.Add(TeamTag);

		// Set agent name
		FString AgentName = FString::Printf(TEXT("%s_Agent_%d"),
			TeamID == 0 ? TEXT("Red") : TEXT("Blue"), AgentIndex);
		Agent->SetActorLabel(AgentName);

		UE_LOG(LogTemp, Log, TEXT("TeamManager: Spawned %s at %s"), *AgentName, *SpawnLocation.ToString());
	}

	return Agent;
}

void ATeamManager::ResetAllAgents()
{
	// Reset team scores
	RedTeamState.Score = 0;
	BlueTeamState.Score = 0;

	// Clear respawn queues
	RedTeamState.RespawnQueue.Empty();
	BlueTeamState.RespawnQueue.Empty();
	RespawnTimers.Empty();

	// Reset all agents
	for (AMocCharacter* Agent : AllAgents)
	{
		if (Agent)
		{
			// Respawn agent at team location
			int32 TeamID = Agent->Tags.Contains(FName("Team_0")) ? 0 : 1;
			FVector SpawnLocation = GetRandomSpawnPoint(GetTeamSpawnLocation(TeamID), SpawnRadius);
			Agent->SetActorLocation(SpawnLocation);

			// Reset health and state
			// TODO: Call Reset() method on character
		}
	}

	// Note: FogOfWarManager handles its own state clearing if needed

	UE_LOG(LogTemp, Log, TEXT("TeamManager: All agents reset"));
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

	// Remove from active agents
	if (TeamID == 0)
	{
		RedTeamState.ActiveAgents.Remove(Agent);
		RedTeamState.RespawnQueue.Add(Agent);
	}
	else if (TeamID == 1)
	{
		BlueTeamState.ActiveAgents.Remove(Agent);
		BlueTeamState.RespawnQueue.Add(Agent);
	}

	// Start respawn timer
	RespawnTimers.Add(Agent, RespawnDelay);

	UE_LOG(LogTemp, Log, TEXT("TeamManager: Agent %s queued for respawn (%.1fs delay)"),
		*Agent->GetName(), RespawnDelay);
}

void ATeamManager::ProcessRespawnQueue(float DeltaTime)
{
	TArray<AMocCharacter*> ToRespawn;

	// Update timers
	for (auto& Pair : RespawnTimers)
	{
		Pair.Value -= DeltaTime;
		if (Pair.Value <= 0.0f)
		{
			ToRespawn.Add(Pair.Key);
		}
	}

	// Respawn agents
	for (AMocCharacter* Agent : ToRespawn)
	{
		if (!Agent)
		{
			continue;
		}

		// Get team ID
		int32 TeamID = Agent->Tags.Contains(FName("Team_0")) ? 0 : 1;

		// Remove from respawn queue
		if (TeamID == 0)
		{
			RedTeamState.RespawnQueue.Remove(Agent);
			RedTeamState.ActiveAgents.Add(Agent);
		}
		else if (TeamID == 1)
		{
			BlueTeamState.RespawnQueue.Remove(Agent);
			BlueTeamState.ActiveAgents.Add(Agent);
		}

		// Respawn at team location
		FVector SpawnLocation = GetRandomSpawnPoint(GetTeamSpawnLocation(TeamID), SpawnRadius);
		Agent->SetActorLocation(SpawnLocation);

		// TODO: Reset agent health and state

		// Remove timer
		RespawnTimers.Remove(Agent);

		// Broadcast event
		OnAgentSpawned.Broadcast(TeamID, Agent);

		UE_LOG(LogTemp, Log, TEXT("TeamManager: Agent %s respawned"), *Agent->GetName());
	}
}

FVector ATeamManager::GetTeamSpawnLocation(int32 TeamID) const
{
	return TeamID == 0 ? RedTeamSpawnLocation : BlueTeamSpawnLocation;
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

FMocTeamState ATeamManager::GetTeamState(int32 TeamID) const
{
	return TeamID == 0 ? RedTeamState : BlueTeamState;
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

void ATeamManager::ReportEnemySighting(int32 ReportingTeamID, AActor* Enemy, FVector Location)
{
	if (!FogOfWarManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamManager: FogOfWarManager not available for enemy sighting report"));
		return;
	}

	FogOfWarManager->ReportEnemy(ReportingTeamID, Enemy, Location);
}

void ATeamManager::ReportResourceDiscovery(int32 TeamID, APickupBase* Resource)
{
	if (!FogOfWarManager || !Resource)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamManager: FogOfWarManager not available for resource discovery report"));
		return;
	}

	bool bAvailable = Resource->IsAvailable();
	FogOfWarManager->ReportResource(TeamID, Resource, bAvailable);
}

FVector ATeamManager::GetLastKnownEnemyPosition(int32 TeamID, AActor* Enemy) const
{
	if (!FogOfWarManager)
	{
		return FVector::ZeroVector;
	}

	return FogOfWarManager->GetLastKnownEnemyPosition(TeamID, Enemy);
}

bool ATeamManager::IsEnemyPositionValid(int32 TeamID, AActor* Enemy) const
{
	if (!FogOfWarManager)
	{
		return false;
	}

	return FogOfWarManager->IsEnemyPositionValid(TeamID, Enemy);
}

//========================================
// v10.2 Squad Commander Access
//========================================

ASquadManager* ATeamManager::GetSquadCommander(int32 TeamID) const
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

void ATeamManager::SetSquadCommander(int32 TeamID, ASquadManager* Commander)
{
	if (TeamID == 0)
	{
		RedTeamCommander = Commander;
		UE_LOG(LogTemp, Log, TEXT("TeamManager: Red Team Commander set"));
	}
	else if (TeamID == 1)
	{
		BlueTeamCommander = Commander;
		UE_LOG(LogTemp, Log, TEXT("TeamManager: Blue Team Commander set"));
	}
}
