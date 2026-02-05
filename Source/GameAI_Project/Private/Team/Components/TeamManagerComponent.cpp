// IntelManagerComponent.cpp
// Phase 3: Intel tracking and observation implementation

#include "Team/Components/TeamManagerComponent.h"
#include "Team/ObjectiveActor.h"
#include "Actor/FollowerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"


UTeamManagerComponent::UTeamManagerComponent()
	: Super()
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
	TeamInfo.AgnetCount++;

	if(RegisteredFollowers.Num() >= MaxFollowers)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamManager] Squad has reached maximum capacity (%d/%d)"),
			RegisteredFollowers.Num(), MaxFollowers);

		OnAllAgentsRegistered_Delegate.Broadcast(RegisteredFollowers.Num());
	}

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
	TeamInfo.AgnetCount = FMath::Max(0, TeamInfo.AgnetCount - 1);

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
