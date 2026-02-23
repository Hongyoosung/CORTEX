// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TeamManager.generated.h"

class AMocCharacter;
class UScholaMocAgent;
class ACapturePoint;
class APickupBase;
class AFogOfWarManager;
class USquadManager;
class UTeamData;

/**
 * Team configuration for customization (v10.2: Now data-driven)
 *
 * Previously stored individual properties (mesh, materials, colors).
 * Now uses UTeamData for centralized, reusable configuration.
 *
 * Migration: Set AppearanceData asset instead of individual properties.
 */
USTRUCT(BlueprintType)
struct FTeamConfiguration
{
	GENERATED_BODY()

	/** Agent appearance data asset (contains mesh, materials, animations, team identity) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	TObjectPtr<UTeamData> TeamData = nullptr;

	FLinearColor GetTeamColor() const;
};

/**
 * Team state tracking for shared knowledge
 */
USTRUCT(BlueprintType)
struct FTeamState
{
	GENERATED_BODY()

	/** Team ID (0 = Red, 1 = Blue) */
	UPROPERTY(BlueprintReadWrite)
	int32 TeamID = 0;

	/** Team color */
	UPROPERTY(BlueprintReadWrite)
	FLinearColor TeamColor = FLinearColor::Red;

	/** Team score */
	UPROPERTY(BlueprintReadWrite)
	int32 Score = 0;

	/** Active agents on this team */
	UPROPERTY(BlueprintReadWrite)
	TArray<AMocCharacter*> ActiveAgents;

	/** Agents waiting to respawn */
	UPROPERTY(BlueprintReadWrite)
	TArray<AMocCharacter*> RespawnQueue;
};

/**
 * Delegate for team events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAgentSpawned, int32, TeamID, AMocCharacter*, Agent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAgentKilled, int32, VictimTeamID, int32, KillerTeamID, AMocCharacter*, Victim);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTeamScoreChanged, int32, TeamID, int32, NewScore);

/**
 * ATeamManager - MOC v10.1 Team Management System
 *
 * Responsibilities:
 * - Spawn and manage 5v5 team composition
 * - Handle agent respawning (5-second delay)
 * - Track team scores and match state
 * - Coordinate with FogOfWarManager for team knowledge
 * - Coordinate team-level rewards and events
 *
 * Usage:
 * 1. Place in level (one per game mode)
 * 2. Set spawn locations for Red and Blue teams
 * 3. Assign CharacterClass (AMocCharacter)
 * 4. Game Mode calls SpawnTeams() at match start
 */
UCLASS()
class GAMEAI_PROJECT_API ATeamManager : public AActor
{
	GENERATED_BODY()

public:
	ATeamManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	//========================================
	// Team Spawning
	//========================================

	/** Spawn both teams (5 agents each) */
	UFUNCTION(BlueprintCallable, Category = "TeamManager")
	void SpawnTeams();

	/** Spawn a single team */
	UFUNCTION(BlueprintCallable, Category = "TeamManager")
	void SpawnTeam(int32 TeamID, int32 AgentCount);

	/** Reset team state for new episode */
	UFUNCTION(BlueprintCallable, Category = "TeamManager|Episode")
	void ResetTeams();

	/** Destroy all agents (for cleanup) */
	UFUNCTION(BlueprintCallable, Category = "TeamManager")
	void DestroyAllAgents();

	//========================================
	// Respawn System
	//========================================

	/** Queue agent for respawn after death */
	UFUNCTION(BlueprintCallable, Category = "TeamManager")
	void QueueRespawn(AMocCharacter* Agent, int32 TeamID);

	/** Get spawn location for team */
	UFUNCTION(BlueprintPure, Category = "TeamManager")
	FVector GetTeamSpawnLocation(int32 TeamID) const;

	//========================================
	// Team State Access
	//========================================

	/** Get team state (for observation collection) */
	UFUNCTION(BlueprintPure, Category = "TeamManager")
	FTeamState GetTeamState(int32 TeamID) const;

