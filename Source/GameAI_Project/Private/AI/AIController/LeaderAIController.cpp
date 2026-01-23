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

	// Optional: Draw debug info
	if (bEnableDebugDrawing)
	{
		DrawDebugInfo();
	}
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

int32 ALeaderAIController::GetFollowerCount() const
{
	UTeamLeaderComponent* TeamLeaderComp = GetTeamLeaderComponent();
	if (TeamLeaderComp)
	{
		return TeamLeaderComp->GetFollowerCount();
	}
	return 0;
}

bool ALeaderAIController::IsMCTSRunning() const
{
	UTeamLeaderComponent* TeamLeaderComp = GetTeamLeaderComponent();
	if (TeamLeaderComp)
	{
		return TeamLeaderComp->IsMCTSRunning();
	}
	return false;
}

float ALeaderAIController::GetLastMCTSDecisionTime() const
{
	UTeamLeaderComponent* TeamLeaderComp = GetTeamLeaderComponent();
	if (TeamLeaderComp)
	{
		return TeamLeaderComp->GetLastMCTSDecisionTime();
	}
	return 0.0f;
}

//------------------------------------------------------------------------------
// DEBUGGING
//------------------------------------------------------------------------------

void ALeaderAIController::DrawDebugInfo()
{
	UTeamLeaderComponent* TeamLeaderComp = GetTeamLeaderComponent();
	if (TeamLeaderComp)
	{
		TeamLeaderComp->DrawDebugInfo();
	}

	// Draw team info above pawn
	APawn* ControlledPawn = GetPawn();
	if (ControlledPawn)
	{
		FString LeaderInfo = FString::Printf(TEXT("Leader | Followers: %d"), GetFollowerCount());
		FVector Location = ControlledPawn->GetActorLocation() + FVector(0, 0, 120);
		DrawDebugString(GetWorld(), Location, LeaderInfo, nullptr, FColor::Yellow, 0.0f, true);

		if (IsMCTSRunning())
		{
			FVector MCTSLocation = ControlledPawn->GetActorLocation() + FVector(0, 0, 140);
			DrawDebugString(GetWorld(), MCTSLocation, TEXT("MCTS Running..."), nullptr, FColor::Orange, 0.0f, true);
		}
	}
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
