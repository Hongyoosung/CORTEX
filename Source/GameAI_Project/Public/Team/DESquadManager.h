// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Types/DEStrategyTypes.h"
#include "Types/DEEventTypes.h"
#include "Types/DERewardTypes.h"
#include "Actors/DECapturePoint.h"
#include "DESquadManager.generated.h"


class ADECharacter;
class ADECapturePoint;

/**
 * Squad-level planning configuration.
 * Owned and populated by ADEMatchManager (or ADEScholaEnvironment).
 * Passed to UDESquadManager::Configure() after Initialize() — no back-reference needed.
 */
USTRUCT(BlueprintType)
struct FDESquadConfig
{
	GENERATED_BODY()

	/** Time budget for MCTS per planning cycle (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|MCTS")
	float MCTSTimeBudget = 0.015f;

	/** Planning interval (seconds) between replanning cycles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|MCTS")
	float PlanningInterval = 0.5f;

	/** Enable epsilon-greedy data collection instead of MCTS */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Training")
	bool bDataCollectionMode = false;

	/** Exploration rate for epsilon-greedy (0=exploit, 1=explore) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Training",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExplorationRate = 0.7f;

	/** Phase 1 RL: fix strategy per episode (sampled at reset) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Training")
	bool bRLTrainingMode = false;

	/** Draw role-assignment labels above agents */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Debug")
	bool bShowDebugInfo = false;

	/** Isolated random stream for reproducible planning decisions */
	FRandomStream RandomStream;
};


/**
 * UDESquadManager — Centralized Tactical Planner
 *
 * UObject owned by ADEMatchManager.  Two instances are created
 * programmatically in ADEMatchManager::BeginPlay() — one per team.
 *
 * Design (— no circular reference):
 *  - ADEMatchManager owns UDESquadManager (parent → child, one-way).
 *  - Configuration is pushed via Configure(FDESquadConfig).
 *  - Agent lists are passed as parameters at call sites; UDESquadManager
 *    never holds a raw pointer back to ADEMatchManager.
 *  - DEScholaEnvironment is the single orchestrator that propagates config
 *    from ADEMatchManager to each UDESquadManager after BeginPlay.
 *
 * Responsibilities:
 *  - Collect FDETeamWorldState from caller-supplied agent arrays
 *  - Run MCTS / epsilon-greedy to select a tactical play
 *  - Distribute role assignments to individual agents
 *  - Trigger replanning on critical events
 */
UCLASS()
class GAMEAI_PROJECT_API UDESquadManager : public UObject
{
	GENERATED_BODY()

public:
	UDESquadManager();

	//========================================
	// Initialization
	//========================================

	/**
	 * Basic setup — assign team ID.
	 * Must be called before Configure() or TickPlanner().
	 * @param InTeamID  Team index (0 = Red, 1 = Blue)
	 */
	void Initialize(int32 InTeamID);

	/**
	 * Push planning configuration from ADEMatchManager / ADEScholaEnvironment.
	 * Safe to call at any time; takes effect on the next planning cycle.
	 */
	void Configure(const FDESquadConfig& InConfig);

	/**
	 * Bind capture points scoped to this environment.
	 * Called by ADEScholaEnvironment after initialization.
	 */
	void BindCapturePoints(const TArray<ADECapturePoint*>& CapturePoints);

	//========================================
	// Planner Tick (called by ADEMatchManager::Tick)
	//========================================

	/**
	 * Advance the planner timer and trigger replanning when the interval elapses.
	 * @param DeltaTime    Frame delta (seconds)
	 * @param TeamAgents   Current live agents for this team (owned by DEMatchManager)
	 * @param EnemyAgents  Current live enemy agents (owned by DEMatchManager)
	 */
	void TickPlanner(float DeltaTime,
	                 const TArray<ADECharacter*>& TeamAgents,
	                 const TArray<ADECharacter*>& EnemyAgents);

	//========================================
	// Centralized Planning
	//========================================

	/**
	 * Main planning entry point — select tactical play and distribute roles.
	 * @param TeamAgents   Live agents on this team
	 * @param EnemyAgents  Live enemy agents
	 */
	void PerformTacticalPlanning(const TArray<ADECharacter*>& TeamAgents,
	                             const TArray<ADECharacter*>& EnemyAgents);


