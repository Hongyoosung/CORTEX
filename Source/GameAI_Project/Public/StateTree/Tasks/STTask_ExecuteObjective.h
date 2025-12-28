// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "RL/RLTypes.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "StateTreeExecutionTypes.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "STTask_ExecuteObjective.generated.h"

class UFollowerStateTreeComponent;
class APawn;
class AAIController;

/**
 * State Tree Task: Execute Objective (v4.0 Macro Actions)
 *
 * Universal execution task that handles ALL objective types using macro actions.
 * Replaces ExecuteAssault, ExecuteDefend, ExecuteSupport, ExecuteMove, ExecuteRetreat.
 *
 * Execution Flow (v4.0):
 * 1. RL policy outputs macro action: [Position, Target, FireMode, Stance]
 * 2. Execute movement via NavMesh pathfinding (EQS + MoveToLocation)
 * 3. Execute aiming via engine auto-aim (SetFocus)
 * 4. Execute fire mode and stance
 * 5. Calculate rewards based on objective progress
 *
 * Requirements:
 * - CurrentObjective must be assigned and active
 * - EQS query assets created in Content/AI/EQS/ (see RunEQSQuery documentation)
 * - PPO policy network loaded via RLlib
 * - NavMesh configured for movement
 *
 * EQS Assets Required (create in UE Editor):
 * - Content/AI/EQS/EQS_ForwardCover.uasset - Cover points toward objective
 * - Content/AI/EQS/EQS_RetreatCover.uasset - Cover points away from enemies
 * - Content/AI/EQS/EQS_Advance.uasset - Forward positions (no cover required)
 * Note: FlankLeft/FlankRight removed to reduce action space (6→4) for faster learning
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

	/** Action application rate (seconds) - controls StateTree update frequency for aiming/firing.
	 * NOTE: This is NOT the action REQUEST rate (controlled by ScholaAgentComponent.DecisionInterval).
	 * Default 0.05s = 20 Hz for smooth aiming and continuous firing updates. */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.01", ClampMax = "0.2"))
	float ActionApplicationInterval = 0.05f;

	//--------------------------------------------------------------------------
	// EQS Query Assets (assign in StateTree editor)
	//--------------------------------------------------------------------------

	/** EQS Query for ForwardCover tactical position */
	UPROPERTY(EditAnywhere, Category = "EQS Queries")
	TObjectPtr<UEnvQuery> ForwardCoverQuery;

	/** EQS Query for Retreat tactical position */
	UPROPERTY(EditAnywhere, Category = "EQS Queries")
	TObjectPtr<UEnvQuery> RetreatQuery;

	/** EQS Query for Advance tactical position */
	UPROPERTY(EditAnywhere, Category = "EQS Queries")
	TObjectPtr<UEnvQuery> AdvanceQuery;

	//--------------------------------------------------------------------------
	// Internal State
	//--------------------------------------------------------------------------

	/** Time since last action application (internal state) */
	float TimeSinceLastAction = 0.0f;

	/** Previous macro action (to detect changes) */
	FMacroAction PreviousMacroAction;
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
	//--------------------------------------------------------------------------
	// v4.0 MACRO ACTION EXECUTION
	//--------------------------------------------------------------------------

	/** Execute movement using EQS + NavMesh */
	void ExecuteMovement(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const;

	/** Execute aiming using SetFocus */
	void ExecuteAiming(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const;

	/** Execute fire mode (HoldFire/Fire/Suppress) */
	void ExecuteFire(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const;

	/** Calculate reward for current objective progress */
	float CalculateObjectiveReward(FStateTreeExecutionContext& Context, float DeltaTime) const;

	/** Check if objective is complete or failed */
	bool CheckObjectiveStatus(FStateTreeExecutionContext& Context) const;

	//--------------------------------------------------------------------------
	// v4.0 MACRO ACTION HELPERS (EQS + NavMesh + SetFocus)
	//--------------------------------------------------------------------------

	/** Query EQS for tactical positions based on position type
	 * Maps tactical position enum to EQS query asset and executes query.
	 * @param Context StateTree execution context
	 * @param PositionType Tactical position to query (Hold, ForwardCover, Retreat, etc.)
	 * @return Array of valid positions (sorted by EQS score, best first)
	 */
	TArray<FVector> QueryEQSPositions(FStateTreeExecutionContext& Context, ETacticalPosition PositionType) const;

	/** Run EQS query and return sorted positions
	 * Executes provided EQS query synchronously using UEnvQueryManager.
	 * Returns empty array and logs ERROR if query is null or execution fails.
	 *
	 * @param Pawn Querier pawn (used as EQS context)
	 * @param Query EQS query asset to execute (assigned via UPROPERTY in StateTree editor)
	 * @param QueryName Name for logging purposes (e.g., "ForwardCover")
	 * @return Array of valid positions (sorted by score), empty if query fails
	 *
	 * Note: Query assets are assigned in StateTree editor, not loaded by path.
	 * Use ForwardCoverQuery, RetreatQuery, or AdvanceQuery properties.
	 */
	TArray<FVector> RunEQSQuery(APawn* Pawn, UEnvQuery* Query, const FString& QueryName) const;

	/** Get enemy actor by index from observation system
	 * Retrieves enemy from SharedContext.VisibleEnemies array.
	 * @param Context StateTree execution context
	 * @param EnemyIndex Index into VisibleEnemies array
	 * @return Enemy actor if valid, nullptr if index out of bounds or enemy invalid
	 */
	AActor* GetEnemyByIndex(FStateTreeExecutionContext& Context, int32 EnemyIndex) const;
};