	/** Get team configuration */
	UFUNCTION(BlueprintPure, Category = "TeamManager")
	FTeamConfiguration GetTeamConfiguration(int32 TeamID) const;

	/** Get all agents on team */
	UFUNCTION(BlueprintPure, Category = "TeamManager")
	TArray<AMocCharacter*> GetTeamAgents(int32 TeamID) const;

	/** Get enemy team agents */
	UFUNCTION(BlueprintPure, Category = "TeamManager")
	TArray<AMocCharacter*> GetEnemyAgents(int32 TeamID) const;

	/** Get team score */
	UFUNCTION(BlueprintPure, Category = "TeamManager")
	int32 GetTeamScore(int32 TeamID) const;

	/** Get total number of agents (both teams) */
	UFUNCTION(BlueprintPure, Category = "TeamManager")
	int32 GetTotalAgentCount() const;

	//========================================
	// Scoring System
	//========================================

	/** Add score to team */
	UFUNCTION(BlueprintCallable, Category = "TeamManager")
	void AddTeamScore(int32 TeamID, int32 Amount);

	/** Subtract score from team */
	UFUNCTION(BlueprintCallable, Category = "TeamManager")
	void SubtractTeamScore(int32 TeamID, int32 Amount);

	/** Register kill event (updates scores) */
	UFUNCTION(BlueprintCallable, Category = "TeamManager")
	void RegisterKill(int32 KillerTeamID, int32 VictimTeamID, AMocCharacter* Victim);

	//========================================
	// Fog of War Integration (Delegates to FogOfWarManager)
	//========================================

	/** Report enemy sighting to team (delegates to FogOfWarManager) */
	UFUNCTION(BlueprintCallable, Category = "TeamManager")
	void ReportEnemySighting(int32 ReportingTeamID, AActor* Enemy, FVector Location);

	/** Report resource discovery to team (delegates to FogOfWarManager) */
	UFUNCTION(BlueprintCallable, Category = "TeamManager")
	void ReportResourceDiscovery(int32 TeamID, APickupBase* Resource);

	/** Get last known enemy position (delegates to FogOfWarManager) */
	UFUNCTION(BlueprintPure, Category = "TeamManager")
	FVector GetLastKnownEnemyPosition(int32 TeamID, AActor* Enemy) const;

	/** Check if enemy position is still valid (delegates to FogOfWarManager) */
	UFUNCTION(BlueprintPure, Category = "TeamManager")
	bool IsEnemyPositionValid(int32 TeamID, AActor* Enemy) const;

	/** Get FogOfWarManager reference */
	UFUNCTION(BlueprintPure, Category = "TeamManager")
	AFogOfWarManager* GetFogOfWarManager() const { return FogOfWarManager; }

	//========================================
	// v10.2 Squad Commander Access
	//========================================

	/** Get Squad Planner for team (v10.2 centralized planning) */
	UFUNCTION(BlueprintPure, Category = "TeamManager")
	USquadManager* GetSquadCommander(int32 TeamID) const;

protected:
	//========================================
	// Internal Helpers
	//========================================

	/** Spawn single agent */
	AMocCharacter* SpawnAgent(int32 TeamID, int32 AgentIndex);

	/** Process respawn queue */
	void ProcessRespawnQueue(float DeltaTime);

	/** Get random spawn point within radius */
	FVector GetRandomSpawnPoint(FVector BaseLocation, float Radius) const;

public:
	//========================================
	// Configuration
	//========================================

	/** Character class to spawn (AMocCharacter) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamManager")
	TSubclassOf<AMocCharacter> CharacterClass;

	/** Red team configuration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamManager|Teams")
	FTeamConfiguration RedTeamConfig;

	/** Blue team configuration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamManager|Teams")
	FTeamConfiguration BlueTeamConfig;

	/** Fog of War Manager reference (set in level or spawned in BeginPlay) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamManager|References")
	TSubclassOf<AFogOfWarManager> FogOfWarManagerClass;

	/** Number of agents per team */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamManager")
	int32 AgentsPerTeam = 5;

