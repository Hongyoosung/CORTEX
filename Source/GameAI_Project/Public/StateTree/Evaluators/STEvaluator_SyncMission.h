// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "Team/Mission.h"
#include "STEvaluator_SyncMission.generated.h"

/**
 * State Tree Evaluator: Sync Mission
 *
 * Syncs assigned Mission from FollowerAgentComponent to State Tree context.
 * Runs every tick to detect Mission changes from team leader.
 *
 * When Mission changes, this can trigger state transitions via conditions.
 *
 * Replaces: STEvaluator_SyncCommand (v2.0)
 */


USTRUCT()
struct GAMEAI_PROJECT_API FSTEvaluator_SyncMissionInstanceData
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

	/** Log Mission changes */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bLogMissionChanges = true;

	//--------------------------------------------------------------------------
	// RUNTIME STATE
	//--------------------------------------------------------------------------

	/** Last Mission type (for change detection) */
	UPROPERTY()
	EMissionType LastMissionType = EMissionType::None;

	/** Last Mission pointer (for change detection) */
	UPROPERTY()
	TObjectPtr<UMission> LastMission = nullptr;
};

USTRUCT(meta = (DisplayName = "Sync Mission", BlueprintType))
struct GAMEAI_PROJECT_API FSTEvaluator_SyncMission : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTEvaluator_SyncMissionInstanceData;

	FSTEvaluator_SyncMission() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void TreeStart(FStateTreeExecutionContext& Context) const override;
	virtual void Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void TreeStop(FStateTreeExecutionContext& Context) const override;
};
