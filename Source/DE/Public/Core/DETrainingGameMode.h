// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DETrainingGameMode.generated.h"



/**
 * Delegate for match events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchStateChanged, EDEMatchState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnScoreUpdated, int32, TeamID, int32, NewScore, FString, Reason);

/**
 * ADETrainingGameMode - Game Mode Orchestrator
 *
 * Responsibilities:
 * - Spawn and manage all game entities (DEMatchManager, CapturePoints)
 * - Subscribe to game events (captures, kills)
 * - Manage scoring system (capture, passive income, kills)
 * - Check win conditions (300 points or 600 seconds)
 * - Coordinate match state transitions
 *
 * Match Flow:
 * 1. BeginPlay: Spawn DEMatchManager, 5 CapturePoints, 12 HealthPacks, 8 AmmoCrates
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
class DE_API ADETrainingGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADETrainingGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
};
