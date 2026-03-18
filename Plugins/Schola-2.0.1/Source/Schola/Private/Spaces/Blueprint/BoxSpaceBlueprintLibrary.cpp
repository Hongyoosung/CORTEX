// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Spaces/Blueprint/BoxSpaceBlueprintLibrary.h"
#include "Spaces/BoxSpace.h"

TInstancedStruct<FBoxSpace> UBoxSpaceBlueprintLibrary::ArraysToBoxSpace(const TArray<float>& InLow, const TArray<float>& InHigh, const TArray<int32>& InShape)
{
	return TInstancedStruct<FBoxSpace>::Make<FBoxSpace>(InLow, InHigh, InShape);
}
