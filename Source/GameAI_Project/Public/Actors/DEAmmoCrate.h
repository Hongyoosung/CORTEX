// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/DEPickupBase.h"
#include "DEAmmoCrate.generated.h"

/**
 * ADEAmmoCrate - Ammunition refill pickup
 *
 * DE Spec :
 * - Spawn Locations: 8 fixed positions (concentrated near capture points)
 * - Ammo Refill: +60 rounds
 * - Respawn Timer: 20 seconds
 * - Visual: Orange holographic box
 *
 * Usage:
 * 1. Place 8 instances in level near capture points
 * 2. Set orange material for visual identification
 * 3. Integrates with WeaponComponent for ammo refill
 */
UCLASS()
class GAMEAI_PROJECT_API ADEAmmoCrate : public ADEPickupBase
{
	GENERATED_BODY()

public:
	ADEAmmoCrate();

protected:
	//========================================
	// Pickup Interface Implementation
	//========================================

	/** Apply ammo refill effect */
	virtual void ApplyPickupEffect_Implementation(AActor* Collector) override;

	/** Check if collector needs ammo */
	virtual bool CanCollect_Implementation(AActor* Collector) const override;

public:
	//========================================
	// Configuration
	//========================================

	/** Amount of ammo added */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DEAmmoCrate")
	int32 AmmoAmount = 60;

	/** Only allow collection if ammo is below this percentage */
	UPROPERTY(EditAnywhere, Category = "DEAmmoCrate")
	float MaxAmmoPercentForCollection = 0.99f;

	/** Play sound on collection? */
	UPROPERTY(EditAnywhere, Category = "DEAmmoCrate|Audio")
	bool bPlayCollectionSound = true;

	/** Collection sound effect */
	UPROPERTY(EditAnywhere, Category = "DEAmmoCrate|Audio")
	USoundBase* CollectionSound = nullptr;
};
