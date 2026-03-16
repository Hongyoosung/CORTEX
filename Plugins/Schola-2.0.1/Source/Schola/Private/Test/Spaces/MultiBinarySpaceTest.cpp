// Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Spaces/MultiBinarySpace.h"


#if WITH_AUTOMATION_TESTS

// Constructor Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiBinarySpaceDefaultTest, "Schola.Spaces.MultiBinarySpace.Default Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiBinarySpaceDefaultTest::RunTest(const FString& Parameters)
{
    FMultiBinarySpace MultiBinarySpace = FMultiBinarySpace();

    TestEqual(TEXT("MultiBinarySpace.Shape == 0"), MultiBinarySpace.Shape, 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiBinarySpaceShapeTest, "Schola.Spaces.MultiBinarySpace.Int Constructor Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiBinarySpaceShapeTest::RunTest(const FString& Parameters)
{
    FMultiBinarySpace MultiBinarySpace = FMultiBinarySpace(10);

    TestEqual(TEXT("MultiBinarySpace.Shape == 10"), MultiBinarySpace.Shape, 10);

    return true;
}

// Method Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiBinarySpaceMergeTest, "Schola.Spaces.MultiBinarySpace.Merge Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiBinarySpaceMergeTest::RunTest(const FString& Parameters)
{
    FMultiBinarySpace MultiBinarySpace = FMultiBinarySpace(10);
    FMultiBinarySpace OtherMultiBinarySpace = FMultiBinarySpace(5);

    MultiBinarySpace.Merge(OtherMultiBinarySpace);

    TestEqual(TEXT("MultiBinarySpace.Shape == 15"), MultiBinarySpace.Shape, 15);

    return true;
}
#endif
