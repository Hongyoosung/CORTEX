// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "LeaderAIController.generated.h"



/**
 * Leader AI Controller
 *
 * This controller manages team leader agents.
 *
 * Architecture:
 * - TeamLeaderComponent runs event-driven MCTS in background thread
 * - Issues strategic commands to followers
 * - No tactical execution (no State Tree/BT needed)
 * - Aggregates team observations
 *
 * Setup:
 * 1. Assign this controller to your leader pawn/character
 * 2. Ensure LeaderCharacter has TeamLeaderComponent
 * 3. Register followers with TeamLeaderComponent
 * 4. MCTS runs automatically on strategic events
 */
UCLASS()
class GAMEAI_PROJECT_API ALeaderAIController : public AAIController
{
	GENERATED_BODY()

public:
	ALeaderAIController();

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void Tick(float DeltaTime) override;

public:
	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------
	/** Enable debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Debug")
	bool bEnableDebugDrawing = false;
};
