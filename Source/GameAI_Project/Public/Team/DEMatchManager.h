// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Types/DEEventTypes.h"
#include "Team/DESquadManager.h"
#include "DEMatchManager.generated.h"

class ADECharacter;
class UDEScholaAgent;
class ADECapturePoint;
class UDESquadManager;
class UDETeamData;
class ADESpawnArea;
class UDERewardData;

/**
 * Team configuration — data-driven via UDETeamData asset.
 */
USTRUCT(BlueprintType)
struct FDETeamConfiguration
{
	GENERATED_BODY()

	/** Team index (0 = Red, 1 = Blue) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	int32 TeamID = 0;

	/** Agent appearance and identity data asset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	TObjectPtr<UDETeamData> TeamData = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	TObjectPtr<ADESpawnArea> DESpawnArea = nullptr;

	FLinearColor GetTeamColor() const;
	void SetTeamColor(const FLinearColor& NewColor);
};

/**
 * Per-team runtime state (active and queued agents).
 */
USTRUCT(BlueprintType)
struct FDETeamState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	int32 TeamID = 0;

	/** Agents currently alive and active */
	UPROPERTY(BlueprintReadWrite)
	TArray<ADECharacter*> ActiveAgents;

	/** Agents awaiting group respawn */
	UPROPERTY(BlueprintReadWrite)
	TArray<ADECharacter*> RespawnQueue;

	int32 CapturePointCount = 0;
};


/**
 * Delegates for match-level events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAgentSpawned,   int32, TeamID, ADECharacter*, Agent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTeamScoreChanged, int32, TeamID, int32, NewScore);


/**
 * ADEMatchManager — Team Spawn & Respawn Authority
 *
 * Single responsibility: spawn, destroy, and respawn agents.
 * Does NOT own tactical planning — that is delegated to UDESquadManager
 * instances created and configured by ADEScholaEnvironment.
 *
 * Usage:
 *  1. Place one ADEMatchManager in the level per game environment.
 *  2. Assign TeamConfigs (TeamData + DESpawnArea per team).
 *  3. ADEScholaEnvironment calls SetEnvID(), BindCapturePoints(),
 *     SpawnTeams(), and injects FDESquadConfig into each UDESquadManager.
 */
UCLASS()
class GAMEAI_PROJECT_API ADEMatchManager : public AActor
{
	GENERATED_BODY()

public:
	ADEMatchManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	//========================================
	// Team Spawning
	//========================================

	/** Spawn both teams (uses AgentsPerTeam count) */
	UFUNCTION(BlueprintCallable, Category = "DEMatchManager")
	void SpawnTeams();

	/** Spawn a single team */
	UFUNCTION(BlueprintCallable, Category = "DEMatchManager")
	void SpawnTeam(int32 TeamID, int32 AgentCount);

	/** Reset all teams and squad planners for a new episode */
	UFUNCTION(BlueprintCallable, Category = "DEMatchManager|Episode")
	void ResetTeams();

	/** Destroy all spawned agents */
	UFUNCTION(BlueprintCallable, Category = "DEMatchManager")
	void DestroyAllAgents();

	//========================================
	// Respawn System
	//========================================

	/** Move a dead agent to the respawn queue */
	UFUNCTION(BlueprintCallable, Category = "DEMatchManager")
	void QueueRespawn(ADECharacter* Agent, int32 TeamID);

	/** Returns an appropriate spawn location for the given team */
	UFUNCTION(BlueprintPure, Category = "DEMatchManager")
	FVector GetTeamSpawnLocation(int32 TeamID) const;

	//========================================
	// Team State Queries
	//========================================

	UFUNCTION(BlueprintPure, Category = "DEMatchManager")
	FDETeamState GetTeamState(int32 TeamID) const;

	UFUNCTION(BlueprintPure, Category = "DEMatchManager")
	FDETeamConfiguration GetTeamConfiguration(int32 TeamID) const;

