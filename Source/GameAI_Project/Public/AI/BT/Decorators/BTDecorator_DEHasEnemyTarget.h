// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTDecorator_DEHasEnemyTarget.generated.h"

/**
 * BTDecorator_DEHasEnemyTarget - Decorator for Enemy Target Validation
 *
 * Functionality:
 * - Checks if a valid enemy target exists in Blackboard
 * - Validates target is alive and within range
 * - Optionally checks line of sight
 * - Can observe value changes for automatic re-evaluation
 *
 * Blackboard Requirements:
 * - "TargetEnemy" (Object) - Enemy actor to validate
 * - "HasTarget" (Bool) - Whether a valid target exists
 *
 * Usage:
 * - Attach to combat-related BT nodes
 * - Use with BTTask_DECombatFire to ensure valid target
 * - Pair with BTService_UpdateCombatTarget for continuous updates
 *
 * Configuration:
 * - bCheckAlive: Verify target is alive
 * - bCheckLineOfSight: Verify line of sight to target
 * - MaxRange: Maximum valid engagement range (0 = no limit)
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API UBTDecorator_DEHasEnemyTarget : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_DEHasEnemyTarget();

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual FString GetStaticDescription() const override;

public:
	//========================================
	// CONFIGURATION
	//========================================

	/** Verify target is alive */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	bool bCheckAlive = true;

	/** Verify line of sight to target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	bool bCheckLineOfSight = false;

	/** Maximum valid engagement range (0 = no limit) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.0"))
	float MaxRange = 0.0f;


	//========================================
	// BLACKBOARD KEYS
	//========================================
	/** Blackboard key for target enemy */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetEnemyKey;

	/** Blackboard key for target validity flag */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasTargetKey;
};
