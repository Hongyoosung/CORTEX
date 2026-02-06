// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Actors/CapturePoint.h"
#include "MocGameMode.generated.h"

// Forward declarations
class ATeamManager;
class ACapturePoint;
class APickupBase;
class AHealthPack;
class AAmmoCrate;
class AMocCharacter;

/**
 * Match state enumeration
 */
UENUM(BlueprintType)
enum class EMocMatchState : uint8
{
	WaitingToStart		UMETA(DisplayName = "Waiting to Start"),
	InProgress			UMETA(DisplayName = "In Progress"),
	RedTeamWon			UMETA(DisplayName = "Red Team Won"),
	BlueTeamWon			UMETA(DisplayName = "Blue Team Won"),
	TimeExpired			UMETA(DisplayName = "Time Expired")
};

/**
 * Delegate for match events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchStateChanged, EMocMatchState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnScoreUpdated, int32, TeamID, int32, NewScore, FString, Reason);

/**
 * AMocGameMode - MOC v10.1 Game Mode Orchestrator
 *
 * Responsibilities:
 * - Spawn and manage all game entities (TeamManager, CapturePoints, Pickups)
 * - Subscribe to game events (captures, kills, pickups)
 * - Manage scoring system (capture, passive income, kills)
 * - Check win conditions (300 points or 600 seconds)
 * - Coordinate match state transitions
 *
 * Match Flow:
 * 1. BeginPlay: Spawn TeamManager, 5 CapturePoints, 12 HealthPacks, 8 AmmoCrates
 * 2. Subscribe to events: OnPointCaptured, OnAgentKilled, OnPickupCollected
 * 3. Tick: Update passive income, check win conditions
 * 4. EndMatch: Broadcast winner, disable gameplay
 *
 * Scoring:
 * - Capture Point: +25 points (instant)
 * - Passive Income: +1 point/sec per owned point
 * - Kill: +5 points
 *
 * Win Conditions:
 * - Score Victory: First team to 300 points
 * - Time Limit: Highest score at 600 seconds
 */
UCLASS()
class GAMEAI_PROJECT_API AMocGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMocGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	//========================================
	// Match Management
	//========================================

	/** Start the match (spawn teams and begin timer) */
	UFUNCTION(BlueprintCallable, Category = "GameMode")
	void StartMatch();

	/** End the match with specific winner */
	UFUNCTION(BlueprintCallable, Category = "GameMode")
	void EndMatch(EMocMatchState WinnerState);

	/** Get current match state */
	UFUNCTION(BlueprintPure, Category = "GameMode")
	EMocMatchState GetMatchState() const { return CurrentMatchState; }

	/** Get match timer */
	UFUNCTION(BlueprintPure, Category = "GameMode")
	float GetMatchTimer() const { return MatchTimer; }

	/** Get time remaining */
	UFUNCTION(BlueprintPure, Category = "GameMode")
	float GetTimeRemaining() const { return FMath::Max(0.0f, MaxMatchDuration - MatchTimer); }

	//========================================
	// Scoring System
	//========================================

	/** Add score to team */
	UFUNCTION(BlueprintCallable, Category = "GameMode|Scoring")
	void AddTeamScore(int32 TeamID, int32 Amount, const FString& Reason);

	/** Get team score */
	UFUNCTION(BlueprintPure, Category = "GameMode|Scoring")
	int32 GetTeamScore(int32 TeamID) const;

	//========================================
	// Entity Access
	//========================================

	/** Get TeamManager instance */
	UFUNCTION(BlueprintPure, Category = "GameMode")
	ATeamManager* GetTeamManager() const { return TeamManager; }

	/** Get capture point by ID */
	UFUNCTION(BlueprintPure, Category = "GameMode")
	const ACapturePoint* GetCapturePoint(ECapturePointID PointID) const;

	/** Get all capture points */
	UFUNCTION(BlueprintPure, Category = "GameMode")
	TArray<ACapturePoint*> GetAllCapturePoints() const;

protected:
	//========================================
	// Initialization
	//========================================

	/** Spawn TeamManager at origin */
	void SpawnTeamManager();

	/** Spawn 5 capture points */
	void SpawnCapturePoints();

	/** Spawn 12 health packs */
	void SpawnHealthPacks();

	/** Spawn 8 ammo crates */
	void SpawnAmmoCrates();

	/** Subscribe to all game events */
	void SubscribeToEvents();

	//========================================
	// Event Handlers
	//========================================

	/** Handle point captured event */
	UFUNCTION()
	void OnPointCaptured(ECapturePointID PointID, ECapturePointOwnership PreviousOwner, ECapturePointOwnership NewOwner);

	/** Handle agent killed event */
	UFUNCTION()
	void OnAgentKilled(int32 VictimTeamID, int32 KillerTeamID, AMocCharacter* Victim);

	/** Handle pickup collected event */
	UFUNCTION()
	void OnPickupCollected(AActor* Collector, EPickupType PickupType);

	//========================================
	// Game Logic
	//========================================

	/** Update passive income from owned points */
	void UpdatePassiveIncome(float DeltaTime);

	/** Check if any team has won */
	void CheckWinConditions();

	/** Count owned points per team */
	void CountOwnedPoints(int32& OutRedOwned, int32& OutBlueOwned) const;

