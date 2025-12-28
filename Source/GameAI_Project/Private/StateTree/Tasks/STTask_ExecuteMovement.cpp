// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/STTask_ExecuteMovement.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Team/FollowerAgentComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

EStateTreeRunStatus FSTTask_ExecuteMovement::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.StateTreeComp)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_ExecuteMovement: StateTreeComp is null"));
		return EStateTreeRunStatus::Failed;
	}

	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	if (!SharedContext.FollowerComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_ExecuteMovement: Missing FollowerComponent"));
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.ControlledPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_ExecuteMovement: ControlledPawn not bound"));
		return EStateTreeRunStatus::Failed;
	}

	// Validate EQS queries
	if (!InstanceData.ForwardCoverQuery || !InstanceData.RetreatQuery || !InstanceData.AdvanceQuery)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_ExecuteMovement: EQS queries not assigned"));
		return EStateTreeRunStatus::Failed;
	}

	// Reset previous action
	InstanceData.PreviousMacroAction = FMacroAction();

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_ExecuteMovement::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Check abort conditions
	if (!SharedContext.bIsAlive || !SharedContext.CurrentObjective)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// Get current action
	const FTacticalAction& Action = SharedContext.CurrentAtomicAction;
	const FMacroAction& CurrentMacro = Action.MacroAction;
	const FMacroAction& PreviousMacro = InstanceData.PreviousMacroAction;

	// Detect action changes (only update movement when position choice changes)
	bool bActionChanged = (CurrentMacro.PositionChoice != PreviousMacro.PositionChoice);

	if (bActionChanged)
	{
		ExecuteMovement(Context, Action, DeltaTime);
		InstanceData.PreviousMacroAction = CurrentMacro;
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ExecuteMovement::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Cleanup if needed
}

void FSTTask_ExecuteMovement::ExecuteMovement(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn || !InstanceData.AIController)
	{
		return;
	}

	const FMacroAction& Macro = Action.MacroAction;
	TArray<FVector> CandidatePositions = QueryEQSPositions(Context, Macro.PositionChoice);

	if (CandidatePositions.Num() > 0)
	{
		float AcceptanceRadius = 100.0f;
		FVector TargetPos = CandidatePositions[0];
		FVector CurrentPos = Pawn->GetActorLocation();

		UWorld* World = Pawn->GetWorld();
		UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;

		// Project target onto NavMesh
		FVector NavTargetPos = TargetPos;
		if (NavSys)
		{
			FNavLocation NavLoc;
			if (NavSys->ProjectPointToNavigation(TargetPos, NavLoc, FVector(500, 500, 500)))
			{
				NavTargetPos = NavLoc.Location;
			}
		}

		// Check if already moving to same destination (within tolerance)
		const float SameDestinationTolerance = 200.0f;
		UPathFollowingComponent* PathComp = InstanceData.AIController->GetPathFollowingComponent();
		if (PathComp && PathComp->GetStatus() == EPathFollowingStatus::Moving)
		{
			FVector CurrentDestination = SharedContext.MovementDestination;
			float DistanceToNewTarget = FVector::Dist(CurrentDestination, NavTargetPos);

			if (DistanceToNewTarget < SameDestinationTolerance)
			{
				return; // Keep current movement
			}
			else
			{
				InstanceData.AIController->StopMovement();
			}
		}

		// Issue new movement command
		FAIMoveRequest MoveReq(NavTargetPos);
		MoveReq.SetAcceptanceRadius(AcceptanceRadius);
		MoveReq.SetUsePathfinding(true);

		FPathFollowingRequestResult MoveResult = InstanceData.AIController->MoveTo(MoveReq);

		SharedContext.MovementDestination = NavTargetPos;
		SharedContext.bIsMoving = (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful);

		if (MoveResult.Code == EPathFollowingRequestResult::Failed)
		{
			UE_LOG(LogTemp, Error, TEXT("[MOVE] '%s': MoveTo FAILED"), *Pawn->GetName());
		}
	}
	else
	{
		// Fallback for Advance: direct pathfinding to objective
		if (Macro.PositionChoice == ETacticalPosition::Advance && SharedContext.CurrentObjective)
		{
			FVector ObjectiveLocation = SharedContext.CurrentObjective->TargetLocation;
			if (!ObjectiveLocation.IsNearlyZero())
			{
				FAIMoveRequest MoveReq(ObjectiveLocation);
				MoveReq.SetAcceptanceRadius(200.0f);
				MoveReq.SetUsePathfinding(true);

				FPathFollowingRequestResult MoveResult = InstanceData.AIController->MoveTo(MoveReq);
				if (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful)
				{
					SharedContext.MovementDestination = ObjectiveLocation;
					SharedContext.bIsMoving = true;
					return;
				}
			}
		}

		InstanceData.AIController->StopMovement();
		SharedContext.bIsMoving = false;
	}
}

TArray<FVector> FSTTask_ExecuteMovement::QueryEQSPositions(FStateTreeExecutionContext& Context, ETacticalPosition PositionType) const
{
	TArray<FVector> Results;
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	APawn* Pawn = InstanceData.ControlledPawn;

	if (!Pawn)
	{
		return Results;
	}

	FVector AgentLocation = Pawn->GetActorLocation();

	switch (PositionType)
	{
	case ETacticalPosition::Hold:
		Results.Add(AgentLocation);
		break;
	case ETacticalPosition::ForwardCover:
		Results = RunEQSQuery(Pawn, InstanceData.ForwardCoverQuery, TEXT("ForwardCover"));
		break;
	case ETacticalPosition::Retreat:
		Results = RunEQSQuery(Pawn, InstanceData.RetreatQuery, TEXT("Retreat"));
		break;
	case ETacticalPosition::Advance:
		Results = RunEQSQuery(Pawn, InstanceData.AdvanceQuery, TEXT("Advance"));
		break;
	}

	return Results;
}

TArray<FVector> FSTTask_ExecuteMovement::RunEQSQuery(APawn* Pawn, UEnvQuery* Query, const FString& QueryName) const
{
	TArray<FVector> Results;

	if (!Pawn || !Query)
	{
		return Results;
	}

	UWorld* World = Pawn->GetWorld();
	if (!World)
	{
		return Results;
	}

	UEnvQueryManager* QueryManager = UEnvQueryManager::GetCurrent(World);
	if (!QueryManager)
	{
		return Results;
	}

	FEnvQueryRequest QueryRequest(Query, Pawn);
	TSharedPtr<FEnvQueryResult> QueryResult = QueryManager->RunInstantQuery(QueryRequest, EEnvQueryRunMode::AllMatching);

	if (!QueryResult.IsValid() || !QueryResult->IsSuccessful())
	{
		return Results;
	}

	const int32 ItemCount = QueryResult->Items.Num();
	if (ItemCount == 0)
	{
		return Results;
	}

	Results.Reserve(ItemCount);

	for (int i = 0; i < ItemCount; ++i)
	{
		FVector ItemLocation = QueryResult->GetItemAsLocation(i);
		if (!ItemLocation.IsZero())
		{
			Results.Add(ItemLocation);
		}
	}

	return Results;
}