	/** Live (active) agents for a team */
	UFUNCTION(BlueprintPure, Category = "DEMatchManager")
	TArray<ADECharacter*> GetTeamAgents(int32 TeamID) const;

	/** All agents on the opposite team */
	UFUNCTION(BlueprintPure, Category = "DEMatchManager")
	TArray<ADECharacter*> GetEnemyAgents(int32 TeamID) const;

	/** Total spawned agent count across both teams */
	UFUNCTION(BlueprintPure, Category = "DEMatchManager")
	int32 GetTotalAgentCount() const;


	//========================================
	// Kill / Score
	//========================================

	/** Called by agent death delegate; queues respawn and emits score event */
	UFUNCTION(BlueprintCallable, Category = "DEMatchManager")
	void RegisterKill(const FDEDeathEventData& DeathEvent);


	//========================================
	// Score Tracking
	//========================================

	/** Award score points to a team */
	void AddTeamScore(int32 TeamID, int32 Points);

	/** Get current score for a team */
	UFUNCTION(BlueprintPure, Category = "DEMatchManager|Score")
	int32 GetTeamScore(int32 TeamID) const;

	/** Returns the team ID of the leader, or -1 on tie */
	UFUNCTION(BlueprintPure, Category = "DEMatchManager|Score")
	int32 GetWinnerTeamID() const;

	/** Reset both team scores to 0 */
	void ResetScores();


	//========================================
	// DECapturePoint Integration
	//========================================

	/** Set EnvID on owned capture points and bind the OnPointCaptured delegate */
	void CapturePointInitialize();

	void ResetCapturePoint();


	//========================================
	// Squad Commander Access
	//========================================

	/** Get the UDESquadManager for the given team */
	UFUNCTION(BlueprintPure, Category = "DEMatchManager")
	UDESquadManager* GetSquadCommander(int32 TeamID) const;


	//========================================
	// Environment Context (injected by ADEScholaEnvironment)
	//========================================

	FORCEINLINE void SetEnvID(int32 InEnvID)             { EnvID = InEnvID; }
	FORCEINLINE int32 GetEnvID() const                   { return EnvID; }

	FORCEINLINE void SetEnvRandomStream(const FRandomStream& InStream) { EnvRandomStream = InStream; }
	FORCEINLINE       FRandomStream& GetEnvRandomStream()              { return EnvRandomStream; }
	FORCEINLINE const FRandomStream& GetEnvRandomStream() const        { return EnvRandomStream; }

	void BindCapturePoints(const TArray<ADECapturePoint*>& InPoints) { EnvCapturePoints = InPoints; }
	const TArray<ADECapturePoint*>& GetCapturePoints() const         { return EnvCapturePoints; }

	void ResetEnvironment();


	//========================================
	// Event Callbacks
	//========================================

	UFUNCTION()
	void OnPointCaptured(int32 PreviousTeam, int32 NewTeam);

	UFUNCTION()
	void OnAgentDied(ADECharacter* DeadAgent, ADECharacter* Killer);


	//========================================
	// Events
	//========================================

	UPROPERTY(BlueprintAssignable, Category = "DEMatchManager|Events")
	FOnAgentSpawned OnAgentSpawned;

	UPROPERTY(BlueprintAssignable, Category = "DEMatchManager|Events")
	FOnTeamScoreChanged OnTeamScoreChanged;



public:
	//========================================
	// Configuration (set in editor)
	//========================================

	/** Reward configuration shared by all agents in this environment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEMatchManager|Rewards")
	UDERewardData* RewardData;

	/** Score points awarded to the killing team per kill */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEMatchManager|Score")
	int32 KillScorePoints = 5;

	/** Score points awarded to the capturing team per capture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEMatchManager|Score")
	int32 CaptureScorePoints = 30;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEMatchManager|Teams")
	TArray<FDETeamConfiguration> TeamConfigs;

	/** Number of agents to spawn per team */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEMatchManager")
	int32 AgentsPerTeam = 5;

