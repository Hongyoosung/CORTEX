// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/MocAbilityBase.h"
#include "AttackAbility.generated.h"

class UWeaponComponent;
class ATeamManager;

/**
 * UAttackAbility - Encapsulates HandleCombat() logic as an ActorComponent.
 *
 * Training mode (ExecuteAbility): scans TM enemy list, performs LOS check,
 * and fires at the nearest visible enemy within range.
 * Inference mode (ExecuteAbilityWithTarget): fires at an explicit target from a BT task.
 *
 * Depends on: UWeaponComponent (via owner), ATeamManager (cached in BeginPlay)
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UAttackAbility : public UMocAbilityBase
{
	GENERATED_BODY()

public:
	UAttackAbility();

	virtual void BeginPlay() override;

	/** Training mode: scan enemy list and fire at nearest visible enemy */
	virtual void ExecuteAbility(float DeltaTime) override;

	/** Inference mode: fire at explicit target */
	virtual void ExecuteAbilityWithTarget(float DeltaTime, AActor* Target) override;

	// ==================== Config Data ====================

	/** Maximum engagement range for scanning enemies (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	float AttackRange = 8000.0f;

private:
	UPROPERTY(Transient)
	TObjectPtr<ATeamManager> CachedTM;
};
