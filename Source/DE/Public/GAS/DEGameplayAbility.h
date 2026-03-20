// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "DEGameplayAbility.generated.h"

/**
 * UDEGameplayAbility - Project base class for all Gameplay Abilities.
 *
 * Cooldown is driven entirely by a Blueprint GameplayEffect asset assigned to
 * CooldownGameplayEffectClass (inherited from UGameplayAbility). Subclasses
 * must assign this in their Blueprint CDO or constructor — no runtime GE
 * construction is performed here.
 *
 * Subclasses: UDEGA_Attack, UDEGA_Heal
 */
UCLASS(Abstract, Blueprintable)
class DE_API UDEGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UDEGameplayAbility();
};
