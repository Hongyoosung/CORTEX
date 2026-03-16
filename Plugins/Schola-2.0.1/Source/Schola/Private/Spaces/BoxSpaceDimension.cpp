// Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Spaces/BoxSpaceDimension.h"


FBoxSpaceDimension::FBoxSpaceDimension()
{
}

FBoxSpaceDimension::FBoxSpaceDimension(float Low, float High)
	: High(High), Low(Low)
{
}


float FBoxSpaceDimension::RescaleValue(float NormalizedValue) const
{
	return (NormalizedValue * (this->High - this->Low)) + this->Low;
}

float FBoxSpaceDimension::NormalizeValue(float Value) const
{
	// Convert a value from the range of this dimension to [0,1]
	return (Value - this->Low) / (this->High - this->Low);
}

float FBoxSpaceDimension::RescaleValue(float Value, float OldHigh, float OldLow) const
{
	// Normalize the value to be between [0,1] based on it's previous range
	float NormalizedValue = (Value - OldLow) / (OldHigh - OldLow);

	// Now blow it back up to the range of this dimension
	return this->RescaleValue(NormalizedValue);
}

bool FBoxSpaceDimension::operator==(const FBoxSpaceDimension& Other) const
{
	return this->High == Other.High && this->Low == Other.Low;
}