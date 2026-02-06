// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MOC/PickupBase.h"
#include "HealthPack.generated.h"

/**
 * AHealthPack - Health restoration pickup
 *
 * MOC v10.1 Spec (Section 0.4):
 * - Spawn Locations: 12 fixed positions (4 near neutral points, 2 per lane)
 * - Heal Amount: +40 HP (instant)
 * - Respawn Timer: 30 seconds after pickup
 * - Visual: Green holographic cross, visible through walls within 20m
 *
 * Usage:
 * 1. Place 12 instances in level at strategic locations
 * 2. Adjust VisibilityRange to 2000cm (20m)
 * 3. Set green material for visual identification
 */
UCLASS()
class GAMEAI_PROJECT_API AHealthPack : public APickupBase
{
	GENERATED_BODY()

public:
	AHealthPack();

protected:
	//========================================
	// Pickup Interface Implementation
	//========================================

	/** Apply healing effect */
	virtual void ApplyPickupEffect_Implementation(AActor* Collector) override;

	/** Check if collector needs health */
	virtual bool CanCollect_Implementation(AActor* Collector) const override;

public:
	//========================================
	// Configuration
	//========================================

	/** Amount of health restored */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HealthPack")
	float HealAmount = 40.0f;

	/** Only allow collection if health is below this percentage */
	UPROPERTY(EditAnywhere, Category = "HealthPack")
	float MaxHealthPercentForCollection = 0.99f;

	/** Play sound on collection? */
	UPROPERTY(EditAnywhere, Category = "HealthPack|Audio")
	bool bPlayCollectionSound = true;

	/** Collection sound effect */
	UPROPERTY(EditAnywhere, Category = "HealthPack|Audio")
	USoundBase* CollectionSound = nullptr;
};
