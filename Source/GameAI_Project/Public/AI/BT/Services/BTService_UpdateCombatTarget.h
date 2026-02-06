// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_UpdateCombatTarget.generated.h"



//--------------------------------------------------------------------------
// TARGET SELECTION
//--------------------------------------------------------------------------

/** How to select target among multiple enemies */
UENUM(BlueprintType)
enum class ETargetSelectionMode : uint8
{
	Nearest			UMETA(DisplayName = "Nearest Enemy"),
	LowestHealth	UMETA(DisplayName = "Lowest Health"),
	HighestThreat	UMETA(DisplayName = "Highest Threat")
};


/**
 * BTService_UpdateCombatTarget - Service for Continuous Combat Target Updates
 *
 * Functionality:
 * - Continuously updates combat target in Blackboard
 * - Uses AI Perception to detect enemies
 * - Selects nearest/most threatening enemy
 * - Clears target when enemy is lost
 * - Updates HasTarget flag automatically
 *
 * Blackboard Updates:
 * - "TargetEnemy" (Object) - Current enemy target
 * - "HasTarget" (Bool) - Whether a valid target exists
 *
 * Usage:
 * - Attach to combat-related BT composite nodes (Selector/Sequence)
 * - Works with BTTask_CombatFire and BTDecorator_HasEnemyTarget
 * - Runs every Interval seconds (default: 0.5s)
 *
 * Configuration:
 * - TargetSelectionMode: How to choose enemy (Nearest, LowestHealth, HighestThreat)
 * - MaxDetectionRange: Maximum range to detect enemies (0 = unlimited)
 * - bRequireLineOfSight: Only target enemies with line of sight
 */
UCLASS()
class GAMEAI_PROJECT_API UBTService_UpdateCombatTarget : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateCombatTarget();

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual FString GetStaticDescription() const override;

	/** Find best enemy target using perception */
	AActor* FindBestTarget(UBehaviorTreeComponent& OwnerComp) const;

	/** Check if target is valid and visible */
	bool IsValidTarget(AActor* Target, APawn* OwnerPawn) const;

	/** Calculate threat level of target (for HighestThreat mode) */
	float CalculateThreatLevel(AActor* Target, APawn* OwnerPawn) const;

public:
	

	

	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** How to select target when multiple enemies detected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Selection")
	ETargetSelectionMode TargetSelectionMode = ETargetSelectionMode::Nearest;

	/** Maximum range to detect enemies (0 = unlimited) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Selection", meta = (ClampMin = "0.0"))
	float MaxDetectionRange = 0.0f;

	/** Require line of sight to target enemy */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Selection")
	bool bRequireLineOfSight = true;

	/** Prefer current target (sticky targeting) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Selection")
	bool bPreferCurrentTarget = true;

	/** Distance bonus for current target (meters) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Selection", meta = (EditCondition = "bPreferCurrentTarget", ClampMin = "0.0"))
	float CurrentTargetBonusRange = 500.0f;

	//--------------------------------------------------------------------------
	// BLACKBOARD KEYS
	//--------------------------------------------------------------------------

	/** Blackboard key for target enemy */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetEnemyKey;

	/** Blackboard key for target validity flag */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasTargetKey;
};
