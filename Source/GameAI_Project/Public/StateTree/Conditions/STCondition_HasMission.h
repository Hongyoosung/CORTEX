// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "STCondition_HasMission.generated.h"

class UMission;

/**
 * State Tree Condition: Has Mission
 *
 * Simple condition that checks if the follower has an active Mission.
 * Used to control transitions between ExecuteMission and Idle states.
 *
 * Returns true when: CurrentMission != null && bHasActiveMission == true
 */

USTRUCT()
struct GAMEAI_PROJECT_API FSTCondition_HasMissionInstanceData
{
	GENERATED_BODY()

	/** Current Mission (bind to FollowerContext.CurrentMission) */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UMission> CurrentMission = nullptr;

	/** Has active Mission (bind to FollowerContext.bHasActiveMission) */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bHasActiveMission = false;

	/** If true, inverts the condition (true when NO Mission) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvertCondition = false;
};

USTRUCT(meta = (DisplayName = "Has Mission"))
struct GAMEAI_PROJECT_API FSTCondition_HasMission : public FStateTreeConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_HasMissionInstanceData;

	FSTCondition_HasMission() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
