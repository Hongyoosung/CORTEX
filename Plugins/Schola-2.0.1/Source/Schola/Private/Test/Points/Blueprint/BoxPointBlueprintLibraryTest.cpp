// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Points/Blueprint/BoxPointBlueprintLibrary.h"
#include "Points/BoxPoint.h"

#if WITH_DEV_AUTOMATION_TESTS

#define TestEqualExactFloat(TestMessage, Actual, Expected) TestEqual(TestMessage, (float)Actual, (float)Expected, 0.0001f)

// ArrayToBoxPoint Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointBlueprintLibrary_ArrayToBoxPoint_BasicTest, "Schola.Points.Blueprint.BoxPointBlueprintLibrary.ArrayToBoxPoint.Basic", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointBlueprintLibrary_ArrayToBoxPoint_BasicTest::RunTest(const FString& Parameters)
{
    TArray<float> Values = {1.0f, 2.0f, 3.0f};

    TInstancedStruct<FBoxPoint> Result = UBoxPointBlueprintLibrary::ArrayToBoxPoint(Values);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FBoxPoint& BoxPoint = Result.Get<FBoxPoint>();
    TestEqual(TEXT("BoxPoint.Values.Num() == 3"), BoxPoint.Values.Num(), 3);
    TestEqualExactFloat(TEXT("BoxPoint[0] == 1.0"), BoxPoint[0], 1.0f);
    TestEqualExactFloat(TEXT("BoxPoint[1] == 2.0"), BoxPoint[1], 2.0f);
    TestEqualExactFloat(TEXT("BoxPoint[2] == 3.0"), BoxPoint[2], 3.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointBlueprintLibrary_ArrayToBoxPoint_EmptyTest, "Schola.Points.Blueprint.BoxPointBlueprintLibrary.ArrayToBoxPoint.Empty", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointBlueprintLibrary_ArrayToBoxPoint_EmptyTest::RunTest(const FString& Parameters)
{
    TArray<float> Values;

    TInstancedStruct<FBoxPoint> Result = UBoxPointBlueprintLibrary::ArrayToBoxPoint(Values);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FBoxPoint& BoxPoint = Result.Get<FBoxPoint>();
    TestEqual(TEXT("BoxPoint.Values.Num() == 0"), BoxPoint.Values.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointBlueprintLibrary_ArrayToBoxPoint_NegativeTest, "Schola.Points.Blueprint.BoxPointBlueprintLibrary.ArrayToBoxPoint.Negative", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointBlueprintLibrary_ArrayToBoxPoint_NegativeTest::RunTest(const FString& Parameters)
{
    TArray<float> Values = {-5.0f, -10.0f, -15.0f};

    TInstancedStruct<FBoxPoint> Result = UBoxPointBlueprintLibrary::ArrayToBoxPoint(Values);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FBoxPoint& BoxPoint = Result.Get<FBoxPoint>();
    TestEqualExactFloat(TEXT("BoxPoint[0] == -5.0"), BoxPoint[0], -5.0f);
    TestEqualExactFloat(TEXT("BoxPoint[1] == -10.0"), BoxPoint[1], -10.0f);
    TestEqualExactFloat(TEXT("BoxPoint[2] == -15.0"), BoxPoint[2], -15.0f);

    return true;
}

// ArrayToBoxPointShaped Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointBlueprintLibrary_ArrayToBoxPointShaped_BasicTest, "Schola.Points.Blueprint.BoxPointBlueprintLibrary.ArrayToBoxPointShaped.Basic", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointBlueprintLibrary_ArrayToBoxPointShaped_BasicTest::RunTest(const FString& Parameters)
{
    TArray<float> Values = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    TArray<int32> Shape = {2, 3};

    TInstancedStruct<FBoxPoint> Result = UBoxPointBlueprintLibrary::ArrayToBoxPointShaped(Values, Shape);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FBoxPoint& BoxPoint = Result.Get<FBoxPoint>();
    TestEqual(TEXT("BoxPoint.Values.Num() == 6"), BoxPoint.Values.Num(), 6);
    TestEqual(TEXT("BoxPoint.Shape.Num() == 2"), BoxPoint.Shape.Num(), 2);
    TestEqual(TEXT("BoxPoint.Shape[0] == 2"), BoxPoint.Shape[0], 2);
    TestEqual(TEXT("BoxPoint.Shape[1] == 3"), BoxPoint.Shape[1], 3);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointBlueprintLibrary_ArrayToBoxPointShaped_3DTest, "Schola.Points.Blueprint.BoxPointBlueprintLibrary.ArrayToBoxPointShaped.3D", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointBlueprintLibrary_ArrayToBoxPointShaped_3DTest::RunTest(const FString& Parameters)
{
    TArray<float> Values;
    for (int32 i = 0; i < 24; i++)
    {
        Values.Add(static_cast<float>(i));
    }
    TArray<int32> Shape = {2, 3, 4};

    TInstancedStruct<FBoxPoint> Result = UBoxPointBlueprintLibrary::ArrayToBoxPointShaped(Values, Shape);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FBoxPoint& BoxPoint = Result.Get<FBoxPoint>();
    TestEqual(TEXT("BoxPoint.Values.Num() == 24"), BoxPoint.Values.Num(), 24);
    TestEqual(TEXT("BoxPoint.Shape.Num() == 3"), BoxPoint.Shape.Num(), 3);
    TestEqual(TEXT("BoxPoint.Shape[0] == 2"), BoxPoint.Shape[0], 2);
    TestEqual(TEXT("BoxPoint.Shape[1] == 3"), BoxPoint.Shape[1], 3);
    TestEqual(TEXT("BoxPoint.Shape[2] == 4"), BoxPoint.Shape[2], 4);

    return true;
}

// BoxPointToArray Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointBlueprintLibrary_BoxPointToArray_BasicTest, "Schola.Points.Blueprint.BoxPointBlueprintLibrary.BoxPointToArray.Basic", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointBlueprintLibrary_BoxPointToArray_BasicTest::RunTest(const FString& Parameters)
{
    TInstancedStruct<FBoxPoint> Point;
    Point.InitializeAs<FBoxPoint>();
    FBoxPoint& BoxPoint = Point.GetMutable<FBoxPoint>();
    BoxPoint.Values = {7.5f, 8.5f, 9.5f};

    TArray<float> Result = UBoxPointBlueprintLibrary::BoxPointToArray(Point);

    TestEqual(TEXT("Result.Num() == 3"), Result.Num(), 3);
    TestEqualExactFloat(TEXT("Result[0] == 7.5"), Result[0], 7.5f);
    TestEqualExactFloat(TEXT("Result[1] == 8.5"), Result[1], 8.5f);
    TestEqualExactFloat(TEXT("Result[2] == 9.5"), Result[2], 9.5f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointBlueprintLibrary_BoxPointToArray_RoundTripTest, "Schola.Points.Blueprint.BoxPointBlueprintLibrary.BoxPointToArray.RoundTrip", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointBlueprintLibrary_BoxPointToArray_RoundTripTest::RunTest(const FString& Parameters)
{
    TArray<float> OriginalValues = {1.1f, 2.2f, 3.3f, 4.4f};

    TInstancedStruct<FBoxPoint> Point = UBoxPointBlueprintLibrary::ArrayToBoxPoint(OriginalValues);
    TArray<float> Result = UBoxPointBlueprintLibrary::BoxPointToArray(Point);

    TestEqual(TEXT("Round trip array length"), Result.Num(), OriginalValues.Num());
    for (int32 i = 0; i < OriginalValues.Num(); i++)
    {
        TestEqualExactFloat(FString::Printf(TEXT("Round trip array[%d]"), i), Result[i], OriginalValues[i]);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointBlueprintLibrary_BoxPointToArray_ShapedRoundTripTest, "Schola.Points.Blueprint.BoxPointBlueprintLibrary.BoxPointToArray.ShapedRoundTrip", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointBlueprintLibrary_BoxPointToArray_ShapedRoundTripTest::RunTest(const FString& Parameters)
{
    TArray<float> OriginalValues = {1.0f, 2.0f, 3.0f, 4.0f};
    TArray<int32> OriginalShape = {2, 2};

    TInstancedStruct<FBoxPoint> Point = UBoxPointBlueprintLibrary::ArrayToBoxPointShaped(OriginalValues, OriginalShape);
    TArray<float> Result = UBoxPointBlueprintLibrary::BoxPointToArray(Point);

    TestEqual(TEXT("Round trip array length"), Result.Num(), OriginalValues.Num());
    for (int32 i = 0; i < OriginalValues.Num(); i++)
    {
        TestEqualExactFloat(FString::Printf(TEXT("Round trip array[%d]"), i), Result[i], OriginalValues[i]);
    }

    return true;
}

#endif




