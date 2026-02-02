// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "RL/RLTypes.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "StateTreeExecutionTypes.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "STTask_ExecuteTacticalMovement.generated.h"

class UFollowerStateTreeComponent;
class UFollowerAgentComponent;
class APawn;
class AAIController;

/**
 * State Tree Task: Execute Tactical Movement v8.0
 *
 * v8.0 Architecture:
 * - MCTS assigns strategies (Assault/Defend/Support/Retreat)
 * - RL outputs tactical parameters (Aggression, CoverPreference, Spread, Risk)
 * - EQS uses tactical parameters as query weights
 * - NavMesh handles pathfinding
 *
 * Key Changes from v7.0:
 * - Removed strategy-to-position mapping (Assault→ForwardCover)
 * - Added ApplyTacticalParameters() for EQS weight modulation
 * - Parameters control HOW to execute strategy, not WHICH strategy
 * - Single unified EQS query with dynamic weight modulation
 *
 * Execution Flow:
 * 1. Get assigned strategy from MCTS (via Mission)
 * 2. Query RL policy for tactical parameters [0,1]^4
 * 3. Modulate EQS query weights based on parameters
 * 4. Execute EQS → NavMesh movement
 */

USTRUCT()
struct GAMEAI_PROJECT_API FSTTask_ExecuteTacticalMovement_InstanceData
{
	GENERATED_BODY()

	/** StateTree component reference */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UFollowerStateTreeComponent> StateTreeComp;

	/** Follower agent component reference (for tactical parameters) */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UFollowerAgentComponent> AgentComponent;

	/** Controlled Pawn reference */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<APawn> ControlledPawn;

	/** AIController reference */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AAIController> AIController;

	/** EQS Query for tactical positioning (unified for all strategies) */
	UPROPERTY(EditAnywhere, Category = "EQS")
	TObjectPtr<UEnvQuery> TacticalPositionQuery;

	/** Update frequency (Hz) - How often to query new tactical parameters */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "2.0", ClampMax = "10.0"))
	float UpdateFrequencyHz = 5.0f;

	/** Movement speed multiplier */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float MovementSpeedMultiplier = 1.0f;

	/** Ticks since last parameter update (for frequency control) */
	int32 TicksSinceLastUpdate = 0;

	/** Previous tactical parameters (to detect changes) */
	FTacticalParameters PreviousTacticalParams;
};

USTRUCT(meta = (DisplayName = "Execute Tactical Movement v8.0"))
struct GAMEAI_PROJECT_API FSTTask_ExecuteTacticalMovement : public FStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ExecuteTacticalMovement_InstanceData;

	FSTTask_ExecuteTacticalMovement() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

protected:
	/**
	 * v8.0: Apply tactical parameters to EQS query weights
	 * Modulates EQS behavior based on RL-controlled tactical parameters
	 *
	 * Parameter Mapping:
	 * - Aggression [0,1] → MinDistanceToEnemy [1000cm, 200cm] (higher aggression = closer)
	 * - CoverPreference [0,1] → CoverWeight [0, 5.0] (higher preference = more cover bias)
	 * - SpreadDistance [0,1] → FormationSpread [200cm, 1000cm] (higher spread = more distance)
	 * - RiskTolerance [0,1] → Stored for retreat logic (handled outside EQS)
	 */
	void ApplyTacticalParameters(
		FStateTreeExecutionContext& Context,
		FTacticalParameters Params) const;

	/**
	 * Run EQS query with current tactical parameters
	 * Returns best tactical position based on modulated weights
	 *
	 * v9.0: Added AssignedStrategy parameter for objective-aware scoring
	 */
	TArray<FVector> RunTacticalEQSQuery(
		APawn* Pawn,
		UEnvQuery* Query,
		FTacticalParameters Params,
		EStrategyType AssignedStrategy) const;

	/**
	 * Execute movement to EQS-selected position
	 * Uses NavMesh pathfinding via AIController
	 */
	void ExecuteMovementToTacticalPosition(
		FStateTreeExecutionContext& Context,
		FVector TargetPosition) const;
};
