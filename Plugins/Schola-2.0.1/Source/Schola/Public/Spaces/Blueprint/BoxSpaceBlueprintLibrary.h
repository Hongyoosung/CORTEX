// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "StructUtils/InstancedStruct.h"
#include "Spaces/Space.h"
#include "Spaces/BoxSpace.h"
#include "BoxSpaceBlueprintLibrary.generated.h"

/**
 * @class UBoxSpaceBlueprintLibrary
 * @brief Blueprint oriented helper functions for creating & inspecting Box Space InstancedStructs.
 * 
 * This library provides utility functions for creating and manipulating Box (continuous) Space instances
 * from within Blueprints. These return TInstancedStruct<FBoxSpace>.
 */
UCLASS()
class SCHOLA_API UBoxSpaceBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:

    /**
     * @brief Converts arrays to a box (continuous) space with specified bounds and shape.
     * @param[in] InLow Array of lower bounds for each dimension.
     * @param[in] InHigh Array of upper bounds for each dimension.
     * @param[in] InShape The dimensional shape of the space.
     * @return A new box space instance.
     */
    UFUNCTION(BlueprintPure, Category="Schola|Space|Box", meta=(DisplayName="From Arrays (Box Space)"))
    static TInstancedStruct<FBoxSpace> ArraysToBoxSpace(UPARAM(DisplayName="Low") const TArray<float>& InLow, UPARAM(DisplayName="High") const TArray<float>& InHigh, UPARAM(DisplayName="Shape") const TArray<int32>& InShape);

};
