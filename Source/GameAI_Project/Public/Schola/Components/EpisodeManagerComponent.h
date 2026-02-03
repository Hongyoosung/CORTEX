// EpisodeManagerComponent.h - Manages episode lifecycle for Schola environments

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/SimulationManagerGameMode.h"
#include "EpisodeManagerComponent.generated.h"

/**
 * Episode Manager Component
 *
 * Responsibilities:
 * - Manage episode lifecycle (start, end, reset)
 * - Track episode counters per environment
 * - Prevent duplicate resets
 * - Bind to SimulationManager episode events
 *
 * Single Responsibility: Episode Lifecycle Management
 */
UCLASS(ClassGroup=(Schola), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UEpisodeManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEpisodeManagerComponent();

	//--------------------------------------------------------------------------
	// INITIALIZATION
	//--------------------------------------------------------------------------

	/**
	 * Bind to SimulationManager episode events
	 * @param Manager - SimulationManagerGameMode reference
	 */
	void BindToSimulationManager(ASimulationManagerGameMode* Manager);

	//--------------------------------------------------------------------------
	// EPISODE LIFECYCLE
	//--------------------------------------------------------------------------

	/**
	 * Start a new episode for this environment
	 * @param EnvID - Environment ID
	 */
	void StartNewEpisode(int32 EnvID);

	/**
	 * Get current episode number for an environment
	 * @param EnvID - Environment ID
	 * @return Episode number
	 */
	int32 GetCurrentEpisode(int32 EnvID) const;

	/**
	 * Check for duplicate reset calls (prevents Schola's hard_reset() bug)
	 * @param EnvID - Environment ID
	 * @return true if this reset should be blocked (duplicate)
	 */
	bool CheckDuplicateReset(int32 EnvID);

	/**
	 * Set environment ID
	 * @param EnvID - Environment ID to set
	 */
	void SetEnvironmentID(int32 EnvID) { EnvironmentID = EnvID; }

	/**
	 * Get environment ID
	 * @return Environment ID
	 */
	int32 GetEnvironmentID() const { return EnvironmentID; }

	//--------------------------------------------------------------------------
	// EVENT HANDLERS
	//--------------------------------------------------------------------------

	/**
	 * Called when an episode starts
	 * @param BroadcastEnvID - Environment ID from broadcast
	 * @param EpisodeNumber - Episode number
	 */
	UFUNCTION()
	void OnEpisodeStarted(int32 BroadcastEnvID, int32 EpisodeNumber);

	/**
	 * Called when an episode ends
	 * @param BroadcastEnvID - Environment ID from broadcast
	 * @param Result - Episode result data
	 */
	UFUNCTION()
	void OnEpisodeEnded(int32 BroadcastEnvID, const FEpisodeResult& Result);

private:
	/** Reference to SimulationManagerGameMode */
	UPROPERTY()
	ASimulationManagerGameMode* SimulationManager = nullptr;

	/** Episode counters per environment (EnvID -> Episode Number) */
	UPROPERTY()
	TMap<int32, int32> EpisodeCounters;

	/** Last reset timestamps to prevent duplicate resets (EnvID -> Timestamp) */
	TMap<int32, double> LastResetTimestamps;

	/** Environment ID assigned by Schola */
	int32 EnvironmentID = -1;

	/** Minimum time between resets (seconds) to prevent duplicates */
	static constexpr double DUPLICATE_RESET_THRESHOLD = 0.5;
};
