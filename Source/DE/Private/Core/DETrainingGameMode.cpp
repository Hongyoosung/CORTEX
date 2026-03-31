// Copyright Epic Games, Inc. All Rights Reserved.
// ADETrainingGameMode is now a minimal shell. All game logic lives in ADEScholaEnvironment.

#include "Core/DETrainingGameMode.h"
#include "Characters/DEAgent.h"
#include "Player/DESpectatorController.h"
#include "Player/DESpectatorHUD.h"

ADETrainingGameMode::ADETrainingGameMode()
{
	// All game logic is owned by ADEScholaEnvironment (one per arena).
	// DETrainingGameMode is now a minimal shell — no spawning, no scoring, no event subscriptions.
	PrimaryActorTick.bCanEverTick = false;

	// Camera observation feature: use spectator controller and HUD
	PlayerControllerClass = ADESpectatorController::StaticClass();
	HUDClass               = ADESpectatorHUD::StaticClass();
}

void ADETrainingGameMode::BeginPlay()
{
	Super::BeginPlay();
	// v10.2: No-op. ADEScholaEnvironment instances handle all match logic.
}

void ADETrainingGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// v10.2: No-op. ADEScholaEnvironment::Tick handles match timer, passive income, win conditions.
}
