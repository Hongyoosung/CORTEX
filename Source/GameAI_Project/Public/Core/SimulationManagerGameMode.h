#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Team/TeamTypes.h"
#include "SimulationManagerGameMode.generated.h"



class UTeamLeaderComponent;
class AScholaCombatEnvironment;
class AObjectiveActor;
struct FDeathEventData;



/**
 * Simulation statistics
 */
USTRUCT(BlueprintType)
struct FSimulationStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	int32 TotalEnvironments = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	int32 TotalTeams = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	float SimulationTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	int32 TotalAgents = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	int32 AliveAgents = 0;
};

/**
 * Episode result info
 */
USTRUCT(BlueprintType)
struct FEpisodeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Episode")
	int32 EpisodeNumber = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Episode")
	int32 WinningTeamID = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Episode")
	int32 LosingTeamID = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Episode")
	float EpisodeDuration = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Episode")
	int32 TotalSteps = 0;
};




DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSimulationStart);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSimulationStop);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEpisodeStarted, int32, EnvironmentID, int32, EpisodeNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEpisodeEnded, int32, EnvironmentID, const FEpisodeResult&, Result);



/** //==========================================================================
 * Simulation Manager GameMode
 *
 * Manages team-based AI simulation with multi-team support.
 * Handles team registration, enemy team tracking, and simulation state.
 *
 * Key Features:
 * - Register multiple teams with unique IDs
 * - Define enemy relationships between teams
 * - Query team information and enemy teams
 * - Track simulation statistics
 * - Broadcast team events
 *
 * Usage:
 * 1. Set this as your GameMode in World Settings
 * 2. Register teams using RegisterTeam() during BeginPlay
 * 3. Set enemy teams using SetEnemyTeams() or AddEnemyTeam()
 * 4. Query teams using GetTeamInfo(), GetEnemyTeams(), etc.
 */ //==========================================================================
UCLASS()
class GAMEAI_PROJECT_API ASimulationManagerGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimulationManagerGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;



	//==========================================================================
	// TEAM REGISTRATION
	//==========================================================================

	/**
	 * Register a team with the simulation manager
	 * @param TeamInfo - Team information struct
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Teams")
	bool RegisterTeam(FTeamInfo TeamInfo);

	/**
	 * Unregister a team
	 * @param TeamID - Team to unregister
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Teams")
	bool UnRegisterTeam(FTeamInfo TeamInfo);


	//==========================================================================
	// OBJECTIVE MANAGEMENT
	//==========================================================================

	/**
	 * Register an ObjectiveActor to the simulation
	 * This method provides a Schola-agnostic interface for ObjectiveActor
	 * The ObjectiveActor doesn't need to know about Schola environment details
	 * @param Objective - ObjectiveActor to register
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Objectives")
	void RegisterObjective(AObjectiveActor* Objective);

	/**
	 * Unregister an ObjectiveActor from the simulation
	 * @param Objective - ObjectiveActor to unregister
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Objectives")
	void UnRegisterObjective(AObjectiveActor* Objective);

	void GetObjectives(int32 TeamID, AObjectiveActor*& Friendly, AObjectiveActor*& Hostile) const;



	//==========================================================================
	// SIMULATION CONTROL
	//==========================================================================

	/**
	 * Start simulation (enables all teams)
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Control")
	void StartSimulation();

	/**
	 * Stop simulation (disables all teams)
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Control")
	void StopSimulation();

	/**
	 * Reset simulation (clears all teams and stats)
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Control")
	void ResetSimulation();



	//==========================================================================
	// VALIDATION & QUERIES
	//==========================================================================
	UFUNCTION(BlueprintPure, Category = "Simulation|Control")
	FORCEINLINE bool IsSimulationRunning() const { return bSimulationRunning; }

	UFUNCTION(BlueprintPure, Category = "Simulation|Stats")
	FSimulationStats GetSimulationStats() const;


	/**
	 * Called when an objective is defeated (durability reaches 0)
	 * Bind this to ObjectiveActor::OnObjectiveDefeated delegate
	 * @param DefeatedTeamID - Team ID that lost their objective
	 */
	UFUNCTION()
	void OnObjectiveDefeated(int32 DefeatedTeamID);


	/**
	 * Award objective capture bonus to winning team
	 * @param TeamID - Team that captured the objective
	 */
	void AwardObjectiveCaptureBonus(int32 TeamID);





public:
	//============ Event Delegates ============

	UPROPERTY(BlueprintAssignable, Category = "Simulation|Episode")
	FOnSimulationStart	OnSimulationStart_Delegate;

	UPROPERTY(BlueprintAssignable, Category = "Simulation|Episode")
	FOnSimulationStop	OnSimulationStop_Delegate;

	

private:
	/** Draw debug information */
	void DrawDebugInformation();


	void DiscoverAndRegisterEnvironments();


public:
	//============ CONFIGURATION ============
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Config")
	bool	bAutoStartSimulation;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Debug")
	bool	bDrawDebugInfo;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Debug")
	float	DebugDrawInterval;


private:
	UPROPERTY()
	TArray<AActor*>							ScholaEnvironmentsArray;

	UPROPERTY()
	TMap<int32, AScholaCombatEnvironment*>	ScholaEnvironmentsMap;

	int32	TotalAgents;

	bool	bSimulationRunning;

	float	SimulationStartTime;

	float	LastDebugDrawTime;
};
