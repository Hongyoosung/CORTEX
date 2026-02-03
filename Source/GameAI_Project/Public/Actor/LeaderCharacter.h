// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/CombatStatsInterface.h"
#include "Core/TeamManagementSubsystem.h"
#include "LeaderCharacter.generated.h"


class USquadManagerComponent;
class UTeamManagerComponent;
class UStrategicPlannerComponent;
class UVisualLoggerComponent;
class AObjectiveActor;
struct FStrategyAssignment;
struct FTeamObservation;


/** //===============================================================
 * Leader Character - v9.0
 *
 * This character manages a team of follower agents using event-driven MCTS.
 *
 * - Added 4 new manager components for separation of concerns
 * - TeamLeaderComponent now acts as coordinator, delegates to managers
 * - RAII async MCTS execution for automatic resource cleanup
 * - Debug visualization separated into VisualLoggerComponent
 */ //===============================================================
UCLASS()
class GAMEAI_PROJECT_API ALeaderCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ALeaderCharacter();


protected:
	virtual void BeginPlay() override;


public:
	virtual void Tick(float DeltaTime) override;


	//===============================================================
	// Team Management API
	//===============================================================

	UFUNCTION(BlueprintCallable, Category = "AI|Squad")
	bool				RegisterFollower(AActor* Follower);

	UFUNCTION(BlueprintCallable, Category = "AI|Squad")
	bool				UnregisterFollower(AActor* Follower);

	UFUNCTION(BlueprintPure, Category = "AI|Squad")
	TArray<AActor*>		GetFollowers() const;

	UFUNCTION(BlueprintPure, Category = "AI|Squad")
	int32				GetFollowerCount() const;

	UFUNCTION(BlueprintPure, Category = "AI|Squad")
	bool				IsFollowerRegistered(AActor* Follower) const;

	UFUNCTION(BlueprintCallable, Category = "AI|Intel")
	void				RegisterEnemy(AActor* Enemy);

	UFUNCTION(BlueprintCallable, Category = "AI|Intel")
	void				UnregisterEnemy(AActor* Enemy);

	UFUNCTION(BlueprintPure, Category = "AI|Intel")
	AObjectiveActor*	GetFriendlyObjective() const;

	UFUNCTION(BlueprintPure, Category = "AI|Intel")
	AObjectiveActor*	GetHostileObjective() const;

	UFUNCTION(BlueprintCallable, Category = "AI|Intel")
	FTeamObservation	BuildTeamObservation(const TArray<AActor*>& Followers);

	UFUNCTION(BlueprintPure, Category = "AI|Intel")
	bool				AreObjectivesDiscovered() const;

	int32				GetTeamID() const;

	

	//===============================================================
	// STRATEGIC PLANNING API
	//===============================================================

	UFUNCTION(BlueprintCallable, Category = "AI|Strategy")
	void RunStrategyAssignmentAsync(const TArray<AActor*>& Agents, const TArray<AObjectiveActor*>& Objectives);

	UFUNCTION(BlueprintPure, Category = "AI|Strategy")
	bool IsRunningMCTS() const;


	//===============================================================
	// EVENT HANDLERS
	//===============================================================
	void OnAllAgentsRegisterd();
	void OnSimulationStart();


private:



public:
	//============= COMPONENTS =================

	/** Intel manager component (enemy tracking, objectives, team observations) */
	UPROPERTY(VisibleAnywhere,				BlueprintReadOnly, Category = "AI|Components|Managers")
	TObjectPtr<UTeamManagerComponent>		TeamManagerComponent;

	/** Strategic planner component (async MCTS planning with RAII) */
	UPROPERTY(VisibleAnywhere,				BlueprintReadOnly, Category = "AI|Components|Managers")
	TObjectPtr<UStrategicPlannerComponent>	StrategicPlannerComponent;

	/** Visual logger component (debug visualization - optional) */
	UPROPERTY(VisibleAnywhere,				BlueprintReadOnly, Category = "AI|Components|Debug")
	TObjectPtr<UVisualLoggerComponent>		VisualLoggerComponent;



	//=============== CACHED =====================
	ASimulationManagerGameMode* GameMode;


	//============= COMBAT STATS =================

	FCombatStats CombatStats;
};
