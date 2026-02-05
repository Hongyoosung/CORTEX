// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/AIController/FollowerAIController.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"

AFollowerAIController::AFollowerAIController()
{
	PrimaryActorTick.bCanEverTick = true;

	// Setup AI perception (optional)
	SetPerceptionComponent(*CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent")));
}

void AFollowerAIController::BeginPlay()
{
	Super::BeginPlay();
}

void AFollowerAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// DIAGNOSTIC: Verify PathFollowingComponent exists (required for MoveToLocation)
	UPathFollowingComponent* PathFollowing = GetPathFollowingComponent();
	UE_LOG(LogTemp, Warning, TEXT("FollowerAIController::OnPossess: Pawn='%s', PathFollowingComponent=%s"),
		*GetNameSafe(InPawn),
		PathFollowing ? TEXT("✅ Valid") : TEXT("❌ NULL - MOVEMENT WILL FAIL!"));

	// Cache components
	InitializeComponents();

	// Auto-start State Tree
	if (bAutoStartStateTree)
	{
		StartStateTree();
	}
}

void AFollowerAIController::OnUnPossess()
{
	// Stop State Tree
	StopStateTree();


	CachedStateTreeComponent = nullptr;

	Super::OnUnPossess();
}

void AFollowerAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Optional: Draw debug info
	if (bEnableDebugDrawing)
	{
		DrawDebugInfo();
	}
}


UFollowerStateTreeComponent* AFollowerAIController::GetStateTreeComponent() const
{
	if (CachedStateTreeComponent)
	{
		return CachedStateTreeComponent;
	}

	APawn* ControlledPawn = GetPawn();
	if (ControlledPawn)
	{
		return ControlledPawn->FindComponentByClass<UFollowerStateTreeComponent>();
	}

	return nullptr;
}

//------------------------------------------------------------------------------
// STATE TREE CONTROL
//------------------------------------------------------------------------------

bool AFollowerAIController::StartStateTree()
{
	UFollowerStateTreeComponent* StateTreeComp = GetStateTreeComponent();
	if (!StateTreeComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("FollowerAIController: No StateTreeComponent found on controlled pawn '%s'"),
			*GetNameSafe(GetPawn()));
		return false;
	}

	// Initialize context
	//StateTreeComp->InitializeContext();

	// Start State Tree
	//if (!StateTreeComp->IsStateTreeRunning())
	//{
	//	StateTreeComp->StartLogic();
	//	UE_LOG(LogTemp, Log, TEXT("FollowerAIController: Started State Tree for pawn '%s'"),
	//		*GetNameSafe(GetPawn()));
	//	return true;
	//}

	return false;
}

void AFollowerAIController::StopStateTree()
{
	UFollowerStateTreeComponent* StateTreeComp = GetStateTreeComponent();
	if (StateTreeComp && StateTreeComp->IsStateTreeRunning())
	{
		StateTreeComp->StopLogic(TEXT("FollowerAIController: OnUnPossess"));
		UE_LOG(LogTemp, Log, TEXT("FollowerAIController: Stopped State Tree for pawn '%s'"),
			*GetNameSafe(GetPawn()));
	}
}

bool AFollowerAIController::IsStateTreeRunning() const
{
	UFollowerStateTreeComponent* StateTreeComp = GetStateTreeComponent();
	return StateTreeComp && StateTreeComp->IsStateTreeRunning();
}

FString AFollowerAIController::GetCurrentStateName() const
{
	UFollowerStateTreeComponent* StateTreeComp = GetStateTreeComponent();
	if (StateTreeComp)
	{
		return StateTreeComp->GetCurrentStateName();
	}
	return TEXT("None");
}

//------------------------------------------------------------------------------
// DEBUGGING
//------------------------------------------------------------------------------

void AFollowerAIController::DrawDebugInfo()
{

	// Draw current state name above pawn
	if (GetPawn())
	{
		const FVector Location = GetPawn()->GetActorLocation() + FVector(0, 0, 120);
		DrawDebugString(GetWorld(), Location, GetCurrentStateName(), nullptr, FColor::Cyan, 0.0f, true);
	}
}

//------------------------------------------------------------------------------
// PRIVATE
//------------------------------------------------------------------------------

void AFollowerAIController::InitializeComponents()
{
	// Cache state tree component
	CachedStateTreeComponent = GetStateTreeComponent();
	if (!CachedStateTreeComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("FollowerAIController: No FollowerStateTreeComponent found on pawn '%s'"),
			*GetNameSafe(GetPawn()));
	}
}
