// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/SpawnArea.h"
#include "Components/BoxComponent.h"

ASpawnArea::ASpawnArea()
{
	PrimaryActorTick.bCanEverTick = false;

	SpawnVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnVolume"));
	SpawnVolume->SetBoxExtent(FVector(300.0f, 300.0f, 100.0f));
	SpawnVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnVolume->SetHiddenInGame(false);
	RootComponent = SpawnVolume;
}

FVector ASpawnArea::GetRandomSpawnPoint() const
{
	if (!SpawnVolume)
	{
		return GetActorLocation();
	}

	const FVector Extent = SpawnVolume->GetScaledBoxExtent();
	const FVector RandomOffset(
		FMath::RandRange(-Extent.X, Extent.X),
		FMath::RandRange(-Extent.Y, Extent.Y),
		0.0f
	);

	return GetActorLocation() + GetActorRotation().RotateVector(RandomOffset);
}

FRotator ASpawnArea::GetSpawnRotation() const
{
	if (bAutoFacingDirection)
	{
		// Red team (0) faces right (+X), Blue team (1) faces left (-X)
		return FRotator(0.0f, TeamID == 0 ? 0.0f : 180.0f, 0.0f);
	}

	return SpawnFacingDirection;
}
