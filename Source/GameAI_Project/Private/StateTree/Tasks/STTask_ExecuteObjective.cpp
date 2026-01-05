// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/STTask_ExecuteObjective.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Team/FollowerAgentComponent.h"
#include "Team/Objective.h"
#include "Combat/WeaponComponent.h"
#include "Combat/HealthComponent.h"
#include "AIController.h"
#include "AITypes.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RL/RLPolicyNetwork.h"
#include "DrawDebugHelpers.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryInstanceBlueprintWrapper.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "DrawDebugHelpers.h"
#include "AI/Navigation/NavigationTypes.h"
#include "TimerManager.h"
#include "NavMesh/NavMeshPath.h"

EStateTreeRunStatus FSTTask_ExecuteObjective::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.StateTreeComp)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_ExecuteObjective: StateTreeComp is null"));
		return EStateTreeRunStatus::Failed;
	}

	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// CRITICAL: Only require FollowerComponent (AIController is optional for Schola compatibility)
	if (!SharedContext.FollowerComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_ExecuteObjective: Missing FollowerComponent"));
		return EStateTreeRunStatus::Failed;
	}

	// Get Pawn from InstanceData (bound to FollowerContext.ControlledPawn)
	APawn* Pawn = InstanceData.ControlledPawn;

	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ STTask_ExecuteObjective: ControlledPawn not bound in StateTree asset!"));
		UE_LOG(LogTemp, Error, TEXT("   Bind 'ControlledPawn' to 'FollowerContext.ControlledPawn' in the StateTree asset."));
		return EStateTreeRunStatus::Failed;
	}

	SharedContext.TimeInTacticalAction = 0.0f;
	SharedContext.ActionProgress = 0.0f;

	// Reset fire update timer and previous action (to trigger initial execution)
	InstanceData.TimeSinceLastFire = 0.0f;
	InstanceData.PreviousMacroAction = FMacroAction();  // Default-initialized (will differ from any real action)

	// v5.0: Validate EQS queries are assigned (prevent runtime failures)
	if (!InstanceData.ForwardCoverQuery || !InstanceData.RetreatQuery || !InstanceData.AdvanceQuery)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ STTask_ExecuteObjective: EQS queries not assigned!"));
		UE_LOG(LogTemp, Error, TEXT("   ForwardCoverQuery: %s"), InstanceData.ForwardCoverQuery ? TEXT("OK") : TEXT("MISSING"));
		UE_LOG(LogTemp, Error, TEXT("   RetreatQuery: %s"), InstanceData.RetreatQuery ? TEXT("OK") : TEXT("MISSING"));
		UE_LOG(LogTemp, Error, TEXT("   AdvanceQuery: %s"), InstanceData.AdvanceQuery ? TEXT("OK") : TEXT("MISSING"));
		UE_LOG(LogTemp, Error, TEXT("   → Assign EQS queries in StateTree editor: STTask_ExecuteObjective properties"));
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_ExecuteObjective::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());


	// Check abort conditions
	if (!SharedContext.bIsAlive || !SharedContext.CurrentObjective)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// Check if objective completed or failed
	if (CheckObjectiveStatus(Context))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	SharedContext.TimeInTacticalAction += DeltaTime;

	// v5.0: Fire update throttling - decouple firing rate from FPS
	// Movement/aiming execute once when action changes (macro actions)
	// Firing executes continuously at fixed rate (default 20 Hz)
	InstanceData = Context.GetInstanceData(*this);
	InstanceData.TimeSinceLastFire += DeltaTime;

	bool bShouldUpdateFire = (InstanceData.TimeSinceLastFire >= InstanceData.FireUpdateInterval);

	// v5.0: Execute macro action components
	const FTacticalAction& Action = SharedContext.CurrentAtomicAction;
	const FMacroAction& CurrentMacro = Action.MacroAction;
	const FMacroAction& PreviousMacro = InstanceData.PreviousMacroAction;

	// CRITICAL: Detect action changes for one-shot execution
	// Movement/Aiming only execute when action changes (macro behavior)
	// Firing updates continuously at FireUpdateInterval
	bool bActionChanged = (
		CurrentMacro.PositionChoice != PreviousMacro.PositionChoice ||
		CurrentMacro.TargetIndex != PreviousMacro.TargetIndex ||
		CurrentMacro.FireMode != PreviousMacro.FireMode
	);

	// One-shot execution (only when action changes)
	if (bActionChanged)
	{
		ExecuteMovement(Context, Action, DeltaTime);  // EQS + NavMesh pathfinding
		ExecuteAiming(Context, Action);                // SetFocus (engine auto-aim)
		InstanceData.PreviousMacroAction = CurrentMacro;
	}

	// Continuous firing updates (at fixed rate)
	if (bShouldUpdateFire)
	{
		InstanceData.TimeSinceLastFire = 0.0f;
		ExecuteFire(Context, Action);
	}

	// Calculate and provide reward (every frame, not throttled)
	float Reward = CalculateObjectiveReward(Context, DeltaTime);
	if (Reward != 0.0f && SharedContext.FollowerComponent)
	{
		SharedContext.FollowerComponent->ProvideReward(Reward, false);
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ExecuteObjective::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// v7.3: Removed verbose exit logging to reduce log spam during training
}


