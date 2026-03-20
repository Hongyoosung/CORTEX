// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayEffect.h"
#include "DEAbilityData.generated.h"

class UDEGA_Attack;
class UDEGA_Heal;
class ADEProjectileBase;
class UNiagaraSystem;
class UAnimMontage;

/**
 * Configuration for the Attack Ability
 */
USTRUCT(BlueprintType)
struct FDEAttackAbilityConfig
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ability")
	TSubclassOf<UDEGA_Attack> AbilityClass;

	/** GE Blueprint to apply damage to the target. Must use SetByCaller with tag Data.Damage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float Range = 8000.0f;

	/** Minimum engagement range (cm). Agents will not fire at enemies closer than this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float MinRange = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float Damage = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float Speed = 6.67f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float RandomCycle = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TSubclassOf<ADEProjectileBase> ProjectileClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float Spread = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bUseAmmo = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	int32 MaxAmmo = 150;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float ReloadTime = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName MuzzleSocketName = TEXT("MuzzleFlash");

	/** Rotation speed when aiming at target (degrees/sec, 0 = instant) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0.0"))
	float AimRotationSpeed = 360.0f;

	/** Require line-of-sight check before firing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bRequireLineOfSight = true;

	/**
	 * Montage played when this unit fires.
	 * Configurable here so the animation can be set in both Test Mode (via UDEAbilityData)
	 * and Training Mode (set via UDEStrategyData::AbilityDataOverride).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> AttackMontage = nullptr;

};

/**
 * Configuration for the Heal Ability
 */
USTRUCT(BlueprintType)
struct FDEHealAbilityConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ability")
	TSubclassOf<UDEGA_Heal> AbilityClass;

	/** GE Blueprint to apply healing. Must use SetByCaller with tag Data.Healing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TSubclassOf<UGameplayEffect> HealEffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float Range = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float Rate = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bRequireLineOfSight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TObjectPtr<UNiagaraSystem> HealBeamSystem;

	/** Mana consumed per heal tick. Heal is blocked when Mana < ManaCost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Mana", meta = (ClampMin = "0.0"))
	float ManaCost = 10.0f;

	/**
	 * Minimum seconds between heal activations.
	 * Each activation heals Rate * HealTickInterval HP and plays the montage once.
	 * Set to 1.0s so the animation has time to complete naturally.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0.0"))
	float HealTickInterval = 1.0f;

	/**
	 * Montage played when this unit heals.
	 * Configurable here so the animation can be set in both Test Mode (via UDEAbilityData)
	 * and Training Mode (via UDEStrategyData::AbilityDataOverride).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> HealMontage = nullptr;

	/** Socket on the skeletal mesh where the heal beam originates (e.g. a hand socket). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName HealBeamSocketName = TEXT("Muzzle_01");

	/** Play rate applied to the montage while the Loop section is repeating. 1.0 = normal speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.01"))
	float LoopPlayRate = 1.0f;

};

/**
 * Data Asset containing all ability configurations for an agent
 */
UCLASS(BlueprintType)
class DE_API UDEAbilityData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	FDEAttackAbilityConfig AttackConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal")
	FDEHealAbilityConfig HealConfig;

	/** Starting and maximum mana for this agent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana", meta = (ClampMin = "0.0"))
	float MaxMana = 100.0f;

	/** Mana regenerated per second. Should be lower than ManaCost / heal interval to allow depletion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana", meta = (ClampMin = "0.0"))
	float ManaRegenRate = 5.0f;
};
