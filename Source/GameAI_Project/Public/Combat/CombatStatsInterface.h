// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CombatStatsInterface.generated.h"


USTRUCT(BlueprintType)
struct FCombatStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Health")
	float MaxHealth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Health")
	float CurrentHealth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Weapon")
	float Damage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Weapon")
	float CooldownTime;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Weapon")
	float CurrentWeaponCooldown;

	FCombatStats()
		: MaxHealth(100.0f)
		, CurrentHealth(100.0f)
		, Damage(10.0f)
		, CooldownTime(1.0f)
		, CurrentWeaponCooldown(0.0f)
	{}
};


/**
 * Interface for actors that can provide combat statistics
 * Implement this interface in your Character/Pawn class to provide
 * health, stamina, shield, and weapon information to the observation system
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UCombatStatsInterface : public UInterface
{
	GENERATED_BODY()
};

class GAMEAI_PROJECT_API ICombatStatsInterface
{
	GENERATED_BODY()

public:
	//--------------------------------------------------------------------------
	// HEALTH & SURVIVAL
	//--------------------------------------------------------------------------

	/**
	 * Get current health percentage (0.0 - 100.0)
	 * @return Health percentage (100 = full health, 0 = dead)
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat Stats")
	float GetHealthPercentage() const;
	virtual float GetHealthPercentage_Implementation() const { return 100.0f; }

	/**
	 * Is the actor alive?
	 * @return True if alive, false if dead
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat Stats")
	bool IsAlive() const;
	virtual bool IsAlive_Implementation() const { return true; }


	//--------------------------------------------------------------------------
	// WEAPON & COMBAT STATE
	//--------------------------------------------------------------------------

	/**
	 * Get current weapon cooldown remaining (seconds)
	 * @return Seconds until weapon can fire again (0 = ready to fire)
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat Stats")
	float GetWeaponCooldown() const;
	virtual float GetWeaponCooldown_Implementation() const { return 0.0f; }

	/**
	 * Can the actor fire their weapon right now?
	 * @return True if weapon can be fired, false otherwise
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat Stats")
	bool CanFireWeapon() const;
	virtual bool CanFireWeapon_Implementation() const { return true; }
};
