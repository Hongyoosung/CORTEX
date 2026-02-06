// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PickupBase.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UParticleSystemComponent;

/**
 * Pickup type enumeration
 */
UENUM(BlueprintType)
enum class EPickupType : uint8
{
	Health		UMETA(DisplayName = "Health Pack"),
	Ammo		UMETA(DisplayName = "Ammo Crate"),
	Custom		UMETA(DisplayName = "Custom")
};

/**
 * Delegate for pickup collection
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPickupCollected, AActor*, Collector, EPickupType, PickupType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPickupRespawned, EPickupType, PickupType);

/**
 * APickupBase - Base class for collectible resources
 *
 * Features:
 * - Automatic respawn timer
 * - Visual feedback (available/collected state)
 * - Through-wall visibility for nearby agents
 * - Collision-based collection
 * - Network replication ready
 *
 * Subclasses:
 * - AHealthPack: Instant +40 HP healing
 * - AAmmoCrate: +60 rounds ammo refill
 */
UCLASS(Abstract)
class GAMEAI_PROJECT_API APickupBase : public AActor
{
	GENERATED_BODY()

public:
	APickupBase();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	//========================================
	// Pickup System
	//========================================

	/** Attempt to collect this pickup */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	virtual bool TryCollect(AActor* Collector);

	/** Is pickup currently available for collection? */
	UFUNCTION(BlueprintPure, Category = "Pickup")
	bool IsAvailable() const { return bIsAvailable; }

	/** Get time until respawn (0 if available) */
	UFUNCTION(BlueprintPure, Category = "Pickup")
	float GetRespawnTimeRemaining() const;

	/** Force respawn immediately */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	void ForceRespawn();

	/** Get pickup type */
	UFUNCTION(BlueprintPure, Category = "Pickup")
	EPickupType GetPickupType() const { return PickupType; }

	/** Get pickup location for AI queries */
	UFUNCTION(BlueprintPure, Category = "Pickup")
	FVector GetPickupLocation() const { return GetActorLocation(); }

protected:
	//========================================
	// Subclass Interface
	//========================================

	/** Override in subclasses to apply pickup effect */
	UFUNCTION(BlueprintNativeEvent, Category = "Pickup")
	void ApplyPickupEffect(AActor* Collector);
	virtual void ApplyPickupEffect_Implementation(AActor* Collector);

	/** Check if collector can use this pickup */
	UFUNCTION(BlueprintNativeEvent, Category = "Pickup")
	bool CanCollect(AActor* Collector) const;
	virtual bool CanCollect_Implementation(AActor* Collector) const;

	//========================================
	// Internal Logic
	//========================================

	/** Called when pickup is collected */
	void OnCollected(AActor* Collector);

	/** Start respawn timer */
	void StartRespawnTimer();

	/** Called when respawn timer completes */
	UFUNCTION()
	void OnRespawnTimerComplete();

	/** Update visual state based on availability */
	void UpdateVisuals();

	/** Overlap handler for collection */
	UFUNCTION()
	void OnCollectionSphereBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

public:
	//========================================
	// Components
	//========================================

	/** Root component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* RootComp;

	/** Visual mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* PickupMesh;

	/** Collection trigger sphere */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USphereComponent* CollectionSphere;

	/** Visual effect (glow, hologram, etc.) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UParticleSystemComponent* VisualEffect;

	//========================================
	// Configuration
	//========================================

	/** Pickup type identifier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup")
	EPickupType PickupType = EPickupType::Custom;

	/** Respawn time after collection (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup")
	float RespawnTime = 30.0f;

	/** Collection radius (cm) */
	UPROPERTY(EditAnywhere, Category = "Pickup")
	float CollectionRadius = 100.0f;

	/** Can be collected multiple times? */
	UPROPERTY(EditAnywhere, Category = "Pickup")
	bool bRespawns = true;

	/** Show through walls within this distance (cm) */
	UPROPERTY(EditAnywhere, Category = "Pickup|Visibility")
	float ThroughWallVisibilityRange = 2000.0f;

	/** Enable through-wall rendering */
	UPROPERTY(EditAnywhere, Category = "Pickup|Visibility")
	bool bVisibleThroughWalls = true;

	/** Show debug visualization */
	UPROPERTY(EditAnywhere, Category = "Pickup|Debug")
	bool bShowDebugInfo = false;

	//========================================
	// Events
	//========================================

	/** Broadcast when pickup is collected */
	UPROPERTY(BlueprintAssignable, Category = "Pickup|Events")
	FOnPickupCollected OnPickupCollected;

	/** Broadcast when pickup respawns */
	UPROPERTY(BlueprintAssignable, Category = "Pickup|Events")
	FOnPickupRespawned OnPickupRespawned;

protected:
	//========================================
	// Runtime State
	//========================================

	/** Is pickup currently available? */
	UPROPERTY(BlueprintReadOnly, Category = "Pickup|State")
	bool bIsAvailable = true;

	/** Respawn timer handle */
	FTimerHandle RespawnTimerHandle;

	/** Time when respawn will complete (for UI) */
	float RespawnCompleteTime = 0.0f;

	/** Material instance for visual feedback */
	UPROPERTY()
	UMaterialInstanceDynamic* DynamicMaterial;
};
