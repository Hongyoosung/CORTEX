// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/STTask_ExecuteTacticalMovement.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Team/Components/TeamCommsComponent.h"
#include "Team/ObjectiveActor.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryInstanceBlueprintWrapper.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Schola/ScholaAgentComponent.h"
#include "Schola/ScholaCombatEnvironment.h"
// v9.0 Phase 4: Use character API instead of FindComponentByClass
#include "Actor/FollowerCharacter.h"

EStateTreeRunStatus FSTTask_ExecuteTacticalMovement::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	UE_LOG(LogTemp, Warning, TEXT("[TACTICAL v8.0] '%s': Entering tactical movement state"),
		InstanceData.ControlledPawn ? *InstanceData.ControlledPawn->GetName() : TEXT("Unknown"));

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

EStateTreeRunStatus FSTTask_ExecuteTacticalMovement::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
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
	// v9.0 FIX: TRAINING MODE CHECK with PIE Override
	// Allows PIE-only testing without Python connection
	// ========================================

	APawn* Pawn = InstanceData.ControlledPawn;
	if (Pawn)
	{
		// v9.0 FIX: Check if we're in PIE mode (development testing)
		const bool bIsPIE = (Pawn->GetWorld()->WorldType == EWorldType::PIE);

		// v9.0 Phase 4: Use character API (ScholaAgent still uses FindComponentByClass as it's not on character interface)
		UScholaAgentComponent* ScholaAgent = Pawn->FindComponentByClass<UScholaAgentComponent>();
		if (ScholaAgent && ScholaAgent->ScholaEnvironment)
		{
			// v9.0 FIX: Allow PIE testing OR training mode
			const bool bShouldBypassGate = bIsPIE || ScholaAgent->ScholaEnvironment->bTrainingActive;

			if (!bShouldBypassGate)
			{
				// Training not active and not PIE - skip movement
				static bool bLoggedOnce = false;
				if (!bLoggedOnce)
				{
					UE_LOG(LogTemp, Warning, TEXT("🔒 [TACTICAL v9.0] Movement DISABLED - Waiting for Python training connection"));
					UE_LOG(LogTemp, Warning, TEXT("   Run docker_train.bat or train_rllib.py to start training"));
					bLoggedOnce = true;
				}
				return EStateTreeRunStatus::Running;
			}
			else if (bIsPIE && !ScholaAgent->ScholaEnvironment->bTrainingActive)
			{
				// PIE mode without training - log once for clarity
				static bool bLoggedPIE = false;
				if (!bLoggedPIE)
				{
					UE_LOG(LogTemp, Log, TEXT("🎮 [TACTICAL v9.0] Movement ENABLED - PIE mode (development testing without Python)"));
					bLoggedPIE = true;
				}
			}
			else
			{
				// Training active - log once for clarity
				static bool bLoggedTraining = false;
				if (!bLoggedTraining)
				{
					UE_LOG(LogTemp, Log, TEXT("✅ [TACTICAL v9.0] Movement ENABLED - Python training connected"));
					bLoggedTraining = true;
				}
			}
		}
		else if (!bIsPIE)
		{
			// No Schola setup and not PIE - skip movement
			static bool bLoggedNoSchola = false;
			if (!bLoggedNoSchola)
			{
				UE_LOG(LogTemp, Warning, TEXT("🔒 [TACTICAL v9.0] Movement DISABLED - ScholaAgentComponent not found (not PIE mode)"));
				UE_LOG(LogTemp, Warning, TEXT("   Add ScholaAgentComponent to agents and connect to Python"));
				bLoggedNoSchola = true;
			}
			return EStateTreeRunStatus::Running;
		}
		else
		{
			// PIE mode without Schola - allow movement for testing
			static bool bLoggedPIENoSchola = false;
			if (!bLoggedPIENoSchola)
			{
				UE_LOG(LogTemp, Log, TEXT("🎮 [TACTICAL v9.0] Movement ENABLED - PIE mode without Schola (development testing)"));
				bLoggedPIENoSchola = true;
			}
		}
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

		UE_LOG(LogTemp, Warning, TEXT("[TACTICAL v8.0] '%s': Updated tactical params - Aggression=%.2f, Cover=%.2f, Spread=%.2f, Risk=%.2f"),
			*InstanceData.ControlledPawn->GetName(),
			TacticalParams.Aggression,
			TacticalParams.CoverPreference,
			TacticalParams.SpreadDistance,
			TacticalParams.RiskTolerance);
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ExecuteTacticalMovement::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
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

void FSTTask_ExecuteTacticalMovement::ApplyTacticalParameters(
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
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();
	APawn* Pawn = InstanceData.ControlledPawn;

	if (!Pawn || !InstanceData.TacticalPositionQuery)
	{
		return;
	}

	// Run EQS query with modulated weights (v9.0: includes objective-aware scoring)
	TArray<FVector> CandidatePositions = RunTacticalEQSQuery(
		Pawn,
		InstanceData.TacticalPositionQuery,
		Params,
		SharedContext.AssignedStrategy
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

TArray<FVector> FSTTask_ExecuteTacticalMovement::RunTacticalEQSQuery(
	APawn* Pawn,
	UEnvQuery* Query,
	FTacticalParameters Params,
	EStrategyType AssignedStrategy) const
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
	float AggressionWeight = FMath::Lerp(0.5f, 5.0f, Params.Aggression); // [0.5, 5.0]

	// 2. CoverPreference → Cover weight
	//    High preference (0.9) → CoverWeight = 5.0 (heavily prioritize cover)
	//    Low preference (0.1) → CoverWeight = 0.5 (mostly ignore cover)
	float CoverWeight = FMath::Lerp(0.5f, 5.0f, Params.CoverPreference);

	// 3. SpreadDistance → Formation spread
	//    High spread (0.9) → IdealSpread = 1000cm (10m, dispersed)
	//    Low spread (0.1) → IdealSpread = 200cm (2m, tight formation)
	float IdealSpreadDistance = FMath::Lerp(200.0f, 1000.0f, Params.SpreadDistance);

	// v9.0 FIX: Strategy-dependent formation weight
	// Base weight from RL parameter, then modulate by strategy needs
	float BaseFormationWeight = FMath::Lerp(5.0f, 1.0f, Params.SpreadDistance);

	float StrategyFormationMultiplier = 1.0f;
	switch (AssignedStrategy)
	{
		case EStrategyType::Assault:
			StrategyFormationMultiplier = 0.4f;  // Spread out to attack (FormationWeight: 0.4-2.0)
			break;
		case EStrategyType::Defend:
			StrategyFormationMultiplier = 1.0f;  // Tight defensive line (FormationWeight: 1.0-5.0)
			break;
		case EStrategyType::Support:
			StrategyFormationMultiplier = 0.6f;  // Some spread for coverage (FormationWeight: 0.6-3.0)
			break;
		case EStrategyType::Retreat:
			StrategyFormationMultiplier = 0.3f;  // Scatter to evade (FormationWeight: 0.3-1.5)
			break;
		default:
			StrategyFormationMultiplier = 1.0f;
			break;
	}

	float FormationWeight = BaseFormationWeight * StrategyFormationMultiplier;

	// 4. v9.0: ObjectiveWeight → Strategy-dependent objective focus
	//    Assault: High weight (approach hostile objective)
	//    Defend: Very high weight (stay near friendly objective)
	//    Support: Low weight (ally proximity dominates)
	//    Retreat: No weight (enemy avoidance dominates)
	float ObjectiveWeight = 0.0f;
	switch (AssignedStrategy)
	{
		case EStrategyType::Assault:
			ObjectiveWeight = 5.0f; // Balanced with aggression
			break;
		case EStrategyType::Defend:
			ObjectiveWeight = 8.0f; // Highest priority - hold position
			break;
		case EStrategyType::Support:
			ObjectiveWeight = 1.0f; // Secondary to ally proximity
			break;
		case EStrategyType::Retreat:
			ObjectiveWeight = 0.0f; // No objective focus
			break;
		default:
			ObjectiveWeight = 0.0f;
			break;
	}

	// v9.0 FIX: Get objective location based on strategy
	// v9.0 Phase 4: Use character API instead of FindComponentByClass
	AObjectiveActor* TargetObjective = nullptr;
	AFollowerCharacter* FollowerChar = Cast<AFollowerCharacter>(Pawn);
	if (FollowerChar)
	{
		// Get Leader via character API (no FindComponentByClass)
		UTeamLeaderComponent* TeamLeader = FollowerChar->GetTeamLeader();
		if (TeamLeader)
		{

			// 4. Determine which objective to use based on strategy
			switch (AssignedStrategy)
			{
			case EStrategyType::Assault:
				TargetObjective = TeamLeader->GetHostileObjective();
				break;
			case EStrategyType::Defend:
				TargetObjective = TeamLeader->GetFriendlyObjective();
				break;
			case EStrategyType::Support:
				TargetObjective = TeamLeader->GetFriendlyObjective();
				break;
			case EStrategyType::Retreat:
				TargetObjective = TeamLeader->GetFriendlyObjective();
				break;
			default:
				break;
			}
			
		}
	}

	// Create EQS request with modulated parameters
	FEnvQueryRequest QueryRequest(Query, Pawn);

	// Apply dynamic parameters to EQS query
	// NOTE: These parameter names MUST match the EQS query asset parameter names
	QueryRequest.SetFloatParam(TEXT("MinDistanceToEnemy"), MinDistanceToEnemy);
	QueryRequest.SetFloatParam(TEXT("AggressionWeight"), AggressionWeight);
	QueryRequest.SetFloatParam(TEXT("CoverWeight"), CoverWeight);
	QueryRequest.SetFloatParam(TEXT("FormationSpread"), IdealSpreadDistance);
	QueryRequest.SetFloatParam(TEXT("FormationWeight"), FormationWeight);
	QueryRequest.SetFloatParam(TEXT("ObjectiveWeight"), ObjectiveWeight); // v9.0: Objective-aware scoring

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

	// v9.0 FIX: Enhanced debug log with objective location validation
	const TCHAR* StrategyName = nullptr;
	switch (AssignedStrategy)
	{
		case EStrategyType::Assault: StrategyName = TEXT("Assault"); break;
		case EStrategyType::Defend: StrategyName = TEXT("Defend"); break;
		case EStrategyType::Support: StrategyName = TEXT("Support"); break;
		case EStrategyType::Retreat: StrategyName = TEXT("Retreat"); break;
		default: StrategyName = TEXT("Unknown"); break;
	}

	if (TargetObjective)
	{
		FVector ObjectiveLocation = TargetObjective->GetActorLocation();
		float DistanceToObjective = FVector::Dist(Pawn->GetActorLocation(), ObjectiveLocation);
		UE_LOG(LogTemp, Log, TEXT("✅ [EQS v9.0] %s | %d positions | ObjWeight=%.1f, FormWeight=%.1f | Objective='%s' at %.0fcm"),
			StrategyName, Results.Num(), ObjectiveWeight, FormationWeight,
			*TargetObjective->GetName(), DistanceToObjective);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ [EQS v9.0] %s | %d positions | ObjWeight=%.1f, FormWeight=%.1f | NO OBJECTIVE AVAILABLE"),
			StrategyName, Results.Num(), ObjectiveWeight, FormationWeight);
	}

	// Log top 5 positions with scores for debugging
	for (int32 i = 0; i < FMath::Min(5, ItemCount); ++i)
	{
		FVector ItemLocation = QueryResult->GetItemAsLocation(i);
		float ItemScore = QueryResult->GetItemScore(i);
		UE_LOG(LogTemp, Log, TEXT("  [%d] Pos=%s, Score=%.2f"),
			i, *ItemLocation.ToCompactString(), ItemScore);
	}

	return Results;
}

void FSTTask_ExecuteTacticalMovement::ExecuteMovementToTacticalPosition(
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
