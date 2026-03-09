// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Team/DETeamWorldState.h"
#include "Types/DEStrategyTypes.h"
#include "Types/DERewardTypes.h"
#include "DETeamDataCollector.generated.h"

/**
 * Training Sample for Team World Model
 * Represents a single (s, a, s', r) transition tuple
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FDETeamTransitionSample
{
	GENERATED_BODY()

	/** Current team state (60-dim) */
	UPROPERTY(BlueprintReadWrite)
	FDETeamWorldState CurrentState;

	/** Tactical play executed (action) */
	UPROPERTY(BlueprintReadWrite)
	ETacticalPlay TacticalPlay;

	/** Next team state after 0.5s (60-dim) */
	UPROPERTY(BlueprintReadWrite)
	FDETeamWorldState NextState;

	/** Observed multi-objective reward */
	UPROPERTY(BlueprintReadWrite)
	FDECompositeReward Reward;

	/** Timestamp of transition */
	UPROPERTY(BlueprintReadWrite)
	float Timestamp;

	/** Match ID for replay tracking */
	UPROPERTY(BlueprintReadWrite)
	int32 MatchID;

	FDETeamTransitionSample()
		: TacticalPlay(ETacticalPlay::StandardComp)
		, Timestamp(0.0f)
		, MatchID(0)
	{}
};

/**
 * UDETeamDataCollector
 *
 * Collects team-level training data during gameplay for world model training.
 * Attaches to ASquadManager or GameMode to record state transitions.
 *
 * Usage:
 * 1. Add component to DESquadManager or GameMode
 * 2. Call BeginRecording() to start data collection
 * 3. Call RecordTransition() every planning cycle
 * 4. Call EndRecording() to save to disk
 *
 * Output Format: CSV or binary files in Content/TrainingData/
 */
UCLASS(Blueprintable, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UDETeamDataCollector : public UActorComponent
{
	GENERATED_BODY()

public:
	UDETeamDataCollector();

	//========================================
	// Recording Control
	//========================================

	/**
	 * Start recording training data
	 * @param InMatchID Unique identifier for this match/episode
	 */
	UFUNCTION(BlueprintCallable, Category = "Training")
	void BeginRecording(int32 InMatchID = 0);

	/**
	 * Stop recording and save data to disk
	 * @param bSaveImmediately If true, write to disk immediately
	 * @return Number of samples collected
	 */
	UFUNCTION(BlueprintCallable, Category = "Training")
	int32 EndRecording(bool bSaveImmediately = true);

	/**
	 * Record a single state transition
	 * @param CurrentState State before action
	 * @param TacticalPlay Action taken
	 * @param NextState State after action (0.5s later)
	 * @param Reward Observed multi-objective reward
	 */
	UFUNCTION(BlueprintCallable, Category = "Training")
	void RecordTransition(
		const FDETeamWorldState& CurrentState,
		ETacticalPlay TacticalPlay,
		const FDETeamWorldState& NextState,
		const FDECompositeReward& Reward
	);

	/**
	 * Save collected samples to disk
	 * @param Filename Custom filename (optional, auto-generated if empty)
	 * @return True if save successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Training")
	bool SaveToDisk(const FString& Filename = TEXT(""));

	/**
	 * Clear all collected samples
	 */
	UFUNCTION(BlueprintCallable, Category = "Training")
	void ClearSamples();

	//========================================
	// Configuration
	//========================================

	/** Enable/disable data collection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Config")
	bool bIsRecording;

	/** Output directory for training data (relative to project Content/) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Config")
	FString OutputDirectory;

	/** Output format: "CSV" or "Binary" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Config")
	FString OutputFormat;

	/** Maximum samples per file (auto-split if exceeded) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Config")
	int32 MaxSamplesPerFile;

	/** Auto-save interval (seconds, 0 = disabled) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Config")
	float AutoSaveIntervalSeconds;

	//========================================
	// Statistics
	//========================================

	/** Get total number of samples collected */
	UFUNCTION(BlueprintPure, Category = "Training|Stats")
	int32 GetSampleCount() const { return CollectedSamples.Num(); }

	/** Get current match ID */
	UFUNCTION(BlueprintPure, Category = "Training|Stats")
	int32 GetCurrentMatchID() const { return CurrentMatchID; }

	/** Get total matches recorded */
	UFUNCTION(BlueprintPure, Category = "Training|Stats")
	int32 GetTotalMatchesRecorded() const { return TotalMatchesRecorded; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Collected training samples */
	UPROPERTY()
	TArray<FDETeamTransitionSample> CollectedSamples;

	/** Current match ID */
	UPROPERTY()
	int32 CurrentMatchID;

	/** Total matches recorded in this session */
	UPROPERTY()
	int32 TotalMatchesRecorded;

	/** Time since last auto-save */
	float TimeSinceLastSave;

	/** File counter for auto-splitting */
	int32 FileCounter;

	//========================================
	// Internal Helpers
	//========================================

	/** Generate filename for output */
	FString GenerateFilename() const;

	/** Save samples to CSV format */
	bool SaveToCSV(const FString& Filepath);

	/** Save samples to binary format */
	bool SaveToBinary(const FString& Filepath);

	/** Convert FDETeamState to CSV row */
	FString TeamStateToCSV(const FDETeamWorldState& State) const;

	/** Convert FDECompositeReward to CSV row */
	FString RewardToCSV(const FDECompositeReward& Reward) const;
};