	/** Red team spawn location */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamManager|Spawn")
	FVector RedTeamSpawnLocation = FVector(-5000.0f, 0.0f, 100.0f);

	/** Blue team spawn location */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamManager|Spawn")
	FVector BlueTeamSpawnLocation = FVector(5000.0f, 0.0f, 100.0f);

	/** Spawn radius (random offset from base location) */
	UPROPERTY(EditAnywhere, Category = "TeamManager|Spawn")
	float SpawnRadius = 300.0f;

	/** Respawn delay after death (seconds) */
	UPROPERTY(EditAnywhere, Category = "TeamManager|Respawn")
	float RespawnDelay = 5.0f;
	

	/** Show debug visualization */
	UPROPERTY(EditAnywhere, Category = "TeamManager|Debug")
	bool bShowDebugInfo = false;

	//========================================
	// v10.2 Squad Planner Configuration
	// One USquadManager is created per team automatically in BeginPlay.
	// All MCTS and data-collection settings are configured here.
	//========================================

	/** Planning interval (seconds) between MCTS replanning cycles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamManager|Squad")
	float PlanningInterval = 0.5f;

	/** Time budget for MCTS per planning cycle (seconds) */
	UPROPERTY(EditAnywhere, Category = "TeamManager|Squad")
	float MCTSTimeBudget = 0.015f;

	/** Batch size for MCTS leaf expansion */
	UPROPERTY(EditAnywhere, Category = "TeamManager|Squad")
	int32 MCTSBatchSize = 8;

	/** Path to team world model ONNX file (shared by both teams; empty = skip MCTS) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamManager|Squad")
	FString TeamWorldModelPath;

	/** Enable data collection mode (ε-greedy instead of MCTS, for faster data collection) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamManager|Squad")
	bool bDataCollectionMode = false;

	/** Exploration rate for ε-greedy policy (0=pure exploitation, 1=pure exploration) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamManager|Squad", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExplorationRate = 0.7f;

	/** Draw role assignment labels above agents */
	UPROPERTY(EditAnywhere, Category = "TeamManager|Squad|Debug")
	bool bDrawRoleAssignments = true;

	/**
	 * Phase 1 RL Training Mode: fix strategy for entire episode (sampled at reset).
	 * Single source of truth — ScholaEnvironment propagates bPhase1RLTraining here.
	 * USquadManager instances read it via their TeamManager pointer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamManager|Training")
	bool bRLTrainingMode = false;

	//========================================
	// Events
	//========================================

	UPROPERTY(BlueprintAssignable, Category = "TeamManager|Events")
	FOnAgentSpawned OnAgentSpawned;

	UPROPERTY(BlueprintAssignable, Category = "TeamManager|Events")
	FOnAgentKilled OnAgentKilled;

	UPROPERTY(BlueprintAssignable, Category = "TeamManager|Events")
	FOnTeamScoreChanged OnTeamScoreChanged;

protected:
	//========================================
	// Runtime State
	//========================================

	TObjectPtr<AFogOfWarManager> FogOfWarManager;

	/** Red team state */
	UPROPERTY(BlueprintReadOnly, Category = "TeamManager|State")
	FTeamState RedTeamState;

	/** Blue team state */
	UPROPERTY(BlueprintReadOnly, Category = "TeamManager|State")
	FTeamState BlueTeamState;

	/** Per-team group respawn timers (-1 = inactive) */
	float TeamRespawnTimers[2] = { -1.0f, -1.0f };

	/** All spawned agents (for cleanup) */
	UPROPERTY()
	TArray<AMocCharacter*> AllAgents;

	/** Squad Planners — created automatically in BeginPlay, one per team */
	UPROPERTY()
	USquadManager* RedTeamCommander;

	UPROPERTY()
	USquadManager* BlueTeamCommander;

};
