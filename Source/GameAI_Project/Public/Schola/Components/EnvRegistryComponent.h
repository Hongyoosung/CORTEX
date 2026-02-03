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
	// REGISTRATION
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
	* Unregister a team from this environment
	* @param TeamID - Team ID to unregister
	 */
	bool UnRegisterTeam(int32 TeamID);


	/**
	 * Register an ObjectiveActor to this environment
	 * Called by ObjectiveActor through SimulationManagerGameMode
	 * @param Objective - Objective actor to register
	 */
	bool RegisterObjectiveActor(AObjectiveActor* Objective);

	/**
	 * Unregister an ObjectiveActor from this environment
	 * @param Objective - Objective actor to unregister
	 */
	bool UnRegisterObjectiveActor(AObjectiveActor* Objective);





	//==========================================================================
	// GETTERS
	//==========================================================================

	FORCEINLINE TArray<int32> GetRegisteredTeams() const { return RegisteredTeamIDs; }
	FORCEINLINE TArray<AObjectiveActor*> GetRegisteredObjectives() const { return RegisteredObjectives; }

	/**
	 * Get friendly objective for a team
	 * @param TeamID - Team ID to query
	 * @return Friendly objective actor (nullptr if not found)
	 */
	AObjectiveActor*	GetFriendlyObjective(int32 TeamID) const;

	/**
	 * Get hostile objective for a team
	 * @param TeamID - Team ID to query
	 * @return Hostile objective actor (nullptr if not found)
	 */
	AObjectiveActor*	GetHostileObjective(int32 TeamID) const;

	/**
	 * Get enemy team IDs for a given team
	 * @param TeamID - Team ID to query
	 * @return Array of enemy team IDs
	 */
	TArray<int32>		GetEnemyTeamIDs(int32 TeamID) const;



	//==========================================================================
	// VALIDATION
	//==========================================================================

	/**
	 * Check if a team is registered to this environment
	 * @param TeamID - Team ID to check
	 * @return true if team is registered
	 */
	bool IsTeamRegistered(int32 TeamID) const;



	//==========================================================================
	// ADVERSARIAL RELATIONSHIPS
	//==========================================================================

	/**
	 * Establish mutual adversarial relationships between registered teams
	 * Creates a table that sets registered team IDs as adversaries
	 * Supports 1:1 and 1:1:1 relationships
	 */
	void SetMutual();




private:
	/** Reference to SimulationManagerGameMode */
	UPROPERTY()
	ASimulationManagerGameMode* SimulationManager = nullptr;

	/** List of team IDs managed by this environment */
	UPROPERTY()
	TArray<int32>				RegisteredTeamIDs;

	/** Registered objective actors */
	UPROPERTY()
	TArray<AObjectiveActor*>	RegisteredObjectives;

	/** Adversarial relationship table: TeamID -> Enemy TeamIDs */
	UPROPERTY()
	TMap<int32, FEnemyTeamList> AdversarialTable;
};