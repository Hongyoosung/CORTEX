// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "NNEUtils/NNEPointToBufferConverter.h"
#include "LogScholaNNE.h"

void FNNEPointToBufferConverter::operator()(const FNNEDictBuffer& InBuffer)
{
    if (!InputPoint.GetPtr<FDictPoint>() || !Space.GetPtr<FDictSpace>())
    {
        UE_LOG(LogScholaNNE, Error, TEXT("Point and Space type mismatch for Dict conversion"));
        return;
    }

    const FDictPoint& DictPoint = InputPoint.Get<FDictPoint>();
    const FDictSpace& DictSpace = Space.Get<FDictSpace>();
    FNNEDictBuffer& DictBuffer = OutputBuffer.GetMutable<FNNEDictBuffer>();

    for (const auto& SpacePair : DictSpace.Spaces)
    {
        const FString& Key = SpacePair.Key;
        if (!DictBuffer.Buffers.Contains(Key))
        {
            // Missing buffer entry; skip but log
            UE_LOG(LogScholaNNE, Error, TEXT("Dict buffer missing key '%s'"), *Key);
            continue;
        }

        const TInstancedStruct<FPoint>* SubPointPtr = DictPoint.Points.Find(Key);
        TInstancedStruct<FNNEPointBuffer>* SubBufferPtr = DictBuffer.Buffers.Find(Key);
        if (!SubPointPtr || !SubBufferPtr || !SubBufferPtr->IsValid())
        {
            UE_LOG(LogScholaNNE, Error, TEXT("Invalid sub-point or sub-buffer for key '%s'"), *Key);
            continue;
        }

        // If sub-point missing, we can't synthesize here; just continue.
        if (!SubPointPtr->IsValid())
        {
            UE_LOG(LogScholaNNE, Error, TEXT("Sub-point for key '%s' is invalid"), *Key);
            continue;
        }

        ConvertPointToBuffer(*SubPointPtr, *SubBufferPtr, SpacePair.Value);
    }
}

void FNNEPointToBufferConverter::operator()(const FNNEBoxBuffer& InBuffer)
{
    if (!InputPoint.GetPtr<FBoxPoint>())
    {
        UE_LOG(LogScholaNNE, Error, TEXT("Point type mismatch for Box conversion"));
        return;
    }

    const FBoxPoint& BoxPoint = InputPoint.Get<FBoxPoint>();
    FNNEBoxBuffer& BoxBuffer = OutputBuffer.GetMutable<FNNEBoxBuffer>();

    if (BoxBuffer.Buffer.Num() != BoxPoint.Values.Num())
    {
        UE_LOG(LogScholaNNE, Error, TEXT("Box buffer size mismatch: buffer=%d, point=%d. Avoiding resize to preserve bindings."),
            BoxBuffer.Buffer.Num(), BoxPoint.Values.Num());
        return;
    }
    if (BoxBuffer.Buffer.Num() > 0)
    {
        FMemory::Memcpy(BoxBuffer.Buffer.GetData(), BoxPoint.Values.GetData(), BoxBuffer.Buffer.Num() * sizeof(float));
    }
}

