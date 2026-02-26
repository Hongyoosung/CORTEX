// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/MocAbilityBase.h"
#include "HealAbility.generated.h"

class ATeamManager;
class AMocCharacter;
class UNiagaraComponent;
class UNiagaraSystem;

/**
 * UHealAbility - Encapsulates TickSupportHealing() and FindNearestInjuredAlly() as an ActorComponent.
 *
 * Training mode (ExecuteAbility): self-selects nearest injured ally within HealRange and heals.
 * Inference mode (ExecuteAbilityWithTarget): heals an explicit target provided by a BT task.
 *
 * Config data (HealRange, HealRate) is owned here — not on MocCharacter.
 * Owns: LastTickHealAmount, CumulativeHealAmount, CurrentHealTargetIdx
 * Provides: GetLastTickHealAmount(), ConsumeHealBurst(), ResetHealState(), ResetTickHeal()
 *
 * Depends on: ATeamManager (cached in BeginPlay)
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UHealAbility : public UMocAbilityBase
{
	GENERATED_BODY()

public:
	UHealAbility();

	virtual void BeginPlay() override;

	/** Training mode: find nearest injured ally and heal */
	virtual void ExecuteAbility(float DeltaTime) override;

	/** Inference mode: heal explicit target */
	virtual void ExecuteAbilityWithTarget(float DeltaTime, AActor* Target) override;

	/** Clear LastTickHealAmount (called from Tick when strategy != Support) */
	void ResetTickHeal() { LastTickHealAmount = 0.0f; }

	/** Reset all heal state and deactivate beam (called from ResetCharacter) */
	void ResetHealState();

	/** HP healed during the most recent Execute call */
	float GetLastTickHealAmount() const { return LastTickHealAmount; }

	/**
	 * Returns true and resets the accumulator if cumulative heal has reached Threshold.
	 * Used by MocRewardCalculator for burst bonus.
	 */
	bool ConsumeHealBurst(float Threshold);

	// ==================== Config Data ====================

	/** Radius within which a Support agent can heal allies (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	float HealRange = 800.0f;

	/** Healing rate in HP per second */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	float HealRate = 10.0f;

	// ==================== Heal Beam VFX ====================

	/** Niagara beam system drawn between healer and target while healing is active */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX")
	TObjectPtr<UNiagaraSystem> HealBeamSystem;

	/** Niagara user parameter name for the beam source position (world-space) */
	UPROPERTY(EditDefaultsOnly, Category = "VFX")
	FName BeamStartParamName = FName("BeamStart");

	/** Niagara user parameter name for the beam target position (world-space) */
	UPROPERTY(EditDefaultsOnly, Category = "VFX")
	FName BeamEndParamName = FName("BeamEnd");

private:
	AMocCharacter* FindNearestInjuredAlly() const;

	/** Activate/update the heal beam toward Target; pass nullptr to deactivate */
	void UpdateHealBeam(const AActor* Target);

	UPROPERTY(Transient)
	TObjectPtr<ATeamManager> CachedTM;

	/** Runtime beam component — spawned in BeginPlay if HealBeamSystem is set */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> HealBeamComponent;

	float LastTickHealAmount = 0.0f;
	float CumulativeHealAmount = 0.0f;
	int32 CurrentHealTargetIdx = -1;
};
