// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Components/MocAbilityComponent.h"
#include "Combat/Abilities/MocAttackAbility.h"
#include "Combat/Abilities/MocHealAbility.h"
#include "Characters/MocCharacter.h"

UMocAbilityComponent::UMocAbilityComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // Let character or trainers call them
}

void UMocAbilityComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<AMocCharacter>(GetOwner());
	if (!OwnerCharacter) return;

	if (AbilityData)
	{
		// Attack Ability
		AttackAbility = NewObject<UMocAttackAbility>(this);
		AttackAbility->Initialize(OwnerCharacter);
		AttackAbility->SetConfig(AbilityData->AttackConfig);

		// Heal Ability
		HealAbility = NewObject<UMocHealAbility>(this);
		HealAbility->Initialize(OwnerCharacter);
		HealAbility->SetConfig(AbilityData->HealConfig);
	}
}

void UMocAbilityComponent::ResetAbilities()
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
