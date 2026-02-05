// ScholaCombatEnvironment.h - Schola environment for combat AI training

#pragma once

#include "CoreMinimal.h"
#include "Environment/StaticEnvironment.h"
#include "ScholaCombatEnvironment.generated.h"



class ASimulationManagerGameMode;
class AObjectiveActor;
class UScholaAgentComponent;
class UEnvRegistryComponent;
class UEpisodeManagerComponent;



DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnScholaEnvironmentInitialized);


/** //==========================================================================
 * Schola Combat Environment
 *
 * v9.0 REFACTORED: Component-based architecture following Single Responsibility Principle
 *
 * Coordinates Schola environment functionality through actor components:
 * - UEnvRegistryComponent: Team and objective registration
 * - UEpisodeManagerComponent: Episode lifecycle management
 *
 * Architecture:
 * - Spawns at level start (place in level or spawn in GameMode)
 * - Auto-discovers ScholaAgentComponents on follower pawns
 * - Starts gRPC server on configured port (default: 50051)
 * - Communicates with Python RLlib training script
 *
 * Usage:
 * 1. Place this actor in your level (or spawn in GameMode::BeginPlay)
 * 2. Configure team IDs in EnvRegistry component
 * 3. Ensure follower pawns have ScholaAgentComponent
 * 4. Start UE5 + run Python training script (train_rllib.py)
 */ //==========================================================================
UCLASS()
class GAMEAI_PROJECT_API AScholaCombatEnvironment : public AStaticScholaEnvironment
{
	GENERATED_BODY()

public:
	AScholaCombatEnvironment(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;



	//==========================================================================
	// SCHOLA ENVIRONMENT INTERFACE (AAbstractScholaEnvironment)
	//==========================================================================
	virtual void InitializeEnvironment() override;
	virtual void ResetEnvironment() override;
	virtual void InternalRegisterAgents(TArray<FTrainerAgentPair>& OutAgentTrainerPairs) override;
	virtual void SetEnvironmentOptions(const TMap<FString, FString>& Options) override;
	virtual void SeedEnvironment(int Seed) override;


	//==========================================================================
	// TEAM MANAGEMENT INTERFACE
	//==========================================================================
	bool RegisterTeam(int32 TeamID);
	bool RegisterObjective(AObjectiveActor* Objective);
	bool UnRegisterTeam(int32 TeamID);
	bool UnRegisterObjective(AObjectiveActor* Objective);

	int32 GetRegisterTeamCount()	const;
	int32 GetRegisteredObjectiveCount() const;
	


	//==========================================================================
	// GETTERS
	//==========================================================================

	/** Get the environment ID assigned by Schola */
	UFUNCTION(BlueprintCallable, Category = "Schola")
	FORCEINLINE		int32	GetEnvId() const { return ScholaEnvID; }



private:
	/** Validate agent for training (has required components) */
	bool ValidateAgent(UScholaAgentComponent* Agent) const;



public:
	//=========================== Delegated ===============================
	FOnScholaEnvironmentInitialized OnScholaEnvironmentInitialized_Delegate;



public:
	//==========================================================================
	// COMPONENTS (v9.0 REFACTOR)
	//==========================================================================

	/** Environment Registry Component - manages team/objective registration */
	UPROPERTY(VisibleAnywhere,	BlueprintReadOnly, Category = "Schola|Components")
	UEnvRegistryComponent*		EnvRegistry;

	/** Episode Manager Component - manages episode lifecycle */
	UPROPERTY(VisibleAnywhere,	BlueprintReadOnly, Category = "Schola|Components")
	UEpisodeManagerComponent*	EpisodeManager;


	//==========================================================================
	// CONFIGURATION
	//==========================================================================

	/** Auto-discover agents in level (finds all ScholaAgentComponents) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bAutoDiscoverAgents;


	//==========================================================================
	// STATE
	//==========================================================================

	/** All registered Schola agent components */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	TArray<UScholaAgentComponent*> RegisteredAgents;

	/** Reference to simulation manager */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	ASimulationManagerGameMode* SimulationManager;

	/** Is gRPC server running? */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	bool bServerRunning;

	/** Has InternalRegisterAgents been called this session? */
	bool bAgentsRegistered;

	/** Has environment been reset by Python? (indicates training is active) */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	bool bTrainingActive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	int32 ScholaEnvID;
};
