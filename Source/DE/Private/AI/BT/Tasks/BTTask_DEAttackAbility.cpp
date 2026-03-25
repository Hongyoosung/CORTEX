// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Tasks/BTTask_DEAttackAbility.h"
#include "Characters/DEAgent.h"
#include "GAS/DEGameplayTags.h"
#include "GAS/Abilities/DEGA_Attack.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"

UBTTask_DEAttackAbility::UBTTask_DEAttackAbility()
{
	NodeName = TEXT("Attack Ability");
	bNotifyTick = true;
	bCreateNodeInstance = true;  // bIsFiring is per-instance state; must not be shared across agents
	TargetEnemyKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_DEAttackAbility, TargetEnemyKey), AActor::StaticClass());
	HasTargetKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_DEAttackAbility, HasTargetKey));

	TargetEnemyKey.SelectedKeyName = "TargetEnemy";
	HasTargetKey.SelectedKeyName = "HasTarget";
}

EBTNodeResult::Type UBTTask_DEAttackAbility::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (bContinuousFire)
	{
		bIsFiring = true;
		return EBTNodeResult::InProgress;
	}

	// Single-shot mode: resolve target and fire immediately
	AAIController* AICtrl = OwnerComp.GetAIOwner();
	ADEAgent* Character = AICtrl ? Cast<ADEAgent>(AICtrl->GetPawn()) : nullptr;
	if (!Character || !Character->IsAlive()) return EBTNodeResult::Failed;

	UDEGA_Attack* Ability = Character->GetAttackAbility();
	if (!Ability || !Ability->CanFire()) return EBTNodeResult::Failed;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB || !BB->GetValueAsBool(HasTargetKey.SelectedKeyName)) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(TargetEnemyKey.SelectedKeyName));
	if (!Target || !Ability->IsTargetValid(Target)) return EBTNodeResult::Failed;

	FGameplayEventData Payload;
	Payload.EventTag = DEGameplayTags::Ability_Attack;
	Payload.Target   = Target;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Character, DEGameplayTags::Ability_Attack, Payload);

	return EBTNodeResult::Succeeded;
}

EBTNodeResult::Type UBTTask_DEAttackAbility::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	bIsFiring = false;
	return EBTNodeResult::Aborted;
}

void UBTTask_DEAttackAbility::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	if (!bIsFiring)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AAIController* AICtrl = OwnerComp.GetAIOwner();
	ADEAgent* Character = AICtrl ? Cast<ADEAgent>(AICtrl->GetPawn()) : nullptr;
	if (!Character || !Character->IsAlive())
	{
		bIsFiring = false;
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB || !BB->GetValueAsBool(HasTargetKey.SelectedKeyName))
	{
		bIsFiring = false;
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(TargetEnemyKey.SelectedKeyName));
	UDEGA_Attack* Ability = Character->GetAttackAbility();

	if (!Target || !Ability || !Ability->IsTargetValid(Target))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BTTask_Attack] %s — FAIL: Target=%s Ability=%s IsTargetValid=%s"),
			*Character->GetName(),
			Target   ? TEXT("OK")    : TEXT("NULL"),
			Ability  ? TEXT("OK")    : TEXT("NULL"),
			(Target && Ability) ? (Ability->IsTargetValid(Target) ? TEXT("true") : TEXT("false")) : TEXT("N/A"));
		bIsFiring = false;
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	if (Ability->CanFire())
	{
		FGameplayEventData Payload;
		Payload.EventTag = DEGameplayTags::Ability_Attack;
		Payload.Target   = Target;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			Character, DEGameplayTags::Ability_Attack, Payload);
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("[BTTask_Attack] %s — CanFire=false (reloading or on cooldown)"),
			*Character->GetName());
	}
}
