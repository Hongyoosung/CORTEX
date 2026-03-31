// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Points/Blueprint/MultiBinaryPointBlueprintLibrary.h"
#include "Points/MultiBinaryPoint.h"

#if WITH_DEV_AUTOMATION_TESTS

// ArrayToMultiBinaryPoint Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiBinaryPointBlueprintLibrary_ArrayToMultiBinaryPoint_BasicTest, "Schola.Points.Blueprint.MultiBinaryPointBlueprintLibrary.ArrayToMultiBinaryPoint.Basic", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiBinaryPointBlueprintLibrary_ArrayToMultiBinaryPoint_BasicTest::RunTest(const FString& Parameters)
{
    TArray<bool> Values = {true, false, true, false};

    TInstancedStruct<FMultiBinaryPoint> Result = UMultiBinaryPointBlueprintLibrary::ArrayToMultiBinaryPoint(Values);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FMultiBinaryPoint& MultiBinaryPoint = Result.Get<FMultiBinaryPoint>();
    TestEqual(TEXT("MultiBinaryPoint.Values.Num() == 4"), MultiBinaryPoint.Values.Num(), 4);
    TestTrue(TEXT("MultiBinaryPoint.Values[0] == true"), MultiBinaryPoint.Values[0]);
    TestFalse(TEXT("MultiBinaryPoint.Values[1] == false"), MultiBinaryPoint.Values[1]);
    TestTrue(TEXT("MultiBinaryPoint.Values[2] == true"), MultiBinaryPoint.Values[2]);
    TestFalse(TEXT("MultiBinaryPoint.Values[3] == false"), MultiBinaryPoint.Values[3]);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiBinaryPointBlueprintLibrary_ArrayToMultiBinaryPoint_EmptyTest, "Schola.Points.Blueprint.MultiBinaryPointBlueprintLibrary.ArrayToMultiBinaryPoint.Empty", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiBinaryPointBlueprintLibrary_ArrayToMultiBinaryPoint_EmptyTest::RunTest(const FString& Parameters)
{
    TArray<bool> Values;

    TInstancedStruct<FMultiBinaryPoint> Result = UMultiBinaryPointBlueprintLibrary::ArrayToMultiBinaryPoint(Values);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FMultiBinaryPoint& MultiBinaryPoint = Result.Get<FMultiBinaryPoint>();
    TestEqual(TEXT("MultiBinaryPoint.Values.Num() == 0"), MultiBinaryPoint.Values.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiBinaryPointBlueprintLibrary_ArrayToMultiBinaryPoint_AllTrueTest, "Schola.Points.Blueprint.MultiBinaryPointBlueprintLibrary.ArrayToMultiBinaryPoint.AllTrue", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiBinaryPointBlueprintLibrary_ArrayToMultiBinaryPoint_AllTrueTest::RunTest(const FString& Parameters)
{
    TArray<bool> Values = {true, true, true, true, true};

    TInstancedStruct<FMultiBinaryPoint> Result = UMultiBinaryPointBlueprintLibrary::ArrayToMultiBinaryPoint(Values);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FMultiBinaryPoint& MultiBinaryPoint = Result.Get<FMultiBinaryPoint>();
    TestEqual(TEXT("MultiBinaryPoint.Values.Num() == 5"), MultiBinaryPoint.Values.Num(), 5);
    for (int32 i = 0; i < MultiBinaryPoint.Values.Num(); i++)
    {
        TestTrue(FString::Printf(TEXT("MultiBinaryPoint.Values[%d] == true"), i), MultiBinaryPoint.Values[i]);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiBinaryPointBlueprintLibrary_ArrayToMultiBinaryPoint_AllFalseTest, "Schola.Points.Blueprint.MultiBinaryPointBlueprintLibrary.ArrayToMultiBinaryPoint.AllFalse", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiBinaryPointBlueprintLibrary_ArrayToMultiBinaryPoint_AllFalseTest::RunTest(const FString& Parameters)
{
    TArray<bool> Values = {false, false, false};

    TInstancedStruct<FMultiBinaryPoint> Result = UMultiBinaryPointBlueprintLibrary::ArrayToMultiBinaryPoint(Values);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FMultiBinaryPoint& MultiBinaryPoint = Result.Get<FMultiBinaryPoint>();
    TestEqual(TEXT("MultiBinaryPoint.Values.Num() == 3"), MultiBinaryPoint.Values.Num(), 3);
    for (int32 i = 0; i < MultiBinaryPoint.Values.Num(); i++)
    {
        TestFalse(FString::Printf(TEXT("MultiBinaryPoint.Values[%d] == false"), i), MultiBinaryPoint.Values[i]);
    }

    return true;
}

// MultiBinaryPointToArray Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiBinaryPointBlueprintLibrary_MultiBinaryPointToArray_BasicTest, "Schola.Points.Blueprint.MultiBinaryPointBlueprintLibrary.MultiBinaryPointToArray.Basic", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiBinaryPointBlueprintLibrary_MultiBinaryPointToArray_BasicTest::RunTest(const FString& Parameters)
{
    TInstancedStruct<FMultiBinaryPoint> Point;
    Point.InitializeAs<FMultiBinaryPoint>();
    Point.GetMutable<FMultiBinaryPoint>().Values = {true, true, false, true};

    TArray<bool> Result = UMultiBinaryPointBlueprintLibrary::MultiBinaryPointToArray(Point);

    TestEqual(TEXT("Result.Num() == 4"), Result.Num(), 4);
    TestTrue(TEXT("Result[0] == true"), Result[0]);
    TestTrue(TEXT("Result[1] == true"), Result[1]);
    TestFalse(TEXT("Result[2] == false"), Result[2]);
    TestTrue(TEXT("Result[3] == true"), Result[3]);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiBinaryPointBlueprintLibrary_MultiBinaryPointToArray_RoundTripTest, "Schola.Points.Blueprint.MultiBinaryPointBlueprintLibrary.MultiBinaryPointToArray.RoundTrip", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiBinaryPointBlueprintLibrary_MultiBinaryPointToArray_RoundTripTest::RunTest(const FString& Parameters)
{
    TArray<bool> OriginalValues = {false, true, false, true, true, false};

    TInstancedStruct<FMultiBinaryPoint> Point = UMultiBinaryPointBlueprintLibrary::ArrayToMultiBinaryPoint(OriginalValues);
    TArray<bool> Result = UMultiBinaryPointBlueprintLibrary::MultiBinaryPointToArray(Point);

    TestEqual(TEXT("Round trip array length"), Result.Num(), OriginalValues.Num());
    for (int32 i = 0; i < OriginalValues.Num(); i++)
    {
        TestEqual(FString::Printf(TEXT("Round trip array[%d]"), i), Result[i], OriginalValues[i]);
    }

    return true;
}

#endif




