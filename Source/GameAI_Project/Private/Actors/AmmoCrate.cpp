// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/AmmoCrate.h"
#include "Combat/CombatStatsInterface.h"
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

	if (Collector->Implements<UCombatStatsInterface>())
	{
		float ActualAdded = ICombatStatsInterface::Execute_AddAmmo(Collector, AmmoAmount);

		if (ActualAdded > 0.0f)
		{
			UE_LOG(LogTemp, Log, TEXT("AmmoCrate: Added %s via Interface for %.1f Ammo"),
				*Collector->GetName(), ActualAdded);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AmmoCrate: Collector %s does not implement CombatStatsInterface"), *Collector->GetName());
	}

	// Play collection sound
	if (bPlayCollectionSound && CollectionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CollectionSound, GetActorLocation(), 1.0f, 1.0f );
	}


	// Call parent implementation
	Super::ApplyPickupEffect_Implementation(Collector);
}

bool AAmmoCrate::CanCollect_Implementation(AActor* Collector) const
{
	if (!Collector) { return false; }

	if (Collector->Implements<UCombatStatsInterface>())
	{
		float CurrentAmmo = ICombatStatsInterface::Execute_GetAmmoPercentage(Collector);

		return CurrentAmmo < MaxAmmoPercentForCollection;
	}


	// If no weapon component, don't allow collection
	return false;
}
