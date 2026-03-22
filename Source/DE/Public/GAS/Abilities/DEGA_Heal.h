// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GAS/DEGameplayAbility.h"
#include "Data/DEAbilityData.h"
#include "DEGA_Heal.generated.h"

class ADEAgent;
class UNiagaraComponent;
class ADEMatchManager;
class UDESupportAnimInstance;

/**
 * UDEGA_Heal - GAS-based healing ability.
 *
 * Replaces UDEHealAbility. Heals the most injured ally within range (HP-priority scoring).
 * Applies a GE_Heal GameplayEffect to the target's ASC.
 *
 * Activation:
 * - AI: TryActivateAbilityByTag(Ability.Heal)
 * - Training: ActivateAbility called from ProcessTrainingAbilities timer
 */
UCLASS()
class DE_API UDEGA_Heal : public UDEGameplayAbility
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

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	void SetConfig(const FDEHealAbilityConfig& InConfig);

	// --- Reward system pass-throughs ---
	float GetLastTickHealAmount() const { return LastTickHealAmount; }
	void ResetTickHeal() { LastTickHealAmount = 0.0f; }
	bool ConsumeHealBurst(float Threshold);
	void ResetHealState();

	/**
	 * Returns true if there is at least one injured ally within heal range.
	 * @param HealthThreshold  Ally is considered injured when GetHealthPercentage() < HealthThreshold.
	 *                         Used by the BT decorator so its configurable property is respected.
	 */
	bool HasInjuredAllyInRange(float HealthThreshold = 1.0f) const;

	/** Get the current heal target (may be null if not channeling) */
	ADEAgent* GetCurrentTarget() const { return CurrentTarget; }

protected:
	/**
	 * Find the best injured ally using priority scoring.
	 * Score = LowHPScore * 0.7 + DistanceScore * 0.3
	 * Prioritizes lowest-HP ally over nearest.
	 */
	ADEAgent* FindBestInjuredAlly(float HealthThreshold = 1.0f) const;
	void UpdateHealBeam(const AActor* Target);

private:
	/** Applies one heal tick to CurrentTarget and re-evaluates whether to continue the channel. */
	void OnHealTick();

	/** Updates BeamStart/BeamEnd Niagara parameters every frame while the channel is active. */
	void OnBeamUpdateTick();

	/** Applies the heal GE to Target and records LastTickHealAmount / CumulativeHealAmount. */
	void ApplyHealToTarget(ADEAgent* Target, float HealAmount);

	UPROPERTY()
	FDEHealAbilityConfig Config;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> HealBeamComponent;

	/** Current heal target held across the channel lifetime. Re-evaluated each heal tick. */
	UPROPERTY(Transient)
	TObjectPtr<ADEAgent> CurrentTarget;

	/** Repeating timer — fires at HealTickInterval to apply heal and re-validate the target. */
	FTimerHandle HealTickTimerHandle;

	/** Repeating timer — fires each frame to update BeamStart/BeamEnd world positions. */
	FTimerHandle BeamUpdateTimerHandle;

	float LastTickHealAmount = 0.0f;
	float CumulativeHealAmount = 0.0f;
	float LastHealTime = -1.0f;

	UPROPERTY(Transient)
	mutable TObjectPtr<ADEMatchManager> CachedMatchManager;

	/** Base AnimInstance cached per activation — used for section redirects regardless of subclass. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimInstance> CachedAnimInstance;

	/** Cached only when the anim instance is UDESupportAnimInstance — optional extra features. */
	UPROPERTY(Transient)
	TObjectPtr<UDESupportAnimInstance> CachedSupportAnimInstance;

	ADEMatchManager* GetMatchManager() const;

	/** Resolves and caches the UDESupportAnimInstance from the owner's skeletal mesh. */
	UDESupportAnimInstance* GetSupportAnimInstance() const;
};
