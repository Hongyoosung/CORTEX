// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MocGameMode.generated.h"

// Forward declarations
class AMatchManager;
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
 * - Spawn and manage all game entities (MatchManager, CapturePoints)
 * - Subscribe to game events (captures, kills)
 * - Manage scoring system (capture, passive income, kills)
 * - Check win conditions (300 points or 600 seconds)
 * - Coordinate match state transitions
 *
 * Match Flow:
 * 1. BeginPlay: Spawn MatchManager, 5 CapturePoints, 12 HealthPacks, 8 AmmoCrates
 * 2. Subscribe to events: OnPointCaptured, OnAgentKilled
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
};
