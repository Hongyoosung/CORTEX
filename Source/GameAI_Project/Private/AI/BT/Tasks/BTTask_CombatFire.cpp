// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Tasks/BTTask_CombatFire.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Character.h"
#include "Characters/MocCharacter.h"
#include "Combat/Abilities/AttackAbility.h"
#include "Combat/CombatStatsInterface.h"
#include "DrawDebugHelpers.h"
#include "Kismet/KismetMathLibrary.h"

UBTTask_CombatFire::UBTTask_CombatFire()
{
	NodeName = "Combat Fire";
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	bCreateNodeInstance = false;

	// Setup default blackboard keys
	TargetEnemyKey.SelectedKeyName = "TargetEnemy";
	HasTargetKey.SelectedKeyName = "HasTarget";
}

EBTNodeResult::Type UBTTask_CombatFire::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// Validate combat conditions
	AActor* Target = nullptr;
	UAttackAbility* Ability = nullptr;
	if (!ValidateCombatConditions(OwnerComp, Target, Ability))
	{
		return EBTNodeResult::Failed;
	}

	if (bContinuousFire)
	{
		bIsFiring = true;
		return EBTNodeResult::InProgress;
	}
	else
	{
		APawn* OwnerPawn = OwnerComp.GetAIOwner()->GetPawn();
		AimAtTarget(OwnerPawn, Target);

		bool bFired = FireWeapon(Ability, Target);
		return bFired ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
	}
}

EBTNodeResult::Type UBTTask_CombatFire::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	bIsFiring = false;
	return EBTNodeResult::Aborted;
}

void UBTTask_CombatFire::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	if (!bIsFiring)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AActor* Target = nullptr;
	UAttackAbility* Ability = nullptr;
	if (!ValidateCombatConditions(OwnerComp, Target, Ability))
	{
		bIsFiring = false;
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	APawn* OwnerPawn = OwnerComp.GetAIOwner()->GetPawn();
	if (!IsTargetValid(Target, OwnerPawn))
	{
		bIsFiring = false;
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AimAtTarget(OwnerPawn, Target);
	FireWeapon(Ability, Target);
}

bool UBTTask_CombatFire::ValidateCombatConditions(UBehaviorTreeComponent& OwnerComp, AActor*& OutTarget, UAttackAbility*& OutAbility) const
{
	// Get AI Controller
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		return false;
	}

	// Get controlled pawn
	APawn* ControlledPawn = AIController->GetPawn();
	if (!ControlledPawn)
	{
		return false;
	}

	AMocCharacter* MocChar = Cast<AMocCharacter>(ControlledPawn);
	if (!MocChar)
	{
		return false;
	}

	OutAbility = MocChar->GetAttackAbility();
	if (!OutAbility)
	{
		UE_LOG(LogBehaviorTree, Warning, TEXT("BTTask_CombatFire: AttackAbility not found on %s"), *MocChar->GetName());
		return false;
	}

	if (!OutAbility->CanFire())
	{
		return false;
	}

	// Get target from blackboard
	UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	if (!BlackboardComp)
	{
		return false;
	}

	// Check if has target flag is set
	bool bHasTarget = BlackboardComp->GetValueAsBool(HasTargetKey.SelectedKeyName);
	if (!bHasTarget)
	{
		return false;
	}

	// Get target actor
	OutTarget = Cast<AActor>(BlackboardComp->GetValueAsObject(TargetEnemyKey.SelectedKeyName));
	if (!OutTarget)
	{
		return false;
	}

	// Check if target is alive
	if (OutTarget->Implements<UCombatStatsInterface>())
	{
		if (!ICombatStatsInterface::Execute_IsAlive(OutTarget))
		{
			return false;
		}
	}

	return true;
}

bool UBTTask_CombatFire::IsTargetValid(AActor* Target, APawn* OwnerPawn) const
{
	if (!Target || !OwnerPawn)
	{
		return false;
	}

	// Check if target is alive
	if (Target->Implements<UCombatStatsInterface>())
	{
		if (!ICombatStatsInterface::Execute_IsAlive(Target))
		{
			return false;
		}
	}

	// Check distance if max range is specified
	if (MaxEngagementRange > 0.0f)
	{
		float Distance = FVector::Dist(OwnerPawn->GetActorLocation(), Target->GetActorLocation());
		if (Distance > MaxEngagementRange)
		{
			return false;
		}
	}

	// Check line of sight if required
	if (bRequireLineOfSight)
	{
		FVector StartLocation = OwnerPawn->GetActorLocation();
		FVector TargetLocation = Target->GetActorLocation();

		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(OwnerPawn);
		QueryParams.AddIgnoredActor(Target);

		UWorld* World = OwnerPawn->GetWorld();
		bool bHit = World->LineTraceSingleByChannel(
			HitResult,
			StartLocation,
			TargetLocation,
			ECC_Visibility,
			QueryParams
		);

		// If we hit something, target is not visible
		if (bHit)
		{
			return false;
		}
	}

	return true;
}

void UBTTask_CombatFire::AimAtTarget(APawn* OwnerPawn, AActor* Target)
{
	if (!OwnerPawn || !Target)
	{
		return;
	}

	// Calculate direction to target
	FVector DirectionToTarget = Target->GetActorLocation() - OwnerPawn->GetActorLocation();
	DirectionToTarget.Z = 0.0f; // Keep rotation on horizontal plane
	DirectionToTarget.Normalize();

	// Calculate target rotation
	FRotator TargetRotation = DirectionToTarget.Rotation();
	FRotator CurrentRotation = OwnerPawn->GetActorRotation();

	// Apply rotation (instant or interpolated)
	FRotator NewRotation;
	if (AimRotationSpeed > 0.0f)
	{
		float DeltaTime = OwnerPawn->GetWorld()->GetDeltaSeconds();
		NewRotation = FMath::RInterpConstantTo(CurrentRotation, TargetRotation, DeltaTime, AimRotationSpeed);
	}
	else
	{
		NewRotation = TargetRotation;
	}

	// Set rotation
	OwnerPawn->SetActorRotation(NewRotation);
}

bool UBTTask_CombatFire::FireWeapon(UAttackAbility* Ability, AActor* Target)
{
	if (!Ability || !Target) return false;
	return Ability->FireAtTarget(Target, bUsePredictiveAiming);
}