void FSTTask_ExecuteObjective::ExecuteMovement(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[MOVE v5.0] No ControlledPawn available"));
		return;
	}

	// FIX (Issue #2): AIController may not be assigned yet (brief period after StateTree starts)
	// Skip movement until AIController is available - this is normal during initialization
	if (!InstanceData.AIController)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[MOVE v5.0] '%s': AIController not yet assigned, skipping movement this tick"), *Pawn->GetName());
		return;
	}

	// Verify AIController controls this pawn (should always be true now)
	if (InstanceData.AIController->GetPawn() != Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[STTask_ExecuteObjective] AIController '%s' doesn't control pawn '%s' - skipping movement"),
			*InstanceData.AIController->GetName(), *Pawn->GetName());
		return;
	}

	// v5.0: Use macro action for high-level tactical movement
	const FMacroAction& Macro = Action.MacroAction;

	TArray<FVector> CandidatePositions = QueryEQSPositions(Context, Macro.PositionChoice);

	if (CandidatePositions.Num() > 0)
	{
		float AcceptanceRadius = 100.0f;
		FVector TargetPos = CandidatePositions[0];
		FVector CurrentPos = Pawn->GetActorLocation();
		float Distance = FVector::Dist(CurrentPos, TargetPos);

		// DIAGNOSTIC: Check all prerequisites for movement
		UCharacterMovementComponent* MoveComp = Pawn->FindComponentByClass<UCharacterMovementComponent>();
		UPathFollowingComponent* PathComp = InstanceData.AIController->GetPathFollowingComponent();
		UWorld* World = Pawn->GetWorld();
		UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;

		// Check if NavMesh has valid path to destination
		bool bPathExists = false;
		FPathFindingQuery Query;
		if (NavSys)
		{
			Query = FPathFindingQuery(*InstanceData.AIController, *NavSys->GetDefaultNavDataInstance(), CurrentPos, TargetPos);
			FPathFindingResult Result = NavSys->FindPathSync(Query);
			bPathExists = Result.IsSuccessful() && Result.Path.IsValid();

			if (!bPathExists)
			{
				// Check if target location is even ON the NavMesh
				FNavLocation NavLoc;
				bool bOnNavMesh = NavSys->ProjectPointToNavigation(TargetPos, NavLoc, FVector(500, 500, 500));

				// Draw debug to show the problem
				DrawDebugSphere(World, TargetPos, 100.0f, 12, FColor::Red, false, 5.0f);
				DrawDebugLine(World, CurrentPos, TargetPos, FColor::Red, false, 5.0f, 0, 3.0f);
			}
		}

		// CRITICAL DIAGNOSTIC: Check PathFollowingComponent status
		if (PathComp)
		{
			EPathFollowingStatus::Type PathStatus = PathComp->GetStatus();
			bool bHasValidPath = PathComp->HasValidPath();

			// v5.0: Check if already moving to same destination (within tolerance)
			// If so, DON'T interrupt - let current movement complete
			const float SameDestinationTolerance = 200.0f;  // 2 meters tolerance
			bool bAlreadyMovingToSameDestination = false;

			if (PathStatus == EPathFollowingStatus::Moving && bHasValidPath)
			{
				// Get current movement destination
				FVector CurrentDestination = SharedContext.MovementDestination;
				float DistanceToNewTarget = FVector::Dist(CurrentDestination, TargetPos);

				if (DistanceToNewTarget < SameDestinationTolerance)
				{
					bAlreadyMovingToSameDestination = true;

					return;  // Early exit - keep current movement
				}
			}

			UE_LOG(LogTemp, Verbose, TEXT("[PATHFOLLOW DIAG] Status=%s, HasValidPath=%d, RequestID=%d"),
				PathStatus == EPathFollowingStatus::Idle ? TEXT("Idle") :
				PathStatus == EPathFollowingStatus::Waiting ? TEXT("Waiting") :
				PathStatus == EPathFollowingStatus::Paused ? TEXT("Paused") :
				PathStatus == EPathFollowingStatus::Moving ? TEXT("Moving") : TEXT("Unknown"),
				bHasValidPath ? 1 : 0,
				(int32)PathComp->GetCurrentRequestId().GetID());

			// v5.0: Only stop if moving to DIFFERENT destination
			if (PathStatus == EPathFollowingStatus::Moving && !bAlreadyMovingToSameDestination)
			{
				UE_LOG(LogTemp, Log, TEXT("[PATHFOLLOW v5.0] '%s': New destination requested, stopping current movement"),
					*Pawn->GetName());
				InstanceData.AIController->StopMovement();
			}
		}

		// FIX: Project target position onto NavMesh before MoveToLocation
		FVector NavTargetPos = TargetPos;
		if (NavSys && !bPathExists)
		{
			FNavLocation NavLoc;
			if (NavSys->ProjectPointToNavigation(TargetPos, NavLoc, FVector(500, 500, 500)))
			{
				NavTargetPos = NavLoc.Location;
				UE_LOG(LogTemp, Warning, TEXT("[MOVE FIX] Projected EQS position onto NavMesh (offset: %.1f units)"),
					FVector::Dist(TargetPos, NavTargetPos));

				// Re-check path with projected position
				Query = FPathFindingQuery(*InstanceData.AIController, *NavSys->GetDefaultNavDataInstance(), CurrentPos, NavTargetPos);
				FPathFindingResult Result = NavSys->FindPathSync(Query);
				bPathExists = Result.IsSuccessful() && Result.Path.IsValid();
			}
		}

		// Try movement - with detailed result checking
		FAIMoveRequest MoveReq(NavTargetPos);
		MoveReq.SetAcceptanceRadius(AcceptanceRadius);
		MoveReq.SetUsePathfinding(true);

		FPathFollowingRequestResult MoveResult = InstanceData.AIController->MoveTo(MoveReq);

		// v5.0: Only log failures at Error level, success at Verbose
		if (MoveResult.Code == EPathFollowingRequestResult::Failed)
		{
			UE_LOG(LogTemp, Error, TEXT("[MOVE v5.0] '%s': MoveTo FAILED! RequestID=%s"),
				*Pawn->GetName(), *MoveResult.MoveId.ToString());

			// Log details only on failure
			if (bPathExists)
			{
				UE_LOG(LogTemp, Error, TEXT("[MOVE BUG] FindPathSync succeeded but MoveTo failed!"));
			}
		}

		SharedContext.MovementDestination = NavTargetPos;  // Use projected position
		SharedContext.bIsMoving = (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful);
	}
	else
	{
		// No valid position found - EQS query failed
		// ELEGANT FALLBACK: For Advance, use direct NavMesh pathfinding to objective
		if (Macro.PositionChoice == ETacticalPosition::Advance)
		{
			// Get objective location from context
			FVector ObjectiveLocation = SharedContext.CurrentObjective.Get()->TargetLocation;

			if (!ObjectiveLocation.IsNearlyZero())
			{
				// Use direct NavMesh pathfinding (bypasses EQS)
				FAIMoveRequest MoveReq(ObjectiveLocation);
				MoveReq.SetAcceptanceRadius(200.0f); // Larger radius for objective
				MoveReq.SetUsePathfinding(true);

				FPathFollowingRequestResult MoveResult = InstanceData.AIController->MoveTo(MoveReq);

				if (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful)
				{
					SharedContext.MovementDestination = ObjectiveLocation;
					SharedContext.bIsMoving = true;

					UE_LOG(LogTemp, Log, TEXT("[MOVE v5.0] ✅ '%s': EQS failed but using direct pathfinding to objective (fallback)"),
						*Pawn->GetName());
					return; // Fallback succeeded
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[MOVE v5.0] ❌ '%s': EQS failed AND direct pathfinding failed - truly stuck!"),
						*Pawn->GetName());
				}
			}
		}

		// Fallback failed or not applicable - stop movement
		InstanceData.AIController->StopMovement();
		SharedContext.bIsMoving = false;

		if (Macro.PositionChoice != ETacticalPosition::Hold)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MOVE v5.0] ⚠️ '%s': EQS query failed for %s - holding position"),
				*Pawn->GetName(), *UEnum::GetValueAsString(Macro.PositionChoice));
		}
	}
}

