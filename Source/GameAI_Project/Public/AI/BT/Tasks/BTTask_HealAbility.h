// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_HealAbility.generated.h"

class UHealAbility;
class AMocCharacter;

/**
 * BTTask_HealAbility - BT Task that delegates healing to UHealAbility.
 *
 * Calls HealAbility->ExecuteAbility() (self-selecting target internally).
 * Runs indefinitely while strategy=Support; BT decorator aborts on strategy change.
 *
 * ExecuteTask → InProgress
 * TickTask    → calls HealAbility->ExecuteAbility(DeltaSeconds)
 * Fails if character dead / HealAbility component missing
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API UBTTask_HealAbility : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_HealAbility();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};
