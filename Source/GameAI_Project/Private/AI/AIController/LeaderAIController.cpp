// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/AIController/LeaderAIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"

ALeaderAIController::ALeaderAIController()
{
	PrimaryActorTick.bCanEverTick = true;

	// Setup AI perception (optional)
	SetPerceptionComponent(*CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent")));
}

void ALeaderAIController::BeginPlay()
{
	Super::BeginPlay();
}

void ALeaderAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Cache components
	InitializeComponents();
}

void ALeaderAIController::OnUnPossess()
{
	Super::OnUnPossess();
}

void ALeaderAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}
