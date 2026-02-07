// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Team/MocTeamInterface.h"
#include "Combat/CombatStatsInterface.h"
#include "MocCharacter.generated.h"

// Forward declarations
class UHealthComponent;
class UWeaponComponent;
class UScholaMocAgent;
class UAIPerceptionStimuliSourceComponent;
class UBehaviorTree;
struct FDeathEventData;

/**
 * AMocCharacter - MOC v10.1 Component-Based Agent Character
 *
 * Component Architecture:
 * - UHealthComponent: Damage, death, respawn
 * - UWeaponComponent: Firing, ammo, cooldown
 * - UScholaMocAgent: RL training interface
 * - UAIPerceptionStimuliSourceComponent: AI visibility
 *
 * Team Identification:
 * - Uses Actor Tags: "Team_0" (Red), "Team_1" (Blue)
 * - Set by TeamManager on spawn
 *
 * Death/Respawn Flow:
 * 1. HealthComponent broadcasts OnDeath
 * 2. MocCharacter::OnDeath() called
 * 3. Disable movement, physics ragdoll
 * 4. Notify GameMode → TeamManager
 * 5. TeamManager queues respawn (5 seconds)
 * 6. TeamManager calls ResetCharacter()
 * 7. Re-enable movement, restart AI
 *
 * AI Integration:
 * - Auto-possesses AIController on spawn
 * - Runs Behavior Tree
 * - BT tasks access components for combat/movement
 * - Schola agent collects observations from components
 *
 * Usage:
 * 1. Set AIControllerClass and BehaviorTree in Blueprint
 * 2. TeamManager spawns with team tag
 * 3. AI takes over automatically
 */
UCLASS()
class GAMEAI_PROJECT_API AMocCharacter : public ACharacter, public IMocTeamInterface, public ICombatStatsInterface
{
	GENERATED_BODY()

public:
	AMocCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;


	//========================================
	// Team Interfaces Implementions
	//========================================
	virtual int32 	GetTeamID_Implementation() const override;


	//========================================
	// Combat Interfaces Implementions
	//========================================
	virtual float	GetHealthPercentage() const override;
	virtual bool	IsAlive() const override;
	virtual float	GetWeaponCooldown() const override;
	virtual bool	CanFireWeapon() const override;


	//========================================
	// Component Access
	//========================================

	/** Get HealthComponent */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	/** Get WeaponComponent */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	/** Get ScholaMocAgent */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UScholaMocAgent* GetScholaAgent() const { return ScholaAgent; }


	//========================================
	// Respawn
	//========================================


	/** Reset character state (called by TeamManager on respawn) */
	UFUNCTION(BlueprintCallable, Category = "Character|Respawn")
	void ResetCharacter();


protected:
	//========================================
	// Event Handlers
	//========================================

	/** Handle death event from HealthComponent */
	UFUNCTION()
	void OnDeath(const FDeathEventData& DeathEvent);

public:
	//========================================
	// Components
	//========================================

	/** Health management */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UHealthComponent* HealthComponent;

	/** Weapon and combat */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UWeaponComponent* WeaponComponent;

	/** RL training interface */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UScholaMocAgent* ScholaAgent;

	/** AI perception registration */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UAIPerceptionStimuliSourceComponent* StimuliSource;

	//========================================
	// Configuration
	//========================================

	/** AI Controller class */
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	TSubclassOf<AAIController> AIControllerClass;

	/** Behavior Tree to run */
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	UBehaviorTree* BehaviorTree;

	/** Vision range for fog-of-war updates (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Vision")
	float VisionRange = 3000.0f; // 30 meters

	//========================================
	// v10.2 Command Interface
	//========================================

	/**
	 * Receive strategy command from Squad Commander
	 * Replaces individual MCTS decision-making in v10.2 architecture
	 *
	 * @param NewStrategy - Strategy assigned by centralized commander
	 */
	UFUNCTION(BlueprintCallable, Category = "Character|Commands")
	void SetCommandedStrategy(EStrategyType NewStrategy);

	/**
	 * Get current commanded strategy (for Blackboard update)
	 */
	UFUNCTION(BlueprintPure, Category = "Character|Commands")
	EStrategyType GetCommandedStrategy() const { return CommandedStrategy; }

	/** Agent ID for team coordination */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Identity")
	int32 AgentID = 0;

protected:
	//========================================
	// Runtime State
	//========================================

	/** Is dead? */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bIsAlive = true;

	/** Current strategy assigned by Squad Commander (v10.2) */
	UPROPERTY(BlueprintReadOnly, Category = "AI|Strategy")
	EStrategyType CommandedStrategy = EStrategyType::Assault;

	/** Reference to Squad Commander (set on spawn) */
	UPROPERTY()
	class ASquadManager* SquadCommander;
};
