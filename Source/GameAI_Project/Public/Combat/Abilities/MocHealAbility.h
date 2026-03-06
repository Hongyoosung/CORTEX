// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/MocAbility.h"
#include "Combat/Abilities/MocAbilityData.h"
#include "MocHealAbility.generated.h"

class UNiagaraComponent;

/**
 * Specialized ability class for healing allies
 */
UCLASS()
class GAMEAI_PROJECT_API UMocHealAbility : public UMocAbility
{
	GENERATED_BODY()

public:
	virtual void Initialize(AMocCharacter* InOwner) override;
	virtual void Execute(float DeltaTime) override;
	virtual void ExecuteWithTarget(float DeltaTime, AActor* Target) override;

	void SetConfig(const FHealAbilityConfig& InConfig);

	void ResetHealState();
	void ResetTickHeal() { LastTickHealAmount = 0.0f; }
	float GetLastTickHealAmount() const { return LastTickHealAmount; }
	bool ConsumeHealBurst(float Threshold);

private:
	UPROPERTY()
	FHealAbilityConfig Config;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> HealBeamComponent;

	float LastTickHealAmount = 0.0f;
	float CumulativeHealAmount = 0.0f;

	AMocCharacter* FindNearestInjuredAlly() const;
	void UpdateHealBeam(const AActor* Target);
};
