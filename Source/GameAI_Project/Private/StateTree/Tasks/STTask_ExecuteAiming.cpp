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

	// Get current action
	const FTacticalAction& Action = SharedContext.CurrentAtomicAction;
	const FMacroAction& CurrentMacro = Action.MacroAction;
	const FMacroAction& PreviousMacro = InstanceData.PreviousMacroAction;

	// Detect target changes (only update aim when target changes)
	bool bActionChanged = (CurrentMacro.TargetIndex != PreviousMacro.TargetIndex);

	if (bActionChanged)
	{
		ExecuteAiming(Context, Action, DeltaTime);
		InstanceData.PreviousMacroAction = CurrentMacro;
	}
	else
	{
		// Continuous aim tracking (SetFocus handles this automatically)
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ExecuteAiming::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Cleanup if needed
}

void FSTTask_ExecuteAiming::ExecuteAiming(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		return;
	}

	// Configure character movement for aiming
	UCharacterMovementComponent* MoveComp = Pawn->FindComponentByClass<UCharacterMovementComponent>();
	if (MoveComp)
	{
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->bUseControllerDesiredRotation = true;
		MoveComp->RotationRate = FRotator(0.0f, InstanceData.RotationSpeed, 0.0f);
	}

	const FMacroAction& Macro = Action.MacroAction;

	// If no enemies visible, face objective direction
	if (SharedContext.VisibleEnemies.Num() == 0)
	{
		if (InstanceData.AIController && SharedContext.CurrentObjective && !SharedContext.CurrentObjective->TargetLocation.IsNearlyZero())
		{
			FVector ObjectiveLocation = SharedContext.CurrentObjective->TargetLocation;
			FVector PawnLocation = Pawn->GetActorLocation();
			FVector DirectionToObjective = (ObjectiveLocation - PawnLocation).GetSafeNormal();
			FRotator DesiredRotation = DirectionToObjective.Rotation();

			InstanceData.AIController->SetControlRotation(DesiredRotation);
		}
		return;
	}

	if (Macro.TargetIndex >= 0 && SharedContext.VisibleEnemies.Num() > 0)
	{
		// Clamp target index to valid range
		int32 ClampedIndex = FMath::Clamp(Macro.TargetIndex, 0, SharedContext.VisibleEnemies.Num() - 1);
		AActor* TargetEnemy = SharedContext.VisibleEnemies[ClampedIndex];

		// Validate enemy is alive and valid
		if (TargetEnemy && TargetEnemy->IsValidLowLevel() && InstanceData.AIController)
		{
			// Manually set controller rotation for immediate response
			FVector EnemyLocation = TargetEnemy->GetActorLocation();
			FVector PawnLocation = Pawn->GetActorLocation();
			FVector AimDirection = (EnemyLocation - PawnLocation).GetSafeNormal();
			FRotator DesiredRotation = AimDirection.Rotation();

			InstanceData.AIController->SetControlRotation(DesiredRotation);
			InstanceData.AIController->SetFocus(TargetEnemy);
			SharedContext.PrimaryTarget = TargetEnemy;
		}
	}
	else if (Macro.TargetIndex < 0)
	{
		// RL policy explicitly chose "no target" - clear focus
		if (InstanceData.AIController)
		{
			InstanceData.AIController->ClearFocus(EAIFocusPriority::Gameplay);
		}
		SharedContext.PrimaryTarget = nullptr;
	}
}
