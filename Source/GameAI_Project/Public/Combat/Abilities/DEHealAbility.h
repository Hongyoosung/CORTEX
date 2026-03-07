// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/DEAbility.h"
#include "Data/DEAbilityData.h"
#include "DEHealAbility.generated.h"

class UNiagaraComponent;

/**
 * Specialized ability class for healing allies
 */
UCLASS()
class GAMEAI_PROJECT_API UDEHealAbility : public UDEAbility
{
	GENERATED_BODY()

public:
	virtual void Initialize(ADECharacter* InOwner) override;
	virtual void Execute(float DeltaTime) override;
	virtual void ExecuteWithTarget(float DeltaTime, AActor* Target) override;

	void SetConfig(const FDEHealAbilityConfig& InConfig);

	void ResetHealState();
	void ResetTickHeal() { LastTickHealAmount = 0.0f; }
	float GetLastTickHealAmount() const { return LastTickHealAmount; }
	bool ConsumeHealBurst(float Threshold);

private:
	UPROPERTY()
	FDEHealAbilityConfig Config;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> HealBeamComponent;

	float LastTickHealAmount = 0.0f;
	float CumulativeHealAmount = 0.0f;

	ADECharacter* FindNearestInjuredAlly() const;
	void UpdateHealBeam(const AActor* Target);
};
