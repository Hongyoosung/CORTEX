// Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Spaces/DictSpace.h"
#include "Spaces/BoxSpace.h"
#include "Points/BoxPoint.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDictSpaceDefaultTest, "Schola.Spaces.DictSpace.Default Creation Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDictSpaceDefaultTest::RunTest(const FString& Parameters)
{
	FDictSpace DictSpace = FDictSpace();

	TestEqual(TEXT("DictSpace.Spaces.Num() == 0"), DictSpace.Spaces.Num(), 0);
	TestEqual(TEXT("DictSpace.Num() == 0"), DictSpace.Num(), 0);
	TestEqual(TEXT("DictSpace.GetFlattenedSize() == 0"), DictSpace.GetFlattenedSize(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDictSpaceNumAndGetFlattenedSizeTest, "Schola.Spaces.DictSpace.Num and GetFlattenedSize Test", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDictSpaceNumAndGetFlattenedSizeTest::RunTest(const FString& Parameters)
{
	FDictSpace DictSpace = FDictSpace();

	FBoxSpace BoxSpace1;
	BoxSpace1.Add(-1.0, 1.0);

	FBoxSpace BoxSpace2;
	BoxSpace2.Add(-2.0, 2.0);
	BoxSpace2.Add(-3.0, 3.0);
	

	DictSpace.Spaces.Add("box1", TInstancedStruct<FSpace>::Make<FBoxSpace>(BoxSpace1));
	DictSpace.Spaces.Add("box2", TInstancedStruct<FSpace>::Make<FBoxSpace>(BoxSpace2));

	TestEqual(TEXT("DictSpace.Num() == 2"), DictSpace.Num(), 2);
	TestEqual(TEXT("DictSpace.GetFlattenedSize() == 3"), DictSpace.GetFlattenedSize(), 3);

	return true;
}
#endif