public:
	//========================================
	// Configuration - Classes
	//========================================

	/** TeamManager class */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameMode|Classes")
	TSubclassOf<ATeamManager> TeamManagerClass;

	/** CapturePoint class */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameMode|Classes")
	TSubclassOf<ACapturePoint> CapturePointClass;

	/** HealthPack class */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameMode|Classes")
	TSubclassOf<APickupBase> HealthPackClass;

	/** AmmoCrate class */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameMode|Classes")
	TSubclassOf<APickupBase> AmmoCrateClass;

	//========================================
	// Configuration - Match Settings
	//========================================

	/** Maximum match duration (seconds) */
	UPROPERTY(EditAnywhere, Category = "GameMode|Match")
	float MaxMatchDuration = 600.0f;

	/** Score required to win */
	UPROPERTY(EditAnywhere, Category = "GameMode|Match")
	int32 WinningScore = 300;

	/** Points awarded for capturing a point */
	UPROPERTY(EditAnywhere, Category = "GameMode|Scoring")
	int32 CaptureReward = 25;

	/** Points awarded for killing an enemy */
	UPROPERTY(EditAnywhere, Category = "GameMode|Scoring")
	int32 KillPoints = 5;

	/** Passive income rate (points per second per owned point) */
	UPROPERTY(EditAnywhere, Category = "GameMode|Scoring")
	float PassiveIncomeRate = 1.0f;

	/** Auto-start match on BeginPlay */
	UPROPERTY(EditAnywhere, Category = "GameMode|Match")
	bool bAutoStartMatch = true;

	//========================================
	// Configuration - Capture Point Locations
	//========================================

	/** Point A location (Red Base) */
	UPROPERTY(EditAnywhere, Category = "GameMode|CapturePoints")
	FVector PointA_Location = FVector(-7000.0f, 0.0f, 100.0f);

	/** Point B location (North Outpost) */
	UPROPERTY(EditAnywhere, Category = "GameMode|CapturePoints")
	FVector PointB_Location = FVector(-3500.0f, 4000.0f, 150.0f);

	/** Point C location (Center Plaza) */
	UPROPERTY(EditAnywhere, Category = "GameMode|CapturePoints")
	FVector PointC_Location = FVector(0.0f, 0.0f, 100.0f);

	/** Point D location (South Outpost) */
	UPROPERTY(EditAnywhere, Category = "GameMode|CapturePoints")
	FVector PointD_Location = FVector(3500.0f, -4000.0f, 150.0f);

	/** Point E location (Blue Base) */
	UPROPERTY(EditAnywhere, Category = "GameMode|CapturePoints")
	FVector PointE_Location = FVector(7000.0f, 0.0f, 100.0f);

	//========================================
	// Configuration - Pickup Locations
	//========================================

	/** Health pack locations (12 total) */
	UPROPERTY(EditAnywhere, Category = "GameMode|Pickups")
	TArray<FVector> HealthPackLocations;

	/** Ammo crate locations (8 total) */
	UPROPERTY(EditAnywhere, Category = "GameMode|Pickups")
	TArray<FVector> AmmoCrateLocations;

	/** Show debug visualization */
	UPROPERTY(EditAnywhere, Category = "GameMode|Debug")
	bool bShowDebugInfo = false;

	//========================================
	// Events
	//========================================

	/** Broadcast when match state changes */
	UPROPERTY(BlueprintAssignable, Category = "GameMode|Events")
	FOnMatchStateChanged OnMatchStateChanged;

	/** Broadcast when score updates */
	UPROPERTY(BlueprintAssignable, Category = "GameMode|Events")
	FOnScoreUpdated OnScoreUpdated;

protected:
	//========================================
	// Runtime State
	//========================================

	/** TeamManager instance */
	UPROPERTY(BlueprintReadOnly, Category = "GameMode|State")
	ATeamManager* TeamManager = nullptr;

	/** Capture points mapped by ID */
	UPROPERTY(BlueprintReadOnly, Category = "GameMode|State")
	TMap<ECapturePointID, ACapturePoint*> CapturePointsMap;

	/** Spawned health packs */
	UPROPERTY()
	TArray<APickupBase*> HealthPacks;

	/** Spawned ammo crates */
	UPROPERTY()
	TArray<APickupBase*> AmmoCrates;

	/** Current match state */
	UPROPERTY(BlueprintReadOnly, Category = "GameMode|State")
	EMocMatchState CurrentMatchState = EMocMatchState::WaitingToStart;

	/** Match timer (seconds) */
	UPROPERTY(BlueprintReadOnly, Category = "GameMode|State")
	float MatchTimer = 0.0f;

	/** Passive income accumulator (for fractional points) */
	float PassiveIncomeAccumulator = 0.0f;

	/** Has match ended? */
	bool bMatchEnded = false;
};
