// IntelManagerComponent.cpp
// Phase 3: Intel tracking and observation implementation

#include "Team/Components/TeamManagerComponent.h"
#include "Team/ObjectiveActor.h"
#include "Actor/FollowerCharacter.h"
#include "Core/SimulationManagerGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TeamManagerComponent.h"


UTeamManagerComponent::UTeamManagerComponent()
	: Super()
	, TeamID(0)
	, MaxFollowers(10)
	, FriendlyObjective(nullptr)
	, HostileObjective(nullptr)
{
}

void UTeamManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("[TeamManager] Initialized (TeamID: %d)"), TeamID);
}

bool UTeamManagerComponent::RegisterFollower(AActor* Follower)
{
	if (!Follower)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamManager] RegisterFollower: Null follower provided"));
		return false;
	}

	if (RegisteredFollowers.Contains(Follower))
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamManager] RegisterFollower: %s already registered"), *Follower->GetName());
		return false;
	}

	if (RegisteredFollowers.Num() >= MaxFollowers)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamManager] RegisterFollower: Squad full (%d/%d), cannot register %s"),
			RegisteredFollowers.Num(), MaxFollowers, *Follower->GetName());
		return false;
	}

	RegisteredFollowers.Add(Follower);

	UE_LOG(LogTemp, Log, TEXT("[TeamManager] Registered follower: %s (Total: %d/%d)"),
		*Follower->GetName(), RegisteredFollowers.Num(), MaxFollowers);

	return true;
}

bool UTeamManagerComponent::UnregisterFollower(AActor* Follower)
{
	if (!Follower)
	{
		return false;
	}

	int32 RemovedCount = RegisteredFollowers.Remove(Follower);

	if (RemovedCount <= 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[TeamManager] Unregistered follower failed %s (Remaining: %d)"),
			*Follower->GetName(), RegisteredFollowers.Num());

		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[TeamManager] Unregistered follower: %s (Remaining: %d)"),
		*Follower->GetName(), RegisteredFollowers.Num());

	return true;
}

TArray<AActor*> UTeamManagerComponent::GetFollowers() const
{
	return RegisteredFollowers;
}


bool UTeamManagerComponent::IsFollowerRegistered(AActor* Follower) const
{
	return RegisteredFollowers.Contains(Follower);
}

void UTeamManagerComponent::DiscoverWorldObjectives()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamManager] DiscoverWorldObjectives: No world context"));
		return;
	}

	// Get SimulationManager for enemy relationship queries
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(
		UGameplayStatics::GetGameMode(World)
	);

	if (!SimManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamManager] No SimulationManagerGameMode found - cannot discover objectives"));
		return;
	}

	// Find all ObjectiveActors in world
	TArray<AActor*> AllObjectives;
	UGameplayStatics::GetAllActorsOfClass(World, AObjectiveActor::StaticClass(), AllObjectives);

	if (AllObjectives.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamManager] No ObjectiveActors found in world"));
		return;
	}

	// Classify objectives based on TeamID and enemy relationships
	for (AActor* Actor : AllObjectives)
	{
		AObjectiveActor* Objective = Cast<AObjectiveActor>(Actor);
		if (!Objective)
		{
			continue;
		}

		// Friendly objective: OwnerTeamID matches our TeamID
		if (Objective->OwnerTeamID == this->TeamID)
		{
			FriendlyObjective = Objective;
			UE_LOG(LogTemp, Log, TEXT("[TeamManager] Discovered friendly objective: %s (TeamID: %d)"),
				*Objective->GetName(), Objective->OwnerTeamID);
		}
		// Hostile objective: SimManager confirms enemy relationship
		else if (SimManager->AreTeamsEnemies(this->TeamID, Objective->OwnerTeamID))
		{
			HostileObjective = Objective;
			UE_LOG(LogTemp, Log, TEXT("[TeamManager] Discovered hostile objective: %s (TeamID: %d)"),
				*Objective->GetName(), Objective->OwnerTeamID);
		}
	}

	// Verify discovery
	if (FriendlyObjective && HostileObjective)
	{
		UE_LOG(LogTemp, Log, TEXT("✅ [TeamManager v9.0] Successfully discovered both objectives (TeamID: %d)"), TeamID);

		// v9.0: Broadcast event to notify subscribers (e.g., FollowerAgentComponent)
		OnObjectivesDiscovered.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("📡 [TeamManager v9.0] OnObjectivesDiscovered event broadcast (TeamID: %d)"), TeamID);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ [TeamManager] Failed to discover all objectives (TeamID: %d, Friendly: %s, Hostile: %s)"),
			TeamID,
			FriendlyObjective ? TEXT("OK") : TEXT("MISSING"),
			HostileObjective ? TEXT("OK") : TEXT("MISSING"));
	}
}

void UTeamManagerComponent::PushContextToFollower(AObjectiveActor* Friendly, AObjectiveActor* Hostile)
{
	for (AActor* Follower : RegisteredFollowers)
	{
		if (!Follower)
		{
			continue;
		}

		AFollowerCharacter* FollowerChar = Cast<AFollowerCharacter>(Follower);
		if (!FollowerChar)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TeamManager] Follower %s is not a valid AFollowerCharacter"), *Follower->GetName());
			return;
		}

		FollowerChar->UpdateObjectiveContext(Friendly, Hostile);
	}
}

void UTeamManagerComponent::RegisterEnemy(AActor* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	if (!VisibleEnemies.Contains(Enemy))
	{
		VisibleEnemies.Add(Enemy);

		UE_LOG(LogTemp, Log, TEXT("[TeamManager] Registered enemy: %s (Total: %d)"),
			*Enemy->GetName(), VisibleEnemies.Num());
	}
}

void UTeamManagerComponent::UnregisterEnemy(AActor* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	int32 RemovedCount = VisibleEnemies.Remove(Enemy);

	if (RemovedCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[TeamManager] Unregistered enemy: %s (Remaining: %d)"),
			*Enemy->GetName(), VisibleEnemies.Num());
	}
}

TArray<AActor*> UTeamManagerComponent::GetVisibleEnemies() const
{
	return VisibleEnemies.Array();
}

void UTeamManagerComponent::ClearVisibleEnemies()
{
	int32 PreviousCount = VisibleEnemies.Num();
	VisibleEnemies.Empty();

	if (PreviousCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[TeamManager] Cleared %d Visible enemies"), PreviousCount);
	}
}

FTeamObservation UTeamManagerComponent::BuildTeamObservation(const TArray<AActor*>& Followers)
{
	// Use the static helper from FTeamObservation to build complete observation
	// This includes all follower observations and team metrics
	AActor* PrimaryObjective = HostileObjective ? Cast<AActor>(HostileObjective) : Cast<AActor>(FriendlyObjective);
	TArray<AActor*> Enemies = GetVisibleEnemies();

	FTeamObservation Observation = FTeamObservation::BuildFromTeam(
		Followers,
		PrimaryObjective,
		Enemies
	);

	// Cache observation
	CurrentTeamObservation = Observation;

	return Observation;
}
