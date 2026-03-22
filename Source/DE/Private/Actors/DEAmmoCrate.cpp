// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/DEAmmoCrate.h"
#include "Characters/DEAgent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"

ADEAmmoCrate::ADEAmmoCrate()
{
	PickupType = EPickupType::Ammo;
	RespawnTime = 20.0f;
	bVisibleThroughWalls = false;

	if (PickupMesh)
	{
		PickupMesh->SetCustomDepthStencilValue(251);
	}
}

void ADEAmmoCrate::ApplyPickupEffect_Implementation(AActor* Collector)
{
	if (!Collector) return;

	if (ADEAgent* Character = Cast<ADEAgent>(Collector))
	{
		int32 ActualAdded = Character->AddAmmo(AmmoAmount);
		if (ActualAdded > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("DEAmmoCrate: Added ammo for %s"), *Collector->GetName());
		}
	}

	if (bPlayCollectionSound && CollectionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CollectionSound, GetActorLocation(), 1.0f, 1.0f);
	}

	Super::ApplyPickupEffect_Implementation(Collector);
}

bool ADEAmmoCrate::CanCollect_Implementation(AActor* Collector) const
{
	if (!Collector) return false;

	if (const ADEAgent* Character = Cast<ADEAgent>(Collector))
	{
		return Character->GetAmmoPercentage() < MaxAmmoPercentForCollection;
	}

	return false;
}
