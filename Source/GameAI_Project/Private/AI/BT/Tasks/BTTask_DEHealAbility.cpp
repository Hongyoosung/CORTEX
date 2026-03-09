// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Tasks/BTTask_DEHealAbility.h"
#include "Characters/DECharacter.h"
#include "GAS/DEGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"

UBTTask_DEHealAbility::UBTTask_DEHealAbility()
{
	NodeName = TEXT("Heal Ability (GAS)");
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
	if (!Character || !Character->IsAlive())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (!ASC)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Activate heal ability via GAS tag
	FGameplayTagContainer HealTag;
	HealTag.AddTag(DEGameplayTags::Ability_Heal);
	ASC->TryActivateAbilitiesByTag(HealTag);
}
