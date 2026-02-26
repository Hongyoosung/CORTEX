// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/MocAbilityBase.h"
#include "AttackAbility.generated.h"

class AProjectileBase;
class USkeletalMeshComponent;
class UAnimMontage;
class ATeamManager;

USTRUCT(BlueprintType)
struct FWeaponFireData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	AActor* Target = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector FireLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector FireDirection = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float ActualDamage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	AProjectileBase* Projectile = nullptr;

	FWeaponFireData() {}

	FWeaponFireData(AActor* InTarget, const FVector& InFireLocation, const FVector& InFireDirection, float InDamage, AProjectileBase* InProjectile)
		: Target(InTarget), FireLocation(InFireLocation), FireDirection(InFireDirection), ActualDamage(InDamage), Projectile(InProjectile) {
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponFired, const FWeaponFireData&, FireData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponReloadStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponReloadCompleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponOutOfAmmo);

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UAttackAbility : public UMocAbilityBase
{
	GENERATED_BODY()

public:
	UAttackAbility();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual void ExecuteAbility(float DeltaTime) override;
	virtual void ExecuteAbilityWithTarget(float DeltaTime, AActor* Target) override;

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	bool FireAtTarget(AActor* Target, bool bUsePrediction = false);

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	bool FireInDirection(const FVector& Direction);

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	bool FireAtLocation(const FVector& Location);

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void StartFiring(AActor* Target = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void StopFiring();

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	bool CanFire() const;

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	bool IsOnCooldown() const;

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	float GetRemainingCooldown() const;

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	float GetCooldownProgress() const;

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	int32 GetCurrentAmmo() const { return CurrentAmmo; }

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	float GetAmmoPercentage() const { return MaxAmmo > 0 ? static_cast<float>(CurrentAmmo) / static_cast<float>(MaxAmmo) : 1.0f; }

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	int32 GetMaxAmmo() const { return MaxAmmo; }

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	bool HasAmmo() const { return !bUseAmmo || CurrentAmmo > 0; }

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	bool IsReloading() const { return bIsReloading; }

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	float GetFireRate() const { return AttackSpeed; }

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	float GetTimeBetweenShots() const { return AttackSpeed > 0.0f ? 1.0f / AttackSpeed : 999.0f; }

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void Reload();

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void AddAmmo(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void RefillAmmo();

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	FVector GetMuzzleLocation() const;

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	FRotator GetMuzzleRotation() const;

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	FVector CalculatePredictedAimLocation(AActor* Target) const;

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void DrawDebugInfo();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Attack")
	float AttackRange = 8000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Damage", meta = (ClampMin = "0.0"))
	float Damage = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AttackPowerRandomWeight = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinDamageMultiplier = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|FireRate", meta = (ClampMin = "0.1", ClampMax = "100.0"))
	float AttackSpeed = 6.67f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|FireRate", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AttackRandomCycle = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Projectile")
	TSubclassOf<AProjectileBase> ProjectileClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Projectile")
	FName MuzzleSocketName = TEXT("MuzzleFlash");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Projectile", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float WeaponSpread = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Projectile")
	bool bUsePredictiveAiming = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Projectile", meta = (EditCondition = "bUsePredictiveAiming", ClampMin = "0.0"))
	float PredictionLookahead = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Animation")
	UAnimMontage* FireMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Animation")
	FName FireMontageSection = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Animation", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float FireMontagePlayRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Ammo")
	bool bUseAmmo = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Ammo", meta = (EditCondition = "bUseAmmo", ClampMin = "1"))
	int32 MaxAmmo = 150;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Ammo", meta = (EditCondition = "bUseAmmo", ClampMin = "0"))
	int32 StartingAmmo = 150;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Ammo", meta = (EditCondition = "bUseAmmo"))
	bool bAutoReload = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Ammo", meta = (EditCondition = "bUseAmmo", ClampMin = "0.1"))
	float ReloadTime = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Debug")
	bool bEnableDebugDrawing = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	int32 CurrentAmmo = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	bool bIsReloading = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	bool bIsFiring = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	AActor* CurrentFireTarget = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Stats")
	int32 ShotsFired = 0;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnWeaponFired OnWeaponFired;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnWeaponReloadStarted OnWeaponReloadStarted;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnWeaponReloadCompleted OnWeaponReloadCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnWeaponOutOfAmmo OnWeaponOutOfAmmo;

protected:
	bool FireInternal(const FVector& FireDirection, AActor* Target = nullptr);
	AProjectileBase* SpawnProjectile(const FVector& FireLocation, const FVector& FireDirection, float ProjectileDamage);
	float CalculateRandomizedDamage() const;
	float CalculateRandomizedCooldown() const;
	float CalculateAccuracyModifier() const;
	void ProcessAutoFire();

	UFUNCTION()
	void CompleteReload();

	USkeletalMeshComponent* FindOwnerMesh() const;

private:
	UPROPERTY(Transient)
	AMocCharacter* CachedOwner = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<ATeamManager> CachedTM;

	UPROPERTY(Transient)
	USkeletalMeshComponent* CachedMeshComponent = nullptr;

	FTimerHandle ReloadTimerHandle;
	FTimerHandle AutoFireTimerHandle;

	// 최적화: 틱을 사용하지 않는 타임스탬프 쿨다운
	float LastFireTime = -9999.0f;
	float CurrentRequiredCooldown = 0.0f;
};