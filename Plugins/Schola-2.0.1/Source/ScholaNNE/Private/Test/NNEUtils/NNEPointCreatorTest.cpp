// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"

#include "NNEUtils/NNEPointCreator.h"

#define TestEqualExactFloat(TestMessage, Actual, Expected) TestEqual(TestMessage, (float)Actual, (float)Expected, 0.0001f)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNNEPointCreatorDiscreteBufferTest, "Schola.Policies.NNE.NNEPointCreator.DiscreteBuffer Test", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNNEPointCreatorDiscreteBufferTest::RunTest(const FString& Parameters)
{
	FMultiDiscreteSpace DiscreteSpace({5,});
    
    TArray<float> BufferData = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
    FNNEMultiDiscreteBuffer NNEBuffer(5);
    NNEBuffer.Buffer = BufferData;
    
    TInstancedStruct<FSpace> SpaceStruct;
    SpaceStruct.InitializeAs<FMultiDiscreteSpace>(DiscreteSpace);
    
    TInstancedStruct<FPoint> OutputPoint;
	OutputPoint.InitializeAs<FMultiDiscretePoint>();
	OutputPoint.GetMutable<FMultiDiscretePoint>().Values.Init(0, 1);
    
    TInstancedStruct<FNNEPointBuffer> BufferStruct;
    BufferStruct.InitializeAs<FNNEMultiDiscreteBuffer>(NNEBuffer);
    
    FNNEPointCreator::CreatePoint(BufferStruct, OutputPoint, SpaceStruct);
    
    const FMultiDiscretePoint& ResultPoint = OutputPoint.Get<FMultiDiscretePoint>();
    TestEqual(TEXT("DiscretePoint should have one dimension"), ResultPoint.Values.Num(), 1);
    TestEqual(TEXT("DiscretePoint value should be 2 (index of max value in [0,0,1,0,0])"), ResultPoint.Values[0], 2);
    
    return true;
}
