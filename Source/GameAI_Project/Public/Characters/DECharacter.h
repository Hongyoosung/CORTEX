// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Team/DETeamInterface.h"
#include "Types/DEStrategyTypes.h"
#include "Types/DEGameStateTypes.h"
#include "Types/DEEQSTypes.h"
#include "Types/DEObservationTypes.h"
#include "Types/DERewardTypes.h"
#include "Types/DEEventTypes.h"
#include "Combat/Components/DEAbilityComponent.h"
#include "Combat/DECombatStatsInterface.h"
#include "Team/DEMatchManager.h"
#include "DECharacter.generated.h"

// Forward declarations

class UDERewardSubsystem;
class UDEHealthComponent;
class UDEScholaAgent;
class UAIPerceptionStimuliSourceComponent;
class UDEEQSExecutor;

class UEnvQuery;
class ADEFogOfWarManager;
class UNiagaraComponent;
class UNiagaraSystem;
struct FDEDeathEventData;
struct FDETeamInfo;

/**
 * ADECharacter - MOC v10.2 Component-Based Agent Character
 *
 * Component Architecture:
 * - UDEHealthComponent: Damage, death, respawn
 * - UAttackAbility: Firing, ammo, cooldown, target scanning
 * - UDEScholaAgent: RL training interface
 * - UAIPerceptionStimuliSourceComponent: AI visibility
 *
 * Team Identification:
 * - TeamID property: 0 (Red), 1 (Blue)
 * - Assigned by DEMatchManager on spawn
 *
 * Death/Respawn Flow:
 * 1. DEHealthComponent broadcasts OnDeath
 * 2. DECharacter::OnDeath() called
 * 3. Disable movement, physics ragdoll
 * 4. Notify GameMode → DEMatchManager
 * 5. DEMatchManager queues respawn (5 seconds)
 * 6. DEMatchManager calls ResetCharacter()
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
 * 2. DEMatchManager spawns and assigns TeamID
 * 3. AI takes over automatically
 */


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAgentDeathEvent, const FDEDeathEventData&, DeathEvent);

class ADECharacter;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAgentDied, ADECharacter*, DeadAgent, ADECharacter*, Killer);


UCLASS()
class GAMEAI_PROJECT_API ADECharacter : public ACharacter, public IDETeamInterface, public IDECombatStatsInterface
{
	GENERATED_BODY()

public:
	ADECharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;


	//========================================
	// ID Interfaces Implementions
	//========================================
	virtual int32 	GetTeamID_Implementation() const override;
	virtual int32 	GetEnvID_Implementation() const override;
	virtual void 	SetTeamID_Implementation(int32 NewTeamID) override;
	virtual void 	SetEnvID_Implementation(int32 NewEnvID) override;


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

	/** Get DEHealthComponent */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UDEHealthComponent* GetHealthComponent() const { return DEHealthComponent; }

	/** Get AbilityComponent (manages all agent abilities) */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UDEAbilityComponent* GetAbilityComponent() const { return AbilityComponent; }

	/** Get DEScholaAgent */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UDEScholaAgent* GetScholaAgent() const { return ScholaAgent; }



	//========================================
	// Tatical Action Interface (EQS weight management and execution)
	//========================================

	/**
	 * Update EQS weights from Actuator.
	 * Stores weights on Character (single source of truth).
	 * Does NOT trigger execution - call PerformTacticalAction() separately.
	 *
	 * @param NewWeights - 7-dim EQS weights from RL policy or ONNX inference
	 */
	UFUNCTION(BlueprintCallable, Category = "Character|EQS")
	void UpdateTacticalWeights(const FDEEQSWeightParameters& NewWeights);

	/**
	 * Execute tactical action based on current EQS weights.
	 *
	 * Training Mode (AIController without Blackboard):
	 *   Runs EQS query SYNCHRONOUSLY using RunInstantQuery(),
	 *   then commands AIController->MoveToLocation() to navigate to best position.
	 *   Movement respects MaxWalkSpeed (600 cm/s) and physics.
	 *   Agent walks to target using pathfinding (NOT teleportation).
	 *
	 * Runtime Mode (AIController with Blackboard/BT):
	 *   Syncs weights to Blackboard. Behavior Tree handles EQS asynchronously.
	 */
	UFUNCTION(BlueprintCallable, Category = "Character|EQS")
	void PerformTacticalAction();

	/** Get current EQS weights (read by EQS queries, Trainer, BT tasks) */
	UFUNCTION(BlueprintPure, Category = "Character|EQS")
	FDEEQSWeightParameters GetEQSWeights() const { return CurrentEQSWeights; }

	/** Returns true if weights were updated since last consume, then clears the flag */
	bool ConsumeNewWeights() { bool b = bWeightsDirty; bWeightsDirty = false; return b; }

