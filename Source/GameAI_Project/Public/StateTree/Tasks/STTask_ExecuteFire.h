// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "RL/RLTypes.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "StateTreeExecutionTypes.h"
#include "STTask_ExecuteFire.generated.h"

class UFollowerStateTreeComponent;
class APawn;
class AAIController;

/**
 * State Tree Task: Execute Fire (v5.0 Macro Actions)
 *
 * Handles weapon firing based on target selection.
 * Extracted from STTask_ExecuteObjective for better modularity.
 *
 * Execution (v5.0):
 * - Fire control via TargetIndex: -1 = hold fire, >= 0 = engage if LOS clear
 * - Always checks line-of-sight before firing (prevents shooting through walls)
 * - Integrates with WeaponComponent for actual firing
 */

USTRUCT()
struct GAMEAI_PROJECT_API FSTTask_ExecuteFireInstanceData
{
	GENERATED_BODY()

	/** StateTree component reference */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UFollowerStateTreeComponent> StateTreeComp;

	/** Controlled Pawn reference */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<APawn> ControlledPawn;

	/** AIController reference */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AAIController> AIController;

	/** Action application rate (seconds) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.01", ClampMax = "0.2"))
	float ActionApplicationInterval = 0.05f;

	/** Time since last fire action */
	float TimeSinceLastAction = 0.0f;
};

USTRUCT(meta = (DisplayName = "Execute Fire"))
struct GAMEAI_PROJECT_API FSTTask_ExecuteFire : public FStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ExecuteFireInstanceData;

	FSTTask_ExecuteFire() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

protected:
	/** Execute fire mode */
	void ExecuteFire(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const;
};
