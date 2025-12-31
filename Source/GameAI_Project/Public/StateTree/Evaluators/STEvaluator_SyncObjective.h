// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "Team/Objective.h"
#include "STEvaluator_SyncObjective.generated.h"

/**
 * State Tree Evaluator: Sync Objective
 *
 * Syncs assigned objective from FollowerAgentComponent to State Tree context.
 * Runs every tick to detect objective changes from team leader.
 *
 * When objective changes, this can trigger state transitions via conditions.
 *
 * Replaces: STEvaluator_SyncCommand (v2.0)
 */


USTRUCT()
struct GAMEAI_PROJECT_API FSTEvaluator_SyncObjectiveInstanceData
{
	GENERATED_BODY()

	//--------------------------------------------------------------------------
	// INPUT BINDING - StateTreeComp provides access to shared context
	//--------------------------------------------------------------------------

	/**
	 * StateTree component reference - provides access to shared context
	 * Bind this to "FollowerStateTreeComponent" in the StateTree editor
	 */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<class UFollowerStateTreeComponent> StateTreeComp;

	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** Log objective changes */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bLogObjectiveChanges = true;

	//--------------------------------------------------------------------------
	// RUNTIME STATE
	//--------------------------------------------------------------------------

	/** Last objective type (for change detection) */
	UPROPERTY()
	EObjectiveType LastObjectiveType = EObjectiveType::None;

	/** Last objective pointer (for change detection) */
	UPROPERTY()
	TObjectPtr<UObjective> LastObjective = nullptr;
};

USTRUCT(meta = (DisplayName = "Sync Objective", BlueprintType))
struct GAMEAI_PROJECT_API FSTEvaluator_SyncObjective : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTEvaluator_SyncObjectiveInstanceData;

	FSTEvaluator_SyncObjective() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void TreeStart(FStateTreeExecutionContext& Context) const override;
	virtual void Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void TreeStop(FStateTreeExecutionContext& Context) const override;
};
