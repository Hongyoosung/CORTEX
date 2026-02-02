// IntelManagerComponent.cpp
// Phase 3: Intel tracking and observation implementation

#include "Team/Components/IntelManagerComponent.h"
#include "Team/ObjectiveActor.h"
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
	if (!GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("[IntelManager] DiscoverWorldObjectives: No world context"));
		return;
	}

	// Search for friendly objective
	TArray<AActor*> FriendlyFound;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), FriendlyObjectiveTag, FriendlyFound);

	if (FriendlyFound.Num() > 0)
	{
		FriendlyObjective = Cast<AObjectiveActor>(FriendlyFound[0]);
		if (FriendlyObjective)
		{
			UE_LOG(LogTemp, Log, TEXT("[IntelManager] Discovered friendly objective: %s (TeamID: %d)"),
				*FriendlyObjective->GetName(), FriendlyObjective->OwnerTeamID);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[IntelManager] No friendly objective found with tag: %s"), *FriendlyObjectiveTag.ToString());
	}

	// Search for hostile objective
	TArray<AActor*> HostileFound;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), HostileObjectiveTag, HostileFound);

	if (HostileFound.Num() > 0)
	{
		HostileObjective = Cast<AObjectiveActor>(HostileFound[0]);
		if (HostileObjective)
		{
			UE_LOG(LogTemp, Log, TEXT("[IntelManager] Discovered hostile objective: %s (TeamID: %d)"),
				*HostileObjective->GetName(), HostileObjective->OwnerTeamID);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[IntelManager] No hostile objective found with tag: %s"), *HostileObjectiveTag.ToString());
	}

	// Verify discovery
	if (FriendlyObjective && HostileObjective)
	{
		UE_LOG(LogTemp, Log, TEXT("[IntelManager] Successfully discovered both objectives"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[IntelManager] Failed to discover all objectives (Friendly: %s, Hostile: %s)"),
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
