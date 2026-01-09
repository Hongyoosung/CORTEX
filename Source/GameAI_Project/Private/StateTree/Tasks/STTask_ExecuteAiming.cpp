// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/STTask_ExecuteAiming.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Team/FollowerAgentComponent.h"
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

	// v6.0: Get current strategy (simplified action space)
	const FMacroAction& CurrentAction = SharedContext.CurrentAction;
	const FMacroAction& PreviousAction = InstanceData.PreviousMacroAction;

	// Detect strategy changes
	bool bStrategyChanged = (CurrentAction.Strategy != PreviousAction.Strategy);

	// v6.0: Always execute aiming (target selection is handled by STTask_ExecuteFire)
	ExecuteAiming(Context, DeltaTime);

	if (bStrategyChanged)
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

	// v6.0: Simplified aiming - aim at PrimaryTarget if set (by STTask_ExecuteFire)
	// or face Mission direction if no target

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

	// Aim at primary target if set (by ExecuteFire task)
	if (SharedContext.PrimaryTarget && InstanceData.AIController)
	{
		FVector EnemyLocation = SharedContext.PrimaryTarget->GetActorLocation();
		FVector PawnLocation = Pawn->GetActorLocation();
		FVector AimDirection = (EnemyLocation - PawnLocation).GetSafeNormal();
		FRotator DesiredRotation = AimDirection.Rotation();

		InstanceData.AIController->SetControlRotation(DesiredRotation);
		InstanceData.AIController->SetFocus(SharedContext.PrimaryTarget);
	}
	// Otherwise face Mission direction
	else if (InstanceData.AIController && SharedContext.CurrentMission && !SharedContext.CurrentMission->TargetLocation.IsNearlyZero())
	{
		FVector MissionLocation = SharedContext.CurrentMission->TargetLocation;
		FVector PawnLocation = Pawn->GetActorLocation();
		FVector DirectionToMission = (MissionLocation - PawnLocation).GetSafeNormal();
		FRotator DesiredRotation = DirectionToMission.Rotation();

		InstanceData.AIController->SetControlRotation(DesiredRotation);
		InstanceData.AIController->ClearFocus(EAIFocusPriority::Gameplay);
	}
}
