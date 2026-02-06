// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/FogOfWarManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Actors/PickupBase.h"

AFogOfWarManager::AFogOfWarManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	// Set default name
	SetActorLabel(TEXT("FogOfWarManager"));
}

void AFogOfWarManager::BeginPlay()
{
	Super::BeginPlay();

	// TODO: Initialize render targets if not set in Blueprint
	// This would be done in a future implementation with UTexture2D creation
	if (!RedTeamFogTexture || !BlueTeamFogTexture)
	{
		UE_LOG(LogTemp, Warning, TEXT("FogOfWarManager: Render targets not assigned. Visual fog-of-war will not function."));
	}

	UE_LOG(LogTemp, Log, TEXT("FogOfWarManager: Initialized (Enemy Memory: %.1fs)"), EnemyMemoryDuration);
}

void AFogOfWarManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Decay enemy positions periodically
	TimeSinceDecayCheck += DeltaTime;
	if (TimeSinceDecayCheck >= DecayCheckInterval)
	{
		DecayEnemyPositions(DeltaTime);
		TimeSinceDecayCheck = 0.0f;
	}

	// Debug visualization
	if (bShowDebugInfo)
	{
		DrawDebugInfo();
	}
}

//========================================
// Dynamic Objects (Enemies)
//========================================

void AFogOfWarManager::ReportEnemy(int32 TeamID, AActor* Enemy, FVector Location)
{
	if (!Enemy)
	{
		return;
	}

	float CurrentTime = GetWorld()->GetTimeSeconds();

	// Update timestamp and position
	GetEnemyTimestamps(TeamID).Add(Enemy, CurrentTime);
	GetEnemyPositions(TeamID).Add(Enemy, Location);

	if (bShowDebugInfo)
	{
		UE_LOG(LogTemp, VeryVerbose, TEXT("FogOfWar: Team %d spotted enemy %s at %s"),
			TeamID, *Enemy->GetName(), *Location.ToCompactString());
	}
}

FVector AFogOfWarManager::GetLastKnownEnemyPosition(int32 TeamID, AActor* Enemy) const
{
	const TMap<AActor*, FVector>& Positions = GetEnemyPositions(TeamID);

	if (Positions.Contains(Enemy) && IsEnemyPositionValid(TeamID, Enemy))
	{
		return Positions[Enemy];
	}

	return FVector::ZeroVector;
}

bool AFogOfWarManager::IsEnemyPositionValid(int32 TeamID, AActor* Enemy) const
{
	const TMap<AActor*, float>& Timestamps = GetEnemyTimestamps(TeamID);

	if (!Timestamps.Contains(Enemy))
	{
		return false;
	}

	float LastSeenTime = Timestamps[Enemy];
	float CurrentTime = GetWorld()->GetTimeSeconds();
	float Age = CurrentTime - LastSeenTime;

	return Age < EnemyMemoryDuration;
}

TArray<AActor*> AFogOfWarManager::GetRememberedEnemies(int32 TeamID) const
{
	TArray<AActor*> ValidEnemies;
	const TMap<AActor*, float>& Timestamps = GetEnemyTimestamps(TeamID);

	float CurrentTime = GetWorld()->GetTimeSeconds();

	for (const auto& Pair : Timestamps)
	{
		float Age = CurrentTime - Pair.Value;
		if (Age < EnemyMemoryDuration && Pair.Key)
		{
			ValidEnemies.Add(Pair.Key);
		}
	}

	return ValidEnemies;
}

//========================================
// Static Objects (Resources)
//========================================

