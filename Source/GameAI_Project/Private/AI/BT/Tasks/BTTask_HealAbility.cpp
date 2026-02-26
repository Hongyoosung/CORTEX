// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Tasks/BTTask_HealAbility.h"
#include "Combat/Abilities/HealAbility.h"
#include "Characters/MocCharacter.h"
#include "AIController.h"

UBTTask_HealAbility::UBTTask_HealAbility()
{
	NodeName = TEXT("Heal Ability");
	bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_HealAbility::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTTask_HealAbility::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::Aborted;
}

void UBTTask_HealAbility::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
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

	UHealAbility* Ability = Character->HealAbility;
	if (!Ability)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	Ability->ExecuteAbility(DeltaSeconds);
}
