// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Types/DEStrategyTypes.h"
#include "Types/DEEventTypes.h"
#include "Types/DERewardTypes.h"
#include "Actors/DECapturePoint.h"
#include "DESquadManager.generated.h"


class ADEAgent;
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
 * UDESquadManager — Centralized Role Assigner
 *
 * UObject owned by ADEMatchManager.  Two instances are created
 * programmatically in ADEMatchManager::BeginPlay() — one per team.
 *
 * Responsibilities:
 *  - Assign roles to agents via round-robin per episode (Phase 1 RL)
 *  - Distribute role assignments to individual agents via SetCommandedStrategy()
 */
UCLASS()
class DE_API UDESquadManager : public UObject
{
	GENERATED_BODY()

public:
	UDESquadManager();

	//========================================
	// Initialization
	//========================================

	/**
	 * Basic setup — assign team ID.
	 * Must be called before Configure() or Reset().
	 * @param InTeamID  Team index (0 = Red, 1 = Blue)
	 */
	void Initialize(int32 InTeamID);

	/**
	 * Push planning configuration from ADEMatchManager / ADEScholaEnvironment.
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
	 * Periodic validation — verifies role count invariants.
	 * @param DeltaTime    Frame delta (seconds)
	 * @param TeamAgents   Current live agents for this team
	 * @param EnemyAgents  Current live enemy agents (unused, kept for call-site compat)
	 */
	void TickPlanner(float DeltaTime,
	                 const TArray<ADEAgent*>& TeamAgents,
	                 const TArray<ADEAgent*>& EnemyAgents);

	//========================================
	// Role Query
	//========================================

	/**
	 * Get the assigned strategy for a specific agent slot.
	 * @param AgentIndex  Slot index in the team (0-4)
	 */
	EDEStrategyType GetAgentStrategy(int32 AgentIndex) const;

	//========================================
	// Episode Management
	//========================================

	/**
	 * Reset planner state for a new episode and immediately assign strategies.
	 * Must be called before the first Schola observation of the new episode.
	 * @param TeamAgents  Initial agent list for the new episode
	 */
	void Reset(const TArray<ADEAgent*>& TeamAgents);


	//========================================
	// Debug Accessors
	//========================================

	FORCEINLINE int32 GetTeamID()   const { return TeamID; }
	FORCEINLINE const TArray<EDEStrategyType>& GetCurrentRoleAssignments() const { return CurrentRoleAssignments; }



protected:
	//========================================
	// Internal Implementation
	//========================================

	/** Sample a round-robin tactical play for Phase 1 RL training and push to agents */
	void SampleRandomTacticalPlay(const TArray<ADEAgent*>& TeamAgents);

	/** Assign roles to agents via SetCommandedStrategy() */
	void DistributeRoles(const TArray<EDEStrategyType>& Roles,
	                     const TArray<ADEAgent*>& TeamAgents) const;


	//========================================
	// Runtime State
	//========================================

	int32 TeamID = 0;

	/** Configuration pushed from ADEMatchManager / ADEScholaEnvironment */
	FDESquadConfig Config;

	TArray<EDEStrategyType> CurrentRoleAssignments;

	/** Incremented each episode; drives round-robin strategy rotation */
	int32 EpisodeCount = 0;

	float ValidationTickCounter = 0.0f;

	/** Capture points scoped to this environment (set via BindCapturePoints) */
	TArray<ADECapturePoint*> CachedCapturePoints;
};
