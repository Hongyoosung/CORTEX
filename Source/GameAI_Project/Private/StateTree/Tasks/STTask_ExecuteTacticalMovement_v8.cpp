// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/STTask_ExecuteTacticalMovement_v8.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryInstanceBlueprintWrapper.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

EStateTreeRunStatus FSTTask_ExecuteTacticalMovement_v8::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.StateTreeComp)
	{
		UE_LOG(LogTemp, Error, TEXT("[TACTICAL v8.0] StateTreeComp is null"));
		return EStateTreeRunStatus::Failed;
	}

	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	if (!SharedContext.FollowerComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[TACTICAL v8.0] Missing FollowerComponent"));
		return EStateTreeRunStatus::Failed;
	}

	if (!InstanceData.ControlledPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[TACTICAL v8.0] ControlledPawn not bound"));
		return EStateTreeRunStatus::Failed;
	}

	// Validate EQS query
	if (!InstanceData.TacticalPositionQuery)
	{
		UE_LOG(LogTemp, Error, TEXT("[TACTICAL v8.0] TacticalPositionQuery not assigned"));
		return EStateTreeRunStatus::Failed;
	}

	// Set agent component reference
	InstanceData.AgentComponent = SharedContext.FollowerComponent;

	// Reset previous params
	InstanceData.PreviousTacticalParams = FTacticalParameters();
	InstanceData.TicksSinceLastUpdate = 999; // Force immediate update

	UE_LOG(LogTemp, Log, TEXT("[TACTICAL v8.0] '%s': Entered tactical movement state"),
		*InstanceData.ControlledPawn->GetName());

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_ExecuteTacticalMovement_v8::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	// ========================================
	// v8.0 EXECUTION LAYER (Layer 3 of Hierarchy)
	//
	// ARCHITECTURE:
	// Layer 1 (MCTS): Assigns strategies to agents
	// Layer 2 (RL): Outputs tactical parameters [0,1]^4
	// Layer 3 (EQS): Uses parameters as query weights → This task
	// Layer 4 (Rules): Combat execution (auto-targeting, firing)
	//
	// THIS TASK'S ROLE:
	// - Read tactical parameters from RL policy (via FollowerAgentComponent)
	// - Modulate EQS query weights based on parameters
	// - Execute EQS-selected position via NavMesh
	// - Update at configurable frequency (default: 5 Hz)
	// ========================================

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Check abort conditions
	if (!SharedContext.bIsAlive)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	if (!InstanceData.AgentComponent || !InstanceData.ControlledPawn || !InstanceData.AIController)
	{
		return EStateTreeRunStatus::Failed;
	}

	// ========================================
	// v8.0: Event-driven or periodic parameter updates (2-5 Hz)
	// Reduces inference cost while maintaining responsiveness
	// ========================================

	InstanceData.TicksSinceLastUpdate++;

	// Calculate update interval based on frequency (60 FPS / 5 Hz = 12 ticks)
	int32 UpdateInterval = FMath::RoundToInt(60.0f / InstanceData.UpdateFrequencyHz);

	if (InstanceData.TicksSinceLastUpdate < UpdateInterval)
	{
		// Continue executing current movement, don't query new parameters yet
		return EStateTreeRunStatus::Running;
	}

	InstanceData.TicksSinceLastUpdate = 0;

	// Get current macro action from RL policy (tactical + combat parameters)
	FMacroAction CurrentAction = InstanceData.AgentComponent->GetCurrentMacroAction();
	FTacticalParameters TacticalParams = CurrentAction.TacticalParams;

	// Clamp parameters to [0,1] range (safety check)
	TacticalParams.Clamp();

	// Detect significant parameter changes (avoid redundant EQS queries)
	bool bParamsChanged =
		FMath::Abs(TacticalParams.Aggression - InstanceData.PreviousTacticalParams.Aggression) > 0.1f ||
		FMath::Abs(TacticalParams.CoverPreference - InstanceData.PreviousTacticalParams.CoverPreference) > 0.1f ||
		FMath::Abs(TacticalParams.SpreadDistance - InstanceData.PreviousTacticalParams.SpreadDistance) > 0.1f;

	if (bParamsChanged || InstanceData.TicksSinceLastUpdate == 0)
	{
		// Apply tactical parameters to EQS query and execute movement
		ApplyTacticalParameters(Context, TacticalParams);

		// Update previous params
		InstanceData.PreviousTacticalParams = TacticalParams;

		UE_LOG(LogTemp, Verbose, TEXT("[TACTICAL v8.0] '%s': Updated tactical params - Aggression=%.2f, Cover=%.2f, Spread=%.2f, Risk=%.2f"),
			*InstanceData.ControlledPawn->GetName(),
			TacticalParams.Aggression,
			TacticalParams.CoverPreference,
			TacticalParams.SpreadDistance,
			TacticalParams.RiskTolerance);
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ExecuteTacticalMovement_v8::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// Stop movement on exit
	if (InstanceData.AIController)
	{
		InstanceData.AIController->StopMovement();
	}

	UE_LOG(LogTemp, Log, TEXT("[TACTICAL v8.0] '%s': Exited tactical movement state"),
		InstanceData.ControlledPawn ? *InstanceData.ControlledPawn->GetName() : TEXT("Unknown"));
}

