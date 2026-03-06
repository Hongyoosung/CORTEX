// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Tasks/BTTask_AttackAbility.h"
#include "Combat/Components/MocAbilityComponent.h"
#include "Combat/Abilities/MocAttackAbility.h"
#include "Characters/MocCharacter.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"

UBTTask_AttackAbility::UBTTask_AttackAbility()
{
	NodeName = TEXT("Attack Ability");
	bNotifyTick = true;
	TargetEnemyKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_AttackAbility, TargetEnemyKey), AActor::StaticClass());
	HasTargetKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_AttackAbility, HasTargetKey));
}

EBTNodeResult::Type UBTTask_AttackAbility::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTTask_AttackAbility::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::Aborted;
}

void UBTTask_AttackAbility::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* AICtrl = OwnerComp.GetAIOwner();
	if (!AICtrl)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AMocCharacter* Character = Cast<AMocCharacter>(AICtrl->GetPawn());
	if (!Character || !Character->IsAlive_Implementation())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	UMocAbilityComponent* AbilityComp = Character->GetAbilityComponent();
	if (!AbilityComp || !AbilityComp->GetAttackAbility())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}
	UMocAttackAbility* Ability = AbilityComp->GetAttackAbility();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const bool bHasTarget = BB->GetValueAsBool(HasTargetKey.SelectedKeyName);
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(TargetEnemyKey.SelectedKeyName));
	if (!bHasTarget || !Target)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	Ability->ExecuteWithTarget(DeltaSeconds, Target);
}
