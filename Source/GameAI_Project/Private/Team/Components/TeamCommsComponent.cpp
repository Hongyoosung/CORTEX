// TeamCommsComponent.cpp
// Phase 3: Team communication (extracted from FollowerAgentComponent)

#include "Team/Components/TeamCommsComponent.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "EngineUtils.h"

UTeamCommsComponent::UTeamCommsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTeamCommsComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UTeamCommsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean unregistration
	UnregisterFromTeamLeader();

	Super::EndPlay(EndPlayReason);
}

bool UTeamCommsComponent::RegisterWithTeamLeader()
{
	// Step 1: Resolve TeamLeaderComponent if not already set
	if (!TeamLeader)
	{
		if (!ResolveTeamLeaderComponent())
		{
			if (bEnableVerboseLogging)
			{
				UE_LOG(LogTemp, Warning, TEXT("[TeamComms] %s: Failed to resolve TeamLeaderComponent"),
					*GetOwner()->GetName());
			}
			return false;
		}
	}

	// Step 2: Attempt registration
	if (!TeamLeader)
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamComms] %s: TeamLeader is null after resolution"),
			*GetOwner()->GetName());
		return false;
	}

	bool bSuccess = TeamLeader->RegisterFollower(GetOwner());

	if (bSuccess)
	{
		bIsRegistered = true;
		OnRegistrationChanged.Broadcast(true);

		if (bEnableVerboseLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("[TeamComms] %s: Successfully registered with leader %s"),
				*GetOwner()->GetName(),
				*TeamLeaderActor->GetName());
		}
	}
	else
	{
		if (bEnableVerboseLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TeamComms] %s: Registration failed (squad may be full)"),
				*GetOwner()->GetName());
		}
	}

	return bSuccess;
}

void UTeamCommsComponent::UnregisterFromTeamLeader()
{
	if (TeamLeader && bIsRegistered)
	{
		TeamLeader->UnregisterFollower(GetOwner());
		bIsRegistered = false;
		OnRegistrationChanged.Broadcast(false);

		if (bEnableVerboseLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("[TeamComms] %s: Unregistered from leader"),
				*GetOwner()->GetName());
		}
	}
}

void UTeamCommsComponent::SignalEventToLeader(
	EStrategicEvent Event,
	AActor* Instigator,
	FVector Location,
	int32 Priority
)
{
	if (!TeamLeader)
	{
		if (bEnableVerboseLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TeamComms] %s: Cannot signal event - no team leader"),
				*GetOwner()->GetName());
		}
		return;
	}

	// Forward event to team leader
	TeamLeader->ProcessStrategicEvent(Event, Instigator, Location, Priority);

	// Broadcast local event
	OnEventSignaled.Broadcast(Event, Instigator, Priority);

	if (bEnableVerboseLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[TeamComms] %s: Signaled event %d (priority %d) to leader"),
			*GetOwner()->GetName(),
			static_cast<int32>(Event),
			Priority);
	}
}

UTeamLeaderComponent* UTeamCommsComponent::GetTeamLeader() const
{
	if(!TeamLeader)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamComms] %s: GetTeamLeader called but TeamLeader is null"),
			*GetOwner()->GetName());

		return nullptr;
	}

	return TeamLeader;
}

int32 UTeamCommsComponent::GetTeamID() const
{
	if (TeamLeader)
	{
		return TeamLeader->TeamID;
	}

	return -1;
}

AActor* UTeamCommsComponent::FindTeamLeaderByTeamID()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Search for actor with TeamLeaderComponent matching our TeamID
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		UTeamLeaderComponent* LeaderComp = Actor->FindComponentByClass<UTeamLeaderComponent>();
		if (LeaderComp && LeaderComp->TeamID == this->TeamID)
		{
			if (bEnableVerboseLogging)
			{
				UE_LOG(LogTemp, Log, TEXT("[TeamComms] %s: Found team leader by TeamID %d: %s"),
					*GetOwner()->GetName(),
					TeamID,
					*Actor->GetName());
			}
			return Actor;
		}
	}

	if (bEnableVerboseLogging)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TeamComms] %s: No team leader found with TeamID %d"),
			*GetOwner()->GetName(),
			TeamID);
	}

	return nullptr;
}

bool UTeamCommsComponent::ResolveTeamLeaderComponent()
{
	// Step 1: Find TeamLeaderActor by TeamID if not cached
	if (!TeamLeaderActor)
	{
		TeamLeaderActor = FindTeamLeaderByTeamID();
		if (!TeamLeaderActor)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TeamComms] %s: No team leader actor found with TeamID %d"),
				*GetOwner()->GetName(),
				TeamID);
			return false;
		}
	}

	// Step 2: Get TeamLeaderComponent from actor
	TeamLeader = TeamLeaderActor->FindComponentByClass<UTeamLeaderComponent>();
	if (!TeamLeader)
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamComms] %s: TeamLeaderActor %s has no TeamLeaderComponent"),
			*GetOwner()->GetName(),
			*TeamLeaderActor->GetName());
		return false;
	}

	// Step 3: Verify TeamID matches
	if (TeamLeader->TeamID != this->TeamID)
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamComms] %s: TeamID mismatch - Expected %d, Leader has %d"),
			*GetOwner()->GetName(),
			this->TeamID,
			TeamLeader->TeamID);
		return false;
	}

	if (bEnableVerboseLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[TeamComms] %s: Resolved TeamLeaderComponent from %s (TeamID: %d)"),
			*GetOwner()->GetName(),
			*TeamLeaderActor->GetName(),
			TeamID);
	}

	return true;
}
