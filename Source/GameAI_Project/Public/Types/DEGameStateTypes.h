// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DEGameStateTypes.generated.h"

/**
 * Team ownership
 */
UENUM(BlueprintType)
enum class EDETeamOwnership : uint8
{
	Red UMETA(DisplayName = "Red Team"),
	Blue UMETA(DisplayName = "Blue Team"),
	Neutral UMETA(DisplayName = "Neutral")
};

/**
 * Agent statistics for DE Arena
 */
USTRUCT(BlueprintType)
struct FAgentStats
{
	GENERATED_BODY()

	// Combat
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float MovementSpeed = 600.0f; // UE5 units/second (~6 m/s)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float SprintSpeed = 900.0f; // 1.5x multiplier

	// Weapon System (Hitscan rifle)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float FireRate = 0.15f; // 6.67 rounds/second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float Damage = 15.0f; // 7 shots to kill at full health

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float Accuracy = 0.85f; // Base hit probability

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float EffectiveRange = 5000.0f; // 50m optimal range

	// Perception
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float VisionRange = 8000.0f; // 80m sight radius

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float VisionAngle = 90.0f; // 90-degree FOV cone

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float HearingRange = 3000.0f; // 30m audio detection

	FAgentStats() = default;
};

/**
 * Capture point state
 */
USTRUCT(BlueprintType)
struct FDECapturePointState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	int32 OwnerTeam = -1; // -1 = neutral, 0 = red, 1 = blue

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	float CaptureProgress = 0.0f; // 0.0 to 1.0

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	bool bContested = false;

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	int32 RedAgentsPresent = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	int32 BlueAgentsPresent = 0;

	FDECapturePointState() = default;
};
