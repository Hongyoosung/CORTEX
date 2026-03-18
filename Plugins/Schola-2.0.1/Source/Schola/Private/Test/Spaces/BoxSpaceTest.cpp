// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Spaces/BoxSpace.h"
#include "Points/MultiBinaryPoint.h"
#if WITH_DEV_AUTOMATION_TESTS
#define TestEqualExactFloat(TestMessage, Actual, Expected) TestEqual(TestMessage, (float)Actual, (float)Expected, 0.0001f)

// Constructor Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceDefaultTest, "Schola.Spaces.BoxSpace.Default Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceDefaultTest::RunTest(const FString& Parameters)
{
    FBoxSpace BoxSpace = FBoxSpace();

    TestEqual(TEXT("BoxSpace.Dimensions.Num() == 0"), BoxSpace.Dimensions.Num(), 0);
    TestEqual(TEXT("BoxSpace.Shape.Num() == 0"), BoxSpace.Shape.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceLowHighArrayCreationTest, "Schola.Spaces.BoxSpace.TArrays Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceLowHighArrayCreationTest::RunTest(const FString& Parameters)
{
    TArray<float> Low = { -1.0, -2.0, -3.0 };
    TArray<float> High = { 1.0, 2.0, 3.0 };

    FBoxSpace BoxSpace = FBoxSpace(Low, High);

    TestEqual(TEXT("BoxSpace.Dimensions.Num() == 3"), BoxSpace.Dimensions.Num(), 3);
    TestEqual(TEXT("BoxSpace.Shape.Num() == 0"), BoxSpace.Shape.Num(), 0);

    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].Low == -1.0"), BoxSpace.Dimensions[0].Low, -1.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[1].Low == -2.0"), BoxSpace.Dimensions[1].Low, -2.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[2].Low == -3.0"), BoxSpace.Dimensions[2].Low, -3.0);

    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].High == 1.0"), BoxSpace.Dimensions[0].High, 1.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[1].High == 2.0"), BoxSpace.Dimensions[1].High, 2.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[2].High == 3.0"), BoxSpace.Dimensions[2].High, 3.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceLowHighArrayWithShapeTest, "Schola.Spaces.BoxSpace.TArrays With Shape Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceLowHighArrayWithShapeTest::RunTest(const FString& Parameters)
{
    TArray<float> Low = { -1.0, -2.0, -3.0, -4.0, -5.0, -6.0 };
    TArray<float> High = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 };
    TArray<int> Shape = { 2, 3 };

    FBoxSpace BoxSpace = FBoxSpace(Low, High, Shape);

    TestEqual(TEXT("BoxSpace.Dimensions.Num() == 6"), BoxSpace.Dimensions.Num(), 6);
    TestEqual(TEXT("BoxSpace.Shape.Num() == 2"), BoxSpace.Shape.Num(), 2);
    TestEqual(TEXT("BoxSpace.Shape[0] == 2"), BoxSpace.Shape[0], 2);
    TestEqual(TEXT("BoxSpace.Shape[1] == 3"), BoxSpace.Shape[1], 3);

    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].Low == -1.0"), BoxSpace.Dimensions[0].Low, -1.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[5].High == 6.0"), BoxSpace.Dimensions[5].High, 6.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceDimensionArrayCreationTest, "Schola.Spaces.BoxSpace.DimensionArray Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceDimensionArrayCreationTest::RunTest(const FString& Parameters)
{
    TArray<FBoxSpaceDimension> Dimensions = { FBoxSpaceDimension(-1.0, 1.0), FBoxSpaceDimension(-2.0, 2.0), FBoxSpaceDimension(-3.0, 3.0) };

    FBoxSpace BoxSpace = FBoxSpace(Dimensions);

    TestEqual(TEXT("BoxSpace.Dimensions.Num() == 3"), BoxSpace.Dimensions.Num(), 3);
	TestEqual(TEXT("BoxSpace.Shape == {3}"), BoxSpace.Shape, TArray<int>({ 3 }));
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].Low == -1.0"), BoxSpace.Dimensions[0].Low, -1.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[1].Low == -2.0"), BoxSpace.Dimensions[1].Low, -2.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[2].Low == -3.0"), BoxSpace.Dimensions[2].Low, -3.0);

    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].High == 1.0"), BoxSpace.Dimensions[0].High, 1.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[1].High == 2.0"), BoxSpace.Dimensions[1].High, 2.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[2].High == 3.0"), BoxSpace.Dimensions[2].High, 3.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceDimensionArrayWithShapeTest, "Schola.Spaces.BoxSpace.DimensionArrayWithShape Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceDimensionArrayWithShapeTest::RunTest(const FString& Parameters)
{
    TArray<FBoxSpaceDimension> Dimensions = { 
        FBoxSpaceDimension(-1.0, 1.0), 
        FBoxSpaceDimension(-2.0, 2.0), 
        FBoxSpaceDimension(-3.0, 3.0),
        FBoxSpaceDimension(-4.0, 4.0)
    };
    TArray<int> Shape = { 2, 2 };

    FBoxSpace BoxSpace = FBoxSpace(Dimensions, Shape);

    TestEqual(TEXT("BoxSpace.Dimensions.Num() == 4"), BoxSpace.Dimensions.Num(), 4);
    TestEqual(TEXT("BoxSpace.Shape.Num() == 2"), BoxSpace.Shape.Num(), 2);
    TestEqual(TEXT("BoxSpace.Shape[0] == 2"), BoxSpace.Shape[0], 2);
    TestEqual(TEXT("BoxSpace.Shape[1] == 2"), BoxSpace.Shape[1], 2);

    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].Low == -1.0"), BoxSpace.Dimensions[0].Low, -1.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[3].High == 4.0"), BoxSpace.Dimensions[3].High, 4.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceInitializerListsTest, "Schola.Spaces.BoxSpace.InitializerLists Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceInitializerListsTest::RunTest(const FString& Parameters)
{
    FBoxSpace BoxSpace = FBoxSpace({ -5.0f, -10.0f }, { 5.0f, 10.0f });

    TestEqual(TEXT("BoxSpace.Dimensions.Num() == 2"), BoxSpace.Dimensions.Num(), 2);

    //Create an array of shape [2] by default
    TestEqual(TEXT("BoxSpace.Shape.Num() == 1"), BoxSpace.Shape.Num(), 1);
    TestEqual(TEXT("BoxSpace.Shape[0] == 2"), BoxSpace.Shape[0], 2);

    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].Low == -5.0"), BoxSpace.Dimensions[0].Low, -5.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].High == 5.0"), BoxSpace.Dimensions[0].High, 5.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[1].Low == -10.0"), BoxSpace.Dimensions[1].Low, -10.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[1].High == 10.0"), BoxSpace.Dimensions[1].High, 10.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceInitializerListsWithShapeTest, "Schola.Spaces.BoxSpace.InitializerListsWithShape Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceInitializerListsWithShapeTest::RunTest(const FString& Parameters)
{
    FBoxSpace BoxSpace = FBoxSpace({ 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 2, 2 });

    TestEqual(TEXT("BoxSpace.Dimensions.Num() == 4"), BoxSpace.Dimensions.Num(), 4);
    TestEqual(TEXT("BoxSpace.Shape.Num() == 2"), BoxSpace.Shape.Num(), 2);
    TestEqual(TEXT("BoxSpace.Shape[0] == 2"), BoxSpace.Shape[0], 2);
    TestEqual(TEXT("BoxSpace.Shape[1] == 2"), BoxSpace.Shape[1], 2);

    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].Low == 0.0"), BoxSpace.Dimensions[0].Low, 0.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[3].High == 1.0"), BoxSpace.Dimensions[3].High, 1.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceShapeConstructorTest, "Schola.Spaces.BoxSpace.Shape Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceShapeConstructorTest::RunTest(const FString& Parameters)
{
    TArray<int> Shape = { 3, 4, 5 };
    FBoxSpace BoxSpace = FBoxSpace(Shape);

    TestEqual(TEXT("BoxSpace.Shape.Num() == 3"), BoxSpace.Shape.Num(), 3);
    TestEqual(TEXT("BoxSpace.Shape[0] == 3"), BoxSpace.Shape[0], 3);
    TestEqual(TEXT("BoxSpace.Shape[1] == 4"), BoxSpace.Shape[1], 4);
    TestEqual(TEXT("BoxSpace.Shape[2] == 5"), BoxSpace.Shape[2], 5);
    
    // This constructor creates a space with preallocated shape but uninitialized dimensions
    TestEqual(TEXT("BoxSpace.Dimensions.Num() == 60"), BoxSpace.Dimensions.Num(), 60); // 3*4*5 = 60

    return true;
}

