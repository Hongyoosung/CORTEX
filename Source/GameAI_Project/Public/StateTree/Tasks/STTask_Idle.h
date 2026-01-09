// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "StateTreeExecutionTypes.h"
#include "STTask_Idle.generated.h"

class UFollowerStateTreeComponent;

/**
 * State Tree Task: Idle
 *
 * Fallback task that keeps the StateTree running when no Mission is active.
 * Continuously returns Running status to prevent StateTree termination.
 *
 * Purpose:
 * - Prevents StateTree from stopping when agents await commands
 * - Allows agents to remain responsive to new Missions
 * - Provides a stable "waiting" state
 */

USTRUCT()
struct GAMEAI_PROJECT_API FSTTask_IdleInstanceData
{
	GENERATED_BODY()

	/** StateTree component reference */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UFollowerStateTreeComponent> StateTreeComp;
};

USTRUCT(meta = (DisplayName = "Idle (Wait for Mission)"))
struct GAMEAI_PROJECT_API FSTTask_Idle : public FStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_IdleInstanceData;

	FSTTask_Idle() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
