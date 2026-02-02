// SquadManagerComponent.cpp
// Phase 3: Follower roster management implementation

#include "Team/Components/SquadManagerComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

USquadManagerComponent::USquadManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void USquadManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("[SquadManager] Initialized (MaxFollowers: %d)"), MaxFollowers);
}

void USquadManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Process pending registrations (happens once on first tick)
	if (PendingFollowerRegistration.Num() > 0)
	{
		ProcessPendingRegistrations();
	}
}

bool USquadManagerComponent::RegisterFollower(AActor* Follower)
{
	if (!Follower)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SquadManager] RegisterFollower: Null follower provided"));
		return false;
	}

	if (Followers.Contains(Follower))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SquadManager] RegisterFollower: %s already registered"), *Follower->GetName());
		return false;
	}

	if (Followers.Num() >= MaxFollowers)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SquadManager] RegisterFollower: Squad full (%d/%d), cannot register %s"),
			Followers.Num(), MaxFollowers, *Follower->GetName());
		return false;
	}

	// Add to roster
	Followers.Add(Follower);

	UE_LOG(LogTemp, Log, TEXT("[SquadManager] Registered follower: %s (Total: %d/%d)"),
		*Follower->GetName(), Followers.Num(), MaxFollowers);

	// Fire event
	OnFollowerRegistered.Broadcast(Follower);

	return true;
}

bool USquadManagerComponent::UnregisterFollower(AActor* Follower)
{
	if (!Follower)
	{
		return false;
	}

	int32 RemovedCount = Followers.Remove(Follower);

	if (RemovedCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[SquadManager] Unregistered follower: %s (Remaining: %d)"),
			*Follower->GetName(), Followers.Num());

		// Fire event
		OnFollowerUnregistered.Broadcast(Follower);

		return true;
	}

	return false;
}

void USquadManagerComponent::QueueFollowerRegistration(AActor* Follower)
{
	if (Follower && !PendingFollowerRegistration.Contains(Follower))
	{
		PendingFollowerRegistration.Add(Follower);

		UE_LOG(LogTemp, Log, TEXT("[SquadManager] Queued follower for registration: %s"), *Follower->GetName());
	}
}

TArray<AActor*> USquadManagerComponent::GetAliveFollowers() const
{
	TArray<AActor*> AliveFollowers;

	for (AActor* Follower : Followers)
	{
		if (!Follower)
		{
			continue;
		}

		// Check if follower has health component (simple alive check)
		// For now, assume all registered followers are alive
		// TODO: Integrate with health system when available
		AliveFollowers.Add(Follower);
	}

	return AliveFollowers;
}

void USquadManagerComponent::ProcessPendingRegistrations()
{
	if (PendingFollowerRegistration.Num() == 0)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[SquadManager] Processing %d pending registrations"), PendingFollowerRegistration.Num());

	TArray<AActor*> ToProcess = PendingFollowerRegistration;
	PendingFollowerRegistration.Empty();

	for (AActor* Follower : ToProcess)
	{
		RegisterFollower(Follower);
	}
}
