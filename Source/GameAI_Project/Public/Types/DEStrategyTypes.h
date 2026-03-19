// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DEStrategyTypes.generated.h"

/**
 * Five tactical strategies for DE
 */
UENUM(BlueprintType)
enum class EDEStrategyType : uint8
{
	Assault UMETA(DisplayName = "Assault"),
	Defend UMETA(DisplayName = "Defend"),
	Support UMETA(DisplayName = "Support"),
};

/**
 * Tactical option — records the commanded strategy assigned to an agent.
 */
USTRUCT(BlueprintType)
struct FDETacticalOption
{
	GENERATED_BODY()

	/** Selected strategy */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	EDEStrategyType Strategy = EDEStrategyType::Assault;

	/** Confidence score from MCTS */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float Confidence = 0.5f;

	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	FVector TargetPosition = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float Duration = 0.5f;


	FDETacticalOption(EDEStrategyType InStrategy = EDEStrategyType::Defend, float InConfidence = 0.5f, FVector InTargetPosition = FVector(0.0f, 0.0f, 0.0f), float InDuration = 3.0f)
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
