// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StrategyTypes.generated.h"

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
 * Tactical option output from MCTS
 */
USTRUCT(BlueprintType)
struct FTacticalOption
{
	GENERATED_BODY()

	/** Selected strategy */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	EStrategyType Strategy = EStrategyType::Assault;

	/** Confidence score from MCTS */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float Confidence = 0.5f;

	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	FVector TargetPosition = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float Duration = 0.5f;


	FTacticalOption(EStrategyType InStrategy = EStrategyType::Defend, float InConfidence = 0.5f, FVector InTargetPosition = FVector(0.0f, 0.0f, 0.0f), float InDuration = 3.0f)
		: Strategy(InStrategy)
		, Confidence(InConfidence)
		, TargetPosition(InTargetPosition)
		, Duration(InDuration)
	{}

	FString ToString() const
	{
		return FString::Printf(TEXT("Strategy=%d, Confidence=%.2f, TargetPosition=%s, Duration=%f"),
			static_cast<int32>(Strategy),
			Confidence,
			*TargetPosition.ToString(),
			Duration);
	}
};