	/** Get the last EQS target location (for debugging) */
	UFUNCTION(BlueprintPure, Category = "Character|EQS")
	FVector GetLastEQSTargetLocation() const { return LastEQSTargetLocation; }

	UFUNCTION()
	void ProcessTrainingAbilities();


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
	void SetCommandedStrategy(EDEStrategyType NewStrategy);

	/**
	 * Get current commanded strategy (for Blackboard update)
	 */
	UFUNCTION(BlueprintPure, Category = "Character|Commands")
	EDEStrategyType GetCommandedStrategy() const { return CommandedStrategy; }


	//========================================
	// Reward Interface (forwarding to RewardCalculator)
	//========================================

	/**
	 * Compute a single-step reward for the state transition (Prev → Current).
	 * Delegates to UDERewardCalculator — DETrainer calls this, never the calculator directly.
	 */
	float ComputeStepReward(EDEStrategyType Strategy,
		const FDEObservation& Prev,
		const FDEObservation& Current,
		const FDEEQSWeightParameters& Action);



	//========================================
	// Match Access
	//========================================
	UFUNCTION(BlueprintPure, Category = "Character|Match")
	ADEMatchManager* GetMatchManager() const;

	/** Reset character state (called by DEMatchManager on respawn) */
	UFUNCTION(BlueprintCallable, Category = "Character|Respawn")
	void ResetCharacter();

	void Activate();
	void Deactivate();



	//========================================
	// Heal Interface (for DERewardCalculator)
	//========================================

	/** HP healed on an ally during the most recent heal tick (pass-through to HealAbility) */
	float GetLastTickHealAmount() const;

	/**
	 * Returns true and resets the accumulator if cumulative heal on the current target
	 * has reached or exceeded Threshold. Pass-through to HealAbility.
	 */
	bool ConsumeHealBurst(float Threshold);





protected:
	//========================================
	// Event Handlers
	//========================================

	/** Handle death event from DEHealthComponent */
	UFUNCTION()
	void OnDeath(const FDEDeathEventData& DeathEvent);


	//========================================
	// Strategy-specific
	//========================================
	/** Apply strategy-specific stat multipliers (Support: -50% damage/range, -30% max HP) */
	void ApplyStrategyStatModifiers(EDEStrategyType Strategy);

	


public:

	//========================================
	// Reward System
	//========================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Rewards")
	FDERewardState RewardState;


	//========================================
	// Components
	//========================================

	/** Health management */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UDEHealthComponent* DEHealthComponent;

	/** RL training interface */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UDEScholaAgent* ScholaAgent;

	/** AI perception registration */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UAIPerceptionStimuliSourceComponent* StimuliSource;



	//========================================
	// Configuration
	//========================================

	/** Vision range for fog-of-war updates (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Vision")
	float VisionRange; // 30 meters

	/** Agent Info for team coordination */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Identity")
	int32 AgentID;

	FLinearColor TeamColor;



	//========================================
	// EQS Configuration
	//========================================

	/** Unified ability component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UDEAbilityComponent* AbilityComponent;

	/** EQS Executor component - handles query execution with correct parameter names */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UDEEQSExecutor* EQSExecutor;

	/** EQS result acceptance radius for movement (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|EQS")
	float EQSAcceptanceRadius;



	//========================================
	// CapturePoints
	//========================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "AI|Environment")
	TArray<ADECapturePoint*> AssignedCapturePoints;





protected:
	TObjectPtr<UDERewardSubsystem> RewardSubsystem;
	
	/** Team ID (0 = Red, 1 = Blue) - Assigned by DEMatchManager on spawn, or set directly in editor for test maps */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Identity")
	int32 TeamID;

	/** Environment ID for parallel environment isolation. Set by DEMatchManager on spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Identity")
	int32 EnvID = 0;



	//========================================
	// Runtime State
	//========================================

	/** Is dead? */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bIsAlive;

	/** Current strategy assigned by Squad Commander (v10.2) */
	UPROPERTY(BlueprintReadOnly, Category = "AI|Strategy")
	EDEStrategyType CommandedStrategy;

	/** Current EQS weights (set by Actuator, read by EQS/Trainer/BT) */
	UPROPERTY(BlueprintReadOnly, Category = "AI|EQS")
	FDEEQSWeightParameters CurrentEQSWeights;

	/** True when Actuator wrote new weights that haven't been consumed yet */
	bool bWeightsDirty;

	/** Last EQS target location (for debugging) */
	UPROPERTY(BlueprintReadOnly, Category = "AI|EQS")
	FVector LastEQSTargetLocation;

	/** Time when character was last spawned/reset (for death diagnostics) */
	float SpawnTime;

	FTimerHandle TrainingAbilityTimerHandle;


	

public:
	//============= Event Delegated ===================

	FOnAgentDeathEvent OnAgentDeathEvent_Delegate;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnAgentDied OnAgentDied_Delegate;

};
