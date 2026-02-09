// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/HealthPack.h"
#include "Combat/Components/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"

AHealthPack::AHealthPack()
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

void AHealthPack::ApplyPickupEffect_Implementation(AActor* Collector)
{
	if (!Collector)
	{
		return;
	}

	// Try to find HealthComponent
	UHealthComponent* HealthComp = Collector->FindComponentByClass<UHealthComponent>();
	if (HealthComp)
	{
		// Apply healing
		float CurrentHealth = HealthComp->GetCurrentHealth();
		
		float MaxHealth = HealthComp->GetMaxHealth();
		float NewHealth = FMath::Min(CurrentHealth + HealAmount, MaxHealth);
		float ActualHealing = NewHealth - CurrentHealth;

		HealthComp->SetHealth(NewHealth);

		UE_LOG(LogTemp, Log, TEXT("HealthPack: Healed %s for %.1f HP (%.1f -> %.1f)"),
			*Collector->GetName(), ActualHealing, CurrentHealth, NewHealth);
	}
	else
	{
		// Fallback: Try to cast to Character and access Health directly
		ACharacter* Character = Cast<ACharacter>(Collector);
		if (Character)
		{
			// Note: This requires Character class to have Health property
			// For now, just log warning
			UE_LOG(LogTemp, Warning, TEXT("HealthPack: No HealthComponent found on %s"), *Collector->GetName());
		}
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

bool AHealthPack::CanCollect_Implementation(AActor* Collector) const
{
	if (!Collector)
	{
		return false;
	}

	// Check if collector needs health
	UHealthComponent* HealthComp = Collector->FindComponentByClass<UHealthComponent>();
	if (HealthComp)
	{
		float HealthPercent = HealthComp->GetHealthPercentage();
		return HealthPercent < MaxHealthPercentForCollection;
	}

	// If no health component, allow collection (fail-safe)
	return true;
}
