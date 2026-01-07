// TacticalRewardProvider.cpp - Schola reward provider for combat events

#include "Schola/TacticalRewardProvider.h"
#include "Team/FollowerAgentComponent.h"

UTacticalRewardProvider::UTacticalRewardProvider()
{
}

void UTacticalRewardProvider::Initialize()
{
	if (bAutoFindFollower && FollowerAgent == nullptr)
	{
		FollowerAgent = FindFollowerAgent();
	}

	if (FollowerAgent)
	{
		LastRewardValue = FollowerAgent->GetAccumulatedReward();

		UE_LOG(LogTemp, Log, TEXT("[TacticalRewardProvider] Initialized with FollowerAgent %s"),
			*FollowerAgent->GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalRewardProvider] No FollowerAgent found"));
	}
}

float UTacticalRewardProvider::GetReward()
{
	if (!FollowerAgent)
	{
		return 0.0f;
	}

	float CurrentReward = FollowerAgent->GetAccumulatedReward();
	float DeltaReward = CurrentReward - LastRewardValue;
	LastRewardValue = CurrentReward;

	return DeltaReward;
}

void UTacticalRewardProvider::Reset()
{
	LastRewardValue = 0.0f;

	if (FollowerAgent)
	{
		FollowerAgent->ResetEpisode();
		LastRewardValue = FollowerAgent->GetAccumulatedReward();
	}
}

UFollowerAgentComponent* UTacticalRewardProvider::FindFollowerAgent() const
{
	AActor* Owner = Cast<AActor>(GetOuter());
	if (!Owner)
	{
		UActorComponent* OuterComponent = Cast<UActorComponent>(GetOuter());
		if (OuterComponent)
		{
			Owner = OuterComponent->GetOwner();
		}
	}

	if (Owner)
	{
		return Owner->FindComponentByClass<UFollowerAgentComponent>();
	}

	return nullptr;
}
