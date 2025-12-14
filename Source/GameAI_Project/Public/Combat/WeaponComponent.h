// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponComponent.generated.h"

// Forward declarations
class AProjectileBase;
class USkeletalMeshComponent;
class UAnimMontage;

/**
 * Weapon fire mode
 */
UENUM(BlueprintType)
enum class EWeaponFireMode : uint8
{
	Single UMETA(DisplayName = "Single Shot"),
	Burst UMETA(DisplayName = "Burst Fire"),
	FullAuto UMETA(DisplayName = "Full Auto")
};

/**
 * Weapon fire data
 */
USTRUCT(BlueprintType)
struct FWeaponFireData
{
	GENERATED_BODY()

	/** Target that was fired at */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	AActor* Target = nullptr;

	/** Fire location (socket location) */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector FireLocation = FVector::ZeroVector;

	/** Fire direction */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector FireDirection = FVector::ZeroVector;

	/** Actual damage for this shot (after randomization) */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float ActualDamage = 0.0f;

	/** Projectile spawned */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	AProjectileBase* Projectile = nullptr;

	FWeaponFireData() {}

	FWeaponFireData(AActor* InTarget, const FVector& InFireLocation, const FVector& InFireDirection, float InDamage, AProjectileBase* InProjectile)
		: Target(InTarget)
		, FireLocation(InFireLocation)
		, FireDirection(InFireDirection)
		, ActualDamage(InDamage)
		, Projectile(InProjectile)
	{}
};

/**
 * Weapon delegates
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponFired, const FWeaponFireData&, FireData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponReloadStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponReloadCompleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponOutOfAmmo);

/**
 * UWeaponComponent - AAA-Level Weapon System
 *
 * Features:
 * - Configurable damage with randomization (Attack Power Random Weight)
 * - Configurable fire rate with variance (Attack Random Cycle)
 * - Predictive aiming (lead target based on velocity)
 * - Fires from socket with spread
 * - Ammo system (optional)
 * - Burst fire support
 * - Cooldown management
 * - Network replication ready
 *
 * Configuration:
 * - Damage: Base damage per shot
 * - AttackSpeed: Shots per second (fire rate)
 * - AttackRandomCycle: Fire rate variance (±%)
 * - AttackPowerRandomWeight: Damage variance (±%)
 *
 * Usage:
 * 1. Attach to an Actor with SkeletalMeshComponent
 * 2. Configure weapon properties
 * 3. Set MuzzleSocketName to socket on mesh
 * 4. Call FireAtTarget() or FireInDirection() from StateTree/BehaviorTree
 *
 * Integration:
 * - Used by STTask_ExecuteObjective for combat
 * - Spawns AProjectileBase actors
 * - Integrates with HealthComponent for damage dealing
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//--------------------------------------------------------------------------
	// FIRING
	//--------------------------------------------------------------------------

	/**
	 * Fire weapon at specific target
	 * 
	 * WARNING: This method provides RULE-BASED AIMING ASSISTANCE (auto-aim + predictive targeting).
	 * DO NOT USE for RL agents - use FireInDirection() instead to allow agents to learn aiming.
	 * 
	 * @param Target Target actor to aim at
	 * @param bUsePrediction Use predictive aiming (lead target)
	 * @return True if weapon fired successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	bool FireAtTarget(AActor* Target, bool bUsePrediction = true);

	/**
	 * Fire weapon in specific direction
	 * @param Direction Direction to fire (will be normalized)
	 * @return True if weapon fired successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	bool FireInDirection(const FVector& Direction);

	/**
	 * Fire weapon at specific location
	 * @param Location World location to aim at
	 * @return True if weapon fired successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	bool FireAtLocation(const FVector& Location);

	/**
	 * Start firing (for full auto mode)
	 * @param Target Target to track and fire at
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void StartFiring(AActor* Target = nullptr);

	/**
	 * Stop firing (for full auto mode)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void StopFiring();

	//--------------------------------------------------------------------------
	// QUERIES
	//--------------------------------------------------------------------------

	/** Can fire right now? */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	bool CanFire() const;

	/** Is on cooldown? */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	bool IsOnCooldown() const { return CurrentCooldown > 0.0f; }

	/** Get remaining cooldown time */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	float GetRemainingCooldown() const { return FMath::Max(CurrentCooldown, 0.0f); }

	/** Get current ammo */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	int32 GetCurrentAmmo() const { return CurrentAmmo; }

	/** Get max ammo */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	int32 GetMaxAmmo() const { return MaxAmmo; }

	/** Has ammo? */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	bool HasAmmo() const { return !bUseAmmo || CurrentAmmo > 0; }

	/** Is reloading? */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	bool IsReloading() const { return bIsReloading; }

	/** Get fire rate (shots per second) */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	float GetFireRate() const { return AttackSpeed; }

	/** Get time between shots */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	float GetTimeBetweenShots() const { return AttackSpeed > 0.0f ? 1.0f / AttackSpeed : 999.0f; }

	//--------------------------------------------------------------------------
	// AMMO & RELOAD
	//--------------------------------------------------------------------------

	/** Reload weapon */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void Reload();

	/** Add ammo */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void AddAmmo(int32 Amount);

	/** Set ammo to full */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void RefillAmmo();

	//--------------------------------------------------------------------------
	// UTILITY
	//--------------------------------------------------------------------------

	/** Get muzzle location (socket location on mesh) */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	FVector GetMuzzleLocation() const;

	/** Get muzzle rotation */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	FRotator GetMuzzleRotation() const;

	/** Calculate predicted aim location for moving target */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	FVector CalculatePredictedAimLocation(AActor* Target) const;

	/** Draw debug visualization */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void DrawDebugInfo();

