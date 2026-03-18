// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "NNEUtils/NNEPointCreator.h"
#include "LogScholaNNE.h"


int GetMaxIndex(TConstArrayView<float>& InArray)
{
	float CurrMax = InArray[0];
	int	  Index = 0;
	int	  CurrIndex = 0;
	for (const float& Value : InArray)
	{
		if (Value > CurrMax)
		{
			CurrMax = Value;
			Index = CurrIndex;
		}
		CurrIndex += 1;
	}
	return Index;
}


void FNNEPointCreator::operator()(const FNNEDictBuffer& InBuffer)
{
	if (!this->OutputPoint.IsValid())
	{
		this->OutputPoint.InitializeAs<FDictPoint>();
	}

    for(const TPair<FString, TInstancedStruct<FNNEPointBuffer>>& Pair : InBuffer.Buffers)
    {
        const FString& Key = Pair.Key;
		const TInstancedStruct<FNNEPointBuffer>& Buffer = Pair.Value;
        if (this->Space.Get<FDictSpace>().Spaces.Contains(Key))
        {
            // Use const references to avoid unnecessary copies during cleanup
            const TInstancedStruct<FSpace>& SubSpace = this->Space.Get<FDictSpace>().Spaces[Key];
			//This might not exist so we use FindOrAdd
			TInstancedStruct<FPoint>&		SubPoint = this->OutputPoint.GetMutable<FDictPoint>().Points.FindOrAdd(Key);
			FNNEPointCreator::CreatePoint(Buffer, SubPoint, SubSpace);
        }
        else
        {
			UE_LOG(LogScholaNNE, Error, TEXT("NNE Point Creator: Key %s not found in Dict Space"), *Key);
        }
    }
}

void FNNEPointCreator::operator()(const FNNEBoxBuffer& InBuffer)
{
	if (!this->OutputPoint.IsValid())
	{
		this->OutputPoint.InitializeAs<FBoxPoint>();
	}

    this->OutputPoint.GetMutable<FBoxPoint>().Values = InBuffer.Buffer;
}

void FNNEPointCreator::operator()(const FNNEMultiBinaryBuffer& InBuffer)
{
	if (!this->OutputPoint.IsValid())
	{
		this->OutputPoint.InitializeAs<FMultiBinaryPoint>();
	}

	TArray<bool>& PointValuesRef = this->OutputPoint.GetMutable<FMultiBinaryPoint>().Values;
	for (int i = 0; i < InBuffer.Buffer.Num(); i++)
	{
		if (InBuffer.Buffer[i] > 0.5f)
		{
			PointValuesRef[i] = true;
		}
		else
		{
			PointValuesRef[i] = false;
		}
	}
}

void FNNEPointCreator::operator()(const FNNEDiscreteBuffer& InBuffer)
{
	if (!this->OutputPoint.IsValid())
	{
		this->OutputPoint.InitializeAs<FDiscretePoint>();
	}

	FDiscretePoint& OutputPointRef = this->OutputPoint.GetMutable<FDiscretePoint>();
	const FDiscreteSpace& SpaceRef = this->Space.Get<FDiscreteSpace>();

	int CurrIndex = 0;
	int NumDims = SpaceRef.GetNumDimensions();
	
	// Advance by element count, not bytes. GetTypeSize() returns bytes and must not be used for pointer arithmetic.
	// TODO see if we can simplify this down a bit
	const float* SliceStart = InBuffer.Buffer.GetData();
	TConstArrayView<float> Slice = MakeConstArrayView<float>(SliceStart, SpaceRef.High);
	OutputPointRef.Value = GetMaxIndex(Slice);
}

void FNNEPointCreator::operator()(const FNNEMultiDiscreteBuffer& InBuffer)
{
	if (!this->OutputPoint.IsValid())
	{
		this->OutputPoint.InitializeAs<FMultiDiscretePoint>();
	}

	FMultiDiscretePoint&	   OutputPointRef = this->OutputPoint.GetMutable<FMultiDiscretePoint>();
	const FMultiDiscreteSpace& SpaceRef = this->Space.Get<FMultiDiscreteSpace>();
	int						   NumDims = SpaceRef.GetNumDimensions();
	if (OutputPointRef.Values.Num() == 0)
	{
		OutputPointRef.Values.Init(0,NumDims);
	}

	int CurrIndex = 0;
	for (int DimIndex = 0; DimIndex < NumDims; DimIndex++)
	{
		// Advance by element count, not bytes. GetTypeSize() returns bytes and must not be used for pointer arithmetic.
		const float* SliceStart = InBuffer.Buffer.GetData() + CurrIndex;
		TConstArrayView<float> Slice = MakeConstArrayView<float>(SliceStart, SpaceRef.High[DimIndex]);
		int	MaxValueIndex = GetMaxIndex(Slice);
		OutputPointRef.Values[DimIndex] = MaxValueIndex;
		CurrIndex += SpaceRef.High[DimIndex];
	}
}

