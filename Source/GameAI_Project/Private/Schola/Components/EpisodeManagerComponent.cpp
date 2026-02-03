// EpisodeManagerComponent.cpp - Implementation

#include "Schola/Components/EpisodeManagerComponent.h"
#include "Core/SimulationManagerGameMode.h"

UEpisodeManagerComponent::UEpisodeManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

//------------------------------------------------------------------------------
// INITIALIZATION
//------------------------------------------------------------------------------

void UEpisodeManagerComponent::BindToSimulationManager(ASimulationManagerGameMode* Manager)
{
	SimulationManager = Manager;

	if (!SimulationManager || !IsValid(SimulationManager))
	{
		UE_LOG(LogTemp, Error, TEXT("[EpisodeManager] Cannot bind - SimulationManager invalid!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[EpisodeManager] Binding episode events to SimulationManager..."));
	UE_LOG(LogTemp, Warning, TEXT("  - Environment ID: %d"), EnvironmentID);
	UE_LOG(LogTemp, Warning, TEXT("  - SimulationManager: %s"), *SimulationManager->GetName());

	// Bind to episode lifecycle events (AddUniqueDynamic prevents duplicate bindings)
	SimulationManager->OnEpisodeStarted.AddUniqueDynamic(this, &UEpisodeManagerComponent::OnEpisodeStarted);
	SimulationManager->OnEpisodeEnded.AddUniqueDynamic(this, &UEpisodeManagerComponent::OnEpisodeEnded);

	UE_LOG(LogTemp, Warning, TEXT("[EpisodeManager] Episode event bindings CONFIRMED ✓"));
}

//------------------------------------------------------------------------------
// EPISODE LIFECYCLE
//------------------------------------------------------------------------------

void UEpisodeManagerComponent::StartNewEpisode(int32 EnvID)
{
	if (!SimulationManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[EpisodeManager] Cannot start episode - SimulationManager is null"));
		return;
	}

	// Get/increment episode counter
	int32& EpisodeNum = EpisodeCounters.FindOrAdd(EnvID);

	// Clear termination flags for this environment
	SimulationManager->SetEnvironmentTerminationFlags(EnvID, false, false, false);

	// Start new episode
	SimulationManager->StartNewEpisode(EnvID, EpisodeNum);

	// Increment episode counter
	EpisodeNum++;

	UE_LOG(LogTemp, Warning, TEXT("[EpisodeManager] Environment %d reset complete (next episode: %d)"),
		EnvID, EpisodeNum);
}

int32 UEpisodeManagerComponent::GetCurrentEpisode(int32 EnvID) const
{
	const int32* Episode = EpisodeCounters.Find(EnvID);
	return Episode ? *Episode : 0;
}

bool UEpisodeManagerComponent::CheckDuplicateReset(int32 EnvID)
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

//------------------------------------------------------------------------------
// EVENT HANDLERS
//------------------------------------------------------------------------------

void UEpisodeManagerComponent::OnEpisodeStarted(int32 BroadcastEnvID, int32 EpisodeNumber)
{
	// Only respond to events for THIS environment's EnvID
	if (BroadcastEnvID != EnvironmentID)
	{
		// Not our environment, ignore
		return;
	}

	int32 LogicalEpisode = EpisodeCounters.FindOrAdd(BroadcastEnvID);

	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [EpisodeManager] EnvID %d - Episode %d STARTED                 ║"),
		EnvironmentID, LogicalEpisode);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════════╝"));

	// CRITICAL: Validate environment state
	if (!SimulationManager || !IsValid(SimulationManager))
	{
		UE_LOG(LogTemp, Error, TEXT("[EpisodeManager] CRITICAL: SimulationManager invalid in OnEpisodeStarted!"));
	}
}

void UEpisodeManagerComponent::OnEpisodeEnded(int32 BroadcastEnvID, const FEpisodeResult& Result)
{
	// Only respond to events for THIS environment's EnvID
	if (BroadcastEnvID != EnvironmentID)
	{
		// Not our environment, ignore
		return;
	}

	int32 LogicalEpisode = EpisodeCounters.FindOrAdd(BroadcastEnvID);

	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [EpisodeManager] EnvID %d - Episode %d ENDED                   ║"),
		EnvironmentID, LogicalEpisode);
	UE_LOG(LogTemp, Warning, TEXT("║ Winner: Team %d | Loser: Team %d                               ║"),
		Result.WinningTeamID, Result.LosingTeamID);
	UE_LOG(LogTemp, Warning, TEXT("║ Duration: %.2fs | Steps: %d                                    ║"),
		Result.EpisodeDuration, Result.TotalSteps);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════════╝"));

	// CRITICAL: Validate environment state
	if (!SimulationManager || !IsValid(SimulationManager))
	{
		UE_LOG(LogTemp, Error, TEXT("[EpisodeManager] CRITICAL: SimulationManager invalid in OnEpisodeEnded!"));
	}

	UE_LOG(LogTemp, Warning, TEXT("[EpisodeManager] EnvID %d episode ended - agents will send termination signals"),
		EnvironmentID);
	UE_LOG(LogTemp, Warning, TEXT("[EpisodeManager] Waiting for Python to call reset() for EnvID %d (NOT auto-resetting)"),
		EnvironmentID);
}
