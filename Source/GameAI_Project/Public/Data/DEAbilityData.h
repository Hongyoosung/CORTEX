// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DEAbilityData.generated.h"

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float Range = 8000.0f;

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
	float ReloadTime = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	UAnimMontage* FireMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName MuzzleSocketName = TEXT("MuzzleFlash");
};

/**
 * Configuration for the Heal Ability
 */
USTRUCT(BlueprintType)
struct FDEHealAbilityConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float Range = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float Rate = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bRequireLineOfSight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TObjectPtr<UNiagaraSystem> HealBeamSystem;
};

/**
 * Data Asset containing all ability configurations for an agent
 */
UCLASS(BlueprintType)
class GAMEAI_PROJECT_API UDEAbilityData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	FDEAttackAbilityConfig AttackConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal")
	FDEHealAbilityConfig HealConfig;
};
