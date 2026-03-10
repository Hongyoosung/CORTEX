// Copyright Epic Games, Inc. All Rights Reserved.

#include "GAS/DEGameplayTags.h"

namespace DEGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(State_Dead,			"State.Dead");
	UE_DEFINE_GAMEPLAY_TAG(State_Debuff,		"State.Debuff");
	UE_DEFINE_GAMEPLAY_TAG(State_Invulnerable,	"State.Invulnerable");

	UE_DEFINE_GAMEPLAY_TAG(Ability_Attack,	"Ability.Attack");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Heal,	"Ability.Heal");

	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Attack, "Cooldown.Attack");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Heal,	"Cooldown.Heal");

	UE_DEFINE_GAMEPLAY_TAG(Data_Damage,  "Data.Damage");
	UE_DEFINE_GAMEPLAY_TAG(Data_Healing, "Data.Healing");
}
