// ScholaEnvironment.h - Schola training environment for v10.2 Commander-Executor Architecture

#pragma once

#include "CoreMinimal.h"
#include "Environment/StaticEnvironment.h"
#include "Core/MocGameMode.h"
#include "ScholaEnvironment.generated.h"


class UScholaMocAgent;
class UEpisodeManagerComponent;
class AMocTrainer;
class AMatchManager;
class ASpawnArea;
class APickupBase;
class AMocCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnScholaEnvironmentInitialized);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnvMatchStateChanged, EMocMatchState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEnvScoreUpdated, int32, TeamID, int32, NewScore, FString, Reason);

/**
 * Schola Training Environment (v10.2 Parallel Isolated Architecture)
 *
 * Each AScholaEnvironment instance owns ALL game logic for one 5v5 arena:
 * - Match state, scoring, win conditions
 * - One MatchManager (editor-assigned)
 * - 2 ASpawnAreas (editor-assigned)
 * - Pickups (editor-assigned)
 *
 * Multiple environments run in parallel in one level, fully isolated by EnvID.
 * AMocGameMode is now a minimal shell.
 */
UCLASS()
class GAMEAI_PROJECT_API AScholaEnvironment : public AStaticScholaEnvironment
{
	GENERATED_BODY()

public:
	AScholaEnvironment(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;


	//==========================================================================
	// SCHOLA ENVIRONMENT INTERFACE (AAbstractScholaEnvironment)
	//==========================================================================
	virtual void InitializeEnvironment() override;
	virtual void ResetEnvironment() override;
	virtual void RegisterAgents(TArray<APawn*>& OutTrainerControlledPawns) override;
	virtual void SeedEnvironment(int Seed) override;


	//==========================================================================
	// MATCH MANAGEMENT (absorbed from AMocGameMode)
	//==========================================================================

	/** Start the match (spawn teams and begin timer) */
	UFUNCTION(BlueprintCallable, Category = "Schola|Match")
	void StartMatch();

	/** End the match with specific winner */
	UFUNCTION(BlueprintCallable, Category = "Schola|Match")
	void EndMatch(EMocMatchState WinnerState);

	/** Get current match state */
	UFUNCTION(BlueprintPure, Category = "Schola|Match")
	EMocMatchState GetMatchState() const { return CurrentMatchState; }

	/** Get match timer */
	UFUNCTION(BlueprintPure, Category = "Schola|Match")
	float GetMatchTimer() const { return MatchTimer; }

	/** Get time remaining */
	UFUNCTION(BlueprintPure, Category = "Schola|Match")
	float GetTimeRemaining() const { return FMath::Max(0.0f, MaxMatchDuration - MatchTimer); }




	//==========================================================================
	// GETTERS
	//==========================================================================

	/** Get the environment ID assigned by Schola */
	UFUNCTION(BlueprintCallable, Category = "Schola")
	FORCEINLINE int32 GetEnvId() const { return ScholaEnvID; }

	/** Get owned MatchManager */
	UFUNCTION(BlueprintPure, Category = "Schola")
	AMatchManager* GetMatchManager() const { return OwnedMatchManager; }

	/** 외부 액터(MatchManager, SpawnArea, Trainer 등)에서 난수 스트림 접근 시 사용 */
	UFUNCTION(BlueprintPure, Category = "Schola|Random")
	FRandomStream& GetRandomStream() { return EnvRandomStream; }




public:
	//=========================== Delegates ===============================
	FOnScholaEnvironmentInitialized OnScholaEnvironmentInitialized_Delegate;

	UPROPERTY(BlueprintAssignable, Category = "Schola|Events")
	FOnEnvMatchStateChanged OnEnvMatchStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Schola|Events")
	FOnEnvScoreUpdated OnEnvScoreUpdated;


public:
	//==========================================================================
	// COMPONENTS
	//==========================================================================

	/** Episode Manager Component - manages episode lifecycle */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Components")
	UEpisodeManagerComponent* EpisodeManager;

	


	//==========================================================================
	// OWNED ACTORS (editor-assignable per environment)
	//==========================================================================

	/** MatchManager for this environment (assign in editor) */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Schola|Owned")
	AMatchManager* OwnedMatchManager = nullptr;




	//==========================================================================
	// MATCH CONFIGURATION
	//==========================================================================

	/** Maximum match duration (seconds) */
	UPROPERTY(EditAnywhere, Category = "Schola|Match")
	float MaxMatchDuration = 600.0f;

	/** Score required to win */
	UPROPERTY(EditAnywhere, Category = "Schola|Match")
	int32 WinningScore = 300;

	/** Points awarded for killing an enemy */
	UPROPERTY(EditAnywhere, Category = "Schola|Scoring")
	int32 KillPoints = 5;

	/** Passive income rate (points per second per owned point) */
	UPROPERTY(EditAnywhere, Category = "Schola|Scoring")
	float PassiveIncomeRate = 1.0f;

	/** Auto-start match on BeginPlay */
	UPROPERTY(EditAnywhere, Category = "Schola|Match")
	bool bAutoStartMatch = true;


	//==========================================================================
	// TRAINING CONFIGURATION
	//==========================================================================

	/** Enable RL training mode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bTrainingMode = true;

	/** Auto-discover agents from OwnedTeamManager */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bAutoDiscoverAgents = true;

	/** Trainer class to use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	TSubclassOf<AMocTrainer> TrainerClass;

	/** Auto-spawn trainers for agents that don't have one */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bAutoSpawnTrainers = true;

	/** Phase 1 RL Training: fix strategy per episode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config|v10.2")
	bool bPhase1RLTraining = false;


	/** Log tactical play assignments for analysis */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config|v10.2")
	bool bLogTacticalPlays;


	//==========================================================================
	// STATE
	//==========================================================================

	/** All registered Schola agent components */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	TArray<UScholaMocAgent*> RegisteredAgents;

	/** Is gRPC server running? */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	bool bServerRunning;

	bool bAgentsRegistered;
	bool bEnvironmentInitialized = false;

	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	bool bTrainingActive;

	UPROPERTY()
	TArray<AMocTrainer*> SpawnedTrainers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	int32 ScholaEnvID;

protected:
	//==========================================================================
	// MATCH RUNTIME STATE
	//==========================================================================

	UPROPERTY(BlueprintReadOnly, Category = "Schola|Match|State")
	EMocMatchState CurrentMatchState = EMocMatchState::WaitingToStart;

	UPROPERTY(BlueprintReadOnly, Category = "Schola|Match|State")
	float MatchTimer = 0.0f;

	float PassiveIncomeAccumulator = 0.0f;
	bool bMatchEnded = false;

	/** 각 환경 인스턴스에 종속된 독립적인 난수 스트림 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Random")
	FRandomStream EnvRandomStream;
};
