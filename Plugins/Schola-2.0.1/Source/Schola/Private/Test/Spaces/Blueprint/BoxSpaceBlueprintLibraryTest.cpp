// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Spaces/Blueprint/BoxSpaceBlueprintLibrary.h"
#include "Spaces/BoxSpace.h"

#if WITH_DEV_AUTOMATION_TESTS

#define TestEqualExactFloat(TestMessage, Actual, Expected) TestEqual(TestMessage, (float)Actual, (float)Expected, 0.0001f)

// ArraysToBoxSpace Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceBlueprintLibrary_ArraysToBoxSpace_BasicTest, "Schola.Spaces.Blueprint.BoxSpaceBlueprintLibrary.ArraysToBoxSpace.Basic", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceBlueprintLibrary_ArraysToBoxSpace_BasicTest::RunTest(const FString& Parameters)
{
    TArray<float> Low = {-1.0f, -2.0f, -3.0f};
    TArray<float> High = {1.0f, 2.0f, 3.0f};
    TArray<int32> Shape = {3};

    TInstancedStruct<FBoxSpace> Result = UBoxSpaceBlueprintLibrary::ArraysToBoxSpace(Low, High, Shape);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FBoxSpace& BoxSpace = Result.Get<FBoxSpace>();
    TestEqual(TEXT("BoxSpace.Dimensions.Num() == 3"), BoxSpace.Dimensions.Num(), 3);
    TestEqual(TEXT("BoxSpace.Shape.Num() == 1"), BoxSpace.Shape.Num(), 1);
    TestEqual(TEXT("BoxSpace.Shape[0] == 3"), BoxSpace.Shape[0], 3);

    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].Low == -1.0"), BoxSpace.Dimensions[0].Low, -1.0f);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[1].Low == -2.0"), BoxSpace.Dimensions[1].Low, -2.0f);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[2].Low == -3.0"), BoxSpace.Dimensions[2].Low, -3.0f);

    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].High == 1.0"), BoxSpace.Dimensions[0].High, 1.0f);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[1].High == 2.0"), BoxSpace.Dimensions[1].High, 2.0f);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[2].High == 3.0"), BoxSpace.Dimensions[2].High, 3.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceBlueprintLibrary_ArraysToBoxSpace_WithShapeTest, "Schola.Spaces.Blueprint.BoxSpaceBlueprintLibrary.ArraysToBoxSpace.WithShape", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceBlueprintLibrary_ArraysToBoxSpace_WithShapeTest::RunTest(const FString& Parameters)
{
    TArray<float> Low = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    TArray<float> High = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    TArray<int32> Shape = {2, 3};

    TInstancedStruct<FBoxSpace> Result = UBoxSpaceBlueprintLibrary::ArraysToBoxSpace(Low, High, Shape);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FBoxSpace& BoxSpace = Result.Get<FBoxSpace>();
    TestEqual(TEXT("BoxSpace.Dimensions.Num() == 6"), BoxSpace.Dimensions.Num(), 6);
    TestEqual(TEXT("BoxSpace.Shape.Num() == 2"), BoxSpace.Shape.Num(), 2);
    TestEqual(TEXT("BoxSpace.Shape[0] == 2"), BoxSpace.Shape[0], 2);
    TestEqual(TEXT("BoxSpace.Shape[1] == 3"), BoxSpace.Shape[1], 3);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceBlueprintLibrary_ArraysToBoxSpace_EmptyTest, "Schola.Spaces.Blueprint.BoxSpaceBlueprintLibrary.ArraysToBoxSpace.Empty", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceBlueprintLibrary_ArraysToBoxSpace_EmptyTest::RunTest(const FString& Parameters)
{
    TArray<float> Low;
    TArray<float> High;
    TArray<int32> Shape;

    TInstancedStruct<FBoxSpace> Result = UBoxSpaceBlueprintLibrary::ArraysToBoxSpace(Low, High, Shape);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FBoxSpace& BoxSpace = Result.Get<FBoxSpace>();
    TestEqual(TEXT("BoxSpace.Dimensions.Num() == 0"), BoxSpace.Dimensions.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceBlueprintLibrary_ArraysToBoxSpace_NegativeRangeTest, "Schola.Spaces.Blueprint.BoxSpaceBlueprintLibrary.ArraysToBoxSpace.NegativeRange", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceBlueprintLibrary_ArraysToBoxSpace_NegativeRangeTest::RunTest(const FString& Parameters)
{
    TArray<float> Low = {-10.0f, -20.0f};
    TArray<float> High = {-5.0f, -10.0f};
    TArray<int32> Shape = {2};

    TInstancedStruct<FBoxSpace> Result = UBoxSpaceBlueprintLibrary::ArraysToBoxSpace(Low, High, Shape);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FBoxSpace& BoxSpace = Result.Get<FBoxSpace>();
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].Low == -10.0"), BoxSpace.Dimensions[0].Low, -10.0f);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].High == -5.0"), BoxSpace.Dimensions[0].High, -5.0f);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[1].Low == -20.0"), BoxSpace.Dimensions[1].Low, -20.0f);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[1].High == -10.0"), BoxSpace.Dimensions[1].High, -10.0f);

    return true;
}

#endif




