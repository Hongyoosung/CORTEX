// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Tasks/BTTask_DEAttackAbility.h"
#include "Combat/Components/DEAbilityComponent.h"
#include "Combat/Abilities/DEAttackAbility.h"
#include "Characters/DECharacter.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"

UBTTask_DEAttackAbility::UBTTask_DEAttackAbility()
{
	NodeName = TEXT("Attack Ability");
	bNotifyTick = true;
	TargetEnemyKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_DEAttackAbility, TargetEnemyKey), AActor::StaticClass());
	HasTargetKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_DEAttackAbility, HasTargetKey));
}

EBTNodeResult::Type UBTTask_DEAttackAbility::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTTask_DEAttackAbility::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::Aborted;
}

void UBTTask_DEAttackAbility::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* AICtrl = OwnerComp.GetAIOwner();
	if (!AICtrl)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	ADECharacter* Character = Cast<ADECharacter>(AICtrl->GetPawn());
	if (!Character || !Character->IsAlive_Implementation())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	UDEAbilityComponent* AbilityComp = Character->GetAbilityComponent();
	if (!AbilityComp || !AbilityComp->GetAttackAbility())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}
	UDEAttackAbility* Ability = AbilityComp->GetAttackAbility();

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
