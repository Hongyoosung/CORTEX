// Copyright Epic Games, Inc. All Rights Reserved.
// v10.2: AMocGameMode is now a minimal shell. All game logic lives in AScholaEnvironment.

#include "Core/MocGameMode.h"
#include "Characters/MocCharacter.h"

AMocGameMode::AMocGameMode()
{
	// v10.2: All game logic is owned by AScholaEnvironment (one per arena).
	// MocGameMode is now a minimal shell — no spawning, no scoring, no event subscriptions.
	PrimaryActorTick.bCanEverTick = false;
}

void AMocGameMode::BeginPlay()
{
	Super::BeginPlay();
	// v10.2: No-op. AScholaEnvironment instances handle all match logic.
}

void AMocGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// v10.2: No-op. AScholaEnvironment::Tick handles match timer, passive income, win conditions.
}
