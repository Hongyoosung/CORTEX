// Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Spaces/BoxSpaceDimension.h"
#if WITH_AUTOMATION_TESTS
#define TestEqualExactFloat(TestMessage, Actual, Expected) TestEqual(TestMessage, (float)Actual, (float)Expected, 0.0001f)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceDimensionDefaultTest, "Schola.Spaces.BoxSpace.BoxSpaceDimension.Default Creation Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceDimensionDefaultTest::RunTest(const FString& Parameters)
{
    FBoxSpaceDimension BoxSpaceDimension = FBoxSpaceDimension();

    TestEqualExactFloat(TEXT("BoxSpaceDimension.Low == -1.0"), BoxSpaceDimension.Low, -1.0f);
	TestEqualExactFloat(TEXT("BoxSpaceDimension.High == 1.0"), BoxSpaceDimension.High, 1.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceDimensionBoundsTest, "Schola.Spaces.BoxSpace.BoxSpaceDimension.Bounds Creation Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceDimensionBoundsTest::RunTest(const FString& Parameters)
{
    FBoxSpaceDimension BoxSpaceDimension = FBoxSpaceDimension(-2.0, 3.0);

    TestEqualExactFloat(TEXT("BoxSpaceDimension.Low == -2.0"), BoxSpaceDimension.Low, -2.0f);
	TestEqualExactFloat(TEXT("BoxSpaceDimension.High == 3.0"), BoxSpaceDimension.High, 3.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceDimensionZeroOneUnitTest, "Schola.Spaces.BoxSpace.BoxSpaceDimension.Zero One Unit Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceDimensionZeroOneUnitTest::RunTest(const FString& Parameters)
{
    FBoxSpaceDimension BoxSpaceDimension = FBoxSpaceDimension::ZeroOneUnitDimension();

    TestEqualExactFloat(TEXT("BoxSpaceDimension.Low == 0.0"), BoxSpaceDimension.Low, 0.0);
    TestEqualExactFloat(TEXT("BoxSpaceDimension.High == 1.0"), BoxSpaceDimension.High, 1.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceDimensionCenteredUnitTest, "Schola.Spaces.BoxSpace.BoxSpaceDimension.Centered Unit Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceDimensionCenteredUnitTest::RunTest(const FString& Parameters)
{
    FBoxSpaceDimension BoxSpaceDimension = FBoxSpaceDimension::CenteredUnitDimension();

    TestEqualExactFloat(TEXT("BoxSpaceDimension.Low == -0.5"), BoxSpaceDimension.Low, -0.5);
    TestEqualExactFloat(TEXT("BoxSpaceDimension.High == 0.5"), BoxSpaceDimension.High, 0.5);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceDimensionDenormalizeTest, "Schola.Spaces.BoxSpace.BoxSpaceDimension.Denormalize Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceDimensionDenormalizeTest::RunTest(const FString& Parameters)
{
    FBoxSpaceDimension BoxSpaceDimension = FBoxSpaceDimension(-3.0, 3.0);

    TestEqualExactFloat(TEXT("BoxSpaceDimension.RescaleValue(0.0) == -3.0"), BoxSpaceDimension.RescaleValue(0.0), -3.0);
    TestEqualExactFloat(TEXT("BoxSpaceDimension.RescaleValue(0.5) == 0.0"), BoxSpaceDimension.RescaleValue(0.5), 0.0);
    TestEqualExactFloat(TEXT("BoxSpaceDimension.RescaleValue(1) == 3.0"), BoxSpaceDimension.RescaleValue(1), 3.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceDimensionRescaleTest, "Schola.Spaces.BoxSpace.BoxSpaceDimension.Rescale Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceDimensionRescaleTest::RunTest(const FString& Parameters)
{
    FBoxSpaceDimension BoxSpaceDimension = FBoxSpaceDimension(0, 10.0);

    TestEqualExactFloat(TEXT("BoxSpaceDimension.RescaleValue(0.0, 10.0, 0.0) == 0.0"), BoxSpaceDimension.RescaleValue(0.0, 10.0, 0.0), 0.0);
    TestEqualExactFloat(TEXT("BoxSpaceDimension.RescaleValue(5.0, 10.0, 0.0) == 5.0"), BoxSpaceDimension.RescaleValue(5.0, 10.0, 0.0), 5.0);

    TestEqualExactFloat(TEXT("BoxSpaceDimension.RescaleValue(7.5, 10.0, 5.0) == 5.0"), BoxSpaceDimension.RescaleValue(7.5, 10.0, 5.0), 5.0);
    TestEqualExactFloat(TEXT("BoxSpaceDimension.RescaleValue(3.0, 5.0, 0.0) == 6.0"), BoxSpaceDimension.RescaleValue(3.0, 5.0, 0.0), 6.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxSpaceDimensionNormalizeTest, "Schola.Spaces.BoxSpace.BoxSpaceDimension.Normalize Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxSpaceDimensionNormalizeTest::RunTest(const FString& Parameters)
{
    FBoxSpaceDimension BoxSpaceDimension = FBoxSpaceDimension(-3.0, 3.0);

    TestEqualExactFloat(TEXT("BoxSpaceDimension.NormalizeValue(-3.0) == 0.0"), BoxSpaceDimension.NormalizeValue(-3.0), 0.0);
    TestEqualExactFloat(TEXT("BoxSpaceDimension.NormalizeValue(0.0) == 0.5"), BoxSpaceDimension.NormalizeValue(0.0), 0.5);
    TestEqualExactFloat(TEXT("BoxSpaceDimension.NormalizeValue(3.0) == 1.0"), BoxSpaceDimension.NormalizeValue(3.0), 1.0);

    return true;
}
#endif