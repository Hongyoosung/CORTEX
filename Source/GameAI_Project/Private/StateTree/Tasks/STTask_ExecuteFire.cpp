// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/STTask_ExecuteFire.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Team/FollowerAgentComponent.h"
#include "Combat/WeaponComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

EStateTreeRunStatus FSTTask_ExecuteFire::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.StateTreeComp || !InstanceData.ControlledPawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.TimeSinceLastAction = 0.0f;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_ExecuteFire::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Check abort conditions
	if (!SharedContext.bIsAlive)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// Action throttling - fire at fixed rate (default 20 Hz)
	InstanceData.TimeSinceLastAction += DeltaTime;

	bool bShouldExecuteAction = (InstanceData.TimeSinceLastAction >= InstanceData.ActionApplicationInterval);

	if (bShouldExecuteAction)
	{
		InstanceData.TimeSinceLastAction = 0.0f;
		const FTacticalAction& Action = SharedContext.CurrentAtomicAction;
		ExecuteFire(Context, Action);
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ExecuteFire::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Cleanup if needed
}

void FSTTask_ExecuteFire::ExecuteFire(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		return;
	}

	UWeaponComponent* WeaponComp = Pawn->FindComponentByClass<UWeaponComponent>();
	if (!WeaponComp)
	{
		return;
	}

	const FMacroAction& Macro = Action.MacroAction;

	// Get focus target
	AActor* FocusTarget = nullptr;
	if (InstanceData.AIController)
	{
		FocusTarget = InstanceData.AIController->GetFocusActor();
	}
	else
	{
		FocusTarget = SharedContext.PrimaryTarget;
	}

	// Calculate fire direction
	FVector FireDirection = FVector::ForwardVector;

	if (FocusTarget)
	{
		FVector TargetLocation = FocusTarget->GetActorLocation();
		FVector PawnLocation = Pawn->GetActorLocation();
		FireDirection = (TargetLocation - PawnLocation).GetSafeNormal();
	}
	else if (InstanceData.AIController)
	{
		FireDirection = InstanceData.AIController->GetControlRotation().Vector();
	}
	else
	{
		FireDirection = Pawn->GetActorForwardVector();
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
				return;
			}

			// Line-of-sight check
			FVector StartLocation = Pawn->GetActorLocation() + FVector(0, 0, 150);
			FVector EndLocation = FocusTarget->GetActorLocation() + FVector(0, 0, 150);

			FHitResult HitResult;
			FCollisionQueryParams Params;
			Params.AddIgnoredActor(Pawn);
			Params.bTraceComplex = false;

			UWorld* World = Pawn->GetWorld();
			if (!World)
			{
				return;
			}

			bool bHitSomething = World->LineTraceSingleByChannel(
				HitResult,
				StartLocation,
				EndLocation,
				ECC_Visibility,
				Params
			);

			// Only fire if clear LOS
			if (!bHitSomething || HitResult.GetActor() == FocusTarget)
			{
				WeaponComp->FireInDirection(FireDirection);
			}
		}
		break;

	case EFireMode::Suppress:
		// Suppression fire
		if (WeaponComp->CanFire())
		{
			WeaponComp->FireInDirection(FireDirection);
		}
		break;
	}
}
