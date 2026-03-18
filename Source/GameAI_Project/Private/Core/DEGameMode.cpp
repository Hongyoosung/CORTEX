// Copyright Epic Games, Inc. All Rights Reserved.
// v10.2: ADEGameMode is now a minimal shell. All game logic lives in ADEScholaEnvironment.

#include "Core/DEGameMode.h"
#include "Characters/DECharacter.h"
#include "Player/DESpectatorController.h"
#include "Player/DESpectatorHUD.h"

ADEGameMode::ADEGameMode()
{
	// All game logic is owned by ADEScholaEnvironment (one per arena).
	// DEGameMode is now a minimal shell — no spawning, no scoring, no event subscriptions.
	PrimaryActorTick.bCanEverTick = false;

	// Camera observation feature: use spectator controller and HUD
	PlayerControllerClass = ADESpectatorController::StaticClass();
	HUDClass               = ADESpectatorHUD::StaticClass();
}

void ADEGameMode::BeginPlay()
{
	Super::BeginPlay();
	// v10.2: No-op. ADEScholaEnvironment instances handle all match logic.
}

void ADEGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// v10.2: No-op. ADEScholaEnvironment::Tick handles match timer, passive income, win conditions.
}
