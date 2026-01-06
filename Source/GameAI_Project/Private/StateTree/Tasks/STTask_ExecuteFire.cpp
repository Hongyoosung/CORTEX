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
		ExecuteFire(Context);
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ExecuteFire::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Cleanup if needed
}

void FSTTask_ExecuteFire::ExecuteFire(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn || !SharedContext.FollowerComponent)
	{
		return;
	}

	UWeaponComponent* WeaponComp = Pawn->FindComponentByClass<UWeaponComponent>();
	if (!WeaponComp)
	{
		return;
	}

	// v6.0: Get current strategy from RL policy
	EStrategyType CurrentStrategy = SharedContext.FollowerComponent->GetCurrentStrategy();

	// Retreat strategy = hold fire
	if (CurrentStrategy == EStrategyType::Retreat)
	{
		StopFiring(Context);
		return;
	}

	// Get visible enemies (assuming this is available via FollowerComponent or perception)
	TArray<AActor*> VisibleEnemies = SharedContext.FollowerComponent->GetPerceivedEnemies();

	if (VisibleEnemies.Num() == 0)
	{
		StopFiring(Context);
		return;
	}

	// Select target based on strategy
	AActor* Target = SelectTarget(CurrentStrategy, VisibleEnemies, SharedContext);

	if (!Target || !WeaponComp->CanFire())
	{
		StopFiring(Context);
		return;
	}

	// CRITICAL FIX: Stop movement before firing to prevent moonwalking
	if (InstanceData.AIController && SharedContext.bIsMoving)
	{
		InstanceData.AIController->StopMovement();
		SharedContext.bIsMoving = false;
	}

	// Calculate fire direction
	FVector TargetLocation = Target->GetActorLocation();
	FVector PawnLocation = Pawn->GetActorLocation();
	FVector FireDirection = (TargetLocation - PawnLocation).GetSafeNormal();

	// Ensure character is facing the target before firing
	if (InstanceData.AIController)
	{
		FRotator DesiredRotation = FireDirection.Rotation();
		InstanceData.AIController->SetControlRotation(DesiredRotation);
		InstanceData.AIController->SetFocus(Target);
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
	if (!bHitSomething || HitResult.GetActor() == Target)
	{
		// Clear LOS - fire at target
		WeaponComp->FireInDirection(FireDirection);

		UE_LOG(LogTemp, Verbose, TEXT("[FIRE v6.0] '%s' → '%s': Firing (Strategy: %s, clear LOS)"),
			*Pawn->GetName(),
			*Target->GetName(),
			*UEnum::GetValueAsString(CurrentStrategy));
	}
	else
	{
		// LOS blocked - hold fire to avoid wasting ammo on walls
		UE_LOG(LogTemp, Verbose, TEXT("[FIRE v6.0] '%s': Target '%s' blocked by '%s', holding fire"),
			*Pawn->GetName(),
			*Target->GetName(),
			*GetNameSafe(HitResult.GetActor()));
	}
}

AActor* FSTTask_ExecuteFire::SelectTarget(
	EStrategyType Strategy,
	const TArray<AActor*>& Enemies,
	const FFollowerStateTreeContext& Context) const
{
	if (Enemies.Num() == 0)
	{
		return nullptr;
	}

	APawn* ControlledPawn = Context.ControlledPawn;
	if (!ControlledPawn)
	{
		return nullptr;
	}

	switch (Strategy)
	{
		case EStrategyType::Assault:
		case EStrategyType::Defend:
			// Priority: Closest enemy
			return GetClosestEnemy(Enemies, ControlledPawn);

		case EStrategyType::Support:
			// Priority: Enemy threatening ally
			if (Context.FollowerComponent)
			{
				return GetEnemyThreateningAlly(Enemies, Context.FollowerComponent);
			}
			return GetClosestEnemy(Enemies, ControlledPawn);

		default:
			return nullptr;
	}
}

AActor* FSTTask_ExecuteFire::GetClosestEnemy(const TArray<AActor*>& Enemies, APawn* Pawn) const
{
	if (!Pawn || Enemies.Num() == 0)
	{
		return nullptr;
	}

	AActor* ClosestEnemy = nullptr;
	float MinDistance = FLT_MAX;
	FVector PawnLocation = Pawn->GetActorLocation();

	for (AActor* Enemy : Enemies)
	{
		if (!Enemy)
		{
			continue;
		}

		float Distance = FVector::Dist(PawnLocation, Enemy->GetActorLocation());
		if (Distance < MinDistance)
		{
			MinDistance = Distance;
			ClosestEnemy = Enemy;
		}
	}

	return ClosestEnemy;
}

AActor* FSTTask_ExecuteFire::GetEnemyThreateningAlly(const TArray<AActor*>& Enemies, UFollowerAgentComponent* FollowerComp) const
{
	if (!FollowerComp || Enemies.Num() == 0)
	{
		return nullptr;
	}

	// Get ally context to see if any ally needs help
	FAllyContext AllyCtx = FollowerComp->GetAllyContext();
	if (!AllyCtx.bAllyNeedsHelp || !AllyCtx.ClosestAlly)
	{
		// No ally in danger, default to closest enemy
		return GetClosestEnemy(Enemies, Cast<APawn>(FollowerComp->GetOwner()));
	}

	// Find enemy closest to the threatened ally
	AActor* ThreateningEnemy = nullptr;
	float MinDistance = FLT_MAX;
	FVector AllyLocation = AllyCtx.ClosestAlly->GetActorLocation();

	for (AActor* Enemy : Enemies)
	{
		if (!Enemy)
		{
			continue;
		}

		float Distance = FVector::Dist(AllyLocation, Enemy->GetActorLocation());
		if (Distance < MinDistance)
		{
			MinDistance = Distance;
			ThreateningEnemy = Enemy;
		}
	}

	return ThreateningEnemy;
}

void FSTTask_ExecuteFire::StopFiring(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// Clear focus
	if (InstanceData.AIController)
	{
		InstanceData.AIController->ClearFocus(EAIFocusPriority::Gameplay);
	}
}
