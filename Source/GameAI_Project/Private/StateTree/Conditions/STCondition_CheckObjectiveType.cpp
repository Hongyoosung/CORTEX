// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Conditions/STCondition_CheckObjectiveType.h"

bool FSTCondition_CheckObjectiveType::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// Get objective data from bound properties
	const UObjective* CurrentObjective = InstanceData.CurrentObjective;
	const bool bHasActiveObjective = InstanceData.bHasActiveObjective;

	// DIAGNOSTIC: Log every evaluation
	static int32 EvalCounter = 0;
	EvalCounter++;

	UE_LOG(LogTemp, Warning, TEXT("🔍 [CHECK OBJECTIVE TYPE] Eval #%d: Objective=%s, Active=%d, AcceptedTypes=%d, RequireActive=%d"),
		EvalCounter,
		CurrentObjective ? *UEnum::GetValueAsString(CurrentObjective->Type) : TEXT("NULL"),
		bHasActiveObjective ? 1 : 0,
		InstanceData.AcceptedObjectiveTypes.Num(),
		InstanceData.bRequireActiveObjective ? 1 : 0);

	// Check if no accepted types configured
	if (InstanceData.AcceptedObjectiveTypes.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHECK OBJECTIVE TYPE] ❌ No AcceptedObjectiveTypes configured!"));
		return InstanceData.bInvertCondition;
	}

	// Check validity if required
	if (InstanceData.bRequireActiveObjective && !bHasActiveObjective)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CHECK OBJECTIVE TYPE] ❌ RequireActive=true but bHasActiveObjective=false → FAIL"));
		return InstanceData.bInvertCondition; // No active objective = false (or true if inverted)
	}

	// Check if objective is null
	if (!CurrentObjective)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CHECK OBJECTIVE TYPE] ❌ CurrentObjective is NULL → FAIL"));
		return InstanceData.bInvertCondition; // Null objective = false (or true if inverted)
	}

	// Check if objective type matches any accepted types
	// Filter out invalid/MAX values (legacy data migration issue from v4.0 enum simplification)
	bool bMatches = false;
	int32 ValidTypeCount = 0;
	for (EObjectiveType AcceptedType : InstanceData.AcceptedObjectiveTypes)
	{
		// Skip EObjectiveType_MAX (invalid/uninitialized enum values)
		if (static_cast<uint8>(AcceptedType) >= static_cast<uint8>(EObjectiveType::Retreat) + 1)
		{
			continue; // Invalid enum value, skip
		}
		ValidTypeCount++;
		if (AcceptedType == CurrentObjective->Type)
		{
			bMatches = true;
		}
	}

	// Calculate how many types are invalid (legacy data migration issue)
	int32 InvalidCount = InstanceData.AcceptedObjectiveTypes.Num() - ValidTypeCount;

	// If no valid types configured (all were MAX), accept any valid objective type as fallback
	if (ValidTypeCount == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHECK OBJECTIVE TYPE] ⚠️ All AcceptedObjectiveTypes are invalid (MAX)! Accepting any valid objective. Re-save StateTree asset to fix."));
		// Accept any valid objective type (None through Retreat)
		bool bValidObjectiveType = static_cast<uint8>(CurrentObjective->Type) <= static_cast<uint8>(EObjectiveType::Retreat);
		return InstanceData.bInvertCondition ? !bValidObjectiveType : bValidObjectiveType;
	}

	// If majority of configured types are invalid (>50%), assume legacy config - accept all valid types
	if (InvalidCount > ValidTypeCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CHECK OBJECTIVE TYPE] ⚠️ Majority of AcceptedObjectiveTypes are invalid (%d MAX vs %d valid). Accepting any valid objective. Re-save StateTree asset."),
			InvalidCount, ValidTypeCount);
		bool bValidObjectiveType = static_cast<uint8>(CurrentObjective->Type) <= static_cast<uint8>(EObjectiveType::Retreat);
		return InstanceData.bInvertCondition ? !bValidObjectiveType : bValidObjectiveType;
	}

	bool bResult = InstanceData.bInvertCondition ? !bMatches : bMatches;

	// Log accepted types for debugging (only valid types)
	FString ValidTypesStr;
	for (EObjectiveType Type : InstanceData.AcceptedObjectiveTypes)
	{
		// Only log valid types
		if (static_cast<uint8>(Type) <= static_cast<uint8>(EObjectiveType::Retreat))
		{
			ValidTypesStr += UEnum::GetValueAsString(Type) + TEXT(", ");
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[CHECK OBJECTIVE TYPE] %s CurrentType=%s, ValidTypes=[%s] (%d valid, %d invalid/MAX), Matches=%d, Result=%d"),
		bResult ? TEXT("✅") : TEXT("❌"),
		*UEnum::GetValueAsString(CurrentObjective->Type),
		*ValidTypesStr,
		ValidTypeCount,
		InvalidCount,
		bMatches ? 1 : 0,
		bResult ? 1 : 0);

	return bResult;
}
