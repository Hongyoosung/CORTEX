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
#include "Combat/CombatStatsInterface.h"
#include "Combat/Abilities/AttackAbility.h"
#include "Combat/Abilities/HealAbility.h"
#include "MocCharacter.generated.h"

// Forward declarations

class UHealthComponent;
class UScholaMocAgent;
class UMocRewardCalculator;
class UAIPerceptionStimuliSourceComponent;
class UEnvQuery;
class UMocEQSExecutor;
class USquadManager;
class ATeamManager;
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

	/** Get AttackAbility (weapon + combat logic) */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UAttackAbility* GetAttackAbility() const { return AttackAbility; }

	/** Get ScholaMocAgent */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UScholaMocAgent* GetScholaAgent() const { return ScholaAgent; }

	/** Get RewardCalculator */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UMocRewardCalculator* GetRewardCalculator() const { return RewardCalculator; }

	/** Get TeamManager reference (for Trainer observation gathering) */
	UFUNCTION(BlueprintPure, Category = "Character|Team")
	ATeamManager* GetTeamManager() const { return TM; }

	/** Set TeamManager directly (called by TeamManager::SpawnAgent for multi-env support) */
	void SetTeamManager(ATeamManager* InTM) { TM = InTM; }

	// ==================== Reward Interface (forwarding to RewardCalculator) ====================

	/**
	 * Compute a single-step reward for the state transition (Prev → Current).
	 * Delegates to UMocRewardCalculator — MocTrainer calls this, never the calculator directly.
	 */
	float ComputeStepReward(EStrategyType Strategy,
		const FObservation& Prev,
		const FObservation& Current,
		const FEQSWeightParameters& Action);

	/**
	 * Decompose the per-step reward into labelled components (for debug/logging).
	 */
	FRewardBreakdown ComputeRewardBreakdown(EStrategyType Strategy,
		const FObservation& Prev,
		const FObservation& Current) const;

	/**
	 * Reset per-episode reward state (cumulative reward, momentum counters).
	 * Called by MocTrainer::ResetTrainer().
	 */
	void ResetRewardState();


	//========================================
	// Respawn
	//========================================


	/** Reset character state (called by TeamManager on respawn) */
	UFUNCTION(BlueprintCallable, Category = "Character|Respawn")
	void ResetCharacter();

	/** Update Niagara VFX color based on team */
	UFUNCTION(BlueprintCallable, Category = "Character|VFX")
	void UpdateTeamColorVFX();


	// ==================== Heal Interface (for MocRewardCalculator) ====================

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

public:
	//========================================
	// Components
	//========================================

	/** Health management */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UHealthComponent* HealthComponent;

	/** RL training interface */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UScholaMocAgent* ScholaAgent;

	/** Strategy-conditioned reward calculator */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UMocRewardCalculator* RewardCalculator;

	/** AI perception registration */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UAIPerceptionStimuliSourceComponent* StimuliSource;

	/** Niagara VFX for team color identification */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UNiagaraComponent* TeamColorVFX;

	//========================================
	// Configuration
	//========================================

	/** Vision range for fog-of-war updates (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Vision")
	float VisionRange; // 30 meters

	//========================================
	// EQS Configuration
	//========================================

	/** Attack ability component - encapsulates HandleCombat() logic */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UAttackAbility* AttackAbility;

	/** Heal ability component - encapsulates TickSupportHealing() logic */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UHealAbility* HealAbility;

	/** EQS Executor component - handles query execution with correct parameter names */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UMocEQSExecutor* EQSExecutor;

	/** EQS result acceptance radius for movement (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|EQS")
	float EQSAcceptanceRadius;

	/** Niagara system asset for team identification VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|VFX")
	UNiagaraSystem* TeamColorVFXAsset;

	/** VFX color parameter name */
	UPROPERTY(EditAnywhere, Category = "Character|VFX")
	FName VFXColorParameterName;

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

	/** Returns true if weights were updated since last consume, then clears the flag */
	bool ConsumeNewWeights() { bool b = bWeightsDirty; bWeightsDirty = false; return b; }

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

	/** Get the last EQS target location (for debugging) */
	UFUNCTION(BlueprintPure, Category = "Character|EQS")
	FVector GetLastEQSTargetLocation() const { return LastEQSTargetLocation; }

	UFUNCTION()
	void ProcessTrainingAbilities();

public:
	/** Agent Info for team coordination */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Identity")
	int32 AgentID;

	/** Team ID (0 = Red, 1 = Blue) - Assigned by TeamManager on spawn, or set directly in editor for test maps */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Identity")
	int32 TeamID;

	/** Environment ID for parallel environment isolation. Set by TeamManager on spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Identity")
	int32 EnvID = 0;

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

	/** True when Actuator wrote new weights that haven't been consumed yet */
	bool bWeightsDirty;

	/** Last EQS target location (for debugging) */
	UPROPERTY(BlueprintReadOnly, Category = "AI|EQS")
	FVector LastEQSTargetLocation;

	/** Reference to Squad Commander (set on spawn) */
	USquadManager* SquadCommander;

	/** Time when character was last spawned/reset (for death diagnostics) */
	float SpawnTime;

	TObjectPtr<ATeamManager> TM;

	FTimerHandle TrainingAbilityTimerHandle;

	//========================================
	// Strategy Stat Modifiers
	//========================================

	/** Base stats cached at BeginPlay — used to restore after strategy change */
	float BaseAttackDamage;
	float BaseAttackRange;
	float BaseMaxHealth;

	/** Apply strategy-specific stat multipliers (Support: -50% damage/range, -30% max HP) */
	void ApplyStrategyStatModifiers(EStrategyType Strategy);
};