void FSTTask_ExecuteObjective::ExecuteAiming(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		return;
	}

	// v5.0: Configure CharacterMovementComponent to use controller rotation (engine auto-aim)
	// This allows SetFocus to control pawn rotation automatically
	UCharacterMovementComponent* MoveComp = Pawn->FindComponentByClass<UCharacterMovementComponent>();
	if (MoveComp)
	{
		MoveComp->bOrientRotationToMovement = false;           // Don't override aim with movement direction
		MoveComp->bUseControllerDesiredRotation = true;        // Rotate pawn to match AIController rotation
		// RotationRate uses CharacterMovementComponent default - engine handles interpolation
	}

	// v5.0: Use macro action for engine-driven aiming (SetFocus only)
	const FMacroAction& Macro = Action.MacroAction;

	if (!InstanceData.AIController)
	{
		return; // AIController required for SetFocus
	}

	// Handle target selection based on macro action
	if (Macro.TargetIndex >= 0 && SharedContext.VisibleEnemies.Num() > 0)
	{
		// Clamp target index to valid range (prevents out-of-bounds during training)
		int32 ClampedIndex = FMath::Clamp(Macro.TargetIndex, 0, SharedContext.VisibleEnemies.Num() - 1);
		AActor* TargetEnemy = SharedContext.VisibleEnemies[ClampedIndex];

		// Validate enemy is alive and valid
		if (TargetEnemy && TargetEnemy->IsValidLowLevel())
		{
			// v5.0: Use SetFocus for engine auto-aim (macro action)
			InstanceData.AIController->SetFocus(TargetEnemy);
			SharedContext.PrimaryTarget = TargetEnemy;

			UE_LOG(LogTemp, Verbose, TEXT("[AIM v5.0] '%s' → '%s': SetFocus (engine auto-aim)"),
				*Pawn->GetName(), *TargetEnemy->GetName());

			if (ClampedIndex != Macro.TargetIndex)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[AIM FIX] '%s': Clamped invalid target index %d → %d (VisibleEnemies=%d)"),
					*Pawn->GetName(), Macro.TargetIndex, ClampedIndex, SharedContext.VisibleEnemies.Num());
			}
		}
		else
		{
			// Enemy dead/invalid - keep previous focus (don't clear immediately)
			UE_LOG(LogTemp, Verbose, TEXT("[AIM v5.0] '%s': Target index %d invalid, keeping previous focus"),
				*Pawn->GetName(), ClampedIndex);
		}
	}
	else if (Macro.TargetIndex < 0)
	{
		// RL policy explicitly chose "no target" - clear focus
		InstanceData.AIController->ClearFocus(EAIFocusPriority::Gameplay);
		SharedContext.PrimaryTarget = nullptr;

		UE_LOG(LogTemp, Verbose, TEXT("[AIM v5.0] '%s': Cleared focus (no target selected)"),
			*Pawn->GetName());
	}
	// else: No visible enemies - keep previous focus (don't spam ClearFocus)
}

