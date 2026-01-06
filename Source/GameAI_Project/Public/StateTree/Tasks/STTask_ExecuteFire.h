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
 * State Tree Task: Execute Fire (v6.0 Strategy-Based)
 *
 * Handles weapon firing based on RL-selected strategy.
 * Strategy determines targeting priority and firing behavior.
 *
 * v6.0 Execution:
 * - Gets current strategy from FollowerAgentComponent (Assault/Defend/Support/Retreat)
 * - Retreat strategy = hold fire
 * - Assault/Defend = target closest enemy
 * - Support = target enemy threatening ally
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
	/** v6.0: Execute fire mode based on current strategy */
	void ExecuteFire(FStateTreeExecutionContext& Context) const;

	/** v6.0: Select target based on strategy */
	AActor* SelectTarget(EStrategyType Strategy, const TArray<AActor*>& Enemies, const FFollowerStateTreeContext& Context) const;

	/** Get closest enemy to pawn */
	AActor* GetClosestEnemy(const TArray<AActor*>& Enemies, APawn* Pawn) const;

	/** Get enemy threatening ally (for Support strategy) */
	AActor* GetEnemyThreateningAlly(const TArray<AActor*>& Enemies, UFollowerAgentComponent* FollowerComp) const;

	/** Stop firing and clear focus */
	void StopFiring(FStateTreeExecutionContext& Context) const;
};
