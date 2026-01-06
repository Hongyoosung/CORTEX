// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "RL/RLTypes.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "StateTreeExecutionTypes.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "STTask_ExecuteMovement.generated.h"

class UFollowerStateTreeComponent;
class APawn;
class AAIController;

/**
 * State Tree Task: Execute Movement (v6.0 Strategy-Based)
 *
 * Handles tactical movement using deterministic strategy-to-position mapping.
 * Maps RL-selected strategies to EQS queries for tactical positions.
 *
 * v6.0 Execution:
 * - Gets current strategy from FollowerAgentComponent (Assault/Defend/Support/Retreat)
 * - Maps strategy to tactical position (ForwardCover/Hold/Retreat)
 * - Queries EQS for tactical positions
 * - Uses NavMesh pathfinding via MoveToLocation
 * - No manual velocity input (engine-driven)
 */

USTRUCT()
struct GAMEAI_PROJECT_API FSTTask_ExecuteMovementInstanceData
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

	/** Movement speed multiplier */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float MovementSpeedMultiplier = 1.0f;

	/** EQS Query for ForwardCover tactical position */
	UPROPERTY(EditAnywhere, Category = "EQS Queries")
	TObjectPtr<UEnvQuery> ForwardCoverQuery;

	/** EQS Query for Retreat tactical position */
	UPROPERTY(EditAnywhere, Category = "EQS Queries")
	TObjectPtr<UEnvQuery> RetreatQuery;

	/** EQS Query for Advance tactical position */
	UPROPERTY(EditAnywhere, Category = "EQS Queries")
	TObjectPtr<UEnvQuery> AdvanceQuery;

	/** Previous macro action (to detect changes) */
	FMacroAction PreviousMacroAction;
};

USTRUCT(meta = (DisplayName = "Execute Movement"))
struct GAMEAI_PROJECT_API FSTTask_ExecuteMovement : public FStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ExecuteMovementInstanceData;

	FSTTask_ExecuteMovement() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

protected:
	/** v6.0: Map strategy to tactical position (deterministic) */
	ETacticalPosition StrategyToPosition(EStrategyType Strategy, const FFollowerStateTreeContext& Context) const;

	/** v6.0: Execute movement to specific tactical position using EQS + NavMesh */
	void ExecuteMovementToPosition(FStateTreeExecutionContext& Context, ETacticalPosition PositionType, float DeltaTime) const;

	/** Query EQS for tactical positions */
	TArray<FVector> QueryEQSPositions(FStateTreeExecutionContext& Context, ETacticalPosition PositionType) const;

	/** Run EQS query and return sorted positions */
	TArray<FVector> RunEQSQuery(APawn* Pawn, UEnvQuery* Query, const FString& QueryName) const;
};