void FSTTask_ExecuteObjective::ExecuteFire(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[EXEC FIRE] No ControlledPawn available"));
		return;
	}

	UWeaponComponent* WeaponComp = Pawn->FindComponentByClass<UWeaponComponent>();
	if (!WeaponComp)
	{
		UE_LOG(LogTemp, Error, TEXT("[EXEC FIRE] '%s': No WeaponComponent found"), *Pawn->GetName());
		return;
	}

	// v5.0: Use macro action fire mode
	const FMacroAction& Macro = Action.MacroAction;

	// Get focus target from AIController if available, otherwise use SharedContext.PrimaryTarget
	AActor* FocusTarget = nullptr;
	if (InstanceData.AIController)
	{
		FocusTarget = InstanceData.AIController->GetFocusActor();
	}
	else
	{
		// No AIController (Schola mode) - use SharedContext.PrimaryTarget instead
		FocusTarget = SharedContext.PrimaryTarget;
	}

	// Calculate fire direction
	FVector FireDirection = FVector::ForwardVector;  // Default fallback

	if (FocusTarget)
	{
		// Calculate direction from pawn to target (always accurate)
		FVector TargetLocation = FocusTarget->GetActorLocation();
		FVector PawnLocation = Pawn->GetActorLocation();
		FireDirection = (TargetLocation - PawnLocation).GetSafeNormal();

		UE_LOG(LogTemp, Verbose, TEXT("[FIRE v5.0] '%s' → '%s': Direct fire direction calculated"),
			*Pawn->GetName(), *FocusTarget->GetName());
		UE_LOG(LogTemp, Verbose, TEXT("  Fire dir: %s (Target: %s, Pawn: %s)"),
			*FireDirection.ToString(), *TargetLocation.ToString(), *PawnLocation.ToString());
	}
	else
	{
		// No focus target - use controller rotation as fallback (if available)
		if (InstanceData.AIController)
		{
			FireDirection = InstanceData.AIController->GetControlRotation().Vector();
			UE_LOG(LogTemp, Warning, TEXT("[FIRE FALLBACK] '%s': No focus target, using controller rotation"),
				*Pawn->GetName());
		}
		else
		{
			// No AIController - use pawn's forward vector
			FireDirection = Pawn->GetActorForwardVector();
			UE_LOG(LogTemp, Warning, TEXT("[FIRE FALLBACK] '%s': No AIController, using pawn forward vector"),
				*Pawn->GetName());
		}
	}

	switch (Macro.FireMode)
	{
	case EFireMode::HoldFire:
		// Don't fire
		break;

	case EFireMode::Fire:
		// Fire at focused target
		if (FocusTarget)
		{
			if (!WeaponComp->CanFire())
			{
				return; // On cooldown or out of ammo
			}

			// FIX (Issue #1): Check line-of-sight before firing to prevent shooting walls
			FVector StartLocation = Pawn->GetActorLocation() + FVector(0, 0, 150); // Eye level
			FVector EndLocation = FocusTarget->GetActorLocation() + FVector(0, 0, 150);

			FHitResult HitResult;
			FCollisionQueryParams Params;
			Params.AddIgnoredActor(Pawn);
			Params.bTraceComplex = false;

			UWorld* World = Pawn->GetWorld();
			if (!World)
			{
				return; // No world, can't trace
			}

			bool bHitSomething = World->LineTraceSingleByChannel(
				HitResult,
				StartLocation,
				EndLocation,
				ECC_Visibility,
				Params
			);

			// Only fire if we have clear LOS to target (not blocked by walls/obstacles)
			if (!bHitSomething || HitResult.GetActor() == FocusTarget)
			{
				// Clear LOS - fire at target
				WeaponComp->FireInDirection(FireDirection);
			}
			else
			{
				// LOS blocked - hold fire to avoid wasting ammo on walls
				UE_LOG(LogTemp, Verbose, TEXT("[FIRE LOS] '%s': Target '%s' blocked by '%s', holding fire"),
					*Pawn->GetName(),
					*FocusTarget->GetName(),
					*GetNameSafe(HitResult.GetActor()));
			}
		}
		break;

	case EFireMode::Suppress:
		// Suppression fire in current aim direction (even if no visible target)
		if (WeaponComp->CanFire())
		{
			WeaponComp->FireInDirection(FireDirection);
		}
		break;
	}
}