// Method Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceAddDimensionTest, "Schola.Spaces.BoxSpace.Add Dimension Test ", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceAddDimensionTest::RunTest(const FString& Parameters)
{
    FBoxSpace BoxSpace = FBoxSpace();

    BoxSpace.Add(-1.0, 1.0);
    BoxSpace.Add(FBoxSpaceDimension(-2.0, 2.0));
    BoxSpace.Add(-3.0, 3.0);

    TestEqual(TEXT("BoxSpace.Dimensions.Num() == 3"), BoxSpace.Dimensions.Num(), 3);

    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].Low == -1.0"), BoxSpace.Dimensions[0].Low, -1.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[1].Low == -2.0"), BoxSpace.Dimensions[1].Low, -2.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[2].Low == -3.0"), BoxSpace.Dimensions[2].Low, -3.0);

    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[0].High == 1.0"), BoxSpace.Dimensions[0].High, 1.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[1].High == 2.0"), BoxSpace.Dimensions[1].High, 2.0);
    TestEqualExactFloat(TEXT("BoxSpace.Dimensions[2].High == 3.0"), BoxSpace.Dimensions[2].High, 3.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceNormalizedObservationSpaceTest, "Schola.Spaces.BoxSpace.Get Normalized Observation Space Test ", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceNormalizedObservationSpaceTest::RunTest(const FString& Parameters)
{
    FBoxSpace BoxSpace = FBoxSpace();
    BoxSpace.Add(-1.0, 1.0);
    BoxSpace.Add(-2.0, 2.0);
    BoxSpace.Add(-3.0, 3.0);

    FBoxSpace NormalizedBoxSpace = BoxSpace.GetNormalizedObservationSpace();

    TestEqual(TEXT("NormalizedBoxSpace.Dimensions.Num() == 3"), NormalizedBoxSpace.Dimensions.Num(), 3);

    TestEqualExactFloat(TEXT("NormalizedBoxSpace.Dimensions[0].Low == 0.0"), NormalizedBoxSpace.Dimensions[0].Low, 0.0);
    TestEqualExactFloat(TEXT("NormalizedBoxSpace.Dimensions[1].Low == 0.0"), NormalizedBoxSpace.Dimensions[1].Low, 0.0);
    TestEqualExactFloat(TEXT("NormalizedBoxSpace.Dimensions[2].Low == 0.0"), NormalizedBoxSpace.Dimensions[2].Low, 0.0);

    TestEqualExactFloat(TEXT("NormalizedBoxSpace.Dimensions[0].High == 1.0"), NormalizedBoxSpace.Dimensions[0].High, 1.0);
    TestEqualExactFloat(TEXT("NormalizedBoxSpace.Dimensions[1].High == 1.0"), NormalizedBoxSpace.Dimensions[1].High, 1.0);
    TestEqualExactFloat(TEXT("NormalizedBoxSpace.Dimensions[2].High == 1.0"), NormalizedBoxSpace.Dimensions[2].High, 1.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceFlattenedSizeTest, "Schola.Spaces.BoxSpace.Get Flattened Size Test ", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceFlattenedSizeTest::RunTest(const FString& Parameters)
{
    FBoxSpace BoxSpace = FBoxSpace();
    BoxSpace.Add(-1.0, 1.0);
    BoxSpace.Add(-2.0, 2.0);
    BoxSpace.Add(-3.0, 3.0);

    TestEqual(TEXT("BoxSpace.GetFlattenedSize() == 3"), BoxSpace.GetFlattenedSize(), 3);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceNormalizeObservationTest, "Schola.Spaces.BoxSpace.Normalize Observation Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceNormalizeObservationTest::RunTest(const FString& Parameters)
{
    FBoxSpace BoxSpace = FBoxSpace();
    BoxSpace.Add(0, 1.0);
    BoxSpace.Add(-2.0, 2.0);
    BoxSpace.Add(-3.0, 3.0);

    FBoxPoint BoxPoint = FBoxPoint();
    BoxPoint.Add(0.0);
    BoxPoint.Add(0.0);
    BoxPoint.Add(0.0);

    FBoxPoint NormalizedBoxPoint = BoxSpace.NormalizeObservation(BoxPoint);

    TestEqualExactFloat(TEXT("NormalizedBoxPoint[0] == 0.0"), NormalizedBoxPoint[0], 0.0);
    TestEqualExactFloat(TEXT("NormalizedBoxPoint[1] == 0.5"), NormalizedBoxPoint[1], 0.5);
    TestEqualExactFloat(TEXT("NormalizedBoxPoint[2] == 0.5"), NormalizedBoxPoint[2], 0.5);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceValidateWrongDataTypeTest, "Schola.Spaces.BoxSpace.Validate Wrong Type of Point Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceValidateWrongDataTypeTest::RunTest(const FString& Parameters)
{
    FBoxSpace BoxSpace = FBoxSpace();
    BoxSpace.Add(-1.0, 1.0);
    BoxSpace.Add(-2.0, 2.0);
    BoxSpace.Add(-3.0, 3.0);

    TInstancedStruct<FPoint> Point;
    Point.InitializeAs<FMultiBinaryPoint>();

    ESpaceValidationResult Result = BoxSpace.Validate(Point);

    TestEqual(TEXT("BoxSpace.Validate(Point) == ESpaceValidationResult::WrongDataType"), Result, ESpaceValidationResult::WrongDataType);

    return true;
}
#endif