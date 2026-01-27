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

	/**
	 * gRPC server port for Python RLlib communication
	 * NOTE: Only ONE environment actor should have bEnableTraining=true in multi-actor setup
	 * The others will be discovered automatically via CollectEnvironments()
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	int32 ServerPort = 50051;

	/** Auto-discover agents in level (finds all ScholaAgentComponents) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bAutoDiscoverAgents = true;

	/**
	 * Team IDs managed by THIS environment actor
	 * Example for 4-actor setup (32 agents, 8 teams):
	 *   - Actor 0: [0, 1] → Env 0 (Teams 0,1 = 4v4)
	 *   - Actor 1: [2, 3] → Env 1 (Teams 2,3 = 4v4)
	 *   - Actor 2: [4, 5] → Env 2 (Teams 4,5 = 4v4)
	 *   - Actor 3: [6, 7] → Env 3 (Teams 6,7 = 4v4)
	 *
	 * CRITICAL: Each actor must have UNIQUE team IDs (no overlap)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	TArray<int32> TrainingTeamIDs;

	/**
	 * DEPRECATED: Team-to-Environment mapping (use TrainingTeamIDs instead)
	 * In multi-actor architecture, each actor IS its own environment.
	 * Use TrainingTeamIDs to specify which teams this actor manages.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config|Deprecated", meta = (DeprecationMessage = "Use TrainingTeamIDs instead"))
	TMap<int32, int32> TeamToEnvironmentMap;

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

	/** Episode counters per logical environment (4 environments = 4 counters) */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	TMap<int32, int32> LogicalEnvironmentEpisodes;

	//--------------------------------------------------------------------------
	// UTILITY
	//--------------------------------------------------------------------------

	/** Get the environment ID assigned by Schola */
	UFUNCTION(BlueprintCallable, Category = "Schola")
	int32 GetEnvId() const { return EnvId; }

	/** Discover all ScholaAgentComponents in level */
	UFUNCTION(BlueprintCallable, Category = "Schola")
	void DiscoverAgents();

	/** Register a single agent manually */
	UFUNCTION(BlueprintCallable, Category = "Schola")
	bool RegisterAgent(UScholaAgentComponent* Agent);

	/** Bind to SimulationManager episode events */
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
