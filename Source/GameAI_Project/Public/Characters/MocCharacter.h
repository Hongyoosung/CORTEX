// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Team/MocTeamInterface.h"
#include "Types/StrategyTypes.h"
#include "Types/GameStateTypes.h"
#include "Types/EQSTypes.h"
#include "Combat/CombatStatsInterface.h"
#include "MocCharacter.generated.h"

// Forward declarations

class UHealthComponent;
class UWeaponComponent;
class UScholaMocAgent;
class UAIPerceptionStimuliSourceComponent;
class UActuatorComponent;
class UBehaviorTree;
class UEnvQuery;
class ASquadManager;
class AMocGameMode;
class ATeamManager;
class AFogOfWarManager;
class UNiagaraComponent;
struct FDeathEventData;
struct FMocTeamInfo;

/**
 * AMocCharacter - MOC v10.2 Component-Based Agent Character
 *
 * Component Architecture:
 * - UHealthComponent: Damage, death, respawn
 * - UWeaponComponent: Firing, ammo, cooldown
 * - UScholaMocAgent: RL training interface
 * - UAIPerceptionStimuliSourceComponent: AI visibility
 *
 * Team Identification:
 * - TeamID property: 0 (Red), 1 (Blue)
 * - Assigned by TeamManager on spawn
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
 * 2. TeamManager spawns and assigns TeamID
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
	virtual float	GetHealthPercentage_Implementation() const override;
	virtual float	Heal_Implementation(float HealAmount) override;
	virtual bool	IsAlive_Implementation() const override;
	virtual float	GetWeaponCooldown_Implementation() const override;
	virtual bool	CanFireWeapon_Implementation() const override;
	virtual int32	AddAmmo_Implementation(int32 AmmoAmount) override;
	virtual float	GetAmmoPercentage_Implementation() const override;
	
	


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

	/** Update Niagara VFX color based on team */
	UFUNCTION(BlueprintCallable, Category = "Character|VFX")
	void UpdateTeamColorVFX();


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

	/** Schola actuator component (wraps TacticalParameterActuator) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UActuatorComponent* TacticalActuatorComponent;

	/** AI perception registration */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UAIPerceptionStimuliSourceComponent* StimuliSource;

	/** Niagara VFX for team color identification */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UNiagaraComponent* TeamColorVFX;

	//========================================
	// Configuration
	//========================================

	/** Behavior Tree to run */
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	UBehaviorTree* BehaviorTree;

	/** Vision range for fog-of-war updates (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Vision")
	float VisionRange; // 30 meters

	//========================================
	// EQS Configuration
	//========================================

	/** EQS Query Template for tactical positioning */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|EQS")
	UEnvQuery* TacticalEQS;

	/** EQS search radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|EQS")
	float EQSSearchRadius = 2000.0f;

	/** EQS result acceptance radius for movement (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|EQS")
	float EQSAcceptanceRadius = 50.0f;

	/** Niagara system asset for team identification VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|VFX")
	class UNiagaraSystem* TeamColorVFXAsset;

	/** VFX color parameter name */
	UPROPERTY(EditAnywhere, Category = "Character|VFX")
	FName VFXColorParameterName = FName("TeamColor");

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


	//========================================
	// v10.2 EQS Weight Storage & Execution
	//========================================

	/**
	 * Update EQS weights from Actuator.
	 * Stores weights on Character (single source of truth).
	 * Does NOT trigger execution - call PerformTacticalAction() separately.
	 *
	 * @param NewWeights - 7-dim EQS weights from RL policy or ONNX inference
	 */
	UFUNCTION(BlueprintCallable, Category = "Character|EQS")
	void UpdateTacticalWeights(const FEQSWeightParameters& NewWeights);

	/** Get current EQS weights (read by EQS queries, Trainer, BT tasks) */
	UFUNCTION(BlueprintPure, Category = "Character|EQS")
	FEQSWeightParameters GetEQSWeights() const { return CurrentEQSWeights; }

	/**
	 * Execute tactical action based on current EQS weights.
	 *
	 * Training Mode (no AIController/BT):
	 *   Runs EQS query SYNCHRONOUSLY using RunInstantQuery(),
	 *   then moves character to best location immediately.
	 *   Returns after movement is initiated, ensuring post-action state
	 *   is available for the next GetObservation() call.
	 *
	 * Runtime Mode (AIController with BT):
	 *   Syncs weights to Blackboard. Behavior Tree handles EQS asynchronously.
	 */
	UFUNCTION(BlueprintCallable, Category = "Character|EQS")
	void PerformTacticalAction();

	/** Get the last EQS target location (for debugging) */
	UFUNCTION(BlueprintPure, Category = "Character|EQS")
	FVector GetLastEQSTargetLocation() const { return LastEQSTargetLocation; }


public:
	/** Agent Info for team coordination */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Identity")
	int32 AgentID;

	/** Team ID (0 = Red, 1 = Blue) - Assigned by TeamManager on spawn */
	UPROPERTY(BlueprintReadOnly, Category = "AI|Identity")
	int32 TeamID;



protected:
	//========================================
	// Runtime State
	//========================================

	/** Is dead? */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bIsAlive;

	/** Current strategy assigned by Squad Commander (v10.2) */
	UPROPERTY(BlueprintReadOnly, Category = "AI|Strategy")
	EStrategyType CommandedStrategy;

	/** Current EQS weights (set by Actuator, read by EQS/Trainer/BT) */
	UPROPERTY(BlueprintReadOnly, Category = "AI|EQS")
	FEQSWeightParameters CurrentEQSWeights;

	/** Last EQS target location (for debugging) */
	UPROPERTY(BlueprintReadOnly, Category = "AI|EQS")
	FVector LastEQSTargetLocation = FVector::ZeroVector;

	/** Reference to Squad Commander (set on spawn) */
	TObjectPtr<ASquadManager> SquadCommander;

	/** Time when character was last spawned/reset (for death diagnostics) */
	float SpawnTime = 0.0f;

	TObjectPtr<AMocGameMode> GameMode;

	TObjectPtr<ATeamManager> TM;

	TObjectPtr<AFogOfWarManager> FogManager;
};
