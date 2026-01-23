// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/CombatStatsInterface.h"
#include "LeaderCharacter.generated.h"

class UTeamLeaderComponent;

/**
 * Leader Character
 *
 * This character manages a team of follower agents using event-driven MCTS.
 *
 * Architecture:
 * - TeamLeaderComponent: Runs MCTS for strategic decisions, issues commands to followers
 * - No State Tree/BT: Leader only makes decisions, doesn't execute tactical actions
 * - Combat stats: Health, stamina, weapon system (same as followers)
 *
 * Usage:
 * 1. Spawn this character with LeaderAIController
 * 2. Register followers via TeamLeaderComponent->RegisterFollower()
 * 3. MCTS runs automatically on strategic events
 * 4. Commands are automatically issued to followers
 */
UCLASS()
class GAMEAI_PROJECT_API ALeaderCharacter : public ACharacter, public ICombatStatsInterface
{
	GENERATED_BODY()

public:
	ALeaderCharacter();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	//--------------------------------------------------------------------------
	// COMBAT STATS (ICombatStatsInterface Implementation)
	//--------------------------------------------------------------------------
	virtual float		GetHealthPercentage_Implementation	() const override;
	virtual bool		IsAlive_Implementation				() const override;
	virtual float		GetWeaponCooldown_Implementation	() const override;
	virtual bool		CanFireWeapon_Implementation		() const override;


	//--------------------------------------------------------------------------
	// COMBAT SYSTEM
	//--------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	void TakeDamage(float DamageAmount);

	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	void Heal(float HealAmount);

	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	void Kill();

	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	void Respawn();

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	bool FireWeapon();


private:
	//--------------------------------------------------------------------------
	// HELPER FUNCTIONS
	//--------------------------------------------------------------------------
	/** Update timers (cooldowns, regeneration) */
	void UpdateTimers(float DeltaTime);
	void UpdateWeaponCooldown(float DeltaTime);


public:
	//--------------------------------------------------------------------------
	// COMPONENTS
	//--------------------------------------------------------------------------
	/** Team leader component (strategic MCTS decision-making) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components")
	UTeamLeaderComponent* TeamLeaderComponent;

	//--------------------------------------------------------------------------
	// COMBAT PROPERTIES
	//--------------------------------------------------------------------------
	FCombatStats CombatStats;


private:
	/** Time since last damage (for shield regen) */
	float TimeSinceLastDamage = 0.0f;

	/** Is character dead? */
	bool bIsDead = false;
};