	/** Random scatter radius around DESpawnArea origin */
	UPROPERTY(EditAnywhere, Category = "DEMatchManager|Spawn")
	float SpawnRadius = 300.0f;

	/** Seconds to wait before group respawn */
	UPROPERTY(EditAnywhere, Category = "DEMatchManager|Respawn")
	float RespawnDelay = 7.0f;

	/** Seconds of invincibility granted on respawn */
	UPROPERTY(EditAnywhere, Category = "DEMatchManager|Respawn")
	float RespawnInvincibilityDuration = 3.0f;

	UPROPERTY(EditAnywhere, Category = "DEMatchManager|Debug")
	bool bShowDebugInfo = false;


	//========================================
	// Squad Commander Configuration
	// Propagated to FDESquadConfig by ADEScholaEnvironment.
	//========================================

	/** Seconds between MCTS replanning cycles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEMatchManager|Squad")
	float PlanningInterval = 0.5f;

	/** MCTS time budget per cycle (seconds) */
	UPROPERTY(EditAnywhere, Category = "DEMatchManager|Squad")
	float MCTSTimeBudget = 0.015f;

	/** MCTS leaf expansion batch size */
	UPROPERTY(EditAnywhere, Category = "DEMatchManager|Squad")
	int32 MCTSBatchSize = 8;

	/** Path to the ONNX world model shared by both planners (empty = skip MCTS) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEMatchManager|Squad")
	FString TeamWorldModelPath;

	/**
	 * Standalone / Inference mode — no ADEScholaEnvironment in the level.
	 * When true, BeginPlay() calls CapturePointInitialize() automatically
	 * so capture points are bound without Schola setup.
	 * Assign EnvCapturePoints manually in the editor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEMatchManager|Standalone")
	bool bStandaloneMode = false;

	/** Use epsilon-greedy instead of MCTS (faster data collection) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEMatchManager|Squad")
	bool bDataCollectionMode = false;

	/** Epsilon for epsilon-greedy exploration (0 = exploit, 1 = explore) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEMatchManager|Squad",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExplorationRate = 0.7f;

	/** Draw role labels above agents */
	UPROPERTY(EditAnywhere, Category = "DEMatchManager|Squad|Debug")
	bool bDrawRoleAssignments = true;

	/**
	 * Phase 1 RL Training Mode — strategy fixed for entire episode.
	 * ADEScholaEnvironment propagates bPhase1RLTraining here; planners read it via FDESquadConfig.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEMatchManager|Training")
	bool bRLTrainingMode = false;

	/** Convenience: build an FDESquadConfig snapshot from current properties */
	FDESquadConfig MakeSquadConfig() const;


	//========================================
	// Owned Objects
	//========================================

	UPROPERTY()
	TMap<int32, UDESquadManager*> SquadCommanders;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "DEMatchManager|Env")
	TArray<ADECapturePoint*> EnvCapturePoints;



protected:
	//========================================
	// Internal Helpers
	//========================================

	/** Spawn a single agent and bind its death event */
	ADECharacter* SpawnAgent(int32 TeamID, int32 AgentIndex);

	/** Advance respawn timers; teleport and reactivate agents when ready */
	void ProcessRespawnQueue(float DeltaTime);

	/** Random point within Radius units of BaseLocation (XY plane) */
	FVector GetRandomSpawnPoint(FVector BaseLocation, float Radius) const;


	//========================================
	// Runtime State
	//========================================

	UPROPERTY()
	TMap<int32, FDETeamState> TeamStates;

	/** Isolated random stream injected by ADEScholaEnvironment */
	FRandomStream EnvRandomStream;

	/** Environment instance ID (for parallel env isolation) */
	int32 EnvID = 0;

	/** Group respawn countdown per team (-1 = inactive) */
	float TeamRespawnTimers[2] = { -1.0f, -1.0f };

	/** All spawned agents (for cleanup) */
	UPROPERTY()
	TArray<ADECharacter*> AllAgents;

	/** Per-team cumulative scores (index = TeamID) */
	int32 TeamScores[2] = {0, 0};
};
