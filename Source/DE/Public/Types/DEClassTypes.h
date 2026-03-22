// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DEClassTypes.generated.h"

/**
 * Five tactical classes for DE
 */
UENUM(BlueprintType)
enum class EDEClassType : uint8
{
	Strike UMETA(DisplayName = "Strike"),
	Vanguard UMETA(DisplayName = "Vanguard"),
	Support UMETA(DisplayName = "Support"),
};

/**
 * Tactical option — records the commanded class assigned to an agent.
 */
USTRUCT(BlueprintType)
struct FDETacticalOption
{
	GENERATED_BODY()

	/** Selected class */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	EDEClassType Class = EDEClassType::Strike;

	/** Confidence score from MCTS */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float Confidence = 0.5f;

	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	FVector TargetPosition = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float Duration = 0.5f;


	FDETacticalOption(EDEClassType InClass = EDEClassType::Vanguard, float InConfidence = 0.5f, FVector InTargetPosition = FVector(0.0f, 0.0f, 0.0f), float InDuration = 3.0f)
		: Class(InClass)
		, Confidence(InConfidence)
		, TargetPosition(InTargetPosition)
		, Duration(InDuration)
	{}

	FString ToString() const
	{
		return FString::Printf(TEXT("Class=%d, Confidence=%.2f, TargetPosition=%s, Duration=%f"),
			static_cast<int32>(Class),
			Confidence,
			*TargetPosition.ToString(),
			Duration);
	}
};
