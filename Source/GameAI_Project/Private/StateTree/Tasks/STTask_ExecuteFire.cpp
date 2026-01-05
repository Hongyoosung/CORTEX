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

	// No valid target - don't fire
	if (!FocusTarget || !WeaponComp->CanFire())
	{
		return;
	}

	// CRITICAL FIX: Stop movement before firing to prevent moonwalking
	if (InstanceData.AIController && SharedContext.bIsMoving)
	{
		InstanceData.AIController->StopMovement();
		SharedContext.bIsMoving = false;
	}

	// Calculate fire direction
	FVector TargetLocation = FocusTarget->GetActorLocation();
	FVector PawnLocation = Pawn->GetActorLocation();
	FVector FireDirection = (TargetLocation - PawnLocation).GetSafeNormal();

	// Ensure character is facing the target before firing
	if (InstanceData.AIController)
	{
		FRotator DesiredRotation = FireDirection.Rotation();
		InstanceData.AIController->SetControlRotation(DesiredRotation);
	}

	// Line-of-sight check - only fire if clear LOS
	FVector StartLocation = PawnLocation + FVector(0, 0, 150);  // Eye height
	FVector EndLocation = TargetLocation + FVector(0, 0, 150);

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

	// Only fire if we have clear LOS to target (not blocked by walls/obstacles)
	if (!bHitSomething || HitResult.GetActor() == FocusTarget)
	{
		// Clear LOS - fire at target
		WeaponComp->FireInDirection(FireDirection);

		UE_LOG(LogTemp, Verbose, TEXT("[FIRE v5.0] '%s' → '%s': Firing (clear LOS)"),
			*Pawn->GetName(), *FocusTarget->GetName());
	}
	else
	{
		// LOS blocked - hold fire to avoid wasting ammo on walls
		UE_LOG(LogTemp, Verbose, TEXT("[FIRE v5.0] '%s': Target '%s' blocked by '%s', holding fire"),
			*Pawn->GetName(),
			*FocusTarget->GetName(),
			*GetNameSafe(HitResult.GetActor()));
	}
}
