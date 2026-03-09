// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_DEAttackAbility.generated.h"

class UDEAttackAbility;
class ADECharacter;

/**
 * BTTask_DEAttackAbility - BT Task that delegates combat to UAttackAbility.
 * Replaces BTTask_DECombatFire in inference mode.
 *
 * ExecuteTask → InProgress
 * TickTask    → reads BB keys → calls AttackAbility->ExecuteAbilityWithTarget()
 * Fails if target invalid / weapon can't fire / character dead
 *
 * Blackboard Requirements:
 * - "TargetEnemy" (Object) - Enemy actor to fire at
 * - "HasTarget"   (Bool)   - Whether a valid target exists
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API UBTTask_DEAttackAbility : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_DEAttackAbility();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

public:
	/** Blackboard key for target enemy */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetEnemyKey;

	/** Blackboard key for target validity flag */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasTargetKey;
};
