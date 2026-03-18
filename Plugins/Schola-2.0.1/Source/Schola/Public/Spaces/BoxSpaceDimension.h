// Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once
#include "BoxSpaceDimension.generated.h"

/**
 * @struct FBoxSpaceDimension
 * @brief A struct representing a single dimension of a box (continuous) space.
 * 
 * A box space dimension defines a continuous range with upper and lower bounds
 * for one dimension of a multi-dimensional continuous space. It provides utilities
 * for normalizing and rescaling values within the bounds.
 */
USTRUCT(BlueprintType)
struct SCHOLA_API FBoxSpaceDimension 
{
	GENERATED_BODY()

	/**
	 * @brief The upper bound for this dimension.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Definition")
	float High = 1.0;

	/**
	 * @brief The lower bound for this dimension.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Definition")
	float Low = -1.0;

	/**
	 * @brief Constructs a BoxSpaceDimension with default bounds [-1, 1].
	 */
	FBoxSpaceDimension();

	/**
	 * @brief Constructs a BoxSpaceDimension with specific bounds.
	 * @param[in] Low The lower bound.
	 * @param[in] High The upper bound.
	 */
	FBoxSpaceDimension(float Low, float High);

	/**
	 * @brief Creates a unit dimension in the range [0, 1].
	 * @return A BoxSpaceDimension with bounds [0, 1].
	 */
	static inline FBoxSpaceDimension ZeroOneUnitDimension() { return FBoxSpaceDimension(0, 1); };

	/**
	 * @brief Creates a unit dimension centered at 0 in the range [-0.5, 0.5].
	 * @return A BoxSpaceDimension with bounds [-0.5, 0.5].
	 */
	static inline FBoxSpaceDimension CenteredUnitDimension() { return FBoxSpaceDimension(-0.5, 0.5); };

	/**
	 * @brief Rescales a normalized [0, 1] value to this dimension's bounds.
	 * @param[in] Value The normalized value to rescale.
	 * @return The rescaled value within this dimension's bounds.
	 */
	float RescaleValue(float Value) const;

	/**
	 * @brief Rescales a value from another dimension's bounds to this dimension's bounds.
	 * @param[in] Value The value to rescale.
	 * @param[in] OldHigh The upper bound of the source dimension.
	 * @param[in] OldLow The lower bound of the source dimension.
	 * @return The rescaled value within this dimension's bounds.
	 */
	float RescaleValue(float Value, float OldHigh, float OldLow) const;

	/**
	 * @brief Normalizes a value from this dimension's bounds to [0, 1].
	 * @param[in] Value The value to normalize.
	 * @return The normalized value in the range [0, 1].
	 */
	float NormalizeValue(float Value) const;

	/**
	 * @brief Checks if two BoxSpaceDimensions are equal.
	 * @param[in] Other The dimension to compare to.
	 * @return True if both dimensions have the same bounds, false otherwise.
	 */
	bool operator==(const FBoxSpaceDimension& Other) const;
};