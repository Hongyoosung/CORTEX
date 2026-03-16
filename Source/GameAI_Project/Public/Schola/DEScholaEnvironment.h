// DEScholaEnvironment.h - Schola 2.0.1 multi-agent training environment

#pragma once

#include "CoreMinimal.h"
#include "Schola/DynamicEQSEnvironmentActor.h"   // base class (provides IMultiAgentScholaEnvironment)
#include "TrainingDataTypes/AgentState.h"
#include "Common/InteractionDefinition.h"
#include "Core/DEGameMode.h"
#include "DEScholaEnvironment.generated.h"


class UDEScholaAgent;
class UDEEpisodeManagerComponent;
class ADETrainer;
class ADEMatchManager;
class ADESpawnArea;
class ADEPickupBase;
class ADECharacter;

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
class GAMEAI_PROJECT_API ADEScholaEnvironment : public ADynamicEQSEnvironmentActor
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
	void EndMatch(EDEMatchState WinnerState);

	UFUNCTION(BlueprintPure, Category = "Schola|Match")
	EDEMatchState GetMatchState() const { return CurrentMatchState; }

	UFUNCTION(BlueprintPure, Category = "Schola|Match")
	float GetMatchTimer() const { return MatchTimer; }

	UFUNCTION(BlueprintPure, Category = "Schola|Match")
	float GetTimeRemaining() const { return FMath::Max(0.0f, MaxMatchDuration - MatchTimer); }


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
	float MaxMatchDuration = 600.0f;

	UPROPERTY(EditAnywhere, Category = "Schola|Match")
	int32 WinningScore = 120;

	UPROPERTY(EditAnywhere, Category = "Schola|Match")
	bool bDominationWinEnabled = true;

	UPROPERTY(EditAnywhere, Category = "Schola|Scoring")
	int32 KillPoints = 5;

	UPROPERTY(EditAnywhere, Category = "Schola|Scoring")
	float PassiveIncomeRate = 1.0f;

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

	UPROPERTY(BlueprintReadOnly, Category = "Schola|Match|State")
	float MatchTimer = 0.0f;

	float PassiveIncomeAccumulator = 0.0f;
	bool bMatchEnded = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Random")
	FRandomStream EnvRandomStream;


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