protected:
	/** Internal fire logic */
	bool FireInternal(const FVector& FireDirection, AActor* Target = nullptr);

	/** Spawn projectile */
	AProjectileBase* SpawnProjectile(const FVector& FireLocation, const FVector& FireDirection, float Damage);

	/** Calculate randomized damage */
	float CalculateRandomizedDamage() const;

	/** Calculate randomized cooldown */
	float CalculateRandomizedCooldown() const;

	/** Calculate accuracy modifier based on movement and stance */
	float CalculateAccuracyModifier() const;

	/** Update cooldown */
	void UpdateCooldown(float DeltaTime);

	/** Update auto fire */
	void UpdateAutoFire(float DeltaTime);

	/** Complete reload */
	UFUNCTION()
	void CompleteReload();

	/** Find mesh component on owner */
	USkeletalMeshComponent* FindOwnerMesh() const;

public:
	//--------------------------------------------------------------------------
	// CONFIGURATION - DAMAGE
	//--------------------------------------------------------------------------

	/** Base damage per shot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Damage", meta = (ClampMin = "0.0"))
	float Damage = 10.0f;

	/** Attack power random weight: Damage variance as percentage (0.0 - 1.0)
	 * 0.0 = No variance, 0.2 = ±20% damage variance
	 * Example: Damage=10, Weight=0.2 → Random damage between 8.0 and 12.0
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AttackPowerRandomWeight = 0.1f;

	/** Minimum damage multiplier (clamps low end of random damage) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinDamageMultiplier = 0.5f;

	//--------------------------------------------------------------------------
	// CONFIGURATION - FIRE RATE
	//--------------------------------------------------------------------------

	/** Attack speed: Fire rate in shots per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|FireRate", meta = (ClampMin = "0.1", ClampMax = "100.0"))
	float AttackSpeed = 2.0f;

	/** Attack random cycle: Fire rate variance as percentage (0.0 - 1.0)
	 * 0.0 = No variance, 0.2 = ±20% fire rate variance
	 * Example: AttackSpeed=2.0, Cycle=0.2 → Random cooldown variance ±20%
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|FireRate", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AttackRandomCycle = 0.15f;

	/** Fire mode (single, burst, auto) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|FireRate")
	EWeaponFireMode FireMode = EWeaponFireMode::Single;

	/** Burst count (for burst mode) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|FireRate", meta = (EditCondition = "FireMode == EWeaponFireMode::Burst", ClampMin = "2", ClampMax = "10"))
	int32 BurstCount = 3;

	/** Delay between burst shots (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|FireRate", meta = (EditCondition = "FireMode == EWeaponFireMode::Burst", ClampMin = "0.01"))
	float BurstDelay = 0.1f;

	//--------------------------------------------------------------------------
	// CONFIGURATION - PROJECTILE
	//--------------------------------------------------------------------------

	/** Projectile class to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Projectile")
	TSubclassOf<AProjectileBase> ProjectileClass;

	/** Muzzle socket name on mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Projectile")
	FName MuzzleSocketName = TEXT("MuzzleFlash");

	/** Weapon spread (degrees, 0 = perfect accuracy) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Projectile", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float WeaponSpread = 2.0f;

	/** Use predictive aiming by default? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Projectile")
	bool bUsePredictiveAiming = true;

	/** Prediction lookahead time (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Projectile", meta = (EditCondition = "bUsePredictiveAiming", ClampMin = "0.0"))
	float PredictionLookahead = 0.5f;

	//--------------------------------------------------------------------------
	// CONFIGURATION - ANIMATION
	//--------------------------------------------------------------------------

	/** Animation montage to play when firing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Animation")
	UAnimMontage* FireMontage = nullptr;

	/** Montage section name to play (empty = play from start) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Animation")
	FName FireMontageSection = NAME_None;

	/** Montage play rate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Animation", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float FireMontagePlayRate = 1.0f;

	//--------------------------------------------------------------------------
	// CONFIGURATION - AMMO
	//--------------------------------------------------------------------------

	/** Use ammo system? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Ammo")
	bool bUseAmmo = false;

	/** Maximum ammo */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Ammo", meta = (EditCondition = "bUseAmmo", ClampMin = "1"))
	int32 MaxAmmo = 30;

	/** Starting ammo */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Ammo", meta = (EditCondition = "bUseAmmo", ClampMin = "0"))
	int32 StartingAmmo = 30;

	/** Auto reload when empty? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Ammo", meta = (EditCondition = "bUseAmmo"))
	bool bAutoReload = true;

	/** Reload time (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config|Ammo", meta = (EditCondition = "bUseAmmo", ClampMin = "0.1"))
	float ReloadTime = 2.0f;

	//--------------------------------------------------------------------------
	// CONFIGURATION - DEBUG
	//--------------------------------------------------------------------------

	/** Enable debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Debug")
	bool bEnableDebugDrawing = false;

	//--------------------------------------------------------------------------
	// STATE
	//--------------------------------------------------------------------------

	/** Current cooldown remaining (seconds) */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	float CurrentCooldown = 0.0f;

	/** Current ammo count */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	int32 CurrentAmmo = 0;

	/** Is reloading? */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	bool bIsReloading = false;

	/** Is firing (auto mode)? */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	bool bIsFiring = false;

	/** Current auto fire target */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	AActor* CurrentFireTarget = nullptr;

	/** Shots fired this session */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Stats")
	int32 ShotsFired = 0;

	//--------------------------------------------------------------------------
	// EVENTS
	//--------------------------------------------------------------------------

	/** Fired when weapon fires */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnWeaponFired OnWeaponFired;

	/** Fired when reload starts */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnWeaponReloadStarted OnWeaponReloadStarted;

	/** Fired when reload completes */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnWeaponReloadCompleted OnWeaponReloadCompleted;

	/** Fired when out of ammo */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnWeaponOutOfAmmo OnWeaponOutOfAmmo;

private:
	/** Reload timer handle */
	FTimerHandle ReloadTimerHandle;

	/** Burst fire state */
	int32 CurrentBurstShot = 0;
	FTimerHandle BurstTimerHandle;

	/** Cached mesh component */
	UPROPERTY(Transient)
	USkeletalMeshComponent* CachedMeshComponent = nullptr;
};
