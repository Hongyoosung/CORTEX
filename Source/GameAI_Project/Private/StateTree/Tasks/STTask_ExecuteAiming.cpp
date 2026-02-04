// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/STTask_ExecuteAiming.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"

EStateTreeRunStatus FSTTask_ExecuteAiming::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.StateTreeComp || !InstanceData.ControlledPawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.PreviousMacroAction = FMacroAction();
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_ExecuteAiming::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Check abort conditions
	if (!SharedContext.bIsAlive)
	{
		return EStateTreeRunStatus::Succeeded;
	}


	// Strategy is assigned by MCTS
	const FMacroAction& CurrentAction = SharedContext.CurrentAction;
	const FMacroAction& PreviousAction = InstanceData.PreviousMacroAction;

	// Detect tactical parameter changes (instead of strategy changes)
	bool bParamsChanged =
		FMath::Abs(CurrentAction.TacticalParams.Aggression - PreviousAction.TacticalParams.Aggression) > 0.1f ||
		FMath::Abs(CurrentAction.TacticalParams.CoverPreference - PreviousAction.TacticalParams.CoverPreference) > 0.1f ||
		CurrentAction.CombatParams.Priority != PreviousAction.CombatParams.Priority;

	// Always execute aiming (target selection is handled by STTask_ExecuteFire)
	ExecuteAiming(Context, DeltaTime);

	if (bParamsChanged)
	{
		InstanceData.PreviousMacroAction = CurrentAction;
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ExecuteAiming::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Cleanup if needed
}

void FSTTask_ExecuteAiming::ExecuteAiming(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		return;
	}

	// Configure character movement based on whether we have a target
	UCharacterMovementComponent* MoveComp = Pawn->FindComponentByClass<UCharacterMovementComponent>();
	if (MoveComp)
	{
		bool bHasTarget = (SharedContext.PrimaryTarget != nullptr);

		if (bHasTarget)
		{
			// Aiming mode: face the target
			MoveComp->bOrientRotationToMovement = false;
			MoveComp->bUseControllerDesiredRotation = true;
			MoveComp->RotationRate = FRotator(0.0f, InstanceData.RotationSpeed, 0.0f);
		}
		else
		{
			// Moving mode: face the movement direction
			MoveComp->bOrientRotationToMovement = true;
			MoveComp->bUseControllerDesiredRotation = false;
			MoveComp->RotationRate = FRotator(0.0f, InstanceData.RotationSpeed, 0.0f);
		}
	}


	if (SharedContext.PrimaryTarget && InstanceData.AIController)
	{
		FVector EnemyLocation = SharedContext.PrimaryTarget->GetActorLocation();
		FVector PawnLocation = Pawn->GetActorLocation();
		FVector AimDirection = (EnemyLocation - PawnLocation).GetSafeNormal();
		FRotator DesiredRotation = AimDirection.Rotation();

		InstanceData.AIController->SetControlRotation(DesiredRotation);
		InstanceData.AIController->SetFocus(SharedContext.PrimaryTarget);
	}
}
