// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Conditions/STCondition_HasMission.h"
#include "Team/Mission.h"

bool FSTCondition_HasMission::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// Check if Mission exists and is active
	bool bHasMission = (InstanceData.CurrentMission != nullptr) && InstanceData.bHasActiveMission;

	// Apply inversion if requested
	bool bResult = InstanceData.bInvertCondition ? !bHasMission : bHasMission;

	// DIAGNOSTIC: Log every evaluation (throttled to avoid spam)
	static int32 EvalCounter = 0;
	EvalCounter++;

	if (EvalCounter % 60 == 0) // Log every 60 evaluations (~1 second at 60fps)
	{
		UE_LOG(LogTemp, Display, TEXT("🔍 [HAS Mission] Eval #%d: Mission=%s, Active=%d, Inverted=%d, Result=%d"),
			EvalCounter,
			InstanceData.CurrentMission ? TEXT("Valid") : TEXT("NULL"),
			InstanceData.bHasActiveMission ? 1 : 0,
			InstanceData.bInvertCondition ? 1 : 0,
			bResult ? 1 : 0);
	}

	return bResult;
}
