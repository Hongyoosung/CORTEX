// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/CombatStatsInterface.h"
#include "FollowerCharacter.generated.h"

class UFollowerAgentComponent;
class UFollowerStateTreeComponent;
class UTeamCommsComponent;
class UContextBridgeComponent;
class UVisualLoggerComponent;
class URLPolicyNetwork;


/**
 * Follower Character - State Tree Based (v9.0)
 *
 * This character integrates the hierarchical multi-agent system using State Tree.
 *
 * Architecture (v9.0 - Decomposed):
 * - FollowerAgentComponent: Coordinator, manages 4 v8.0 sub-components (Tactical, Observation, RL, Combat)
 * - FollowerStateTreeComponent: Executes tactical states (Assault, Defend, Support)
 * - TeamCommsComponent: Handles communication with team leader (auto-registration)
 * - ContextBridgeComponent: Decouples StateTree dependencies (dependency inversion pattern)
 * - VisualLoggerComponent: Debug visualization (optional)
 * - Combat stats: Health, stamina, weapon system (via HealthComponent/WeaponComponent)
 *
 * Usage:
 * 1. Spawn this character with FollowerAIController
 * 2. TeamCommsComponent auto-registers with leader (finds by "TeamLeader" tag)
 * 3. Assign State Tree asset in FollowerStateTreeComponent
 * 4. System auto-starts on BeginPlay
 *
 * v9.0 Changes:
 * - Added TeamCommsComponent for decoupled leader communication
 * - Added ContextBridgeComponent for StateTree data sharing
 * - FollowerAgentComponent writes TO ContextBridge, StateTree reads FROM it
 * - Debug visualization separated into VisualLoggerComponent
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
	// COMPONENTS (v9.0 - Decomposed Architecture)
	//--------------------------------------------------------------------------
	/** Follower agent component (coordinator, manages 4 v8.0 sub-components) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Core")
	UFollowerAgentComponent* FollowerAgentComponent;

	/** State Tree component (tactical state management) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Core")
	UFollowerStateTreeComponent* StateTreeComponent;

	/** Team comms component (leader communication, auto-registration) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Communication")
	UTeamCommsComponent* TeamCommsComponent;

	/** Context bridge component (StateTree data bridge - dependency inversion) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|StateTree")
	UContextBridgeComponent* ContextBridgeComponent;

	/** Visual logger component (debug visualization - optional) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Debug")
	UVisualLoggerComponent* VisualLoggerComponent;

private:
	//--------------------------------------------------------------------------
	// COMPONENT COORDINATION (v8.0)
	//--------------------------------------------------------------------------
	/** Handle death event from HealthComponent and coordinate with FollowerAgentComponent */
	UFUNCTION()
	void OnHealthComponentDeath(const struct FDeathEventData& DeathEvent);
};
