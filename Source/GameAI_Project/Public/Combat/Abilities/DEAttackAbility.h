// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/DEAbility.h"
#include "Data/DEAbilityData.h"
#include "DEAttackAbility.generated.h"

class ADEProjectileBase;
class USkeletalMeshComponent;

/**
 * Specialized ability class for attacking
 */
UCLASS()
class GAMEAI_PROJECT_API UDEAttackAbility : public UDEAbility
{
	GENERATED_BODY()

public:
	UDEAttackAbility();

	virtual void Initialize(ADECharacter* InOwner) override;
	virtual void Execute(float DeltaTime) override;
	virtual void ExecuteWithTarget(float DeltaTime, AActor* Target) override;

	void SetConfig(const FDEAttackAbilityConfig& InConfig);

	bool CanFire() const;
	bool FireAtTarget(AActor* Target, bool bUsePredictiveAiming = false);
	
	void RefillAmmo();
	void AddAmmo(int32 Amount);
	
	int32 GetCurrentAmmo() const { return CurrentAmmo; }
	float GetAmmoPercentage() const;
	float GetCooldownProgress() const;
	float GetRemainingCooldown() const;

protected:
	bool FireInternal(const FVector& FireDirection, AActor* Target = nullptr);
	ADEProjectileBase* SpawnProjectile(const FVector& FireLocation, const FVector& FireDirection, float ProjectileDamage);
	
	float CalculateRandomizedDamage() const;
	float CalculateRandomizedCooldown() const;
	float CalculateAccuracyModifier() const;

	FVector GetMuzzleLocation() const;
	FVector CalculatePredictedAimLocation(AActor* Target) const;

private:
	UPROPERTY()
	FDEAttackAbilityConfig Config;

	int32 CurrentAmmo = 0;
	bool bIsReloading = false;
	float LastFireTime = -9999.0f;
	float CurrentRequiredCooldown = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> CachedMeshComponent;

	void CompleteReload();
	FTimerHandle ReloadTimerHandle;
};