void FNNEPointToBufferConverter::operator()(const FNNEMultiDiscreteBuffer& InBuffer)
{
    if (!InputPoint.GetPtr<FDiscretePoint>() || !Space.GetPtr<FDiscreteSpace>())
    {
        UE_LOG(LogScholaNNE, Error, TEXT("Point and Space type mismatch for Discrete conversion"));
        return;
    }

    const FMultiDiscretePoint& DiscretePoint = InputPoint.Get<FMultiDiscretePoint>();
    FNNEMultiDiscreteBuffer& DiscreteBuffer = OutputBuffer.GetMutable<FNNEMultiDiscreteBuffer>();
    const FMultiDiscreteSpace& DiscreteSpace = Space.Get<FMultiDiscreteSpace>();

    if (DiscretePoint.Values.Num() != DiscreteSpace.GetNumDimensions())
    {
        UE_LOG(LogScholaNNE, Error, TEXT("DiscretePoint dimensions (%d) don't match DiscreteSpace dimensions (%d)"),
            DiscretePoint.Values.Num(), DiscreteSpace.GetNumDimensions());
        return;
    }

    int BufferIndex = 0;
    for (int DimIndex = 0; DimIndex < DiscretePoint.Values.Num(); DimIndex++)
    {
        int SelectedValue = DiscretePoint.Values[DimIndex];
        int DimSize = DiscreteSpace.High[DimIndex];

        if (BufferIndex + DimSize > DiscreteBuffer.Buffer.Num())
        {
            UE_LOG(LogScholaNNE, Error, TEXT("Buffer bounds exceeded: end=%d, size=%d"), BufferIndex + DimSize, DiscreteBuffer.Buffer.Num());
            return;
        }

        for (int i = 0; i < DimSize; i++)
        {
            DiscreteBuffer.Buffer[BufferIndex + i] = 0.0f;
        }

        if (SelectedValue >= 0 && SelectedValue < DimSize)
        {
            DiscreteBuffer.Buffer[BufferIndex + SelectedValue] = 1.0f;
        }

        BufferIndex += DimSize;
    }
}

void FNNEPointToBufferConverter::operator()(const FNNEDiscreteBuffer& InBuffer)
{
    if (!InputPoint.GetPtr<FDiscretePoint>() || !Space.GetPtr<FDiscreteSpace>())
    {
        UE_LOG(LogScholaNNE, Error, TEXT("Point and Space type mismatch for Discrete conversion"));
        return;
    }

    const FDiscretePoint& DiscretePoint = InputPoint.Get<FDiscretePoint>();
    FNNEDiscreteBuffer& DiscreteBuffer = OutputBuffer.GetMutable<FNNEDiscreteBuffer>();
    const FDiscreteSpace& DiscreteSpace = Space.Get<FDiscreteSpace>();

    DiscreteBuffer.Buffer.Init(0.0f, DiscreteSpace.GetNumDimensions());
    DiscreteBuffer.Buffer[DiscretePoint.Value] = 1.0f;
}



void FNNEPointToBufferConverter::operator()(const FNNEMultiBinaryBuffer& InBuffer)
{
    if (!InputPoint.GetPtr<FMultiBinaryPoint>() || !Space.GetPtr<FMultiBinarySpace>())
    {
        UE_LOG(LogScholaNNE, Error, TEXT("Point and Space type mismatch for Binary conversion"));
        return;
    }

    const FMultiBinaryPoint& BinaryPoint = InputPoint.Get<FMultiBinaryPoint>();
    FNNEMultiBinaryBuffer& BinaryBuffer = OutputBuffer.GetMutable<FNNEMultiBinaryBuffer>();
    const FMultiBinarySpace& BinarySpace = Space.Get<FMultiBinarySpace>();
    const int ExpectedDims = BinarySpace.GetNumDimensions();

    if (BinaryPoint.Values.Num() != ExpectedDims)
    {
        UE_LOG(LogScholaNNE, Error, TEXT("BinaryPoint dimensions (%d) don't match BinarySpace dimensions (%d)"),
            BinaryPoint.Values.Num(), ExpectedDims);
        return;
    }

    if (BinaryBuffer.Buffer.Num() != ExpectedDims)
    {
        UE_LOG(LogScholaNNE, Error, TEXT("Binary buffer size mismatch: buffer=%d, expected=%d. Avoiding resize to preserve bindings."),
            BinaryBuffer.Buffer.Num(), ExpectedDims);
        return;
    }

    for (int i = 0; i < ExpectedDims; i++)
    {
        BinaryBuffer.Buffer[i] = BinaryPoint.Values[i] ? 1.0f : 0.0f;
    }
}

