// Copyright Epic Games, Inc. All Rights Reserved.

#include "GAS/DEAttributeSet.h"
#include "GAS/DEGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UDEAttributeSet::UDEAttributeSet()
{
	InitHealth(100.0f);
	InitMaxHealth(100.0f);
	InitArmor(0.0f);
	InitDamage(0.0f);
	InitHealing(0.0f);
}

void UDEAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UDEAttributeSet, Health,    COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDEAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDEAttributeSet, Armor,     COND_None, REPNOTIFY_Always);
}

void UDEAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.0f);
	}
}

void UDEAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// --- Consume Damage meta-attribute ---
	if (Data.EvaluatedData.Attribute == GetDamageAttribute())
	{
		float IncomingDamage = GetDamage();
		SetDamage(0.0f); // consume

		// Ignore damage while invulnerable (e.g. post-respawn grace period)
		if (UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent())
		{
			if (ASC->HasMatchingGameplayTag(DEGameplayTags::State_Invulnerable))
			{
				IncomingDamage = 0.0f;
			}
		}

		if (IncomingDamage > 0.0f)
		{
			// Armor mitigation: reduce by Armor * 0.01 per point (1% per armor point)
			const float ArmorValue = GetArmor();
			const float Mitigation = FMath::Clamp(ArmorValue * 0.01f, 0.0f, 0.95f);
			const float MitigatedDamage = IncomingDamage * (1.0f - Mitigation);

			const float NewHealth = FMath::Max(GetHealth() - MitigatedDamage, 0.0f);
			SetHealth(NewHealth);

			// Apply State.Dead tag when health reaches 0
			if (NewHealth <= 0.0f)
			{
				if (UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent())
				{
					FGameplayTagContainer DeadTag;
					DeadTag.AddTag(DEGameplayTags::State_Dead);
					ASC->AddLooseGameplayTags(DeadTag);
				}
			}
		}
	}

	// --- Consume Healing meta-attribute ---
	if (Data.EvaluatedData.Attribute == GetHealingAttribute())
	{
		const float IncomingHeal = GetHealing();
		SetHealing(0.0f); // consume

		if (IncomingHeal > 0.0f)
		{
			const float NewHealth = FMath::Min(GetHealth() + IncomingHeal, GetMaxHealth());
			SetHealth(NewHealth);
		}
	}

	// --- Final clamp on Health ---
	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
	}
}


void UDEAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UDEAttributeSet, Health, OldHealth);
}

void UDEAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UDEAttributeSet, MaxHealth, OldMaxHealth);
}

void UDEAttributeSet::OnRep_Armor(const FGameplayAttributeData& OldArmor)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UDEAttributeSet, Armor, OldArmor);
}
