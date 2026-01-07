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
	if (!SharedContext.bIsAlive)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// v6.0: Get current strategy from appropriate source
	if (!SharedContext.FollowerComponent)
	{
		return EStateTreeRunStatus::Failed;
	}

	// CRITICAL FIX: Read strategy from correct source based on training mode
	// - Schola training: Read from SharedContext.CurrentAction (Python-driven)
	// - Normal/Inference: Read from FollowerComponent (RL policy or fallback heuristic)
	EStrategyType CurrentStrategy;
	if (SharedContext.bScholaActionReceived)
	{
		// Schola training mode: Actions come from Python via TacticalActuator
		CurrentStrategy = SharedContext.CurrentAction.Strategy;
	}
	else
	{
		// Normal/Inference mode: Actions come from RL policy or fallback heuristic
		CurrentStrategy = SharedContext.FollowerComponent->GetCurrentStrategy();
	}

	EStrategyType PreviousStrategy = InstanceData.PreviousMacroAction.Strategy;

	// Detect strategy change
	if (CurrentStrategy != PreviousStrategy)
	{
		// Map strategy to tactical position (deterministic)
		ETacticalPosition TargetPosition = StrategyToPosition(CurrentStrategy, SharedContext);

		// Execute movement
		ExecuteMovementToPosition(Context, TargetPosition, DeltaTime);

		// Update previous strategy
		InstanceData.PreviousMacroAction.Strategy = CurrentStrategy;
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ExecuteMovement::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Cleanup if needed
}

ETacticalPosition FSTTask_ExecuteMovement::StrategyToPosition(
	EStrategyType Strategy,
	const FFollowerStateTreeContext& Context) const
{
	switch (Strategy)
	{
		case EStrategyType::Assault:
			return ETacticalPosition::ForwardCover;

		case EStrategyType::Defend:
			return ETacticalPosition::Hold;

		case EStrategyType::Support:
			// Move toward ally in need
			if (Context.FollowerComponent && Context.FollowerComponent->GetAllyContext().bAllyNeedsHelp)
			{
				return ETacticalPosition::ForwardCover; // Toward ally
			}
			return ETacticalPosition::Hold;

		case EStrategyType::Retreat:
			return ETacticalPosition::Retreat;

		default:
			return ETacticalPosition::Hold;
	}
}

void FSTTask_ExecuteMovement::ExecuteMovementToPosition(FStateTreeExecutionContext& Context, ETacticalPosition PositionType, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn || !InstanceData.AIController)
	{
		return;
	}

	// Query EQS for tactical positions based on position type
	TArray<FVector> CandidatePositions = QueryEQSPositions(Context, PositionType);

	// FALLBACK: If EQS returns no valid positions, stay in place (Hold)
	if (CandidatePositions.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MOVE v6.0] '%s': EQS returned 0 positions for %s, falling back to Hold"),
			*Pawn->GetName(), *UEnum::GetValueAsString(PositionType));

		// Stop current movement and hold position
		if (InstanceData.AIController)
		{
			InstanceData.AIController->StopMovement();
		}
		SharedContext.bIsMoving = false;
		return;
	}

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

		if (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful)
		{
			/*UE_LOG(LogTemp, Display, TEXT("[MOVE v6.0] '%s': Moving to %s (Strategy: %s)"),
				*Pawn->GetName(),
				*UEnum::GetValueAsString(PositionType),
				*UEnum::GetValueAsString(SharedContext.FollowerComponent->GetCurrentStrategy()));*/
		}
		else
		{
			// Log detailed failure reason
			FString FailureReason;
			switch (MoveResult.Code)
			{
				case EPathFollowingRequestResult::Failed:
					FailureReason = TEXT("Pathfinding failed (no valid path to target)");
					break;
				case EPathFollowingRequestResult::AlreadyAtGoal:
					FailureReason = TEXT("Already at goal");
					break;
				default:
					FailureReason = FString::Printf(TEXT("Unknown failure code %d"), static_cast<int32>(MoveResult.Code));
					break;
			}

			/*UE_LOG(LogTemp, Error, TEXT("[MOVE v6.0] '%s': MoveTo FAILED - %s (Position: %s, Target: %s)"),
				*Pawn->GetName(),
				*FailureReason,
				*UEnum::GetValueAsString(PositionType),
				*NavTargetPos.ToCompactString());*/
		}
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
