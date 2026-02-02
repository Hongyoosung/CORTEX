// IntelManagerComponent.cpp
// Phase 3: Intel tracking and observation implementation

#include "Team/Components/IntelManagerComponent.h"
#include "Team/ObjectiveActor.h"
#include "Core/SimulationManagerGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

UIntelManagerComponent::UIntelManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UIntelManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("[IntelManager] Initialized (TeamID: %d)"), TeamID);
}

void UIntelManagerComponent::DiscoverWorldObjectives()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("[IntelManager] DiscoverWorldObjectives: No world context"));
		return;
	}

	// Get SimulationManager for enemy relationship queries
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(
		UGameplayStatics::GetGameMode(World)
	);

	if (!SimManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[IntelManager] No SimulationManagerGameMode found - cannot discover objectives"));
		return;
	}

	// Find all ObjectiveActors in world
	TArray<AActor*> AllObjectives;
	UGameplayStatics::GetAllActorsOfClass(World, AObjectiveActor::StaticClass(), AllObjectives);

	if (AllObjectives.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[IntelManager] No ObjectiveActors found in world"));
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
			UE_LOG(LogTemp, Log, TEXT("[IntelManager] Discovered friendly objective: %s (TeamID: %d)"),
				*Objective->GetName(), Objective->OwnerTeamID);
		}
		// Hostile objective: SimManager confirms enemy relationship
		else if (SimManager->AreTeamsEnemies(this->TeamID, Objective->OwnerTeamID))
		{
			HostileObjective = Objective;
			UE_LOG(LogTemp, Log, TEXT("[IntelManager] Discovered hostile objective: %s (TeamID: %d)"),
				*Objective->GetName(), Objective->OwnerTeamID);
		}
	}

	// Verify discovery
	if (FriendlyObjective && HostileObjective)
	{
		UE_LOG(LogTemp, Log, TEXT("✅ [IntelManager v9.0] Successfully discovered both objectives (TeamID: %d)"), TeamID);

		// v9.0: Broadcast event to notify subscribers (e.g., FollowerAgentComponent)
		OnObjectivesDiscovered.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("📡 [IntelManager v9.0] OnObjectivesDiscovered event broadcast (TeamID: %d)"), TeamID);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ [IntelManager] Failed to discover all objectives (TeamID: %d, Friendly: %s, Hostile: %s)"),
			TeamID,
			FriendlyObjective ? TEXT("OK") : TEXT("MISSING"),
			HostileObjective ? TEXT("OK") : TEXT("MISSING"));
	}
}

void UIntelManagerComponent::RegisterEnemy(AActor* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	if (!KnownEnemies.Contains(Enemy))
	{
		KnownEnemies.Add(Enemy);

		UE_LOG(LogTemp, Log, TEXT("[IntelManager] Registered enemy: %s (Total: %d)"),
			*Enemy->GetName(), KnownEnemies.Num());
	}
}

void UIntelManagerComponent::UnregisterEnemy(AActor* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	int32 RemovedCount = KnownEnemies.Remove(Enemy);

	if (RemovedCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[IntelManager] Unregistered enemy: %s (Remaining: %d)"),
			*Enemy->GetName(), KnownEnemies.Num());
	}
}

TArray<AActor*> UIntelManagerComponent::GetKnownEnemies() const
{
	return KnownEnemies.Array();
}

void UIntelManagerComponent::ClearKnownEnemies()
{
	int32 PreviousCount = KnownEnemies.Num();
	KnownEnemies.Empty();

	if (PreviousCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[IntelManager] Cleared %d known enemies"), PreviousCount);
	}
}

FTeamObservation UIntelManagerComponent::BuildTeamObservation(const TArray<AActor*>& Followers)
{
	// Use the static helper from FTeamObservation to build complete observation
	// This includes all follower observations and team metrics
	AActor* PrimaryObjective = HostileObjective ? Cast<AActor>(HostileObjective) : Cast<AActor>(FriendlyObjective);
	TArray<AActor*> Enemies = GetKnownEnemies();

	FTeamObservation Observation = FTeamObservation::BuildFromTeam(
		Followers,
		PrimaryObjective,
		Enemies
	);

	// Cache observation
	CurrentTeamObservation = Observation;

	return Observation;
}
