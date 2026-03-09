// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Tasks/BTTask_DECombatFire.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Character.h"
#include "Characters/DECharacter.h"
#include "Combat/Abilities/DEAttackAbility.h"
#include "Combat/Components/DEAbilityComponent.h"
#include "Combat/DECombatStatsInterface.h"
#include "DrawDebugHelpers.h"
#include "Kismet/KismetMathLibrary.h"
#include "EngineUtils.h"

UBTTask_DECombatFire::UBTTask_DECombatFire()
{
	NodeName = "Combat Fire";
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	bCreateNodeInstance = false;

	// Setup default blackboard keys
	TargetEnemyKey.SelectedKeyName = "TargetEnemy";
	HasTargetKey.SelectedKeyName = "HasTarget";
}

EBTNodeResult::Type UBTTask_DECombatFire::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// Validate combat conditions
	AActor* Target = nullptr;
	UDEAttackAbility* Ability = nullptr;
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

EBTNodeResult::Type UBTTask_DECombatFire::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	bIsFiring = false;
	return EBTNodeResult::Aborted;
}

void UBTTask_DECombatFire::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	if (!bIsFiring)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AActor* Target = nullptr;
	UDEAttackAbility* Ability = nullptr;
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

bool UBTTask_DECombatFire::ValidateCombatConditions(UBehaviorTreeComponent& OwnerComp, AActor*& OutTarget, UDEAttackAbility*& OutAbility) const
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

	ADECharacter* DEChar = Cast<ADECharacter>(ControlledPawn);
	if (!DEChar)
	{
		return false;
	}

	UDEAbilityComponent* AbilityComp = DEChar->GetAbilityComponent();
	OutAbility = AbilityComp ? AbilityComp->GetAttackAbility() : nullptr;
	if (!OutAbility)
	{
		UE_LOG(LogBehaviorTree, Warning, TEXT("BTTask_DECombatFire: AttackAbility not found on %s"), *DEChar->GetName());
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
	if (OutTarget->Implements<UDECombatStatsInterface>())
	{
		if (!IDECombatStatsInterface::Execute_IsAlive(OutTarget))
		{
			return false;
		}
	}

	return true;
}

bool UBTTask_DECombatFire::IsTargetValid(AActor* Target, APawn* OwnerPawn) const
{
	if (!Target || !OwnerPawn)
	{
		return false;
	}

	// Check if target is alive
	if (Target->Implements<UDECombatStatsInterface>())
	{
		if (!IDECombatStatsInterface::Execute_IsAlive(Target))
		{
			return false;
		}
	}

	// Check distance range
	const float Distance = FVector::Dist(OwnerPawn->GetActorLocation(), Target->GetActorLocation());
	if (MaxEngagementRange > 0.0f && Distance > MaxEngagementRange)
	{
		return false;
	}
	if (MinEngagementRange > 0.0f && Distance < MinEngagementRange)
	{
		return false;
	}

	// Check line of sight if required
	if (bRequireLineOfSight)
	{
		// Trace from shooter's eye height to target's center mass to avoid false blocks at ground level
		FVector StartLocation = OwnerPawn->GetPawnViewLocation();
		FVector TargetLocation = Target->GetActorLocation() + FVector(0.f, 0.f, 50.f);

		// Ignore cross-environment characters so they don't block LOS in multi-env training
		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(OwnerPawn);
		QueryParams.AddIgnoredActor(Target);

		ADECharacter* SelfChar = Cast<ADECharacter>(OwnerPawn);
		if (SelfChar)
		{
			const int32 MyEnvID = SelfChar->GetEnvID_Implementation();
			for (TActorIterator<ADECharacter> It(OwnerPawn->GetWorld()); It; ++It)
			{
				if ((*It)->GetEnvID_Implementation() != MyEnvID)
				{
					QueryParams.AddIgnoredActor(*It);
				}
			}
		}

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

void UBTTask_DECombatFire::AimAtTarget(APawn* OwnerPawn, AActor* Target)
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

bool UBTTask_DECombatFire::FireWeapon(UDEAttackAbility* Ability, AActor* Target)
{
	if (!Ability || !Target) return false;
	return Ability->FireAtTarget(Target, bUsePredictiveAiming);
}
