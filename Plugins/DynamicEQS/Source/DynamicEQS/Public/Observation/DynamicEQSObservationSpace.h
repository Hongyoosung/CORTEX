// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "DynamicEQSObservationSpace.generated.h"

/**
 * Describes the type of observation/action space.
 */
UENUM(BlueprintType)
enum class EDynamicEQSSpaceType : uint8
{
	Box      UMETA(DisplayName = "Box (Continuous)"),
	Discrete UMETA(DisplayName = "Discrete"),
};

/**
 * FDynamicEQSObservationSpace
 * Describes the shape and bounds of an observation or action space.
 */
USTRUCT(BlueprintType)
struct DYNAMICEQS_API FDynamicEQSObservationSpace
{
	GENERATED_BODY()

	/** Number of dimensions in the space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Space")
	int32 Dimensions = 0;

	/** Lower bounds per dimension (Box type). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Space")
	TArray<float> Low;

	/** Upper bounds per dimension (Box type). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Space")
	TArray<float> High;

	/** Space type: continuous box or discrete. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Space")
	EDynamicEQSSpaceType SpaceType = EDynamicEQSSpaceType::Box;

	FDynamicEQSObservationSpace() = default;

	/** Convenience constructor for a flat Box space. */
	FDynamicEQSObservationSpace(int32 InDimensions, float InLow, float InHigh)
		: Dimensions(InDimensions)
		, SpaceType(EDynamicEQSSpaceType::Box)
	{
		Low.Init(InLow, InDimensions);
		High.Init(InHigh, InDimensions);
	}
};
