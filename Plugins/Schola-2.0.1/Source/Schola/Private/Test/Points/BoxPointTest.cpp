// Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Points/BoxPoint.h"

#if WITH_AUTOMATION_TESTS

#define TestEqualExactFloat(TestMessage, Actual, Expected) TestEqual(TestMessage, (float)Actual, (float)Expected, 0.0001f)

// Constructor Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointDefaultConstructorTest, "Schola.Points.BoxPoint.Constructor.Default", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointDefaultConstructorTest::RunTest(const FString& Parameters)
{
    FBoxPoint BoxPoint = FBoxPoint();

    TestEqual(TEXT("BoxPoint.Values.Num() == 0"), BoxPoint.Values.Num(), 0);
    TestEqual(TEXT("BoxPoint.Shape.Num() == 0"), BoxPoint.Shape.Num(), 0);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointTArrayConstructorTest, "Schola.Points.BoxPoint.TArray Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointTArrayConstructorTest::RunTest(const FString& Parameters)
{
    TArray<float> Values = { 1.0f, 2.0f, 3.0f, 4.0f };
    FBoxPoint BoxPoint = FBoxPoint(Values);

    TestEqual(TEXT("BoxPoint.Values.Num() == 4"), BoxPoint.Values.Num(), 4);
    TestEqualExactFloat(TEXT("BoxPoint[0] == 1.0f"), BoxPoint[0], 1.0f);
    TestEqualExactFloat(TEXT("BoxPoint[1] == 2.0f"), BoxPoint[1], 2.0f);
    TestEqualExactFloat(TEXT("BoxPoint[2] == 3.0f"), BoxPoint[2], 3.0f);
    TestEqualExactFloat(TEXT("BoxPoint[3] == 4.0f"), BoxPoint[3], 4.0f);
    TestEqual(TEXT("BoxPoint.Shape.Num() == 0"), BoxPoint.Shape.Num(), 0);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointInitializerListConstructorTest, "Schola.Points.BoxPoint.InitializerList Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointInitializerListConstructorTest::RunTest(const FString& Parameters)
{
    FBoxPoint BoxPoint = FBoxPoint({ 5.0f, 10.0f, 15.0f });

    TestEqual(TEXT("BoxPoint.Values.Num() == 3"), BoxPoint.Values.Num(), 3);
    TestEqualExactFloat(TEXT("BoxPoint[0] == 5.0f"), BoxPoint[0], 5.0f);
    TestEqualExactFloat(TEXT("BoxPoint[1] == 10.0f"), BoxPoint[1], 10.0f);
    TestEqualExactFloat(TEXT("BoxPoint[2] == 15.0f"), BoxPoint[2], 15.0f);
    TestEqual(TEXT("BoxPoint.Shape.Num() == 0"), BoxPoint.Shape.Num(), 0);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointTArrayWithShapeConstructorTest, "Schola.Points.BoxPoint.TArrayWithShape Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointTArrayWithShapeConstructorTest::RunTest(const FString& Parameters)
{
    TArray<float> Values = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
    TArray<int> Shape = { 2, 3 };
    FBoxPoint BoxPoint = FBoxPoint(Values, Shape);

    TestEqual(TEXT("BoxPoint.Values.Num() == 6"), BoxPoint.Values.Num(), 6);
    TestEqual(TEXT("BoxPoint.Shape.Num() == 2"), BoxPoint.Shape.Num(), 2);
    TestEqual(TEXT("BoxPoint.Shape[0] == 2"), BoxPoint.Shape[0], 2);
    TestEqual(TEXT("BoxPoint.Shape[1] == 3"), BoxPoint.Shape[1], 3);
    TestEqualExactFloat(TEXT("BoxPoint[0] == 1.0f"), BoxPoint[0], 1.0f);
    TestEqualExactFloat(TEXT("BoxPoint[5] == 6.0f"), BoxPoint[5], 6.0f);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointInvalidShapeTest, "Schola.Points.BoxPoint.Shape Test ", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointInvalidShapeTest::RunTest(const FString& Parameters)
{
    // TODO raise an error here potentially
    TArray<float> Values = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    TArray<int> Shape = { 2, 3 };
    FBoxPoint BoxPoint = FBoxPoint(Values, Shape);
    
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointFromArrayTest, "Schola.Points.BoxPoint.Constructor.RawPointer", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointFromArrayTest::RunTest(const FString& Parameters)
{
    TArray<float> Values = { 1.0f, 2.0f, 3.0f };
	FBoxPoint BoxPoint = FBoxPoint(Values.GetData(),3);

    TestEqual(TEXT("BoxPoint.Values.Num() == 3"), BoxPoint.Values.Num(), 3);
    TestEqual(TEXT("BoxPoint.Shape.Num() == 1"), BoxPoint.Shape.Num(), 1);
    TestEqual(TEXT("BoxPoint.Shape[0] == 3"), BoxPoint.Shape[0], 3);
    TestEqualExactFloat(TEXT("BoxPoint[0] == 1.0f"), BoxPoint[0], 1.0f);
    TestEqualExactFloat(TEXT("BoxPoint[1] == 2.0f"), BoxPoint[1], 2.0f);
    TestEqualExactFloat(TEXT("BoxPoint[2] == 3.0f"), BoxPoint[2], 3.0f);
    
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointPreallocationConstructorTest, "Schola.Points.BoxPoint.Constructor.Preallocation", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointPreallocationConstructorTest::RunTest(const FString& Parameters)
{
    FBoxPoint BoxPoint = FBoxPoint(5);

    TestEqual(TEXT("BoxPoint.Values has 5 slack space"), BoxPoint.Values.GetSlack(), 5);
    
	return true;
}

// Method Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointAddTest, "Schola.Points.BoxPoint.Add Test ", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointAddTest::RunTest(const FString& Parameters)
{
    FBoxPoint BoxPoint = FBoxPoint();
    BoxPoint.Add(1.0f);
    BoxPoint.Add(2.0f);

    TestEqualExactFloat(TEXT("BoxPoint[0] == 1.0f"), BoxPoint[0], 1.0f);
    TestEqualExactFloat(TEXT("BoxPoint[1] == 2.0f"), BoxPoint[1], 2.0f);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxPointResetTest, "Schola.Points.BoxPoint.Reset Test ", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBoxPointResetTest::RunTest(const FString& Parameters)
{
    FBoxPoint BoxPoint = FBoxPoint();
    BoxPoint.Add(1.0f);
    BoxPoint.Add(2.0f);
    BoxPoint.Reset();

    TestEqual(TEXT("BoxPoint.Values.Num() == 0"), BoxPoint.Values.Num(), 0);

    return true;
}


#endif