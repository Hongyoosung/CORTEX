// Copyright Epic Games, Inc. All Rights Reserved.
// DEPRECATED: Use BTTask_DEAttackAbility instead.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_DECombatFire.generated.h"

class UDEAttackAbility;
class ADECharacter;

/**
 * BTTask_DECombatFire - Behavior Tree Task for Combat Engagement
 *
 * Functionality:
 * - Fires at enemy target stored in Blackboard
 * - Continuously fires while enemy is in sight
 * - Respects weapon cooldown and ammo constraints
 * - Automatically aims at target using WeaponComponent
 * - Stops when enemy is lost or out of ammo
 *
 * Blackboard Requirements:
 * - "TargetEnemy" (Object) - Enemy actor to fire at
 * - "HasTarget" (Bool) - Whether a valid target exists
 *
 * Usage:
 * - Place in BehaviorTree after movement/positioning tasks
 * - Pair with BTDecorator_DEHasEnemyTarget decorator
 * - Use with BTService_UpdateCombatTarget service
 *
 * Configuration:
 * - bUsePredictiveAiming: Use lead targeting for moving enemies
 * - bContinuousFire: Keep firing while target is valid (default: true)
 * - bRequireLineOfSight: Verify line of sight before each shot
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API UBTTask_DECombatFire : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_DECombatFire();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

protected:
	/** Validate combat target and weapon state */
	bool ValidateCombatConditions(UBehaviorTreeComponent& OwnerComp, AActor*& OutTarget, UDEAttackAbility*& OutAbility) const;

	/** Check if target is still valid and visible */
	bool IsTargetValid(AActor* Target, APawn* OwnerPawn) const;

	/** Aim at target (rotate pawn to face target) */
	void AimAtTarget(APawn* OwnerPawn, AActor* Target);

	/** Fire weapon at target */
	bool FireWeapon(UDEAttackAbility* Ability, AActor* Target);

public:
	//========================================
	// CONFIGURATION
	//========================================
	/** Use predictive aiming to lead moving targets */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bUsePredictiveAiming = true;

	/** Continue firing while target is valid (full auto behavior) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bContinuousFire = true;

	/** Require line of sight check before firing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bRequireLineOfSight = true;

	/** Maximum distance to engage target (0 = no limit) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0.0"))
	float MaxEngagementRange = 0.0f;

	/** Minimum distance to engage target — targets closer than this will not be fired at (0 = no limit) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0.0"))
	float MinEngagementRange = 0.0f;

	/** Rotation speed when aiming at target (degrees per second, 0 = instant) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0.0"))
	float AimRotationSpeed = 360.0f;


	//========================================
	// BLACKBOARD KEYS
	//========================================
	/** Blackboard key for target enemy */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetEnemyKey;

	/** Blackboard key for target validity flag */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasTargetKey;

protected:
	/** Track if we're currently in firing mode */
	bool bIsFiring = false;
};
