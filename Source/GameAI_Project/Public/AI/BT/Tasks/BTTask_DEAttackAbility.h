// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_DEAttackAbility.generated.h"

class ADECharacter;

/**
 * BTTask_DEAttackAbility - Executes DEGA_Attack via GAS.
 *
 * Each tick: aims at the blackboard target via DEGA_Attack::AimAtTarget(),
 * validates the target via DEGA_Attack::IsTargetValid(), then activates the
 * attack ability through TryActivateAbilitiesByTag(Ability.Attack).
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
	/** Keep firing each tick while target is valid (full-auto). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bContinuousFire = true;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetEnemyKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasTargetKey;

protected:
	bool bIsFiring = false;
};
