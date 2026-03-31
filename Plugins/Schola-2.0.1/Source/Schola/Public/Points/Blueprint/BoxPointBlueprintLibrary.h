// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "StructUtils/InstancedStruct.h"
#include "Points/Point.h"
#include "Points/BoxPoint.h"
#include "BoxPointBlueprintLibrary.generated.h"

/**
 * @class UBoxPointBlueprintLibrary
 * @brief Blueprint oriented helper functions for creating & inspecting Box Point InstancedStructs.
 * 
 * This library provides utility functions for creating and manipulating Box (continuous) Point instances
 * from within Blueprints. These return TInstancedStruct<FBoxPoint>.
 */
UCLASS()
class SCHOLA_API UBoxPointBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:

    /**
     * @brief Converts an array of float values to a box (continuous) point.
     * @param[in] InValues Array of float values representing continuous values in a box space.
     * @return A new box point instance.
     */
	UFUNCTION(BlueprintPure, Category = "Schola|Point|Box", meta = (DisplayName = "From Array (Box Point)", AutoCreateRefTerm = "InValues"))
	static TInstancedStruct<FBoxPoint> ArrayToBoxPoint(UPARAM(DisplayName="Values") const TArray<float>& InValues);

    /**
     * @brief Converts an array of float values to a box (continuous) point with a specific shape.
     * @param[in] InValues Array of float values representing continuous values.
     * @param[in] InShape The dimensional shape of the box point.
     * @return A new shaped box point instance.
     */
	UFUNCTION(BlueprintPure, Category = "Schola|Point|Box", meta = (DisplayName = "From Array With Shape (Box Point)", AutoCreateRefTerm = "InShape"))
    static TInstancedStruct<FBoxPoint> ArrayToBoxPointShaped(UPARAM(DisplayName="Values") const TArray<float>& InValues, UPARAM(DisplayName="Shape") const TArray<int>& InShape);

    /**
     * @brief Converts a box point to an array of float values.
     * @param[in] InBoxPoint The box point to convert.
     * @return Array of float values from the box point.
     */
    UFUNCTION(BlueprintPure, Category="Schola|Point|Box", meta=(BlueprintAutocast, DisplayName="To Array (Box Point)", CompactNodeTitle="->"))
    static TArray<float> BoxPointToArray(UPARAM(DisplayName="Box Point") const TInstancedStruct<FBoxPoint>& InBoxPoint);

};
