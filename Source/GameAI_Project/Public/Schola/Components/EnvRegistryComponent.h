// EnvRegistryComponent.h - Manages team and objective registration for Schola environments

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnvRegistryComponent.generated.h"


class ASimulationManagerGameMode;
class AObjectiveActor;


USTRUCT()
struct FEnemyTeamList
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> EnemyTeamIDs;
};



/** //==========================================================================
 * Environment Registry Component
 *
 * Responsibilities:
 * - Register and manage teams within this environment
 * - Register and manage ObjectiveActors
 * - Establish adversarial relationships between teams
 * - Provide team/objective queries
 *
 * Single Responsibility: Team and Objective Registration
 */ //==========================================================================
UCLASS(ClassGroup=(Schola), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UEnvRegistryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnvRegistryComponent();

	//==========================================================================
	// TEAM REGISTRATION
	//==========================================================================

	/**
	 * Register a team to this environment
	 * Called by LeaderCharacter through SimulationManagerGameMode
	 * @param TeamID - Team ID to register
	 * @param TeamLeader - Team leader component
	 * @return true if registration succeeded
	 */
	bool RegisterTeam(int32 TeamID);

	/**
	 * Get all registered team IDs for this environment
	 * @return Array of team IDs managed by this environment
	 */
	TArray<int32> GetRegisteredTeams() const { return RegisteredTeamIDs; }

	/**
	 * Check if a team is registered to this environment
	 * @param TeamID - Team ID to check
	 * @return true if team is registered
	 */
	bool IsTeamRegistered(int32 TeamID) const;



	//==========================================================================
	// OBJECTIVE REGISTRATION
	//==========================================================================

	/**
	 * Register an ObjectiveActor to this environment
	 * Called by ObjectiveActor through SimulationManagerGameMode
	 * @param Objective - Objective actor to register
	 */
	void RegisterObjectiveActor(AObjectiveActor* Objective);

	/**
	 * Get all registered objective actors
	 * @return Array of objective actors
	 */
	TArray<AObjectiveActor*> GetRegisteredObjectives() const { return RegisteredObjectives; }

	/**
	 * Get friendly objective for a team
	 * @param TeamID - Team ID to query
	 * @return Friendly objective actor (nullptr if not found)
	 */
	AObjectiveActor* GetFriendlyObjective(int32 TeamID) const;

	/**
	 * Get hostile objective for a team
	 * @param TeamID - Team ID to query
	 * @return Hostile objective actor (nullptr if not found)
	 */
	AObjectiveActor* GetHostileObjective(int32 TeamID) const;



	//==========================================================================
	// ADVERSARIAL RELATIONSHIPS
	//==========================================================================

	/**
	 * Establish mutual adversarial relationships between registered teams
	 * Creates a table that sets registered team IDs as adversaries
	 * Supports 1:1 and 1:1:1 relationships
	 */
	void SetMutual();

	/**
	 * Get enemy team IDs for a given team
	 * @param TeamID - Team ID to query
	 * @return Array of enemy team IDs
	 */
	TArray<int32> GetEnemyTeamIDs(int32 TeamID) const;



	//==========================================================================
	// INITIALIZATION
	//==========================================================================

	/**
	 * Initialize component with SimulationManager reference
	 * @param Manager - SimulationManagerGameMode reference
	 */
	void Initialize(ASimulationManagerGameMode* Manager);

	/**
	 * Get environment ID
	 * @return Environment ID assigned by Schola
	 */
	int32 GetEnvironmentID() const { return EnvironmentID; }

	/**
	 * Set environment ID
	 * @param EnvID - Environment ID to set
	 */
	void SetEnvironmentID(int32 EnvID) { EnvironmentID = EnvID; }




public:
	//==========================================================================
	// EDITOR CONFIGURATION
	//==========================================================================

	/**
	 * Team IDs to be managed by this environment
	 * Example for 4-actor setup (32 agents, 8 teams):
	 *   - Actor 0: [0, 1] → Env 0 (Teams 0,1 = 4v4)
	 *   - Actor 1: [2, 3] → Env 1 (Teams 2,3 = 4v4)
	 *   - Actor 2: [4, 5] → Env 2 (Teams 4,5 = 4v4)
	 *   - Actor 3: [6, 7] → Env 3 (Teams 6,7 = 4v4)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	TArray<int32> TeamIDs;



private:
	/** Reference to SimulationManagerGameMode */
	UPROPERTY()
	ASimulationManagerGameMode* SimulationManager = nullptr;

	/** List of team IDs managed by this environment */
	UPROPERTY()
	TArray<int32> RegisteredTeamIDs;

	/** Registered objective actors */
	UPROPERTY()
	TArray<AObjectiveActor*> RegisteredObjectives;

	/** Adversarial relationship table: TeamID -> Enemy TeamIDs */
	UPROPERTY()
	TMap<int32, FEnemyTeamList> AdversarialTable;

	/** Environment ID assigned by Schola */
	int32 EnvironmentID = -1;
};