float FSTTask_ExecuteObjective::CalculateObjectiveReward(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	if (!SharedContext.CurrentObjective)
	{
		return 0.0f;
	}

	float Reward = 0.0f;

	// Small progress reward (encourage forward progress)
	float ObjProgress = SharedContext.CurrentObjective->GetProgress();
	if (ObjProgress > SharedContext.ActionProgress)
	{
		float ProgressDelta = ObjProgress - SharedContext.ActionProgress;
		Reward += ProgressDelta * 10.0f; // +10 reward per 100% progress
		SharedContext.ActionProgress = ObjProgress;
	}

	// Penalty for time inefficiency (encourage fast completion)
	if (SharedContext.CurrentObjective->TimeLimit > 0.0f)
	{
		float TimeRatio = SharedContext.CurrentObjective->TimeRemaining / SharedContext.CurrentObjective->TimeLimit;
		if (TimeRatio < 0.3f) // Less than 30% time remaining
		{
			Reward -= 0.5f * DeltaTime; // Small time penalty
		}
	}

	return Reward;
}

bool FSTTask_ExecuteObjective::CheckObjectiveStatus(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	if (!SharedContext.CurrentObjective)
	{
		return true; // No objective = exit
	}

	UObjective* Obj = SharedContext.CurrentObjective;

	// Check completion
	if (Obj->IsCompleted())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXEC OBJ] Objective COMPLETED"));

		// Provide completion reward
		if (SharedContext.FollowerComponent)
		{
			SharedContext.FollowerComponent->ProvideReward(50.0f, false); // Major reward
		}

		return true;
	}

	// Check failure
	if (Obj->IsFailed())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXEC OBJ] Objective FAILED"));

		// Provide failure penalty
		if (SharedContext.FollowerComponent)
		{
			SharedContext.FollowerComponent->ProvideReward(-30.0f, false); // Major penalty
		}

		return true;
	}

	return false; // Still active
}

