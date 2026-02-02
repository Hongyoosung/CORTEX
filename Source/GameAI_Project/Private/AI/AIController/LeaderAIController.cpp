// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/AIController/LeaderAIController.h"
#include "Team/Components/TeamLeaderComponent.h"
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
	// Clear cached references
	CachedTeamLeaderComponent = nullptr;

	Super::OnUnPossess();
}

void ALeaderAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

//------------------------------------------------------------------------------
// COMPONENTS
//------------------------------------------------------------------------------

UTeamLeaderComponent* ALeaderAIController::GetTeamLeaderComponent() const
{
	if (CachedTeamLeaderComponent)
	{
		return CachedTeamLeaderComponent;
	}

	APawn* ControlledPawn = GetPawn();
	if (ControlledPawn)
	{
		return ControlledPawn->FindComponentByClass<UTeamLeaderComponent>();
	}

	return nullptr;
}

//------------------------------------------------------------------------------
// TEAM MANAGEMENT
//------------------------------------------------------------------------------

bool ALeaderAIController::IsMCTSRunning() const
{
	UTeamLeaderComponent* TeamLeaderComp = GetTeamLeaderComponent();
	if (TeamLeaderComp)
	{
		return TeamLeaderComp->IsRunningMCTS();
	}
	return false;
}

//------------------------------------------------------------------------------
// PRIVATE
//------------------------------------------------------------------------------

void ALeaderAIController::InitializeComponents()
{
	// Cache team leader component
	CachedTeamLeaderComponent = GetTeamLeaderComponent();
	if (!CachedTeamLeaderComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("LeaderAIController: No TeamLeaderComponent found on pawn '%s'"),
			*GetNameSafe(GetPawn()));
	}
}