void AFogOfWarManager::ReportResource(int32 TeamID, AActor* Resource, bool bAvailable)
{
	if (!Resource)
	{
		return;
	}

	TArray<FExploredResource>& ResourceList = GetResourceList(TeamID);
	FVector ResourceLocation = Resource->GetActorLocation();
	float CurrentTime = GetWorld()->GetTimeSeconds();

	// Check if resource already discovered (use FindByPredicate to avoid duplicates)
	FExploredResource* ExistingResource = ResourceList.FindByPredicate(
		[Resource](const FExploredResource& Item)
		{
			return Item.ActorRef == Resource;
		}
	);

	if (ExistingResource)
	{
		// Update existing resource state
		ExistingResource->bIsAvailable = bAvailable;
		ExistingResource->LastSeenTime = CurrentTime;
		ExistingResource->Location = ResourceLocation; // Update location in case resource moved

		if (bShowDebugInfo)
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("FogOfWar: Team %d updated resource %s (Available: %s)"),
				TeamID, *Resource->GetName(), bAvailable ? TEXT("Yes") : TEXT("No"));
		}
	}
	else
	{
		// Add new resource to permanent memory
		FExploredResource NewResource(Resource, ResourceLocation, bAvailable, CurrentTime);
		ResourceList.Add(NewResource);

		UE_LOG(LogTemp, Log, TEXT("FogOfWar: Team %d discovered new resource %s at %s"),
			TeamID, *Resource->GetName(), *ResourceLocation.ToCompactString());
	}
}

TArray<FExploredResource> AFogOfWarManager::GetKnownResources(int32 TeamID) const
{
	return GetResourceList(TeamID);
}

TArray<FVector> AFogOfWarManager::GetKnownResourceLocations(int32 TeamID) const
{
	TArray<FVector> Locations;
	const TArray<FExploredResource>& ResourceList = GetResourceList(TeamID);

	for (const FExploredResource& Resource : ResourceList)
	{
		Locations.Add(Resource.Location);
	}

	return Locations;
}

//========================================
// Visual System
//========================================

void AFogOfWarManager::UpdateVision(int32 TeamID, FVector ViewerLocation, float Radius)
{
	// TODO: Implement render target fog clearing
	// This is a placeholder for GPU-based fog-of-war rendering
	// Future implementation will:
	// 1. Convert ViewerLocation to render target UV coordinates
	// 2. Draw circle at UV position with Radius
	// 3. Update corresponding render target texture

	// For now, this function serves as an interface for future visual implementation
}

UTextureRenderTarget2D* AFogOfWarManager::GetFogTexture(int32 TeamID) const
{
	return TeamID == 0 ? RedTeamFogTexture : BlueTeamFogTexture;
}

//========================================
// Internal Logic
//========================================

void AFogOfWarManager::DecayEnemyPositions(float DeltaTime)
{
	float CurrentTime = GetWorld()->GetTimeSeconds();

	// Decay Red team enemy positions
	{
		TArray<AActor*> ExpiredEnemies;
		for (const auto& Pair : RedTeamEnemyTimestamps)
		{
			float Age = CurrentTime - Pair.Value;
			if (Age >= EnemyMemoryDuration)
			{
				ExpiredEnemies.Add(Pair.Key);
			}
		}

		for (AActor* Enemy : ExpiredEnemies)
		{
			RedTeamEnemyTimestamps.Remove(Enemy);
			RedTeamEnemyPositions.Remove(Enemy);

			if (bShowDebugInfo)
			{
				UE_LOG(LogTemp, Verbose, TEXT("FogOfWar: Red team forgot enemy %s (expired)"),
					Enemy ? *Enemy->GetName() : TEXT("NULL"));
			}
		}
	}

	// Decay Blue team enemy positions
	{
		TArray<AActor*> ExpiredEnemies;
		for (const auto& Pair : BlueTeamEnemyTimestamps)
		{
			float Age = CurrentTime - Pair.Value;
			if (Age >= EnemyMemoryDuration)
			{
				ExpiredEnemies.Add(Pair.Key);
			}
		}

		for (AActor* Enemy : ExpiredEnemies)
		{
			BlueTeamEnemyTimestamps.Remove(Enemy);
			BlueTeamEnemyPositions.Remove(Enemy);

			if (bShowDebugInfo)
			{
				UE_LOG(LogTemp, Verbose, TEXT("FogOfWar: Blue team forgot enemy %s (expired)"),
					Enemy ? *Enemy->GetName() : TEXT("NULL"));
			}
		}
	}
}

