// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "RL/RLTypes.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "StateTreeExecutionTypes.h"
#include "STTask_ExecuteAiming.generated.h"

class UFollowerStateTreeComponent;
class APawn;
class AAIController;

/**
 * State Tree Task: Execute Aiming (v4.0 Macro Actions)
 *
 * Handles targeting using engine auto-aim (SetFocus).
 * Extracted from STTask_ExecuteObjective for better modularity.
 *
 * Execution:
 * - Uses SetFocus for engine-driven aiming
 * - Manually sets controller rotation for immediate response
 * - Faces objective when no enemies visible
 */

USTRUCT()
struct GAMEAI_PROJECT_API FSTTask_ExecuteAimingInstanceData
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

	/** Rotation speed (degrees/sec) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "90.0", ClampMax = "720.0"))
	float RotationSpeed = 360.0f;

	/** Previous macro action (to detect changes) */
	FMacroAction PreviousMacroAction;
};

USTRUCT(meta = (DisplayName = "Execute Aiming"))
struct GAMEAI_PROJECT_API FSTTask_ExecuteAiming : public FStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ExecuteAimingInstanceData;

	FSTTask_ExecuteAiming() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

protected:
	/** v6.0: Execute aiming using SetFocus (simplified - no action parameter needed) */
	void ExecuteAiming(FStateTreeExecutionContext& Context, float DeltaTime) const;
};
