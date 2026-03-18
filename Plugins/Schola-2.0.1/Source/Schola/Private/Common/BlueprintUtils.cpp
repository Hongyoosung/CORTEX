// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Common/BlueprintErrorUtils.h"
#include "Points/Point.h"

inline void RaiseScriptInvalidInstancedStructError(const FString& InFunctionName)
{
	RaiseInvalidInstancedStructError(InFunctionName);
}

inline void RaiseScriptTypeMismatchError(const TInstancedStruct<FPoint>& InPoint, const FString& ExpectedType, const FString& InFunctionName)
{
	RaiseInstancedStructTypeMismatchError(InPoint, ExpectedType, InFunctionName);
}