TMap<AActor*, float>& AFogOfWarManager::GetEnemyTimestamps(int32 TeamID)
{
	return TeamID == 0 ? RedTeamEnemyTimestamps : BlueTeamEnemyTimestamps;
}

const TMap<AActor*, float>& AFogOfWarManager::GetEnemyTimestamps(int32 TeamID) const
{
	return TeamID == 0 ? RedTeamEnemyTimestamps : BlueTeamEnemyTimestamps;
}

TMap<AActor*, FVector>& AFogOfWarManager::GetEnemyPositions(int32 TeamID)
{
	return TeamID == 0 ? RedTeamEnemyPositions : BlueTeamEnemyPositions;
}

const TMap<AActor*, FVector>& AFogOfWarManager::GetEnemyPositions(int32 TeamID) const
{
	return TeamID == 0 ? RedTeamEnemyPositions : BlueTeamEnemyPositions;
}

TArray<FExploredResource>& AFogOfWarManager::GetResourceList(int32 TeamID)
{
	return TeamID == 0 ? RedTeamKnownResources : BlueTeamKnownResources;
}

const TArray<FExploredResource>& AFogOfWarManager::GetResourceList(int32 TeamID) const
{
	return TeamID == 0 ? RedTeamKnownResources : BlueTeamKnownResources;
}

//========================================
// Debug Visualization
//========================================

void AFogOfWarManager::DrawDebugInfo()
{
	if (!GetWorld())
	{
		return;
	}

	// Draw Red team remembered enemy positions
	for (const auto& Pair : RedTeamEnemyPositions)
	{
		if (IsEnemyPositionValid(0, Pair.Key))
		{
			float Age = GetWorld()->GetTimeSeconds() - RedTeamEnemyTimestamps[Pair.Key];
			float Alpha = 1.0f - (Age / EnemyMemoryDuration);
			FColor Color = FColor::Red;
			Color.A = static_cast<uint8>(Alpha * 255);

			DrawDebugSphere(GetWorld(), Pair.Value, 80.0f, 12, Color, false, -1.0f, 0, 2.0f);
			DrawDebugString(GetWorld(), Pair.Value + FVector(0, 0, 120),
				FString::Printf(TEXT("Red: %.1fs"), Age), nullptr, FColor::Red, 0.0f, true);
		}
	}

	// Draw Blue team remembered enemy positions
	for (const auto& Pair : BlueTeamEnemyPositions)
	{
		if (IsEnemyPositionValid(1, Pair.Key))
		{
			float Age = GetWorld()->GetTimeSeconds() - BlueTeamEnemyTimestamps[Pair.Key];
			float Alpha = 1.0f - (Age / EnemyMemoryDuration);
			FColor Color = FColor::Blue;
			Color.A = static_cast<uint8>(Alpha * 255);

			DrawDebugSphere(GetWorld(), Pair.Value, 80.0f, 12, Color, false, -1.0f, 0, 2.0f);
			DrawDebugString(GetWorld(), Pair.Value + FVector(0, 0, 120),
				FString::Printf(TEXT("Blue: %.1fs"), Age), nullptr, FColor::Blue, 0.0f, true);
		}
	}

	// Draw Red team known resources
	for (const FExploredResource& Resource : RedTeamKnownResources)
	{
		FColor Color = Resource.bIsAvailable ? FColor::Green : FColor::Orange;
		DrawDebugSphere(GetWorld(), Resource.Location, 60.0f, 12, Color, false, -1.0f, 0, 1.5f);
	}

	// Draw Blue team known resources
	for (const FExploredResource& Resource : BlueTeamKnownResources)
	{
		FColor Color = Resource.bIsAvailable ? FColor::Cyan : FColor::Purple;
		DrawDebugSphere(GetWorld(), Resource.Location, 60.0f, 12, Color, false, -1.0f, 0, 1.5f);
	}
}
