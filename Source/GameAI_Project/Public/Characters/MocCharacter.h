// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Team/MocTeamInterface.h"
#include "Types/StrategyTypes.h"
#include "Types/GameStateTypes.h"
#include "Types/EQSTypes.h"
#include "Types/ObservationTypes.h"
#include "Types/RewardTypes.h"
#include "Types/EventTypes.h"
#include "Combat/Components/MocAbilityComponent.h"
#include "Combat/CombatStatsInterface.h"
#include "Team/MatchManager.h"
#include "MocCharacter.generated.h"

// Forward declarations

class UMocRewardSubsystem;
class UHealthComponent;
class UScholaMocAgent;
class UAIPerceptionStimuliSourceComponent;
class UEnvQuery;
class UMocEQSExecutor;
class AFogOfWarManager;
class UNiagaraComponent;
class UNiagaraSystem;
struct FDeathEventData;
struct FMocTeamInfo;

/**
 * AMocCharacter - MOC v10.2 Component-Based Agent Character
 *
 * Component Architecture:
 * - UHealthComponent: Damage, death, respawn
 * - UAttackAbility: Firing, ammo, cooldown, target scanning
 * - UScholaMocAgent: RL training interface
 * - UAIPerceptionStimuliSourceComponent: AI visibility
 *
 * Team Identification:
 * - TeamID property: 0 (Red), 1 (Blue)
 * - Assigned by MatchManager on spawn
 *
 * Death/Respawn Flow:
 * 1. HealthComponent broadcasts OnDeath
 * 2. MocCharacter::OnDeath() called
 * 3. Disable movement, physics ragdoll
 * 4. Notify GameMode → MatchManager
 * 5. MatchManager queues respawn (5 seconds)
 * 6. MatchManager calls ResetCharacter()
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
 * 2. MatchManager spawns and assigns TeamID
 * 3. AI takes over automatically
 */


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAgentDeathEvent, const FDeathEventData&, DeathEvent);


UCLASS()
class GAMEAI_PROJECT_API AMocCharacter : public ACharacter, public IMocTeamInterface, public ICombatStatsInterface
{
	GENERATED_BODY()

public:
	AMocCharacter();

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

	/** Get HealthComponent */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	/** Get AbilityComponent (manages all agent abilities) */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UMocAbilityComponent* GetAbilityComponent() const { return AbilityComponent; }

	/** Get ScholaMocAgent */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UScholaMocAgent* GetScholaAgent() const { return ScholaAgent; }



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
	void UpdateTacticalWeights(const FEQSWeightParameters& NewWeights);

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
	FEQSWeightParameters GetEQSWeights() const { return CurrentEQSWeights; }

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
	void SetCommandedStrategy(EStrategyType NewStrategy);

	/**
	 * Get current commanded strategy (for Blackboard update)
	 */
	UFUNCTION(BlueprintPure, Category = "Character|Commands")
	EStrategyType GetCommandedStrategy() const { return CommandedStrategy; }


	//========================================
	// Reward Interface (forwarding to RewardCalculator)
	//========================================

	/**
	 * Compute a single-step reward for the state transition (Prev → Current).
	 * Delegates to UMocRewardCalculator — MocTrainer calls this, never the calculator directly.
	 */
	float ComputeStepReward(EStrategyType Strategy,
		const FObservation& Prev,
		const FObservation& Current,
		const FEQSWeightParameters& Action);



	//========================================
	// Match Access
	//========================================
	UFUNCTION(BlueprintPure, Category = "Character|Match")
	AMatchManager* GetMatchManager() const;

	/** Reset character state (called by MatchManager on respawn) */
	UFUNCTION(BlueprintCallable, Category = "Character|Respawn")
	void ResetCharacter();

	void Activate();
	void Deactivate();



	//========================================
	// Heal Interface (for MocRewardCalculator)
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

	/** Handle death event from HealthComponent */
	UFUNCTION()
	void OnDeath(const FDeathEventData& DeathEvent);


	//========================================
	// Strategy-specific
	//========================================
	/** Apply strategy-specific stat multipliers (Support: -50% damage/range, -30% max HP) */
	void ApplyStrategyStatModifiers(EStrategyType Strategy);

	


public:

	//========================================
	// Reward System
	//========================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Rewards")
	URewardSettingsDataAsset* RewardSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Rewards")
	FRewardState RewardState;


	//========================================
	// Components
	//========================================

	/** Health management */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UHealthComponent* HealthComponent;

	/** RL training interface */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UScholaMocAgent* ScholaAgent;

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
	UMocAbilityComponent* AbilityComponent;

	/** EQS Executor component - handles query execution with correct parameter names */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UMocEQSExecutor* EQSExecutor;

	/** EQS result acceptance radius for movement (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|EQS")
	float EQSAcceptanceRadius;



	//========================================
	// CapturePoints
	//========================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "AI|Environment")
	TArray<ACapturePoint*> AssignedCapturePoints;





protected:
	TObjectPtr<UMocRewardSubsystem> RewardSubsystem;
	
	/** Team ID (0 = Red, 1 = Blue) - Assigned by MatchManager on spawn, or set directly in editor for test maps */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Identity")
	int32 TeamID;

	/** Environment ID for parallel environment isolation. Set by MatchManager on spawn. */
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
	EStrategyType CommandedStrategy;

	/** Current EQS weights (set by Actuator, read by EQS/Trainer/BT) */
	UPROPERTY(BlueprintReadOnly, Category = "AI|EQS")
	FEQSWeightParameters CurrentEQSWeights;

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

};
