// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Points/Blueprint/BoxPointBlueprintLibrary.h"
#include "Points/BoxPoint.h"
#include "Common/BlueprintErrorUtils.h"

TInstancedStruct<FBoxPoint> UBoxPointBlueprintLibrary::ArrayToBoxPoint(const TArray<float>& InValues)
{
	return TInstancedStruct<FBoxPoint>::Make(InValues);
}

TInstancedStruct<FBoxPoint> UBoxPointBlueprintLibrary::ArrayToBoxPointShaped(const TArray<float>& InValues, const TArray<int>& InShape)
{
	return TInstancedStruct<FBoxPoint>::Make(InValues, InShape);
}

TArray<float> UBoxPointBlueprintLibrary::BoxPointToArray(const TInstancedStruct<FBoxPoint>& InBoxPoint)
{
    // Type check: ensure the InstancedStruct is actually a FBoxPoint
    if (!InBoxPoint.IsValid())
    {
        RaiseInvalidInstancedStructError(TEXT("BoxPointToArray"));
        return TArray<float>();
    }

    const FBoxPoint* TypedPoint = InBoxPoint.GetPtr<FBoxPoint>();
    
    if (!TypedPoint)
    {
        RaiseInstancedStructTypeMismatchError(InBoxPoint, TEXT("FBoxPoint"), TEXT("BoxPointToArray"));
        return TArray<float>();
    }

    return TypedPoint->Values;
}

