// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Types/EventTypes.h"
#include "Team/SquadManager.h"
#include "MatchManager.generated.h"

class AMocCharacter;
class UScholaMocAgent;
class ACapturePoint;
class USquadManager;
class UTeamData;
class ASpawnArea;

/**
 * Team configuration — data-driven via UTeamData asset.
 */
USTRUCT(BlueprintType)
struct FTeamConfiguration
{
	GENERATED_BODY()

	/** Team index (0 = Red, 1 = Blue) */
	UPROPERTY(BlueprintReadWrite)
	int32 TeamID = 0;

	/** Agent appearance and identity data asset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	TObjectPtr<UTeamData> TeamData = nullptr;

	ASpawnArea* SpawnArea = nullptr;

	FLinearColor GetTeamColor() const;
	void SetTeamColor(const FLinearColor& NewColor);
};

/**
 * Per-team runtime state (active and queued agents).
 */
USTRUCT(BlueprintType)
struct FTeamState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	int32 TeamID = 0;

	/** Agents currently alive and active */
	UPROPERTY(BlueprintReadWrite)
	TArray<AMocCharacter*> ActiveAgents;

	/** Agents awaiting group respawn */
	UPROPERTY(BlueprintReadWrite)
	TArray<AMocCharacter*> RespawnQueue;

	int32 CapturePointCount = 0;
};


/**
 * Delegates for match-level events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAgentSpawned,   int32, TeamID, AMocCharacter*, Agent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTeamScoreChanged, int32, TeamID, int32, NewScore);


/**
 * AMatchManager — Team Spawn & Respawn Authority
 *
 * Single responsibility: spawn, destroy, and respawn agents.
 * Does NOT own tactical planning — that is delegated to USquadManager
 * instances created and configured by AScholaEnvironment.
 *
 * Usage:
 *  1. Place one AMatchManager in the level per game environment.
 *  2. Assign TeamConfigs (TeamData + SpawnArea per team).
 *  3. AScholaEnvironment calls SetEnvID(), BindCapturePoints(),
 *     SpawnTeams(), and injects FSquadConfig into each USquadManager.
 */
UCLASS()
class GAMEAI_PROJECT_API AMatchManager : public AActor
{
	GENERATED_BODY()

public:
	AMatchManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	//========================================
	// Team Spawning
	//========================================

	/** Spawn both teams (uses AgentsPerTeam count) */
	UFUNCTION(BlueprintCallable, Category = "MatchManager")
	void SpawnTeams();

	/** Spawn a single team */
	UFUNCTION(BlueprintCallable, Category = "MatchManager")
	void SpawnTeam(int32 TeamID, int32 AgentCount);

	/** Reset all teams and squad planners for a new episode */
	UFUNCTION(BlueprintCallable, Category = "MatchManager|Episode")
	void ResetTeams();

	/** Destroy all spawned agents */
	UFUNCTION(BlueprintCallable, Category = "MatchManager")
	void DestroyAllAgents();

	//========================================
	// Respawn System
	//========================================

	/** Move a dead agent to the respawn queue */
	UFUNCTION(BlueprintCallable, Category = "MatchManager")
	void QueueRespawn(AMocCharacter* Agent, int32 TeamID);

	/** Returns an appropriate spawn location for the given team */
	UFUNCTION(BlueprintPure, Category = "MatchManager")
	FVector GetTeamSpawnLocation(int32 TeamID) const;

	//========================================
	// Team State Queries
	//========================================

	UFUNCTION(BlueprintPure, Category = "MatchManager")
	FTeamState GetTeamState(int32 TeamID) const;

	UFUNCTION(BlueprintPure, Category = "MatchManager")
	FTeamConfiguration GetTeamConfiguration(int32 TeamID) const;

	/** Live (active) agents for a team */
	UFUNCTION(BlueprintPure, Category = "MatchManager")
	TArray<AMocCharacter*> GetTeamAgents(int32 TeamID) const;

	/** All agents on the opposite team */
	UFUNCTION(BlueprintPure, Category = "MatchManager")
	TArray<AMocCharacter*> GetEnemyAgents(int32 TeamID) const;

	/** Total spawned agent count across both teams */
	UFUNCTION(BlueprintPure, Category = "MatchManager")
	int32 GetTotalAgentCount() const;

	//========================================
	// Kill / Score
	//========================================

	/** Called by agent death delegate; queues respawn and emits score event */
	UFUNCTION(BlueprintCallable, Category = "MatchManager")
	void RegisterKill(const FDeathEventData& DeathEvent);

	//========================================
	// CapturePoint Integration
	//========================================

	/** Set EnvID on owned capture points and bind the OnPointCaptured delegate */
	void CapturePointInitialize();

	void ResetCapturePoint();

	//========================================
	// Squad Commander Access
	//========================================

	/** Get the USquadManager for the given team */
	UFUNCTION(BlueprintPure, Category = "MatchManager")
	USquadManager* GetSquadCommander(int32 TeamID) const;

	//========================================
	// Environment Context (injected by AScholaEnvironment)
	//========================================

