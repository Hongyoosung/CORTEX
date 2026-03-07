// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Components/DEAbilityComponent.h"
#include "Combat/Abilities/DEAttackAbility.h"
#include "Combat/Abilities/DEHealAbility.h"
#include "Characters/DECharacter.h"

UDEAbilityComponent::UDEAbilityComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // Let character or trainers call them
}

void UDEAbilityComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ADECharacter>(GetOwner());
	if (!OwnerCharacter) return;

	if (AbilityData)
	{
		// Attack Ability
		AttackAbility = NewObject<UDEAttackAbility>(this);
		AttackAbility->Initialize(OwnerCharacter);
		AttackAbility->SetConfig(AbilityData->AttackConfig);

		// Heal Ability
		HealAbility = NewObject<UDEHealAbility>(this);
		HealAbility->Initialize(OwnerCharacter);
		HealAbility->SetConfig(AbilityData->HealConfig);
	}
}

void UDEAbilityComponent::ResetAbilities()
{
	if (AttackAbility)
	{
		AttackAbility->RefillAmmo();
	}

	if (HealAbility)
	{
		HealAbility->ResetHealState();
	}
}
