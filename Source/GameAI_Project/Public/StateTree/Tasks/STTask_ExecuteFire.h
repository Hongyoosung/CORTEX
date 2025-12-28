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
 * State Tree Task: Execute Fire (v4.0 Macro Actions)
 *
 * Handles weapon firing based on fire mode.
 * Extracted from STTask_ExecuteObjective for better modularity.
 *
 * Execution:
 * - Supports HoldFire, Fire, Suppress modes
 * - Line-of-sight checks before firing
 * - Integrates with WeaponComponent
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