//------------------------------------------------------------------------------
// v5.0 MACRO ACTION HELPERS
//------------------------------------------------------------------------------

TArray<FVector> FSTTask_ExecuteObjective::QueryEQSPositions(FStateTreeExecutionContext& Context, ETacticalPosition PositionType) const
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
		// Stay at current location (no EQS needed)
		Results.Add(AgentLocation);
		break;

	case ETacticalPosition::ForwardCover:
		// Query EQS for cover points closer to objective
		Results = RunEQSQuery(Pawn, InstanceData.ForwardCoverQuery, TEXT("ForwardCover"));
		break;

	case ETacticalPosition::Retreat:
		// Query EQS for cover points away from enemies
		Results = RunEQSQuery(Pawn, InstanceData.RetreatQuery, TEXT("Retreat"));
		break;

	case ETacticalPosition::Advance:
		// Move toward objective without cover requirement
		Results = RunEQSQuery(Pawn, InstanceData.AdvanceQuery, TEXT("Advance"));
		break;
	}

	return Results;
}

TArray<FVector> FSTTask_ExecuteObjective::RunEQSQuery(APawn* Pawn, UEnvQuery* Query, const FString& QueryName) const
{
	TArray<FVector> Results;

	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[EQS v5.0] ❌ RunEQSQuery failed: Invalid Pawn"));
		return Results;
	}

	if (!Query)
	{
		UE_LOG(LogTemp, Error, TEXT("[EQS v5.0] ❌ '%s': No EQS query assigned for '%s'"),
			*Pawn->GetName(), *QueryName);
		UE_LOG(LogTemp, Error, TEXT("[EQS v5.0]    Assign the query asset in StateTree editor: STTask_ExecuteObjective > EQS Queries section"));
		return Results;
	}

	UWorld* World = Pawn->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[EQS v5.0] ❌ '%s': No valid World for EQS query"), *Pawn->GetName());
		return Results;
	}

	UEnvQueryManager* QueryManager = UEnvQueryManager::GetCurrent(World);
	if (!QueryManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[EQS v5.0] ❌ '%s': EnvQueryManager not available"), *Pawn->GetName());
		return Results;
	}

	// Run instant EQS query (synchronous execution)
	FEnvQueryRequest QueryRequest(Query, Pawn);
	TSharedPtr<FEnvQueryResult> QueryResult = QueryManager->RunInstantQuery(QueryRequest, EEnvQueryRunMode::AllMatching);

	if (!QueryResult.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[EQS v5.0] ❌ '%s': EQS query '%s' returned invalid result"),
			*Pawn->GetName(), *QueryName);
		return Results;
	}

	if (!QueryResult->IsSuccessful())
	{
		UE_LOG(LogTemp, Error, TEXT("[EQS v5.0] ❌ '%s': EQS query '%s' failed to execute"),
			*Pawn->GetName(), *QueryName);
		return Results;
	}

	// Extract location results
	const int32 ItemCount = QueryResult->Items.Num();
	if (ItemCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EQS v5.0] ⚠️ '%s': EQS query '%s' returned no valid positions"),
			*Pawn->GetName(), *QueryName);
		return Results;
	}

	Results.Reserve(ItemCount);

	for (int i = 0; i < QueryResult->Items.Num(); ++i)
	{
		FVector ItemLocation = QueryResult->GetItemAsLocation(i);

		if (!ItemLocation.IsZero())
		{
			Results.Add(ItemLocation);
		}
	}

	// v7.3: EQS success logging reduced to Verbose
	return Results;
}

AActor* FSTTask_ExecuteObjective::GetEnemyByIndex(FStateTreeExecutionContext& Context, int32 EnemyIndex) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Get visible enemies from context
	if (EnemyIndex < 0 || EnemyIndex >= SharedContext.VisibleEnemies.Num())
	{
		return nullptr;
	}

	AActor* Enemy = SharedContext.VisibleEnemies[EnemyIndex];

	// Validate enemy is still alive
	if (Enemy && Enemy->IsValidLowLevel())
	{
		return Enemy;
	}

	return nullptr;
}
