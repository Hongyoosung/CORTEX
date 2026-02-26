// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/MocAbilityBase.h"
#include "HealAbility.generated.h"

class ATeamManager;
class AMocCharacter;
class UNiagaraComponent;
class UNiagaraSystem;

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UHealAbility : public UMocAbilityBase
{
	GENERATED_BODY()

public:
	UHealAbility();

	virtual void BeginPlay() override;
	virtual void ExecuteAbility(float DeltaTime) override;
	virtual void ExecuteAbilityWithTarget(float DeltaTime, AActor* Target) override;

	void ResetTickHeal() { LastTickHealAmount = 0.0f; }
	void ResetHealState();

	float GetLastTickHealAmount() const { return LastTickHealAmount; }
	bool ConsumeHealBurst(float Threshold);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	float HealRange = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	float HealRate = 10.0f;

	/** true일 경우 시야(벽 너머)에 가려진 아군에게는 힐이 들어가지 않도록 검사합니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	bool bRequireLineOfSight = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX")
	TObjectPtr<UNiagaraSystem> HealBeamSystem;

	UPROPERTY(EditDefaultsOnly, Category = "VFX")
	FName BeamStartParamName = FName("BeamStart");

	UPROPERTY(EditDefaultsOnly, Category = "VFX")
	FName BeamEndParamName = FName("BeamEnd");

private:
	AMocCharacter* FindNearestInjuredAlly() const;
	void UpdateHealBeam(const AActor* Target);

	UPROPERTY(Transient)
	AMocCharacter* CachedOwner = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<ATeamManager> CachedTM;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> HealBeamComponent;

	float LastTickHealAmount = 0.0f;
	float CumulativeHealAmount = 0.0f;
	int32 CurrentHealTargetIdx = -1;
};