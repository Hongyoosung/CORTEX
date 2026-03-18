// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Spaces/Blueprint/SpaceBlueprintLibrary.h"

#include "Spaces/MultiBinarySpace.h"
#include "Spaces/MultiDiscreteSpace.h"
#include "Spaces/BoxSpace.h"
#include "Spaces/DictSpace.h"
#include "Spaces/DiscreteSpace.h"

ESpaceType USpaceBlueprintLibrary::Space_Type(const FInstancedStruct& InSpace)
{
	if (InSpace.GetScriptStruct() && InSpace.GetScriptStruct()->IsChildOf(FMultiBinarySpace::StaticStruct()))
    {
        return ESpaceType::MultiBinary;
    }

	if (InSpace.GetScriptStruct() && InSpace.GetScriptStruct()->IsChildOf(FDiscreteSpace::StaticStruct()))
	{
		return ESpaceType::Discrete;
	}
    
    if (InSpace.GetScriptStruct() && InSpace.GetScriptStruct()->IsChildOf(FMultiDiscreteSpace::StaticStruct()))
	{
		return ESpaceType::MultiDiscrete;
	}
    
    if (InSpace.GetScriptStruct() && InSpace.GetScriptStruct()->IsChildOf(FBoxSpace::StaticStruct()))
	{
		return ESpaceType::Box;
	}

    if (InSpace.GetScriptStruct() && InSpace.GetScriptStruct()->IsChildOf(FDictSpace::StaticStruct()))
	{
		return ESpaceType::Dict;
	}

    return ESpaceType::MultiBinary;
}

bool USpaceBlueprintLibrary::Space_IsOfType(const FInstancedStruct& InSpace, ESpaceType InType)
{
	return Space_Type(InSpace) == InType;
}
