// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MocTypes.generated.h"

/**
 * Five tactical strategies for MOC v10.1+
 */
UENUM(BlueprintType)
enum class EStrategyType : uint8
{
	Assault UMETA(DisplayName = "Assault"),
	Defend UMETA(DisplayName = "Defend"),
	Support UMETA(DisplayName = "Support"),
};

/**
 * MOC v10.2: Tactical Play - Predefined team compositions for action space pruning
 * Reduces combinatorial explosion (3^5 = 243 combinations → ~10 valid plays)
 */
UENUM(BlueprintType)
enum class ETacticalPlay : uint8
{
	// Aggressive Plays
	AllOutRush UMETA(DisplayName = "All-Out Rush"),          // 5 Assault
	AggressivePush UMETA(DisplayName = "Aggressive Push"),   // 4 Assault, 1 Support

	// Balanced Plays
	Phalanx UMETA(DisplayName = "Phalanx Formation"),        // 2 Defend, 3 Support
	StandardComp UMETA(DisplayName = "Standard Comp"),       // 2 Assault, 2 Defend, 1 Support

	// Defensive Plays
	FortressDefense UMETA(DisplayName = "Fortress Defense"), // 1 Assault, 4 Defend
	TurtleFormation UMETA(DisplayName = "Turtle Formation"), // 5 Defend

	// Tactical Plays
	BaitStrategy UMETA(DisplayName = "Bait Strategy"),       // 1 Assault (bait), 4 Defend (ambush)
	PincerManeuver UMETA(DisplayName = "Pincer Maneuver"),   // 3 Assault (split), 2 Support

	// Support-Focused
	HealerComp UMETA(DisplayName = "Healer Comp"),           // 2 Assault, 1 Defend, 2 Support
	ResourceDeny UMETA(DisplayName = "Resource Deny"),       // 3 Support, 2 Assault

	COUNT UMETA(Hidden)
};

/**
 * MOC v10.2: Critical event types that trigger immediate replanning
 */
UENUM(BlueprintType)
enum class ECriticalEventType : uint8
{
	AllyKilled UMETA(DisplayName = "Ally Killed"),
	EnemyKilled UMETA(DisplayName = "Enemy Killed"),
	ObjectiveCaptured UMETA(DisplayName = "Objective Captured"),
	ObjectiveLost UMETA(DisplayName = "Objective Lost"),
	HealthCritical UMETA(DisplayName = "Team Health Critical"), // Team average < 30%
	COUNT UMETA(Hidden)
};

/**
 * Team ownership
 */
UENUM(BlueprintType)
enum class ETeamOwnership : uint8
{
	Red UMETA(DisplayName = "Red Team"),
	Blue UMETA(DisplayName = "Blue Team"),
	Neutral UMETA(DisplayName = "Neutral")
};

/**
 * Tactical option output from MCTS
 */
USTRUCT(BlueprintType)
struct FTacticalOption
{
	GENERATED_BODY()

	/** Selected strategy */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	EStrategyType Strategy = EStrategyType::Assault;

	/** Target position */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	FVector TargetPosition = FVector::ZeroVector;

	/** Expected duration */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float Duration = 10.0f;

	/** Confidence score from MCTS */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float Confidence = 0.5f;

	FTacticalOption() = default;

	FTacticalOption(EStrategyType InStrategy, const FVector& InTarget, float InDuration, float InConfidence = 0.5f)
		: Strategy(InStrategy)
		, TargetPosition(InTarget)
		, Duration(InDuration)
		, Confidence(InConfidence)
	{}

	FString ToString() const
	{
		return FString::Printf(TEXT("Strategy=%d, Target=%s, Duration=%.1fs, Confidence=%.2f"),
			static_cast<int32>(Strategy),
			*TargetPosition.ToString(),
			Duration,
			Confidence);
	}
};

/**
 * Agent statistics for MOC Arena
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
struct FCapturePointState
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

	FCapturePointState() = default;
};