	FORCEINLINE void SetEnvID(int32 InEnvID)             { EnvID = InEnvID; }
	FORCEINLINE int32 GetEnvID() const                   { return EnvID; }

	FORCEINLINE void SetEnvRandomStream(const FRandomStream& InStream) { EnvRandomStream = InStream; }
	FORCEINLINE       FRandomStream& GetEnvRandomStream()              { return EnvRandomStream; }
	FORCEINLINE const FRandomStream& GetEnvRandomStream() const        { return EnvRandomStream; }

	void BindCapturePoints(const TArray<ACapturePoint*>& InPoints) { EnvCapturePoints = InPoints; }
	const TArray<ACapturePoint*>& GetCapturePoints() const         { return EnvCapturePoints; }

	void ResetEnvironment();

	//========================================
	// Event Callbacks
	//========================================

	void OnPointCaptured(int32 PreviousTeam, int32 NewTeam);

	//========================================
	// Events
	//========================================

	UPROPERTY(BlueprintAssignable, Category = "MatchManager|Events")
	FOnAgentSpawned OnAgentSpawned;

	UPROPERTY(BlueprintAssignable, Category = "MatchManager|Events")
	FOnTeamScoreChanged OnTeamScoreChanged;

public:
	//========================================
	// Configuration (set in editor)
	//========================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MatchManager|Teams")
	TArray<FTeamConfiguration> TeamConfigs;

	/** Number of agents to spawn per team */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MatchManager")
	int32 AgentsPerTeam = 5;

	/** Random scatter radius around SpawnArea origin */
	UPROPERTY(EditAnywhere, Category = "MatchManager|Spawn")
	float SpawnRadius = 300.0f;

	/** Seconds to wait before group respawn */
	UPROPERTY(EditAnywhere, Category = "MatchManager|Respawn")
	float RespawnDelay = 7.0f;

	/** Seconds of invincibility granted on respawn */
	UPROPERTY(EditAnywhere, Category = "MatchManager|Respawn")
	float RespawnInvincibilityDuration = 3.0f;

	UPROPERTY(EditAnywhere, Category = "MatchManager|Debug")
	bool bShowDebugInfo = false;

	//========================================
	// Squad Commander Configuration
	// Propagated to FSquadConfig by AScholaEnvironment.
	//========================================

	/** Seconds between MCTS replanning cycles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MatchManager|Squad")
	float PlanningInterval = 0.5f;

	/** MCTS time budget per cycle (seconds) */
	UPROPERTY(EditAnywhere, Category = "MatchManager|Squad")
	float MCTSTimeBudget = 0.015f;

	/** MCTS leaf expansion batch size */
	UPROPERTY(EditAnywhere, Category = "MatchManager|Squad")
	int32 MCTSBatchSize = 8;

	/** Path to the ONNX world model shared by both planners (empty = skip MCTS) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MatchManager|Squad")
	FString TeamWorldModelPath;

	/** Use epsilon-greedy instead of MCTS (faster data collection) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MatchManager|Squad")
	bool bDataCollectionMode = false;

	/** Epsilon for epsilon-greedy exploration (0 = exploit, 1 = explore) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MatchManager|Squad",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExplorationRate = 0.7f;

	/** Draw role labels above agents */
	UPROPERTY(EditAnywhere, Category = "MatchManager|Squad|Debug")
	bool bDrawRoleAssignments = true;

	/**
	 * Phase 1 RL Training Mode — strategy fixed for entire episode.
	 * AScholaEnvironment propagates bPhase1RLTraining here; planners read it via FSquadConfig.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MatchManager|Training")
	bool bRLTrainingMode = false;

	/** Convenience: build an FSquadConfig snapshot from current properties */
	FSquadConfig MakeSquadConfig() const;

	//========================================
	// Owned Objects
	//========================================

	UPROPERTY()
	TMap<int32, USquadManager*> SquadCommanders;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "MatchManager|Env")
	TArray<ACapturePoint*> EnvCapturePoints;

protected:
	//========================================
	// Internal Helpers
	//========================================

	/** Spawn a single agent and bind its death event */
	AMocCharacter* SpawnAgent(int32 TeamID, int32 AgentIndex);

	/** Advance respawn timers; teleport and reactivate agents when ready */
	void ProcessRespawnQueue(float DeltaTime);

	/** Random point within Radius units of BaseLocation (XY plane) */
	FVector GetRandomSpawnPoint(FVector BaseLocation, float Radius) const;

	//========================================
	// Runtime State
	//========================================

	UPROPERTY()
	TMap<int32, FTeamState> TeamStates;

	/** Isolated random stream injected by AScholaEnvironment */
	FRandomStream EnvRandomStream;

	/** Environment instance ID (for parallel env isolation) */
	int32 EnvID = 0;

	/** Group respawn countdown per team (-1 = inactive) */
	float TeamRespawnTimers[2] = { -1.0f, -1.0f };

	/** All spawned agents (for cleanup) */
	UPROPERTY()
	TArray<AMocCharacter*> AllAgents;
};
