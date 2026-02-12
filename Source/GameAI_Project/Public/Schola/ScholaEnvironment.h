// ScholaEnvironment.h - Schola training environment for v10.2 Commander-Executor Architecture

#pragma once

#include "CoreMinimal.h"
#include "Environment/StaticEnvironment.h"
#include "ScholaEnvironment.generated.h"



class AMocGameMode;
class ASquadManager;
class UScholaMocAgent;
class UEpisodeManagerComponent;
class AMocTrainer;



DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnScholaEnvironmentInitialized);


/** //==========================================================================
 * Schola Training Environment (v10.2)
 *
 * ARCHITECTURE: Centralized Commander-Executor (v10.2)
 *
 * Coordinates Schola RL training environment for hierarchical team-based AI:
 *
 * [Layer 1] Squad Commander (ASquadManager):
 *   - Centralized MCTS planning (15ms budget)
 *   - Tactical Play selection → Role Distribution
 *   - Input: FTeamState (60-dim team state)
 *   - Output: 5 × EStrategyType (Assault/Defend/Support)
 *   - Frequency: 0.5s or critical events
 *
 * [Layer 2] Executor Agents (AMocCharacter × 5):
 *   - Receive role assignments from commander
 *   - RL Policy → EQS Weights (7-dim spatial reasoning)
 *   - No local MCTS (removed in v10.2)
 *
 * [Layer 3] EQS Spatial Reasoning:
 *   - Query 48 samples, 7 weighted tests
 *   - Output: Best tactical location
 *
 * Component Architecture:
 * - UEpisodeManagerComponent: Episode lifecycle management
 * - Integration with AMocGameMode: Game orchestration
 * - Integration with ASquadManager: Centralized planning
 *
 * Key Changes (v10.1 → v10.2):
 * - Planning: Decentralized (5 MCTS) → Centralized (1 MCTS)
 * - Compute: 75ms → 15ms (5× reduction)
 * - Action Space: 243 combinations → ~10 Tactical Plays
 * - Coordination: Implicit → Explicit command-driven
 * - Agents: AMocCharacter with UScholaMocAgent component
 * - Trainers: AMocTrainer for RL training interface
 *
 * Training Integration:
 * 1. Place this actor in level (or spawn in AMocGameMode)
 * 2. Agents auto-discovered (AMocCharacter with UScholaMocAgent)
 * 3. SquadManager performs centralized planning
 * 4. Start UE5 + run Python training script (train_rllib.py)
 *
 * Multi-Environment Setup:
 * - Each actor instance = 1 physical training environment
 * - For 4 parallel environments, spawn 4 actors
 * - Schola CollectEnvironments() aggregates all actors
 */ //==========================================================================
UCLASS()
class GAMEAI_PROJECT_API AScholaEnvironment : public AStaticScholaEnvironment
{
	GENERATED_BODY()

public:
	AScholaEnvironment(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;



	//==========================================================================
	// SCHOLA ENVIRONMENT INTERFACE (AAbstractScholaEnvironment)
	//==========================================================================
	virtual void InitializeEnvironment() override;
	virtual void ResetEnvironment() override;
	virtual void RegisterAgents(TArray<APawn*>& OutTrainerControlledPawns) override;
	virtual void SetEnvironmentOptions(const TMap<FString, FString>& Options) override;
	virtual void SeedEnvironment(int Seed) override;


	//==========================================================================
	// v10.2 COMMANDER INTEGRATION
	//==========================================================================

	/** Get the Squad Commander for a specific team (centralized planner) */
	UFUNCTION(BlueprintCallable, Category = "Schola|v10.2")
	const ASquadManager* GetSquadCommander(int32 TeamID) const;

	/** Get all registered Squad Commanders across all teams */
	UFUNCTION(BlueprintCallable, Category = "Schola|v10.2")
	TArray<ASquadManager*> GetAllSquadCommanders() const;



	//==========================================================================
	// GETTERS
	//==========================================================================

	/** Get the environment ID assigned by Schola */
	UFUNCTION(BlueprintCallable, Category = "Schola")
	FORCEINLINE		int32	GetEnvId() const { return ScholaEnvID; }



private:
	/** Cache Squad Commander references for each team */
	void CacheSquadCommanders();



public:
	//=========================== Delegated ===============================
	FOnScholaEnvironmentInitialized OnScholaEnvironmentInitialized_Delegate;



public:
	//==========================================================================
	// COMPONENTS (v10.2 REFACTOR)
	//==========================================================================

	/** Episode Manager Component - manages episode lifecycle */
	UPROPERTY(VisibleAnywhere,	BlueprintReadOnly, Category = "Schola|Components")
	UEpisodeManagerComponent*	EpisodeManager;


	//==========================================================================
	// CONFIGURATION
	//==========================================================================

	/** Auto-discover agents in level (finds all AMocCharacter with ScholaAgentComponents) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bAutoDiscoverAgents = true;

	/**
	 * Trainer class to use (can be Blueprint subclass of AMocTrainer)
	 * If not set, defaults to AMocTrainer::StaticClass()
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	TSubclassOf<AMocTrainer> TrainerClass;

	/**
	 * Auto-spawn trainers for agents that don't have one
	 * If false, expects trainers to already exist in level
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bAutoSpawnTrainers = true;

	/** Enable centralized planning via Squad Commanders (v10.2) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config|v10.2")
	bool bEnableCentralizedPlanning;

	/** Log tactical play assignments for analysis */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config|v10.2")
	bool bLogTacticalPlays;


	//==========================================================================
	// STATE
	//==========================================================================

	/** All registered Schola agent components */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	TArray<UScholaMocAgent*> RegisteredAgents;

	/** Reference to MOC game mode */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	AMocGameMode* GameMode;

	/** Cached Squad Commander references per team (v10.2) */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State|v10.2")
	TMap<int32, ASquadManager*> SquadCommanders;

	/** Is gRPC server running? */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	bool bServerRunning;

	/** Has InternalRegisterAgents been called this session? */
	bool bAgentsRegistered;

	/** Has InitializeEnvironment been called this session? */
	bool bEnvironmentInitialized = false;

	/** Has environment been reset by Python? (indicates training is active) */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	bool bTrainingActive;

	/** Spawned trainers (for cleanup on EndPlay) */
	UPROPERTY()
	TArray<AMocTrainer*> SpawnedTrainers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	int32 ScholaEnvID;
};
