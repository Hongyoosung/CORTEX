// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DEProjectileBase.generated.h"

// Forward declarations
class USphereComponent;
class UProjectileMovementComponent;
class UParticleSystemComponent;
class UStaticMeshComponent;

/**
 * Projectile hit result data
 */
USTRUCT(BlueprintType)
struct FDEProjectileHitData
{
	GENERATED_BODY()

	/** What was hit */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	AActor* HitActor = nullptr;

	/** Hit location */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector HitLocation = FVector::ZeroVector;

	/** Hit normal */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector HitNormal = FVector::ZeroVector;

	/** Distance traveled */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float DistanceTraveled = 0.0f;

	/** Final damage dealt (after falloff) */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float FinalDamage = 0.0f;

	/** Was this a valid hit? */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bValidHit = false;

	FDEProjectileHitData() {}

	FDEProjectileHitData(AActor* InHitActor, const FVector& InHitLocation, const FVector& InHitNormal,
		float InDistanceTraveled, float InFinalDamage, bool bInValidHit)
		: HitActor(InHitActor)
		, HitLocation(InHitLocation)
		, HitNormal(InHitNormal)
		, DistanceTraveled(InDistanceTraveled)
		, FinalDamage(InFinalDamage)
		, bValidHit(bInValidHit)
	{}
};

/**
 * Projectile delegates
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProjectileHit, const FDEProjectileHitData&, HitData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProjectileExpired);

/**
 * ADEProjectileBase - AAA-Level Projectile System
 *
 * Features:
 * - Configurable damage with distance falloff
 * - Hit validation (friendly fire, double-hit prevention)
 * - Lifetime management for pooling
 * - Prediction support for networked games
 * - Visual/audio feedback hooks
 * - Performance optimized (pooling-friendly)
 *
 * Usage:
 * 1. Spawned by WeaponComponent
 * 2. Travels in straight line (or use homing if enabled)
 * 3. Applies damage on hit
 * 4. Auto-destroys after lifetime expires
 *
 * AAA Optimizations:
 * - Minimal tick overhead (uses ProjectileMovementComponent)
 * - Pooling-friendly lifecycle
 * - Distance-based LOD for VFX
 * - Hit validation prevents exploits
 */
UCLASS()
class GAMEAI_PROJECT_API ADEProjectileBase : public AActor
{
	GENERATED_BODY()

public:
	ADEProjectileBase();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/**
	 * Initialize projectile with firing parameters
	 * Call this immediately after spawning
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Projectile")
	void InitializeProjectile(AActor* InOwner, AActor* InInstigator, float InBaseDamage, const FVector& InDirection);

	/**
	 * Set velocity (alternative to using direction)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Projectile")
	void SetVelocity(const FVector& NewVelocity);

	/**
	 * Get distance traveled since spawn
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Projectile")
	float GetDistanceTraveled() const { return DistanceTraveled; }

	/**
	 * Get remaining lifetime
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Projectile")
	float GetRemainingLifetime() const;


protected:
	/** Handle hit (collision) */
	UFUNCTION()
	void OnProjectileHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** Handle lifetime expiration */
	UFUNCTION()
	void OnLifetimeExpired();

	/** Calculate damage with falloff */
	float CalculateDamageWithFalloff(float Distance) const;

	/** Validate hit (check friendly fire, already hit, etc.) */
	bool ValidateHit(AActor* HitActor) const;

	/** Apply damage to hit actor */
	void ApplyDamageToActor(AActor* HitActor, const FVector& HitLocation, const FVector& HitNormal);

	/** Spawn impact VFX */
	void SpawnImpactEffects(const FVector& ImpactLocation, const FVector& ImpactNormal);

	/** Deactivate projectile (for pooling) */
	void DeactivateProjectile();



public:

	//========================================
	// COMPONENTS
	//========================================

	/** Collision sphere */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Components")
	TObjectPtr<USphereComponent> CollisionComponent = nullptr;

	/** Projectile movement */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement = nullptr;

	/** Visual mesh (optional) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent = nullptr;

	/** Trail VFX (optional) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Components")
	TObjectPtr<UParticleSystemComponent> TrailVFX = nullptr;


	//========================================
	// CONFIGURATION
	//========================================

	/** Base damage (before falloff) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "0.0"))
	float BaseDamage = 10.0f;

	/** Projectile speed (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "100.0"))
	float ProjectileSpeed = 3000.0f;

	/** Projectile lifetime (seconds, 0 = infinite) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "0.0"))
	float Lifetime = 5.0f;

	/** Enable damage falloff with distance? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config")
	bool bEnableDamageFalloff = true;

	/** Distance at which damage starts to fall off (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (EditCondition = "bEnableDamageFalloff", ClampMin = "0.0"))
	float FalloffStartDistance = 1000.0f;

	/** Distance at which damage reaches minimum (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (EditCondition = "bEnableDamageFalloff", ClampMin = "0.0"))
	float FalloffEndDistance = 3000.0f;

	/** Minimum damage multiplier at max distance (0.0 - 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (EditCondition = "bEnableDamageFalloff", ClampMin = "0.0", ClampMax = "1.0"))
	float MinDamageMultiplier = 0.3f;

	/** Enable homing? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config")
	bool bEnableHoming = false;

	/** Homing target (set by weapon component) */
	UPROPERTY(BlueprintReadWrite, Category = "Combat|Config")
	AActor* HomingTarget = nullptr;

	/** Homing acceleration magnitude */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (EditCondition = "bEnableHoming", ClampMin = "0.0"))
	float HomingAcceleration = 5000.0f;

	/** Collision radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "1.0"))
	float CollisionRadius = 10.0f;

	/** Can hit friendly actors? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config")
	bool bAllowFriendlyFire = false;

	/** Destroy on hit? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config")
	bool bDestroyOnHit = true;

	/** Penetrate through actors? (if false, stops on first hit) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config")
	bool bPenetrateActors = false;

	/** Maximum penetrations (0 = infinite if bPenetrateActors) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (EditCondition = "bPenetrateActors", ClampMin = "0"))
	int32 MaxPenetrations = 1;

	/** Enable debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Debug")
	bool bEnableDebugDrawing = false;


	//========================================
	// VFX/SFX HOOKS (Set in Blueprint)
	//========================================

	/** Impact particle system */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|VFX")
	UParticleSystem* ImpactVFX = nullptr;

	/** Impact sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|VFX")
	USoundBase* ImpactSound = nullptr;


	//========================================
	// STATE
	//========================================

	/** Environment ID for parallel environment isolation. Set from owner on init. */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	int32 EnvID = 0;

	/** Who fired this projectile */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	AActor* OwnerActor = nullptr;

	/** Who initiated the firing (for damage attribution) */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	AActor* InstigatorActor = nullptr;

	/** Spawn location (for distance calculation) */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	FVector SpawnLocation = FVector::ZeroVector;

	/** Distance traveled */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	float DistanceTraveled = 0.0f;

	/** Is active? (for pooling) */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	bool bIsActive = false;

	/** Number of actors hit (for penetration) */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	int32 HitCount = 0;



private:
	/** Lifetime timer handle */
	FTimerHandle LifetimeTimerHandle;

	/** Last frame location (for distance tracking) */
	FVector LastFrameLocation = FVector::ZeroVector;

	/** Actors already hit (for penetration tracking) */
	TSet<AActor*> HitActors;



public:
	//========================================
	// EVENTS
	//========================================
	/** Fired when projectile hits something */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnProjectileHit		OnProjectileHit_Delegate;

	/** Fired when projectile expires */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnProjectileExpired	OnProjectileExpired_Delegate;
};