void FSTTask_ExecuteTacticalMovement_v8::ApplyTacticalParameters(
	FStateTreeExecutionContext& Context,
	FTacticalParameters Params) const
{
	// ========================================
	// v8.0: CRITICAL INNOVATION - RL-Controlled EQS Weight Modulation
	//
	// Instead of fixed strategy→position mapping (v7.0):
	//   Assault → ForwardCover (fixed EQS query)
	//
	// v8.0 uses continuous parameter control:
	//   Aggression=0.8 → MinDistance=280cm, CoverWeight=1.5
	//   Aggression=0.3 → MinDistance=860cm, CoverWeight=3.5
	//
	// This allows RL to learn NUANCED tactical behaviors:
	// - "Aggressive Assault" (Aggression=0.9, Cover=0.2)
	// - "Cautious Assault" (Aggression=0.6, Cover=0.6)
	// - "Defensive Hold" (Aggression=0.1, Cover=0.9)
	// ========================================

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	APawn* Pawn = InstanceData.ControlledPawn;

	if (!Pawn || !InstanceData.TacticalPositionQuery)
	{
		return;
	}

	// Run EQS query with modulated weights
	TArray<FVector> CandidatePositions = RunTacticalEQSQuery(
		Pawn,
		InstanceData.TacticalPositionQuery,
		Params
	);

	// FALLBACK: If EQS returns no valid positions, hold current position
	if (CandidatePositions.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TACTICAL v8.0] '%s': EQS returned 0 positions, holding position"),
			*Pawn->GetName());

		if (InstanceData.AIController)
		{
			InstanceData.AIController->StopMovement();
		}
		return;
	}

	// Execute movement to best position
	FVector TargetPosition = CandidatePositions[0];
	ExecuteMovementToTacticalPosition(Context, TargetPosition);

	// Store risk tolerance for retreat logic (used outside EQS)
	if (InstanceData.AgentComponent)
	{
		// Risk tolerance determines when to retreat (health threshold)
		// High risk (0.9) = fight to near death (10% HP)
		// Low risk (0.1) = retreat early (70% HP)
		float RetreatThreshold = FMath::Lerp(0.7f, 0.1f, Params.RiskTolerance);

		// This can be used by combat logic to trigger retreat strategy
		// For now, just log it (full retreat integration happens in v8.5)
		UE_LOG(LogTemp, Verbose, TEXT("[TACTICAL v8.0] '%s': Retreat threshold set to %.0f%% HP (Risk=%.2f)"),
			*Pawn->GetName(), RetreatThreshold * 100.0f, Params.RiskTolerance);
	}
}

