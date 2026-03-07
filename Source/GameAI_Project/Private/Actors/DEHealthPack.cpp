// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/DEHealthPack.h"
#include "Combat/DECombatStatsInterface.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"

ADEHealthPack::ADEHealthPack()
{
	// Set pickup type
	PickupType = EPickupType::Health;

	// MOC v10.1 spec: 30 second respawn
	RespawnTime = 30.0f;

	// MOC v10.1 spec: Visible through walls within 20m (2000cm)
	ThroughWallVisibilityRange = 2000.0f;
	bVisibleThroughWalls = true;

	// Visual customization
	if (PickupMesh)
	{
		// Set green custom depth stencil for health pickups
		PickupMesh->SetCustomDepthStencilValue(250);
	}
}

void ADEHealthPack::ApplyPickupEffect_Implementation(AActor* Collector)
{
	if (!Collector)
	{
		return;
	}

	if (Collector->Implements<UDECombatStatsInterface>())
	{
		float ActualHealed = IDECombatStatsInterface::Execute_Heal(Collector, HealAmount);

		if (ActualHealed > 0.0f)
		{
			UE_LOG(LogTemp, Log, TEXT("DEHealthPack: Healed %s via Interface for %.1f HP"),
				*Collector->GetName(), ActualHealed);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DEHealthPack: Collector %s does not implement CombatStatsInterface"), *Collector->GetName());
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

	if (Collector->Implements<UDECombatStatsInterface>())
	{
		float HealthPercent = IDECombatStatsInterface::Execute_GetHealthPercentage(Collector);

		return HealthPercent < MaxHealthPercentForCollection;
	}

	return false;
}
