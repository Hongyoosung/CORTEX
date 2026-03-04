// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Team/TeamWorldState.h"
#include "Types/StrategyTypes.h"
#include "Types/EventTypes.h"
#include "Types/RewardTypes.h"
#include "Actors/CapturePoint.h"
#include "SquadManager.generated.h"


class UTeamMCTS;
class AMocCharacter;
class ATeamManager;
class AScholaEnvironment;
class UTeamDataCollector;
class ACapturePoint;

/**
 * USquadManager - MOC v10.2 Centralized Tactical Planner
 *
 * UObject (not Actor): owned and ticked by ATeamManager.
 * Two instances are created programmatically in ATeamManager::BeginPlay()
 * — one per team, no level placement required.
 *
 * All configuration (PlanningInterval, MCTSTimeBudget, WorldModelPath, etc.)
 * lives on ATeamManager as the single configuration point.
 *
 * Responsibilities:
 * - Collect global team state from all 5 friendly agents
 * - Run MCTS planning on team-level action space (Tactical Plays)
 * - Distribute role assignments to individual executor agents
 * - Trigger replanning on critical events (Kill/Death, objective capture)
 */
UCLASS()
class GAMEAI_PROJECT_API USquadManager : public UObject
{
	GENERATED_BODY()

public:
	USquadManager();

	/** UObject world context — delegates to TeamManager outer */
	virtual UWorld* GetWorld() const override;

	//========================================
	// Initialization
	//========================================

	/**
	 * Setup with team reference (called by ATeamManager::BeginPlay)
	 * @param InTeamID       - Team ID (0 = Red, 1 = Blue)
	 * @param InTeamManager  - Owning TeamManager (config + agent queries)
	 */
	void Initialize(int32 InTeamID, ATeamManager* InTeamManager);

	/**
	 * Bind capture point events for this environment's capture points.
	 * Called by ScholaEnvironment after initialization to scope events to owned CPs.
	 */
	void BindCapturePoints(const TArray<ACapturePoint*>& CapturePoints);

	/** Set owning ScholaEnvironment so match state is read from it, not AMocGameMode */
	void SetScholaEnvironment(AScholaEnvironment* InEnvironment);

	//========================================
	// Planner Tick (called by ATeamManager::Tick)
	//========================================

	void TickPlanner(float DeltaTime);

	//========================================
	// Centralized Planning
	//========================================

	/**
	 * Main planning entry point — run MCTS and update role assignments.
	 * Performance: ~15ms (MCTS time budget, configured on TeamManager).
	 * Triggers: Every PlanningInterval or on critical events.
	 */
	void PerformTacticalPlanning();

	/** Get current global team state */
	FTeamWorldState GetGlobalTeamState() const;

	/**
	 * Get assigned strategy for specific agent
	 * @param AgentIndex - Agent index in team (0-4)
	 */
	EStrategyType GetAgentStrategy(int32 AgentIndex) const;

	/** Get current active tactical play */
	ETacticalPlay GetActiveTacticalPlay() const { return ActiveTacticalPlay; }

	//========================================
	// Event-Driven Triggers
	//========================================

	/**
	 * Notify planner of critical event requiring immediate replanning.
	 * @param EventType       - Type of critical event
	 * @param InstigatorActor - Actor that triggered event (optional)
	 */
	void ReplanMCTSOnCriticalEvent(ECriticalEventType EventType, AActor* InstigatorActor);

	/** Check if replanning is needed based on timer */
	bool ShouldReplan() const;

	//========================================
	// Episode Management
	//========================================

	/** Reset planner state for new episode */
	void Reset();

	//========================================
	// Debug Accessors
	//========================================

	int32 GetTeamID()                const { return TeamID; }
	int32 GetPlanningCycleCount()     const { return PlanningCycleCount; }
	int32 GetEventDrivenReplanCount() const { return EventDrivenReplanCount; }
	float GetPlanConfidence()         const { return PlanConfidence; }
	const TArray<EStrategyType>& GetCurrentRoleAssignments() const { return CurrentRoleAssignments; }

protected:
	//========================================
	// Internal Implementation
	//========================================

	/** Sample a random tactical play and distribute roles (Phase 1 RL training) */
	void SampleRandomTacticalPlay();

	/**
	 * Returns the subset of all 10 tactical plays that are feasible given the current team state.
	 * A play is infeasible if it assigns a role whose precondition cannot be met:
	 *   - Defend: requires at least one friendly-owned capture point
	 *   - Support: requires at least two alive allies
	 * If all plays are filtered out, returns { StandardComp } as a safe fallback.
	 * Used by both Phase 1 (random/event sampling) and Phase 3 (MCTS/epsilon-greedy).
	 */
	TArray<ETacticalPlay> GetFeasiblePlays(const FTeamWorldState& State) const;

	/** Construct FTeamWorldState from live agents via TeamManager */
	FTeamWorldState CollectTeamState() const;

	/** Convert ETacticalPlay to role distribution (5 × EStrategyType) */
	TArray<EStrategyType> DecodeTacticalPlay(ETacticalPlay Play) const;

	/** Broadcast role assignments to agents via SetCommandedStrategy() */
	void DistributeRoles(const TArray<EStrategyType>& Roles);

	/** Log planning decision for analytics */
	void LogPlanningDecision(ETacticalPlay Play, float Confidence) const;

	/** Draw debug visualization for current tactical play */
	void DrawDebugVisualization() const;

	//========================================
	// Dependencies
	//========================================

	/** Team-level MCTS planner */
	UPROPERTY()
	UTeamMCTS* TeamMCTSPlanner;

	/** Team world model for MCTS predictions */
	UPROPERTY()
	class UTeamWorldModel* TeamWorldModel;

	/** Training data collector */
	UPROPERTY()
	UTeamDataCollector* DataCollector;

	/** Owning TeamManager — source of config and agent queries */
	UPROPERTY()
	ATeamManager* TeamManager;

	/** Owning ScholaEnvironment — source of match timer and capture points (replaces AMocGameMode) */
	UPROPERTY()
	AScholaEnvironment* OwningEnvironment = nullptr;

	//========================================
	// Runtime State
	//========================================

	int32 TeamID = 0;
	TArray<EStrategyType> CurrentRoleAssignments;
	float TimeSinceLastPlan = 0.0f;
	ETacticalPlay ActiveTacticalPlay = ETacticalPlay::StandardComp;
	float PlanConfidence = 0.5f;
	int32 PlanningCycleCount = 0;
	int32 EventDrivenReplanCount = 0;

	FTeamWorldState PreviousTeamState;
	ETacticalPlay PreviousTacticalPlay = ETacticalPlay::StandardComp;
	bool bHasPreviousState = false;

	bool bHealthCriticalTriggered = false;
	float LastPlanningDurationMs = 0.0f;
	float ValidationTickCounter = 0.0f;

	/** Scoped capture points for this environment (set via BindCapturePoints) */
	TArray<ACapturePoint*> CachedCapturePoints;

private:
	FCompositeReward CalculateTeamReward(const FTeamWorldState& OldState, const FTeamWorldState& NewState) const;

	ETacticalPlay SelectEpsilonGreedyAction(const FTeamWorldState& TeamState, const TArray<ETacticalPlay>& FeasiblePlays) const;

	//========================================
	// Critical Event Handlers
	//========================================

	UFUNCTION()
	void OnAgentKilledHandler(int32 VictimTeamID, int32 KillerTeamID, AMocCharacter* Victim);

	UFUNCTION()
	void OnPointCapturedHandler(ECapturePointID PointID, ECapturePointOwnership PreviousOwner, ECapturePointOwnership NewOwner);
};
