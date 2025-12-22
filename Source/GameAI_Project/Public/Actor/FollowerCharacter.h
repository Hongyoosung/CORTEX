// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/CombatStatsInterface.h"
#include "FollowerCharacter.generated.h"

class UFollowerAgentComponent;
class UFollowerStateTreeComponent;
class URLPolicyNetwork;


/**
 * Follower Character - State Tree Based
 *
 * This character integrates the hierarchical multi-agent system using State Tree.
 *
 * Architecture:
 * - FollowerAgentComponent: Receives commands from team leader, manages RL policy
 * - FollowerStateTreeComponent: Executes tactical states (Assault, Defend, Support)
 * - Combat stats: Health, stamina, weapon system
 *
 * Usage:
 * 1. Spawn this character with FollowerAIController
 * 2. Set TeamLeaderActor in FollowerAgentComponent
 * 3. Assign State Tree asset in FollowerStateTreeComponent
 * 4. System auto-starts on BeginPlay
 */
UCLASS()
class GAMEAI_PROJECT_API AFollowerCharacter : public ACharacter, public ICombatStatsInterface
{
	GENERATED_BODY()

public:
	AFollowerCharacter();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	//--------------------------------------------------------------------------
	// COMBAT STATS (ICombatStatsInterface Implementation)
	// Delegates to HealthComponent and WeaponComponent
	//--------------------------------------------------------------------------
	virtual float		GetHealthPercentage_Implementation	() const override;
	virtual bool		IsAlive_Implementation				() const override;
	virtual float		GetWeaponCooldown_Implementation	() const override;
	virtual bool		CanFireWeapon_Implementation		() const override;


public:
	//--------------------------------------------------------------------------
	// COMPONENTS
	//--------------------------------------------------------------------------
	/** Follower agent component (command execution, RL policy) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components")
	UFollowerAgentComponent* FollowerAgentComponent;

	/** State Tree component (tactical state management) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components")
	UFollowerStateTreeComponent* StateTreeComponent;
};
