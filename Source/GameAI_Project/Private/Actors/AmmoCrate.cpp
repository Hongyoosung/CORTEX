// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/AmmoCrate.h"
#include "Combat/Components/WeaponComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"

AAmmoCrate::AAmmoCrate()
{
	// Set pickup type
	PickupType = EPickupType::Ammo;

	// MOC v10.1 spec: 20 second respawn
	RespawnTime = 20.0f;

	// Visual feedback (not through walls by default for ammo)
	bVisibleThroughWalls = false;

	// Visual customization
	if (PickupMesh)
	{
		// Set orange custom depth stencil for ammo pickups
		PickupMesh->SetCustomDepthStencilValue(251);
	}
}

void AAmmoCrate::ApplyPickupEffect_Implementation(AActor* Collector)
{
	if (!Collector)
	{
		return;
	}

	// Try to find WeaponComponent
	UWeaponComponent* WeaponComp = Collector->FindComponentByClass<UWeaponComponent>();
	if (WeaponComp)
	{
		// Apply ammo refill
		int32 CurrentAmmo = WeaponComp->GetCurrentAmmo();
		int32 MaxAmmo = WeaponComp->GetMaxAmmo();

		WeaponComp->AddAmmo(AmmoAmount);

		int32 NewAmmo = WeaponComp->GetCurrentAmmo();
		int32 ActualAmmoAdded = NewAmmo - CurrentAmmo;

		UE_LOG(LogTemp, Log, TEXT("AmmoCrate: Refilled %s with %d ammo (%d -> %d)"),
			*Collector->GetName(), ActualAmmoAdded, CurrentAmmo, NewAmmo);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AmmoCrate: No WeaponComponent found on %s"), *Collector->GetName());
	}

	// Play collection sound
	if (bPlayCollectionSound && CollectionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			CollectionSound,
			GetActorLocation(),
			1.0f,
			1.0f
		);
	}

	// Call parent implementation
	Super::ApplyPickupEffect_Implementation(Collector);
}

bool AAmmoCrate::CanCollect_Implementation(AActor* Collector) const
{
	if (!Collector)
	{
		return false;
	}

	// Check if collector needs ammo
	UWeaponComponent* WeaponComp = Collector->FindComponentByClass<UWeaponComponent>();
	if (WeaponComp)
	{
		// Only allow collection if using ammo system
		if (!WeaponComp->bUseAmmo)
		{
			return false;
		}

		// Check if ammo is below threshold
		float AmmoPercent = WeaponComp->GetAmmoPercentage();
		return AmmoPercent < MaxAmmoPercentForCollection;
	}

	// If no weapon component, don't allow collection
	return false;
}
