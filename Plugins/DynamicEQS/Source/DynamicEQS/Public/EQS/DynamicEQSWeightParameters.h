// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Math/UnrealMathUtility.h"

#include "DynamicEQSWeightParameters.generated.h"

/**
 * FDynamicEQSWeightParameters
 * A generic weight vector passed to an EQS query to control scoring.
 * Each element corresponds to one EQS test weight.
 */
USTRUCT(BlueprintType)
struct DYNAMICEQS_API FDynamicEQSWeightParameters
{
	GENERATED_BODY()

	/** The raw weight values, one per EQS test. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Weights")
	TArray<float> Weights;

	FDynamicEQSWeightParameters() = default;

	explicit FDynamicEQSWeightParameters(const TArray<float>& InWeights)
		: Weights(InWeights)
	{}

	/** Returns a copy of the internal weight array. */
	TArray<float> ToArray() const
	{
		return Weights;
	}

	/** Replaces the internal weight array with the supplied values. */
	void FromArray(const TArray<float>& InWeights)
	{
		Weights = InWeights;
	}

	/** Clamps all weights to [Min, Max] in-place. */
	void Clamp(float Min, float Max)
	{
		for (float& W : Weights)
		{
			W = FMath::Clamp(W, Min, Max);
		}
	}

	/** Returns the number of weights stored. */
	int32 Num() const { return Weights.Num(); }
};
