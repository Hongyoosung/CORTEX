// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/CombatStatsInterface.h"
#include "LeaderCharacter.generated.h"

class UTeamLeaderComponent;
class USquadManagerComponent;
class UIntelManagerComponent;
class UStrategicPlannerComponent;
class UVisualLoggerComponent;

/**
 * Leader Character - v9.0
 *
 * This character manages a team of follower agents using event-driven MCTS.
 *
 * Architecture (v9.0 - Decomposed):
 * - TeamLeaderComponent: Coordinator for all manager components
 * - SquadManagerComponent: Follower roster and capacity management
 * - IntelManagerComponent: Enemy tracking, objective discovery, team observations
 * - StrategicPlannerComponent: Async MCTS strategic planning with RAII pattern
 * - VisualLoggerComponent: Debug visualization (optional)
 * - No State Tree/BT: Leader only makes decisions, doesn't execute tactical actions
 * - Combat stats: Health, stamina, weapon system (same as followers)
 *
 * Usage:
 * 1. Spawn this character with LeaderAIController
 * 2. Register followers via TeamLeaderComponent->RegisterFollower() (delegates to SquadManager)
 * 3. MCTS runs automatically on strategic events via StrategicPlanner
 * 4. Commands are automatically issued to followers
 *
 * v9.0 Changes:
 * - Added 4 new manager components for separation of concerns
 * - TeamLeaderComponent now acts as coordinator, delegates to managers
 * - RAII async MCTS execution for automatic resource cleanup
 * - Debug visualization separated into VisualLoggerComponent
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
	// COMPONENTS (v9.0 - Decomposed Architecture)
	//--------------------------------------------------------------------------
	/** Team leader component (coordinator for all manager components) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Core")
	UTeamLeaderComponent* TeamLeaderComponent;

	/** Squad manager component (follower roster, capacity management) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Managers")
	USquadManagerComponent* SquadManagerComponent;

	/** Intel manager component (enemy tracking, objectives, team observations) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Managers")
	UIntelManagerComponent* IntelManagerComponent;

	/** Strategic planner component (async MCTS planning with RAII) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Managers")
	UStrategicPlannerComponent* StrategicPlannerComponent;

	/** Visual logger component (debug visualization - optional) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Debug")
	UVisualLoggerComponent* VisualLoggerComponent;

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
