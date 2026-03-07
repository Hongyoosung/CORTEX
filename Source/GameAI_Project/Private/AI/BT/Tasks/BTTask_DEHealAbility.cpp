// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Tasks/BTTask_DEHealAbility.h"
#include "Combat/Abilities/DEHealAbility.h"
#include "Characters/DECharacter.h"
#include "AIController.h"

UBTTask_DEHealAbility::UBTTask_DEHealAbility()
{
	NodeName = TEXT("Heal Ability");
	bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_DEHealAbility::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTTask_DEHealAbility::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::Aborted;
}

void UBTTask_DEHealAbility::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
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


}
