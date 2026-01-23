// ScholaCombatEnvironment.h - Schola environment for combat AI training

#pragma once

#include "CoreMinimal.h"
#include "Environment/StaticEnvironment.h"
#include "ScholaCombatEnvironment.generated.h"

class ASimulationManagerGameMode;
class UScholaAgentComponent;

/**
 * Schola Combat Environment
 *
 * Integrates the SBDAPM combat simulation with Schola's RL training framework.
 * Manages gRPC server, agent registration, and episode lifecycle.
 *
 * Architecture:
 * - Spawns at level start (place in level or spawn in GameMode)
 * - Auto-discovers ScholaAgentComponents on follower pawns
 * - Starts gRPC server on configured port (default: 50051)
 * - Communicates with Python RLlib training script
 *
 * Usage:
 * 1. Place this actor in your level (or spawn in GameMode::BeginPlay)
 * 2. Configure port and training settings
 * 3. Ensure follower pawns have ScholaAgentComponent
 * 4. Start UE5 + run Python training script (train_rllib.py)
 */
UCLASS()
class GAMEAI_PROJECT_API AScholaCombatEnvironment : public AStaticScholaEnvironment
{
	GENERATED_BODY()

public:
	AScholaCombatEnvironment(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	//--------------------------------------------------------------------------
	// SCHOLA ENVIRONMENT INTERFACE (AAbstractScholaEnvironment)
	//--------------------------------------------------------------------------

	virtual void InitializeEnvironment() override;
	virtual void ResetEnvironment() override;
	virtual void InternalRegisterAgents(TArray<FTrainerAgentPair>& OutAgentTrainerPairs) override;
	virtual void SetEnvironmentOptions(const TMap<FString, FString>& Options) override;
	virtual void SeedEnvironment(int Seed) override;

	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** Enable Schola training (starts gRPC server) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bEnableTraining = true;

	/** gRPC server port for Python RLlib communication */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	int32 ServerPort = 50051;

	/** Auto-discover agents in level (finds all ScholaAgentComponents) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bAutoDiscoverAgents = true;

	/** Team IDs to include in training (empty = all teams) - v8.5 Vectorized Training: Use this to isolate environments */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	TArray<int32> TrainingTeamIDs;

	//--------------------------------------------------------------------------
	// STATE
	//--------------------------------------------------------------------------

	/** All registered Schola agent components */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	TArray<UScholaAgentComponent*> RegisteredAgents;

	/** Reference to simulation manager */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	ASimulationManagerGameMode* SimulationManager = nullptr;

	/** Is gRPC server running? */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	bool bServerRunning = false;

	/** Has InternalRegisterAgents been called this session? */
	bool bAgentsRegistered = false;

	/** Unique ID for this environment instance (0, 1, 2, 3...) - v8.5 Vectorized Training */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	int32 EnvironmentID = -1;

	/** Current episode number for this environment (independent per environment) */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	int32 CurrentEpisode = -1;

	//--------------------------------------------------------------------------
	// UTILITY
	//--------------------------------------------------------------------------

	/** Discover all ScholaAgentComponents in level */
	UFUNCTION(BlueprintCallable, Category = "Schola")
	void DiscoverAgents();

	/** Register a single agent manually */
	UFUNCTION(BlueprintCallable, Category = "Schola")
	bool RegisterAgent(UScholaAgentComponent* Agent);


	/** Bind to SimulationManager episode events - includes EnvironmentID for filtering */
	UFUNCTION()
	void OnEpisodeStarted(int32 BroadcastEnvID, int32 EpisodeNumber);

	UFUNCTION()
	void OnEpisodeEnded(int32 BroadcastEnvID, const FEpisodeResult& Result);

private:
	/** Validate agent for training (has required components) */
	bool ValidateAgent(UScholaAgentComponent* Agent) const;

	/** Setup episode event bindings */
	void BindEpisodeEvents();
};
