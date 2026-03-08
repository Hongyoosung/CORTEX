// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DESpawnArea.generated.h"

class UBoxComponent;

/**
 * ADESpawnArea - Defines a spawn region for a team within an environment.
 *
 * Place in level and assign to ADEScholaEnvironment via editor.
 * Each environment needs one Red (TeamID=0) and one Blue (TeamID=1) spawn area.
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API ADESpawnArea : public AActor
{
	GENERATED_BODY()

public:
	ADESpawnArea();

	//========================================
	// Spawn Point Generation
	//========================================
	/** Get a random spawn point within the box volume */
	UFUNCTION(BlueprintPure, Category = "DESpawnArea")
	FVector GetRandomSpawnPoint(const FRandomStream& RandomStream) const;

	/** Get spawn rotation (faces toward center of map by default) */
	UFUNCTION(BlueprintPure, Category = "DESpawnArea")
	FRotator GetSpawnRotation() const;


public:
	/** Box volume defining the spawn region */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> SpawnVolume;

	/** Team ID (0 = Red, 1 = Blue) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DESpawnArea")
	int32 TeamID = 0;

	/** Environment ID for parallel environment isolation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DESpawnArea")
	int32 EnvID = 0;

	/** Direction agents face on spawn (0 = use auto-facing toward map center) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DESpawnArea")
	FRotator SpawnFacingDirection = FRotator::ZeroRotator;

	/** If true, auto-compute facing direction based on TeamID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DESpawnArea")
	bool bAutoFacingDirection = true;
};