	/**
	 * Get the assigned strategy for a specific agent slot.
	 * @param AgentIndex  Slot index in the team (0-4)
	 */
	EDEStrategyType GetAgentStrategy(int32 AgentIndex) const;

	/** Current active tactical play */
	ETacticalPlay GetActiveTacticalPlay() const { return ActiveTacticalPlay; }

	//========================================
	// Event-Driven Triggers
	//========================================

	/**
	 * Trigger immediate replanning on a critical game event.
	 * @param EventType       Category of event
	 * @param InstigatorActor Actor that triggered the event (may be null)
	 * @param TeamAgents      Current team agents (for feasibility check)
	 * @param EnemyAgents     Current enemy agents
	 */
	void ReplanOnCriticalEvent(EDECriticalEventType EventType,
	                           AActor* InstigatorActor,
	                           const TArray<ADECharacter*>& TeamAgents,
	                           const TArray<ADECharacter*>& EnemyAgents);

	/** True if a replanning interval has elapsed */
	bool ShouldReplan() const;

	//========================================
	// Episode Management
	//========================================

	/**
	 * Reset planner state for a new episode.
	 * @param TeamAgents  Initial agent list for the new episode
	 */
	void Reset(const TArray<ADECharacter*>& TeamAgents);


	//========================================
	// Debug Accessors
	//========================================

	FORCEINLINE int32 GetTeamID()                const	{ return TeamID; }
	FORCEINLINE int32 GetPlanningCycleCount()     const { return PlanningCycleCount; }
	FORCEINLINE int32 GetEventDrivenReplanCount() const { return EventDrivenReplanCount; }
	FORCEINLINE float GetPlanConfidence()         const { return PlanConfidence; }
	FORCEINLINE const TArray<EDEStrategyType>& GetCurrentRoleAssignments() const { return CurrentRoleAssignments; }



private:

	ETacticalPlay SelectEpsilonGreedyAction(
		const TArray<ETacticalPlay>& FeasiblePlays) const;

	//========================================
	// Critical Event Handlers (bound to DECapturePoint delegates)
	//========================================

	UFUNCTION()
	void OnPointCapturedHandler(ECapturePointID PointID,
		int32 PreviousTeamID,
		int32 NewTeamID);



protected:
	//========================================
	// Internal Implementation
	//========================================

	/** Sample a round-robin tactical play for Phase 1 RL training */
	void SampleRandomTacticalPlay(const TArray<ADECharacter*>& TeamAgents);


	/** Convert ETacticalPlay to a 5-element role array */
	TArray<EDEStrategyType> DecodeTacticalPlay(ETacticalPlay Play) const;

	/** Assign roles to agents via SetCommandedStrategy() */
	void DistributeRoles(const TArray<EDEStrategyType>& Roles,
	                     const TArray<ADECharacter*>& TeamAgents) const;

	/** Log the current planning decision */
	void LogPlanningDecision(ETacticalPlay Play, float Confidence) const;

	/** Draw 3-D debug labels above agents */
	void DrawDebugVisualization(const TArray<ADECharacter*>& TeamAgents) const;


	//========================================
	// Runtime State
	//========================================

	int32 TeamID = 0;

	/** Configuration pushed from ADEMatchManager / ADEScholaEnvironment */
	FDESquadConfig Config;

	TArray<EDEStrategyType> CurrentRoleAssignments;
	float TimeSinceLastPlan       = 0.0f;
	ETacticalPlay ActiveTacticalPlay = ETacticalPlay::StandardComp;
	float PlanConfidence          = 0.5f;
	int32 PlanningCycleCount      = 0;
	int32 EventDrivenReplanCount  = 0;


	ETacticalPlay PreviousTacticalPlay = ETacticalPlay::StandardComp;
	bool bHasPreviousState        = false;

	bool bHealthCriticalTriggered = false;
	float LastPlanningDurationMs  = 0.0f;
	float ValidationTickCounter   = 0.0f;

	/** Incremented each episode; drives round-robin strategy rotation */
	int32 EpisodeCount = 0;

	/** Capture points scoped to this environment (set via BindCapturePoints) */
	TArray<ADECapturePoint*> CachedCapturePoints;
};
