// Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Spaces/DiscreteSpace.h"
#include "Points/MultiBinaryPoint.h"
#if WITH_DEV_AUTOMATION_TESTS
#define TestEqualExactFloat(TestMessage, Actual, Expected) TestEqual(TestMessage, (float)Actual, (float)Expected, 0.0001f)

// Constructor Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiscreteSpaceDefaultTest, "Schola.Spaces.DiscreteSpace.Default Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDiscreteSpaceDefaultTest::RunTest(const FString& Parameters)
{
    FDiscreteSpace DiscreteSpace = FDiscreteSpace();

    TestEqual(TEXT("DiscreteSpace.High == 0"), DiscreteSpace.High, 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiscreteSpaceIntConstructorTest, "Schola.Spaces.DiscreteSpace.Int Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDiscreteSpaceIntConstructorTest::RunTest(const FString& Parameters)
{
    FDiscreteSpace DiscreteSpace = FDiscreteSpace(5);

    TestEqual(TEXT("DiscreteSpace.High == 5"), DiscreteSpace.High, 5);

    return true;
}

// Method Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiscreteSpaceFlattenedSizeTest, "Schola.Spaces.DiscreteSpace.FlattenedSize Test ", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDiscreteSpaceFlattenedSizeTest::RunTest(const FString& Parameters)
{
    FDiscreteSpace DiscreteSpace = FDiscreteSpace(3);

    TestEqualExactFloat(TEXT("DiscreteSpace.GetFlattenedSize() == 3"), DiscreteSpace.GetFlattenedSize(), 3);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiscreteSpaceIsEmptyTest, "Schola.Spaces.DiscreteSpace.Is Empty Test ", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDiscreteSpaceIsEmptyTest::RunTest(const FString& Parameters)
{
    FDiscreteSpace DiscreteSpace = FDiscreteSpace(1);

    TestEqual(TEXT("DiscreteSpace.IsEmpty() == false"), DiscreteSpace.IsEmpty(), false);
    
    FDiscreteSpace EmptyDiscreteSpace = FDiscreteSpace();

    TestEqual(TEXT("EmptyDiscreteSpace.IsEmpty() == true"), EmptyDiscreteSpace.IsEmpty(), true);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiscreteSpaceGetNumDimensionsTest, "Schola.Spaces.DiscreteSpace.Get Num Dimensions Test ", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDiscreteSpaceGetNumDimensionsTest::RunTest(const FString& Parameters)
{
    FDiscreteSpace DiscreteSpace = FDiscreteSpace(2);

    TestEqualExactFloat(TEXT("DiscreteSpace.GetNumDimensions() == 1"), DiscreteSpace.GetNumDimensions(), 1);

    return true;
}

// Validation Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiscreteSpaceValidateWrongDataTypeTest, "Schola.Spaces.DiscreteSpace.Validate Wrong Data Type Test ", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDiscreteSpaceValidateWrongDataTypeTest::RunTest(const FString& Parameters)
{
    FDiscreteSpace DiscreteSpace = FDiscreteSpace(2);

    TInstancedStruct<FPoint> Point;
    Point.InitializeAs<FMultiBinaryPoint>();

    TestEqual(TEXT("DiscreteSpace.Validate(Point) == ESpaceValidationResult::WrongDataType"), DiscreteSpace.Validate(Point), ESpaceValidationResult::WrongDataType);

    return true;
}
#endif