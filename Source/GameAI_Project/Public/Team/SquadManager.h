// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Team/TeamState.h"
#include "Types/MocTypes.h"
#include "SquadManager.generated.h"

// Forward declarations
class UMocAgentWorldModel;
class UTeamMCTS;
class AMocCharacter;
class ATeamManager;

/**
 * ASquadManager - MOC v10.2 Centralized Tactical Commander
 *
 * Core Innovation of v10.2 Architecture:
 * Instead of each agent running independent MCTS, a single Squad Commander
 * performs centralized planning and distributes role assignments to executors.
 *
 * Responsibilities:
 * - Collect global team state from all 5 friendly agents
 * - Run MCTS planning on team-level action space (Tactical Plays)
 * - Distribute role assignments to individual executor agents
 * - Trigger replanning on critical events (Kill/Death, objective capture)
 *
 * Performance Budget:
 * - MCTS: 15ms per planning cycle
 * - Frequency: Every 0.5s or on high-volatility events
 * - Batch inference: Process 8-16 leaves simultaneously
 *
 * Usage:
 * 1. Spawned by ATeamManager at match start (one per team)
 * 2. Initialize() called with TeamID and TeamManager reference
 * 3. Tick() manages planning intervals and event-driven replanning
 * 4. Agents call GetAgentStrategy() to receive role assignments
 */
UCLASS()
class GAMEAI_PROJECT_API ASquadManager : public AActor
{
	GENERATED_BODY()

public:
	ASquadManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	//========================================
	// Initialization
	//========================================

	/**
	 * Setup with team reference (called by TeamManager)
	 * @param InTeamID - Team ID (0 = Red, 1 = Blue)
	 * @param InTeamManager - Reference to TeamManager for agent queries
	 */
	UFUNCTION(BlueprintCallable, Category = "SquadManager")
	void Initialize(int32 InTeamID, ATeamManager* InTeamManager);

	//========================================
	// Centralized Planning
	//========================================

	/**
	 * Main planning entry point - run MCTS and update role assignments
	 * Performance: ~15ms (MCTS time budget)
	 * Triggers: Every 0.5s or on critical events
	 */
	UFUNCTION(BlueprintCallable, Category = "SquadManager")
	void PerformTacticalPlanning();

	/**
	 * Get current global team state
	 * Collects positions, health, strategies from all 5 agents
	 */
	UFUNCTION(BlueprintPure, Category = "SquadManager")
	FTeamState GetGlobalTeamState() const;

	/**
	 * Get assigned strategy for specific agent
	 * @param AgentIndex - Agent index in team (0-4)
	 * @return Strategy assigned by last planning cycle
	 */
	UFUNCTION(BlueprintPure, Category = "SquadManager")
	EStrategyType GetAgentStrategy(int32 AgentIndex) const;

	/**
	 * Get current active tactical play
	 */
	UFUNCTION(BlueprintPure, Category = "SquadManager")
	ETacticalPlay GetActiveTacticalPlay() const { return ActiveTacticalPlay; }

	//========================================
	// Event-Driven Triggers
	//========================================

	/**
	 * Notify commander of critical event requiring immediate replanning
	 * Examples: Ally killed, objective captured, team health critical
	 *
	 * @param EventType - Type of critical event
	 * @param Instigator - Actor that triggered event (optional)
	 */
	UFUNCTION(BlueprintCallable, Category = "SquadManager")
	void ReplanMCTSOnCriticalEvent(ECriticalEventType EventType, AActor* InstigatorActor);

	/**
	 * Check if replanning is needed based on timer or volatility
	 * Conditions: TimeSinceLastPlan >= PlanningInterval
	 */
	UFUNCTION(BlueprintPure, Category = "SquadManager")
	bool ShouldReplan() const;

	//========================================
	// Configuration
	//========================================

	/** Planning interval (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SquadManager|Config")
	float PlanningInterval = 0.5f;

	/** Time budget for MCTS (seconds) */
	UPROPERTY(EditAnywhere, Category = "SquadManager|Config")
	float MCTSTimeBudget = 0.015f;

	/** Batch size for MCTS leaf expansion */
	UPROPERTY(EditAnywhere, Category = "SquadManager|Config")
	int32 MCTSBatchSize = 8;

	/** Enable debug visualization */
	UPROPERTY(EditAnywhere, Category = "SquadManager|Debug")
	bool bShowDebugInfo = true;

	/** Draw debug lines for role assignments */
	UPROPERTY(EditAnywhere, Category = "SquadManager|Debug")
	bool bDrawRoleAssignments = true;

protected:
	//========================================
	// Internal Implementation
	//========================================

	/**
	 * Construct FTeamState from live agents
	 * Queries TeamManager for friendly/enemy positions, health, etc.
	 */
	FTeamState CollectTeamState() const;

	/**
	 * Convert MCTS result (ETacticalPlay) to role distribution
	 * Example: Phalanx → [Defend, Defend, Support, Support, Support]
	 *
	 * @param Play - Selected tactical play from MCTS
	 * @return Array of 5 strategies (one per agent)
	 */
	TArray<EStrategyType> DecodeTacticalPlay(ETacticalPlay Play) const;

	/**
	 * Broadcast role assignments to agents
	 * Calls SetCommandedStrategy() on each AMocCharacter
	 *
	 * @param Roles - Array of 5 strategies to distribute
	 */
	void DistributeRoles(const TArray<EStrategyType>& Roles);

	/**
	 * Log planning decision for analytics
	 * Output: Timestamp, TacticalPlay, Confidence, TeamState
	 */
	void LogPlanningDecision(ETacticalPlay Play, float Confidence) const;

	/**
	 * Draw debug visualization for current tactical play
	 */
	void DrawDebugVisualization() const;

	//========================================
	// Dependencies
	//========================================

	/** Team-level MCTS planner (centralized, v10.2) */
	UPROPERTY()
	UTeamMCTS* TeamMCTSPlanner;

	/** Team-level world model for centralized planning (v10.2) */
	UPROPERTY()
	class UMocTeamWorldModel* TeamWorldModel;

	/** Individual agent world model (wrapped by MocTeamWorldModel) */
	UPROPERTY()
	UMocAgentWorldModel* AgentWorldModel;

	/** Reference to TeamManager for agent queries */
	UPROPERTY()
	ATeamManager* TeamManager;

	//========================================
	// Runtime State
	//========================================

	/** Team ID this commander manages (0 = Red, 1 = Blue) */
	UPROPERTY(BlueprintReadOnly, Category = "SquadManager|State")
	int32 TeamID;

	/**
	 * Current role assignments [5]
	 * Index corresponds to agent index in TeamManager
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SquadManager|State")
	TArray<EStrategyType> CurrentRoleAssignments;

	/** Time since last planning cycle (seconds) */
	UPROPERTY(BlueprintReadOnly, Category = "SquadManager|State")
	float TimeSinceLastPlan;

	/** Current active tactical play */
	UPROPERTY(BlueprintReadOnly, Category = "SquadManager|State")
	ETacticalPlay ActiveTacticalPlay;

	/** Confidence score of current plan [0-1] */
	UPROPERTY(BlueprintReadOnly, Category = "SquadManager|State")
	float PlanConfidence;

	/** Number of planning cycles executed */
	UPROPERTY(BlueprintReadOnly, Category = "SquadManager|Stats")
	int32 PlanningCycleCount;

	/** Number of event-driven replans triggered */
	UPROPERTY(BlueprintReadOnly, Category = "SquadManager|Stats")
	int32 EventDrivenReplanCount;
};
