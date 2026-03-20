// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GAS/DEGameplayAbility.h"
#include "Data/DEAbilityData.h"
#include "DEGA_Attack.generated.h"

class ADEProjectileBase;
class ADEAgent;
class ADEMatchManager;

/**
 * UDEGA_Attack - GAS-based attack ability.
 *
 * Replaces UDEAttackAbility. Spawns projectiles, manages ammo, handles cooldowns via GAS tags.
 *
 * Activation:
 * - AI: TryActivateAbilityByTag(Ability.Attack)
 * - Training: ActivateAbility called from ProcessTrainingAbilities timer
 *
 * The ability auto-selects the nearest enemy if no explicit target is provided via EventData.
 */
UCLASS()
class DE_API UDEGA_Attack : public UDEGameplayAbility
{
	GENERATED_BODY()

public:
	UDEGA_Attack();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// --- Ammo Management ---

	void SetConfig(const FDEAttackAbilityConfig& InConfig) { Config = InConfig; CurrentAmmo = Config.MaxAmmo; }

	bool CanFire() const;
	int32 GetCurrentAmmo() const { return CurrentAmmo; }
	float GetAmmoPercentage() const;
	float GetCooldownProgress() const;
	float GetRemainingCooldown() const;
	void RefillAmmo();
	void AddAmmo(int32 Amount);
	void ResetState();

	// --- Combat Interface (used by BT tasks) ---

	/** Return true if Target is alive, within range, and (if configured) visible. */
	bool IsTargetValid(AActor* Target) const;

	/** Fire a projectile at Target. Returns false if unable to fire. */
	bool FireAtTarget(AActor* Target, const FGameplayAbilityActorInfo* ActorInfo);

protected:
	ADEProjectileBase* SpawnProjectile(const FVector& FireLocation, const FVector& FireDirection, float ProjectileDamage);
	AActor* FindNearestEnemy() const;

	FVector GetMuzzleLocation() const;
	float CalculateRandomizedDamage() const;

private:
	UPROPERTY()
	FDEAttackAbilityConfig Config;

	int32 CurrentAmmo = 0;
	bool bIsReloading = false;
	float LastFireTime = -9999.0f;
	float ReloadStartTime = -9999.0f;

	void StartReload();
	void CompleteReload();
	FTimerHandle ReloadTimerHandle;

	UPROPERTY(Transient)
	mutable TObjectPtr<ADEMatchManager> CachedMatchManager;

	ADEMatchManager* GetMatchManager() const;
};
