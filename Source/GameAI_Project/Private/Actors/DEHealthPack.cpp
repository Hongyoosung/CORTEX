// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/DEHealthPack.h"
#include "Characters/DECharacter.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"

ADEHealthPack::ADEHealthPack()
{
	PickupType = EPickupType::Health;
	RespawnTime = 30.0f;
	ThroughWallVisibilityRange = 2000.0f;
	bVisibleThroughWalls = true;

	if (PickupMesh)
	{
		PickupMesh->SetCustomDepthStencilValue(250);
	}
}

void ADEHealthPack::ApplyPickupEffect_Implementation(AActor* Collector)
{
	if (!Collector) return;

	if (ADECharacter* Character = Cast<ADECharacter>(Collector))
	{
		float ActualHealed = Character->HealCharacter(HealAmount);
		if (ActualHealed > 0.0f)
		{
			UE_LOG(LogTemp, Log, TEXT("DEHealthPack: Healed %s for %.1f HP"), *Collector->GetName(), ActualHealed);
		}
	}

	if (bPlayCollectionSound && CollectionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CollectionSound, GetActorLocation(), 1.0f, 1.0f);
	}

	Super::ApplyPickupEffect_Implementation(Collector);
}

bool ADEHealthPack::CanCollect_Implementation(AActor* Collector) const
{
	if (!Collector) return false;

	if (const ADECharacter* Character = Cast<ADECharacter>(Collector))
	{
		return Character->GetHealthPercentage() < MaxHealthPercentForCollection;
	}

	return false;
}