TArray<FVector> FSTTask_ExecuteTacticalMovement_v8::RunTacticalEQSQuery(
	APawn* Pawn,
	UEnvQuery* Query,
	FTacticalParameters Params) const
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

	// ========================================
	// v8.0: Parameter-to-EQS Weight Mapping
	// ========================================

	// 1. Aggression → Distance to enemy scoring
	//    High aggression (0.9) → MinDistance = 200cm (2m, close range)
	//    Low aggression (0.1) → MinDistance = 1000cm (10m, safe distance)
	float MinDistanceToEnemy = FMath::Lerp(1000.0f, 200.0f, Params.Aggression);
	float AggressionWeight = Params.Aggression * 2.0f; // [0, 2] range for EQS scoring

	// 2. CoverPreference → Cover weight
	//    High preference (0.9) → CoverWeight = 5.0 (heavily prioritize cover)
	//    Low preference (0.1) → CoverWeight = 0.5 (mostly ignore cover)
	float CoverWeight = FMath::Lerp(0.5f, 5.0f, Params.CoverPreference);
	float ExposureWeight = (1.0f - Params.CoverPreference) * 3.0f; // Inverse for open positions

	// 3. SpreadDistance → Formation spread
	//    High spread (0.9) → IdealSpread = 1000cm (10m, dispersed)
	//    Low spread (0.1) → IdealSpread = 200cm (2m, tight formation)
	float IdealSpreadDistance = FMath::Lerp(200.0f, 1000.0f, Params.SpreadDistance);
	float FormationWeight = 3.0f; // Constant weight, distance varies

	// Create EQS request with modulated parameters
	FEnvQueryRequest QueryRequest(Query, Pawn);

	// Apply dynamic parameters to EQS query
	// NOTE: These parameter names MUST match the EQS query asset parameter names
	QueryRequest.SetFloatParam(TEXT("MinDistanceToEnemy"), MinDistanceToEnemy);
	QueryRequest.SetFloatParam(TEXT("AggressionWeight"), AggressionWeight);
	QueryRequest.SetFloatParam(TEXT("CoverWeight"), CoverWeight);
	QueryRequest.SetFloatParam(TEXT("ExposureWeight"), ExposureWeight);
	QueryRequest.SetFloatParam(TEXT("FormationSpread"), IdealSpreadDistance);
	QueryRequest.SetFloatParam(TEXT("FormationWeight"), FormationWeight);

	// Execute query (instant, not async)
	TSharedPtr<FEnvQueryResult> QueryResult = QueryManager->RunInstantQuery(
		QueryRequest,
		EEnvQueryRunMode::AllMatching
	);

	if (!QueryResult.IsValid() || !QueryResult->IsSuccessful())
	{
		UE_LOG(LogTemp, Warning, TEXT("[TACTICAL v8.0] EQS query failed"));
		return Results;
	}

	const int32 ItemCount = QueryResult->Items.Num();
	if (ItemCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TACTICAL v8.0] EQS returned 0 items! Check EQS query configuration and context providers."));
		return Results;
	}

	Results.Reserve(ItemCount);

	// Extract positions from query results (sorted by score, best first)
	for (int i = 0; i < ItemCount; ++i)
	{
		FVector ItemLocation = QueryResult->GetItemAsLocation(i);
		if (!ItemLocation.IsZero())
		{
			Results.Add(ItemLocation);
		}
	}

	/*UE_LOG(LogTemp, Log, TEXT("[TACTICAL v8.0] EQS returned %d positions (Aggression=%.2f → MinDist=%.0fcm, Cover=%.2f → Weight=%.1f)"),
		Results.Num(), Params.Aggression, MinDistanceToEnemy, Params.CoverPreference, CoverWeight);*/

	return Results;
}

void FSTTask_ExecuteTacticalMovement_v8::ExecuteMovementToTacticalPosition(
	FStateTreeExecutionContext& Context,
	FVector TargetPosition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn || !InstanceData.AIController)
	{
		return;
	}

	UWorld* World = Pawn->GetWorld();
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;

	// Project target onto NavMesh
	FVector NavTargetPos = TargetPosition;
	if (NavSys)
	{
		FNavLocation NavLoc;
		if (NavSys->ProjectPointToNavigation(TargetPosition, NavLoc, FVector(500, 500, 500)))
		{
			NavTargetPos = NavLoc.Location;
		}
	}

	// Check if already moving to same destination (within tolerance)
	const float SameDestinationTolerance = 200.0f; // 2m
	UPathFollowingComponent* PathComp = InstanceData.AIController->GetPathFollowingComponent();
	if (PathComp && PathComp->GetStatus() == EPathFollowingStatus::Moving)
	{
		FVector CurrentDestination = SharedContext.MovementDestination;
		float DistanceToNewTarget = FVector::Dist(CurrentDestination, NavTargetPos);

		if (DistanceToNewTarget < SameDestinationTolerance)
		{
			// Already moving to this position, keep current movement
			return;
		}
		else
		{
			// New destination, stop current movement
			InstanceData.AIController->StopMovement();
		}
	}

	// Issue new movement command
	FAIMoveRequest MoveReq(NavTargetPos);
	MoveReq.SetAcceptanceRadius(100.0f); // 1m acceptance radius
	MoveReq.SetUsePathfinding(true);

	FPathFollowingRequestResult MoveResult = InstanceData.AIController->MoveTo(MoveReq);

	// Update shared context
	SharedContext.MovementDestination = NavTargetPos;
	SharedContext.bIsMoving = (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful);

	if (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[TACTICAL v8.0] '%s': Moving to tactical position %s"),
			*Pawn->GetName(),
			*NavTargetPos.ToCompactString());
	}
	else
	{
		// Log detailed failure reason
		FString FailureReason;
		switch (MoveResult.Code)
		{
			case EPathFollowingRequestResult::Failed:
				FailureReason = TEXT("Pathfinding failed (no valid path)");
				break;
			case EPathFollowingRequestResult::AlreadyAtGoal:
				FailureReason = TEXT("Already at goal");
				break;
			default:
				FailureReason = FString::Printf(TEXT("Unknown failure code %d"), static_cast<int32>(MoveResult.Code));
				break;
		}

		/*UE_LOG(LogTemp, Warning, TEXT("[TACTICAL v8.0] '%s': MoveTo FAILED - %s (Target: %s)"),
			*Pawn->GetName(),
			*FailureReason,
			*NavTargetPos.ToCompactString());*/
	}
}
