// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Conditions/STCondition_CheckObjectiveType.h"

bool FSTCondition_CheckObjectiveType::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// Get objective data from bound properties
	const UObjective* CurrentObjective = InstanceData.CurrentObjective;
	const bool bHasActiveObjective = InstanceData.bHasActiveObjective;

	// v4.0.1: Check if ANY objective type is accepted (at least one bool flag must be true)
	bool bAnyTypeAccepted = InstanceData.bAcceptAssault || InstanceData.bAcceptDefend ||
	                        InstanceData.bAcceptSupport || InstanceData.bAcceptRetreat ||
	                        InstanceData.bAcceptNone;

	// DIAGNOSTIC: Log every evaluation
	static int32 EvalCounter = 0;
	EvalCounter++;

	UE_LOG(LogTemp, Warning, TEXT("🔍 [CHECK OBJECTIVE TYPE] Eval #%d: Objective=%s, Active=%d, Flags=[A:%d D:%d S:%d R:%d N:%d], RequireActive=%d"),
		EvalCounter,
		CurrentObjective ? *UEnum::GetValueAsString(CurrentObjective->Type) : TEXT("NULL"),
		bHasActiveObjective ? 1 : 0,
		InstanceData.bAcceptAssault ? 1 : 0,
		InstanceData.bAcceptDefend ? 1 : 0,
		InstanceData.bAcceptSupport ? 1 : 0,
		InstanceData.bAcceptRetreat ? 1 : 0,
		InstanceData.bAcceptNone ? 1 : 0,
		InstanceData.bRequireActiveObjective ? 1 : 0);

	// Check if no accepted types configured
	if (!bAnyTypeAccepted)
	{
		UE_LOG(LogTemp, Error, TEXT("[CHECK OBJECTIVE TYPE] ❌ No objective types accepted (all flags false)!"));
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

	// v4.0.1: Check if objective type matches any accepted flags
	bool bMatches = false;
	switch (CurrentObjective->Type)
	{
	case EObjectiveType::Assault:
		bMatches = InstanceData.bAcceptAssault;
		break;
	case EObjectiveType::Defend:
		bMatches = InstanceData.bAcceptDefend;
		break;
	case EObjectiveType::Support:
		bMatches = InstanceData.bAcceptSupport;
		break;
	case EObjectiveType::Retreat:
		bMatches = InstanceData.bAcceptRetreat;
		break;
	case EObjectiveType::None:
		bMatches = InstanceData.bAcceptNone;
		break;
	default:
		UE_LOG(LogTemp, Error, TEXT("[CHECK OBJECTIVE TYPE] ❌ Unknown ObjectiveType: %d"), static_cast<int32>(CurrentObjective->Type));
		bMatches = false;
		break;
	}

	bool bResult = InstanceData.bInvertCondition ? !bMatches : bMatches;

	UE_LOG(LogTemp, Warning, TEXT("[CHECK OBJECTIVE TYPE] %s CurrentType=%s, Matches=%d, Result=%d"),
		bResult ? TEXT("✅") : TEXT("❌"),
		*UEnum::GetValueAsString(CurrentObjective->Type),
		bMatches ? 1 : 0,
		bResult ? 1 : 0);

	return bResult;
}
