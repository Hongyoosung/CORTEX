// DEEpisodeManagerComponent.cpp - Implementation

#include "Schola/Components/DEEpisodeManagerComponent.h"


UDEEpisodeManagerComponent::UDEEpisodeManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}


//------------------------------------------------------------------------------
// EPISODE LIFECYCLE
//------------------------------------------------------------------------------

void UDEEpisodeManagerComponent::StartNewEpisode(int32 EnvID)
{
	// Get/increment episode counter
	int32& EpisodeNum = EpisodeCounters.FindOrAdd(EnvID);

	// Increment episode counter
	EpisodeNum++;

	UE_LOG(LogTemp, Warning, TEXT("[EpisodeManager] Environment %d reset complete (next episode: %d)"),
		EnvID, EpisodeNum);
}

int32 UDEEpisodeManagerComponent::GetCurrentEpisode(int32 EnvID) const
{
	const int32* Episode = EpisodeCounters.Find(EnvID);
	return Episode ? *Episode : 0;
}

bool UDEEpisodeManagerComponent::CheckDuplicateReset(int32 EnvID)
{
	double CurrentTime = FPlatformTime::Seconds();
	double& LastReset = LastResetTimestamps.FindOrAdd(EnvID, 0.0);

	if ((CurrentTime - LastReset) < DUPLICATE_RESET_THRESHOLD)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EpisodeManager] Duplicate reset blocked for EnvID %d (%.3fs since last reset)"),
			EnvID, CurrentTime - LastReset);
		return true;  // This is a duplicate
	}

	// Record this reset timestamp
	LastReset = CurrentTime;
	return false;  // Not a duplicate
}