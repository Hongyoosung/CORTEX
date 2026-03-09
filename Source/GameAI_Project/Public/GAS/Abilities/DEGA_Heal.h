// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GAS/DEGameplayAbility.h"
#include "Data/DEAbilityData.h"
#include "DEGA_Heal.generated.h"

class ADECharacter;
class UNiagaraComponent;
class ADEMatchManager;

/**
 * UDEGA_Heal - GAS-based healing ability.
 *
 * Replaces UDEHealAbility. Heals nearest injured ally within range.
 * Applies a GE_Heal GameplayEffect to the target's ASC.
 *
 * Activation:
 * - AI: TryActivateAbilityByTag(Ability.Heal)
 * - Training: ActivateAbility called from ProcessTrainingAbilities timer
 */
UCLASS()
class GAMEAI_PROJECT_API UDEGA_Heal : public UDEGameplayAbility
{
	GENERATED_BODY()

public:
	UDEGA_Heal();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	void SetConfig(const FDEHealAbilityConfig& InConfig) { Config = InConfig; }

	// --- Reward system pass-throughs ---
	float GetLastTickHealAmount() const { return LastTickHealAmount; }
	void ResetTickHeal() { LastTickHealAmount = 0.0f; }
	bool ConsumeHealBurst(float Threshold);
	void ResetHealState();

protected:
	ADECharacter* FindNearestInjuredAlly() const;
	void UpdateHealBeam(const AActor* Target);

private:
	UPROPERTY()
	FDEHealAbilityConfig Config;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> HealBeamComponent;

	float LastTickHealAmount = 0.0f;
	float CumulativeHealAmount = 0.0f;

	UPROPERTY(Transient)
	mutable TObjectPtr<ADEMatchManager> CachedMatchManager;

	ADEMatchManager* GetMatchManager() const;
};
