// DEScholaEnvironment.h - Schola 2.0.1 multi-agent training environment

#pragma once

#include "CoreMinimal.h"
#include "Schola/DynamicEQSEnvironmentActor.h"   // base class (provides IMultiAgentScholaEnvironment)
#include "TrainingDataTypes/AgentState.h"
#include "Common/InteractionDefinition.h"
#include "Core/DETrainingGameMode.h"
#include "Types/DEGameStateTypes.h"
#include "DEScholaEnvironment.generated.h"


class UDEScholaAgent;
class UDEEpisodeManagerComponent;
class ADETrainer;
class ADEMatchManager;
class ADESpawnArea;
class ADEPickupBase;
class ADEAgent;



DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnScholaEnvironmentInitialized);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnvMatchStateChanged, EDEMatchState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEnvScoreUpdated, int32, TeamID, int32, NewScore, FString, Reason);

/**
 * Schola 2.0.1 Multi-Agent Training Environment
 *
 * Each ADEScholaEnvironment instance owns ALL game logic for one 5v5 arena.
 * Inherits from ADynamicEQSEnvironmentActor, which provides the IMultiAgentScholaEnvironment
 * implementation, agent registration, GymConnectorManager integration, and default
 * step/reset/init behaviour. Only project-specific overrides are defined here.
 */
UCLASS()
class DE_API ADEScholaEnvironment : public ADynamicEQSEnvironmentActor
{
	GENERATED_BODY()

public:
	ADEScholaEnvironment();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;


	//==========================================================================
	// IMultiAgentScholaEnvironment INTERFACE — project overrides
	//==========================================================================

	virtual void InitializeEnvironment_Implementation(TMap<FString, FInteractionDefinition>& OutAgentDefinitions) override;
	virtual void Reset_Implementation(TMap<FString, FInitialAgentState>& OutAgentState) override;
	virtual void Step_Implementation(const TMap<FString, FInstancedStruct>& InActions, TMap<FString, FAgentState>& OutAgentStates) override;
	virtual void SeedEnvironment_Implementation(int Seed) override;
	virtual void SetEnvironmentOptions_Implementation(const TMap<FString, FString>& Options) override {}


	//==========================================================================
	// MATCH MANAGEMENT
	//==========================================================================

	UFUNCTION(BlueprintCallable, Category = "Schola|Match")
	void StartMatch();

	UFUNCTION(BlueprintCallable, Category = "Schola|Match")
	void EndMatch(EDEMatchState WinnerState, int32 WinningTeamID = -1);

	/** Delegate callback from ADEMatchManager::OnMatchConditionMet */
	UFUNCTION()
	void OnMatchConditionReceived(EDEMatchState WinnerState, int32 WinningTeamID);

	UFUNCTION(BlueprintPure, Category = "Schola|Match")
	EDEMatchState GetMatchState() const { return CurrentMatchState; }

	UFUNCTION(BlueprintPure, Category = "Schola|Match")
	float GetMatchTimer() const;

	UFUNCTION(BlueprintPure, Category = "Schola|Match")
	float GetTimeRemaining() const;


	//=========================================
	// GETTERS
	//=========================================

	UFUNCTION(BlueprintPure, Category = "Schola")
	FORCEINLINE ADEMatchManager*	GetMatchManager() const { return OwnedMatchManager; }

	UFUNCTION(BlueprintPure, Category = "Schola|Random")
	FORCEINLINE FRandomStream&		GetRandomStream()		{ return EnvRandomStream; }



public:
	//=========================================
	// COMPONENTS
	//=========================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Components")
	UDEEpisodeManagerComponent* EpisodeManager;


	//=========================================
	// OWNED ACTORS
	//=========================================

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Schola|Owned")
	ADEMatchManager* OwnedMatchManager = nullptr;


	//=========================================
	// MATCH CONFIGURATION
	//=========================================

	UPROPERTY(EditAnywhere, Category = "Schola|Match")
	bool bAutoStartMatch = true;


	//=========================================
	// TRAINING CONFIGURATION
	//=========================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bTrainingMode = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bAutoDiscoverAgents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	TSubclassOf<ADETrainer> TrainerClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bAutoSpawnTrainers = true;


	//=========================================
	// STATE
	//=========================================

	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	bool bServerRunning;

	bool bAgentsRegistered;
	bool bEnvironmentInitialized = false;

	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	bool bTrainingActive;

	UPROPERTY()
	TArray<ADETrainer*> SpawnedTrainers;

	/** Maps trainer-keyed agent IDs (populated in InitializeEnvironment). */
	TMap<FString, ADETrainer*> AgentTrainerMap;



protected:
	//=========================================
	// MATCH RUNTIME STATE
	//=========================================

	UPROPERTY(BlueprintReadOnly, Category = "Schola|Match|State")
	EDEMatchState CurrentMatchState = EDEMatchState::WaitingToStart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Random")
	FRandomStream EnvRandomStream;

	bool bMatchEnded = false;

private:
	/**
	 * Idempotent: sets EnvID on OwnedMatchManager, initialises capture points,
	 * and binds OnMatchConditionMet. Safe to call from both BeginPlay and
	 * InitializeEnvironment regardless of ordering.
	 */
	void EnsureMatchManagerReady();
	bool bMatchManagerReady = false;


public:
	//=========================================
	// Delegates
	//=========================================
	FOnScholaEnvironmentInitialized OnScholaEnvironmentInitialized_Delegate;

	UPROPERTY(BlueprintAssignable, Category = "Schola|Events")
	FOnEnvMatchStateChanged OnEnvMatchStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Schola|Events")
	FOnEnvScoreUpdated OnEnvScoreUpdated;
};
