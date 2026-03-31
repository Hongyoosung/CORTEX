// Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Spaces/DictSpace.h"

FDictSpace::FDictSpace()
{
}


int FDictSpace::Num()
{
	return Spaces.Num();
}

int FDictSpace::GetNumDimensions() const
{
	return Spaces.Num();
}

bool FDictSpace::IsEmpty() const
{
	return Spaces.IsEmpty();
}

int FDictSpace::GetFlattenedSize() const
{
	int Size = 0;
	for (const TPair<FString, TInstancedStruct<FSpace>>& Pair : Spaces)
	{
		const FSpace* SpacePtr = Pair.Value.GetPtr<FSpace>();
		if (SpacePtr)
		{
			Size += SpacePtr->GetFlattenedSize();
		}
	}
	return Size;
}

ESpaceValidationResult FDictSpace::Validate(const TInstancedStruct<FPoint>& InPoint) const
{
	
	if (!InPoint.GetPtr<FDictPoint>())
	{
		return ESpaceValidationResult::WrongDataType;
	}
	const FDictPoint& DictPoint = InPoint.Get<FDictPoint>();

	for (const TPair<FString, TInstancedStruct<FSpace>>& Pair : Spaces)
	{
		const FString& Key = Pair.Key;
		const FSpace* SubSpacePtr = Pair.Value.GetPtr<FSpace>();
		const TInstancedStruct<FPoint>* SubPoint = DictPoint.Points.Find(Key);
		if (!SubPoint)
		{
			return ESpaceValidationResult::WrongDimensions;
		}
		if (!SubSpacePtr)
		{
			return ESpaceValidationResult::WrongDataType;
		}
		ESpaceValidationResult Result = SubSpacePtr->Validate(*SubPoint);
		if (Result != ESpaceValidationResult::Success)
		{
			return Result;
		}
	}
	return ESpaceValidationResult::Success;
}
