// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

namespace DEGameplayTags
{
	// State tags
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Debuff);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Invulnerable);

	// Ability identity tags
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Heal);

	// Cooldown tags
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Attack);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Heal);

	// SetByCaller magnitude tags
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Damage);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Healing);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_ManaCost);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Ability_Attack_Fire);
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Character_Damaged);
}
