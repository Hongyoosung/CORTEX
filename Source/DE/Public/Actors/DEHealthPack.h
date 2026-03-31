// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/DEPickupBase.h"
#include "DEHealthPack.generated.h"

/**
 * ADEHealthPack - Health restoration pickup
 *
 * DE Spec :
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
class DE_API ADEHealthPack : public ADEPickupBase
{
	GENERATED_BODY()

public:
	ADEHealthPack();

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEHealthPack")
	float HealAmount = 40.0f;

	/** Only allow collection if health is below this percentage */
	UPROPERTY(EditAnywhere, Category = "DEHealthPack")
	float MaxHealthPercentForCollection = 0.99f;

	/** Play sound on collection? */
	UPROPERTY(EditAnywhere, Category = "DEHealthPack|Audio")
	bool bPlayCollectionSound = true;

	/** Collection sound effect */
	UPROPERTY(EditAnywhere, Category = "DEHealthPack|Audio")
	USoundBase* CollectionSound = nullptr;
};
