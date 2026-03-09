// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/DEAbilityData.h"
#include "DEAbilityComponent.generated.h"

class UDEAttackAbility;
class UDEHealAbility;
class ADECharacter;

/**
 * Single component managing all agent abilities (Attack, Heal, etc.)
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UDEAbilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UDEAbilityComponent();

	virtual void BeginPlay() override;

	/** Get the Attack ability object */
	UFUNCTION(BlueprintPure, Category = "Abilities")
	UDEAttackAbility* GetAttackAbility() const { return AttackAbility; }

	/** Get the Heal ability object */
	UFUNCTION(BlueprintPure, Category = "Abilities")
	UDEHealAbility* GetHealAbility() const { return HealAbility; }

	/** Reset all ability states (e.g. on respawn) */
	void ResetAbilities();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	TObjectPtr<UDEAbilityData> AbilityData;

private:
	UPROPERTY(Transient)
	TObjectPtr<UDEAttackAbility> AttackAbility;

	UPROPERTY(Transient)
	TObjectPtr<UDEHealAbility> HealAbility;

	UPROPERTY(Transient)
	ADECharacter* OwnerCharacter;
};
