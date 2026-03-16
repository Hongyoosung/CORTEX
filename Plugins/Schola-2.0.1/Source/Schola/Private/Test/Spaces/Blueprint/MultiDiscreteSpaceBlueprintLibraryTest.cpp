// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Spaces/Blueprint/MultiDiscreteSpaceBlueprintLibrary.h"
#include "Spaces/MultiDiscreteSpace.h"

#if WITH_DEV_AUTOMATION_TESTS

// ArrayToMultiDiscreteSpace Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiDiscreteSpaceBlueprintLibrary_ArrayToMultiDiscreteSpace_BasicTest, "Schola.Spaces.Blueprint.MultiDiscreteSpaceBlueprintLibrary.ArrayToMultiDiscreteSpace.Basic", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiDiscreteSpaceBlueprintLibrary_ArrayToMultiDiscreteSpace_BasicTest::RunTest(const FString& Parameters)
{
    TArray<int32> High = {2, 3, 4};

    TInstancedStruct<FMultiDiscreteSpace> Result = UMultiDiscreteSpaceBlueprintLibrary::ArrayToMultiDiscreteSpace(High);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FMultiDiscreteSpace& MultiDiscreteSpace = Result.Get<FMultiDiscreteSpace>();
	TestEqual(TEXT("MultiDiscreteSpace.High.Num() == 3"), MultiDiscreteSpace.High.Num(), 3);
	TestEqual(TEXT("MultiDiscreteSpace.High[0] == 2"), MultiDiscreteSpace.High[0], 2);
	TestEqual(TEXT("MultiDiscreteSpace.High[1] == 3"), MultiDiscreteSpace.High[1], 3);
	TestEqual(TEXT("MultiDiscreteSpace.High[2] == 4"), MultiDiscreteSpace.High[2], 4);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiDiscreteSpaceBlueprintLibrary_ArrayToMultiDiscreteSpace_EmptyTest, "Schola.Spaces.Blueprint.MultiDiscreteSpaceBlueprintLibrary.ArrayToMultiDiscreteSpace.Empty", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiDiscreteSpaceBlueprintLibrary_ArrayToMultiDiscreteSpace_EmptyTest::RunTest(const FString& Parameters)
{
    TArray<int32> High;

    TInstancedStruct<FMultiDiscreteSpace> Result = UMultiDiscreteSpaceBlueprintLibrary::ArrayToMultiDiscreteSpace(High);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FMultiDiscreteSpace& MultiDiscreteSpace = Result.Get<FMultiDiscreteSpace>();
    TestEqual(TEXT("MultiDiscreteSpace.High.Num() == 0"), MultiDiscreteSpace.High.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiDiscreteSpaceBlueprintLibrary_ArrayToMultiDiscreteSpace_SingleTest, "Schola.Spaces.Blueprint.MultiDiscreteSpaceBlueprintLibrary.ArrayToMultiDiscreteSpace.Single", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiDiscreteSpaceBlueprintLibrary_ArrayToMultiDiscreteSpace_SingleTest::RunTest(const FString& Parameters)
{
    TArray<int32> High = {10};

    TInstancedStruct<FMultiDiscreteSpace> Result = UMultiDiscreteSpaceBlueprintLibrary::ArrayToMultiDiscreteSpace(High);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FMultiDiscreteSpace& MultiDiscreteSpace = Result.Get<FMultiDiscreteSpace>();
    TestEqual(TEXT("MultiDiscreteSpace.High.Num() == 1"), MultiDiscreteSpace.High.Num(), 1);
    TestEqual(TEXT("MultiDiscreteSpace.High[0] == 10"), MultiDiscreteSpace.High[0], 10);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiDiscreteSpaceBlueprintLibrary_ArrayToMultiDiscreteSpace_LargeTest, "Schola.Spaces.Blueprint.MultiDiscreteSpaceBlueprintLibrary.ArrayToMultiDiscreteSpace.Large", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiDiscreteSpaceBlueprintLibrary_ArrayToMultiDiscreteSpace_LargeTest::RunTest(const FString& Parameters)
{
    TArray<int32> High = {5, 10, 15, 20, 25, 30, 35, 40};

    TInstancedStruct<FMultiDiscreteSpace> Result = UMultiDiscreteSpaceBlueprintLibrary::ArrayToMultiDiscreteSpace(High);

    TestTrue(TEXT("Result is valid"), Result.IsValid());
    
    const FMultiDiscreteSpace& MultiDiscreteSpace = Result.Get<FMultiDiscreteSpace>();
	TestEqual(TEXT("MultiDiscreteSpace.High.Num() == 8"), MultiDiscreteSpace.High.Num(), 8);
    TestEqual(TEXT("MultiDiscreteSpace.High[7] == 40"), MultiDiscreteSpace.High[7], 40);

    return true;
}

// MultiDiscreteSpaceToArray Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiDiscreteSpaceBlueprintLibrary_MultiDiscreteSpaceToArray_BasicTest, "Schola.Spaces.Blueprint.MultiDiscreteSpaceBlueprintLibrary.MultiDiscreteSpaceToArray.Basic", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiDiscreteSpaceBlueprintLibrary_MultiDiscreteSpaceToArray_BasicTest::RunTest(const FString& Parameters)
{
    TInstancedStruct<FMultiDiscreteSpace> Space;
    Space.InitializeAs<FMultiDiscreteSpace>();
	Space.GetMutable<FMultiDiscreteSpace>().High = { 5, 10, 15 };

    TArray<int32> Result = UMultiDiscreteSpaceBlueprintLibrary::MultiDiscreteSpaceToArray(Space);

    TestEqual(TEXT("Result.Num() == 3"), Result.Num(), 3);
    TestEqual(TEXT("Result[0] == 5"), Result[0], 5);
    TestEqual(TEXT("Result[1] == 10"), Result[1], 10);
    TestEqual(TEXT("Result[2] == 15"), Result[2], 15);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiDiscreteSpaceBlueprintLibrary_MultiDiscreteSpaceToArray_RoundTripTest, "Schola.Spaces.Blueprint.MultiDiscreteSpaceBlueprintLibrary.MultiDiscreteSpaceToArray.RoundTrip", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMultiDiscreteSpaceBlueprintLibrary_MultiDiscreteSpaceToArray_RoundTripTest::RunTest(const FString& Parameters)
{
    TArray<int32> OriginalArray = {3, 6, 9, 12};

    TInstancedStruct<FMultiDiscreteSpace> Space = UMultiDiscreteSpaceBlueprintLibrary::ArrayToMultiDiscreteSpace(OriginalArray);
    TArray<int32> Result = UMultiDiscreteSpaceBlueprintLibrary::MultiDiscreteSpaceToArray(Space);

    TestEqual(TEXT("Round trip array length"), Result.Num(), OriginalArray.Num());
    for (int32 i = 0; i < OriginalArray.Num(); i++)
    {
        TestEqual(FString::Printf(TEXT("Round trip array[%d]"), i), Result[i], OriginalArray[i]);
    }

    return true;
}

#endif




