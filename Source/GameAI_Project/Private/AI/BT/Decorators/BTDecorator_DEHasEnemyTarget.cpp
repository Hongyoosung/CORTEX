// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Decorators/BTDecorator_DEHasEnemyTarget.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Combat/DECombatStatsInterface.h"
#include "GameFramework/Pawn.h"

UBTDecorator_DEHasEnemyTarget::UBTDecorator_DEHasEnemyTarget()
{
	NodeName = "Has Enemy Target";
	bNotifyBecomeRelevant = false;
	bNotifyTick = false;

	// Setup default blackboard keys
	TargetEnemyKey.SelectedKeyName = "TargetEnemy";
	HasTargetKey.SelectedKeyName = "HasTarget";

	// Allow observer to abort on value change
	FlowAbortMode = EBTFlowAbortMode::Both;
}

bool UBTDecorator_DEHasEnemyTarget::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	// Get blackboard component
	UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	if (!BlackboardComp)
	{
		return false;
	}

	// Check HasTarget flag
	bool bHasTarget = BlackboardComp->GetValueAsBool(HasTargetKey.SelectedKeyName);
	if (!bHasTarget)
	{
		return false;
	}

	// Get target actor
	AActor* Target = Cast<AActor>(BlackboardComp->GetValueAsObject(TargetEnemyKey.SelectedKeyName));
	if (!Target)
	{
		return false;
	}

	// Check if target is alive
	if (bCheckAlive)
	{
		if (Target->Implements<UDECombatStatsInterface>())
		{
			if (!IDECombatStatsInterface::Execute_IsAlive(Target))
			{
				return false;
			}
		}
	}

	// Get owner pawn
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		return false;
	}

	APawn* OwnerPawn = AIController->GetPawn();
	if (!OwnerPawn)
	{
		return false;
	}

	// Check distance if max range is specified
	if (MaxRange > 0.0f)
	{
		float Distance = FVector::Dist(OwnerPawn->GetActorLocation(), Target->GetActorLocation());
		if (Distance > MaxRange)
		{
			return false;
		}
	}

	// Check line of sight if required
	if (bCheckLineOfSight)
	{
		FVector StartLocation = OwnerPawn->GetActorLocation();
		FVector TargetLocation = Target->GetActorLocation();

		// Add offset to start location (eye height)
		StartLocation.Z += 50.0f;

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

		// If we hit something other than the target, line of sight is blocked
		if (bHit && HitResult.GetActor() != Target)
		{
			return false;
		}
	}

	return true;
}

FString UBTDecorator_DEHasEnemyTarget::GetStaticDescription() const
{
	FString Description = FString::Printf(TEXT("Has Enemy Target"));

	if (bCheckAlive)
	{
		Description += TEXT(" (Alive)");
	}

	if (bCheckLineOfSight)
	{
		Description += TEXT(" (LOS)");
	}

	if (MaxRange > 0.0f)
	{
		Description += FString::Printf(TEXT(" (Range: %.0fm)"), MaxRange / 100.0f);
	}

	return Description;
}
