// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "RL/RLTypes.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "StateTreeExecutionTypes.h"
#include "STTask_ExecuteObjective.generated.h"

class UFollowerStateTreeComponent;
class APawn;
class AAIController;

/**
 * State Tree Task: Execute Objective
 *
 * Universal execution task that handles ALL objective types using atomic actions.
 * Replaces ExecuteAssault, ExecuteDefend, ExecuteSupport, ExecuteMove, ExecuteRetreat.
 *
 * Execution Flow:
 * 1. Query RL policy for atomic action (8D: movement, aiming, fire, crouch, ability)
 * 2. Apply spatial constraints from ActionSpaceMask
 * 3. Execute movement, aiming, and discrete actions
 * 4. Calculate rewards based on objective progress
 *
 * Requirements:
 * - CurrentObjective must be assigned and active
 * - ActionMask updated by STEvaluator_SpatialContext
 * - TacticalPolicy network loaded
 */

USTRUCT()
struct GAMEAI_PROJECT_API FSTTask_ExecuteObjectiveInstanceData
{
	GENERATED_BODY()

	/** StateTree component reference - provides access to shared context */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UFollowerStateTreeComponent> StateTreeComp;

	/** Controlled Pawn reference (bind to FollowerContext.ControlledPawn) */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<APawn> ControlledPawn;

	/** AIController reference (bind to FollowerContext.AIController) */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AAIController> AIController;

	/** Movement speed multiplier */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float MovementSpeedMultiplier = 1.0f;

	/** Rotation speed (degrees/sec) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "90.0", ClampMax = "720.0"))
	float RotationSpeed = 360.0f;

	/** Fire if aim within this error (degrees) */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "1.0", ClampMax = "15.0"))
	float AimTolerance = 5.0f;

	/** Action application rate (seconds) - decouples from FPS. Default 0.05s = 20 Hz to match Schola's action frequency */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.01", ClampMax = "0.2"))
	float ActionApplicationInterval = 0.05f;

	/** Time since last action application (internal state) */
	float TimeSinceLastAction = 0.0f;
};

USTRUCT(meta = (DisplayName = "Execute Objective"))
struct GAMEAI_PROJECT_API FSTTask_ExecuteObjective : public FStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ExecuteObjectiveInstanceData;

	FSTTask_ExecuteObjective() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

protected:
	/** Main execution loop - queries policy and applies actions */
	void ExecuteAtomicAction(FStateTreeExecutionContext& Context, float DeltaTime) const;

	/** Apply movement from atomic action */
	void ExecuteMovement(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const;

	/** Apply aiming from atomic action */
	void ExecuteAiming(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const;

	/** Fire weapon if bFire is true */
	void ExecuteFire(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const;

	/** Toggle crouch state */
	void ExecuteCrouch(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const;

	/** Use ability if requested */
	void ExecuteAbility(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const;

	/** Apply spatial constraints to action */
	FTacticalAction ApplyMask(const FTacticalAction& RawAction, const FActionSpaceMask& Mask) const;

	/** Calculate reward for current objective progress */
	float CalculateObjectiveReward(FStateTreeExecutionContext& Context, float DeltaTime) const;

	/** Check if objective is complete or failed */
	bool CheckObjectiveStatus(FStateTreeExecutionContext& Context) const;
};
