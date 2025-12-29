// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "Team/Objective.h"
#include "STCondition_CheckObjectiveType.generated.h"

/**
 * State Tree Condition: Check Objective Type (v4.0.1)
 *
 * Checks if the current objective matches the accepted type(s).
 * Used to control state transitions based on leader-assigned objectives.
 *
 * v4.0.1: Uses bool flags instead of TArray<EObjectiveType> for reliable StateTree serialization.
 * StateTree has a known bug where TArray instance data gets corrupted during context updates.
 *
 * Example:
 * - Transition to "Combat" state when bAcceptAssault=true OR bAcceptDefend=true
 * - Transition to "Retreat" state when bAcceptRetreat=true ONLY
 *
 * Replaces: STCondition_CheckCommandType (v2.0)
 */

USTRUCT()
struct GAMEAI_PROJECT_API FSTCondition_CheckObjectiveTypeInstanceData
{
	GENERATED_BODY()

	//--------------------------------------------------------------------------
	// INPUT BINDINGS (bind these in StateTree editor)
	//--------------------------------------------------------------------------

	/**
	 * Current objective - bind to FollowerContext.CurrentObjective
	 */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UObjective> CurrentObjective = nullptr;

	/**
	 * Is objective valid - bind to FollowerContext.bHasActiveObjective
	 */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bHasActiveObjective = false;

	//--------------------------------------------------------------------------
	// CONFIGURATION (v4.0.1: Bool flags instead of TArray for StateTree reliability)
	//--------------------------------------------------------------------------

	/** Accept Assault objectives (offensive: push toward enemy/objective) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bAcceptAssault = true;

	/** Accept Defend objectives (defensive: hold position/objective) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bAcceptDefend = true;

	/** Accept Support objectives (auxiliary: provide cover/assistance) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bAcceptSupport = true;

	/** Accept Retreat objectives (fallback: disengage and reposition) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bAcceptRetreat = true;

	/** Accept None (no objective assigned) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bAcceptNone = false;

	/** If true, condition is inverted (true when objective does NOT match) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvertCondition = false;

	/** If true, also checks that objective is active */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bRequireActiveObjective = true;
};

USTRUCT(meta = (DisplayName = "Check Objective Type"))
struct GAMEAI_PROJECT_API FSTCondition_CheckObjectiveType : public FStateTreeConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_CheckObjectiveTypeInstanceData;

	FSTCondition_CheckObjectiveType() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
