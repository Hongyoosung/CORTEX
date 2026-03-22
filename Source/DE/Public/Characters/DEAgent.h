// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayEffect.h"
#include "Team/DETeamInterface.h"
#include "Types/DEClassTypes.h"
#include "Types/DEGameStateTypes.h"
#include "Types/DEEQSTypes.h"
#include "Types/DEObservationTypes.h"
#include "Types/DERewardTypes.h"
#include "Types/DEEventTypes.h"
#include "DEAgent.generated.h"

class ADEMatchManager;
class AAIController;
class ADEAgent;
class ADECapturePoint;

class UAbilitySystemComponent;
class UDEAttributeSet;
class UDEGA_Attack;
class UDEGA_Heal;
class UDEAbilityData;
class UAIPerceptionStimuliSourceComponent;
class USpringArmComponent;
class UCameraComponent;
class UEnvQuery;
class UNiagaraComponent;
class UNiagaraSystem;
class UWidgetComponent;
class UDERewardSubsystem;
class UDEScholaAgent;
class UDECombatStatsComponent;
class UDynamicEQSExecutor;
class UDETeamData;
class UDEClassData;

struct FDEDeathEventData;
struct FDETeamInfo;

/**
 * ADEAgent - GAS-Integrated Agent Character
 *
 * Component Architecture:
 * - UAbilitySystemComponent: GAS ability management, effects, tags
 * - UDEAttributeSet: Health, MaxHealth, Armor (replaces DEHealthComponent)
 * - UDEGA_Attack: GAS-based attack ability (replaces DEAttackAbility)
 * - UDEGA_Heal: GAS-based heal ability (replaces DEHealAbility)
 * - UDEScholaAgent: RL training interface
 * - UAIPerceptionStimuliSourceComponent: AI visibility
 *
 * Team Identification:
 * - TeamID property: 0 (Red), 1 (Blue)
 * - Assigned by DEMatchManager on spawn
 *
 * Death/Respawn Flow:
 * 1. AttributeSet Health reaches 0 → State.Dead tag applied
 * 2. ADEAgent::OnHealthChanged() detects death
 * 3. Disable movement, physics ragdoll
 * 4. Notify GameMode → DEMatchManager
 * 5. DEMatchManager queues respawn (5 seconds)
 * 6. DEMatchManager calls ResetCharacter()
 * 7. Re-enable movement, restart AI
 *
 * AI Integration:
 * - Abilities activated via TryActivateAbilityByTag (no input binding)
 * - BT tasks call TryActivateAbilityByTag(Ability.Attack) etc.
 */


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAgentDeathEvent, const FDEDeathEventData&, DeathEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAgentDied, ADEAgent*, DeadAgent, ADEAgent*, Killer);


UCLASS()
class DE_API ADEAgent : public ACharacter, public IAbilitySystemInterface, public IDETeamInterface
{
	GENERATED_BODY()

public:
	ADEAgent();

	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;


	//========================================
	// IAbilitySystemInterface
	//========================================
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;


	//========================================
	// ID Interfaces Implementations
	//========================================
	virtual int32	GetTeamID_Implementation() const override;
	virtual int32	GetEnvID_Implementation() const override;
	virtual void	SetTeamID_Implementation(int32 NewTeamID) override;
	virtual void	SetEnvID_Implementation(int32 NewEnvID) override;


	//========================================
	// GAS Health Queries (replace IDECombatStatsInterface)
	//========================================

	/** Get current health percentage (0.0 - 1.0) */
	UFUNCTION(BlueprintPure, Category = "Character|Health")
	float GetHealthPercentage() const;

	/** Is the character alive? */
	UFUNCTION(BlueprintPure, Category = "Character|Health")
	bool IsAlive() const { return bIsAlive; }


	/** Heal the character via GAS effect */
	UFUNCTION(BlueprintCallable, Category = "Character|Health")
	float HealCharacter(float HealAmount);

	/** Apply damage via GAS meta-attribute */
	UFUNCTION(BlueprintCallable, Category = "Character|Health")
	float ApplyDamageToSelf(float DamageAmount, AActor* DamageInstigator, AActor* DamageCauser,
		const FVector& HitLocation = FVector::ZeroVector, const FVector& HitNormal = FVector::ZeroVector);

	/** Get current health */
	UFUNCTION(BlueprintPure, Category = "Character|Health")
	float GetCurrentHealth() const;

	/** Get max health */
	UFUNCTION(BlueprintPure, Category = "Character|Health")
	float GetMaxHealth() const;

	/** Current mana as a fraction [0, 1] */
	UFUNCTION(BlueprintPure, Category = "Character|Mana")
	float GetManaPercentage() const;

	/** Weapon cooldown progress (0=just fired, 1=ready) */
	UFUNCTION(BlueprintPure, Category = "Character|Combat")
	float GetWeaponCooldown() const;

	/** Can fire weapon? */
	UFUNCTION(BlueprintPure, Category = "Character|Combat")
	bool CanFireWeapon() const;

	/** Add ammo */
	UFUNCTION(BlueprintCallable, Category = "Character|Combat")
	int32 AddAmmo(int32 AmmoAmount);

	/** Get ammo percentage (0.0-1.0) */
	UFUNCTION(BlueprintPure, Category = "Character|Combat")
	float GetAmmoPercentage() const;


	//========================================
	// GAS Ability Access
	//========================================

	/** Get the GAS Attack ability instance */
	UFUNCTION(BlueprintPure, Category = "Character|Abilities")
	UDEGA_Attack* GetAttackAbility() const { return AttackAbility; }

	/** Get the GAS Heal ability instance */
	UFUNCTION(BlueprintPure, Category = "Character|Abilities")
	UDEGA_Heal* GetHealAbility() const { return HealAbility; }

	/** Get DEScholaAgent */
	UFUNCTION(BlueprintPure, Category = "Character|Components")
	UDEScholaAgent* GetScholaAgent() const { return ScholaAgent.Get(); }


	//========================================
	// Tactical Action Interface (EQS weight management and execution)
	//========================================

	UFUNCTION(BlueprintCallable, Category = "Character|EQS")
	void UpdateTacticalWeights(const FDEEQSWeightParameters& NewWeights);

	UFUNCTION(BlueprintCallable, Category = "Character|EQS")
	void PerformTacticalAction();

	void ProcessTrainingAbilities();

	UFUNCTION(BlueprintPure, Category = "Character|EQS")
	FORCEINLINE FDEEQSWeightParameters	GetEQSWeights() const
	{ return CurrentEQSWeights; }

	UFUNCTION(BlueprintPure, Category = "Character|EQS")
	FORCEINLINE FVector					GetLastEQSTargetLocation() const
	{ return LastEQSTargetLocation; }

	FORCEINLINE bool					ConsumeNewWeights()
	{ bool b = bWeightsDirty; bWeightsDirty = false; return b; }


	//========================================
	// Command Interface
	//========================================

	UFUNCTION(BlueprintCallable, Category = "Character|Commands")
	void SetCommandedClass(EDEClassType NewClass);

	UFUNCTION(BlueprintPure, Category = "Character|Commands")
	EDEClassType GetCommandedClass() const;

	/** Returns the UDEClassData for the current commanded class, or null if unset. */
	UFUNCTION(BlueprintPure, Category = "Character|Data")
	UDEClassData* GetCurrentClassData() const;

	/** Returns the team display name from TeamData (e.g. "Red Team"). Empty string if TeamData is unset. */
	UFUNCTION(BlueprintPure, Category = "Character|Team")
	FString GetTeamName() const;

	/** Returns the team primary color from TeamData. Falls back to TeamColor if TeamData is unset. */
	UFUNCTION(BlueprintPure, Category = "Character|Team")
	FLinearColor GetTeamColor() const;

	/**
	 * Apply the mesh, animation blueprint, and stat overrides from TeamData
	 * that correspond to the given class.  Falls back to team-level defaults
	 * for any field left unset in the override struct.
	 * Called automatically by SetCommandedClass; can also be called directly
	 * (e.g. after manually setting TeamData at spawn time).
	 */
	UFUNCTION(BlueprintCallable, Category = "Character|Appearance")
	void ApplyClassAppearance(EDEClassType Class);


	//========================================
	// Reward Interface (forwarding to RewardCalculator)
	//========================================

	float ComputeStepReward(EDEClassType Class,
		const FDEAgentSnapshot& Prev,
		const FDEAgentSnapshot& Current,
		const FDEEQSWeightParameters& Action);


	//========================================
	// Match Access
	//========================================
	UFUNCTION(BlueprintPure, Category = "Character|Match")
	ADEMatchManager* GetMatchManager() const;

	UFUNCTION(BlueprintCallable, Category = "Character|Respawn")
	void ResetCharacter();
	void Activate();
	void Deactivate();


	//========================================
	// Heal Interface (for DERewardCalculator)
	//========================================

	float GetLastTickHealAmount() const;
	bool ConsumeHealBurst(float Threshold);


	//========================================
	// Damage Event System (replaces DEHealthComponent delegates)
	//========================================

	/** Notify that this character dealt damage to another actor (for stats/rewards) */
	void NotifyDamageDealt(AActor* Victim, float DamageAmount);

	/** Notify that this character confirmed a kill */
	void NotifyKillConfirmed(AActor* Victim, float TotalDamageDealt);


protected:
	//========================================
	// Event Handlers
	//========================================

	/** Called when Health attribute changes — handles death detection */
	void OnHealthChanged(const struct FOnAttributeChangeData& Data);

	/** Handle death */
	void HandleDeath(AActor* Killer, float FinalDamage);


public:

	//========================================
	// Reward System
	//========================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Rewards")
	FDERewardState RewardState;


	//========================================
	// Overhead HUD Widget
	//========================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|UI")
	TObjectPtr<UWidgetComponent> OverheadWidgetComponent;


	//========================================
	// Camera (Spectator Mode)
	//========================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Camera")
	TObjectPtr<USpringArmComponent> CameraSpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Camera")
	TObjectPtr<UCameraComponent> AgentCamera;

	UPROPERTY(BlueprintReadOnly, Category = "Debug")
	bool bIsBeingObserved = false;


	//========================================
	// GAS Components
	//========================================

	/** Ability System Component — manages abilities, effects, tags */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|GAS")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	/** Attribute Set — Health, MaxHealth, Armor, meta-attributes */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|GAS")
	TObjectPtr<UDEAttributeSet> AttributeSet;


	//========================================
	// Other Components
	//========================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDEScholaAgent> ScholaAgent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UAIPerceptionStimuliSourceComponent> StimuliSource = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDynamicEQSExecutor> EQSExecutor = nullptr;

	/** Combat statistics tracking (damage, kills, assists) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Combat")
	TObjectPtr<UDECombatStatsComponent> CombatStats;


	//========================================
	// Ability Configuration
	//========================================

	/** Data asset containing attack/heal configs (assigned in Blueprint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Abilities|Config")
	TObjectPtr<UDEAbilityData> AbilityData;

	/**
	 * Team appearance data asset assigned by DEMatchManager at spawn time.
	 * Used by ApplyClassAppearance() to look up per-class mesh/anim/stat overrides.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "AI|Identity")
	TObjectPtr<UDETeamData> TeamData;



	//========================================
	// Configuration
	//========================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Vision")
	float VisionRange = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Identity")
	int32 AgentID = 0;

	FLinearColor TeamColor;


	//========================================
	// EQS Configuration
	//========================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|EQS")
	float EQSAcceptanceRadius = 50.0f;


	//========================================
	// CapturePoints
	//========================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Environment")
	TArray<ADECapturePoint*> AssignedCapturePoints;

	/**
	 * Index into ADEMatchManager::GetCapturePoints() for the base assigned
	 * by the Squad Commander. Written by AssignBasesToAgents() and mirrored
	 * in the Blackboard key "AssignedBaseIndex".
	 * -1 = no assignment.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "AI|Squad")
	int32 AssignedBaseIndex = -1;


	//========================================
	// Damage Stats (delegated to UDECombatStatsComponent)
	//========================================

	/** BP-compatible adapter: total damage taken */
	UFUNCTION(BlueprintPure, Category = "Combat|Stats")
	float GetTotalDamageTaken() const;

	/** BP-compatible adapter: total damage dealt */
	UFUNCTION(BlueprintPure, Category = "Combat|Stats")
	float GetTotalDamageDealt() const;

	/** BP-compatible adapter: kill count */
	UFUNCTION(BlueprintPure, Category = "Combat|Stats")
	int32 GetKillCount() const;

	/** Tracks cumulative damage dealt by each attacker (for assist rewards) */
	const TMap<AActor*, float>& GetDamageContributors() const;

	/** Per-step damage dealt (for Strike in-range hit reward) */
	float GetStepDamageDealt() const;
	void ResetStepDamage();


protected:
	/** Cached match manager — resolved once in BeginPlay, avoids per-step GetAllActorsOfClass scan */
	UPROPERTY()
	TObjectPtr<ADEMatchManager> CachedMatchManager;

	TObjectPtr<UDERewardSubsystem> RewardSubsystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Identity")
	int32 TeamID = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Identity")
	int32 EnvID = 0;


	//========================================
	// Runtime State
	//========================================

	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bIsAlive = true;

	bool bWeightsDirty = false;
	float SpawnTime = 0.0f;
	int32 TacticalActionCallCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AI|EQS")
	FVector LastEQSTargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AI|EQS")
	FDEEQSWeightParameters CurrentEQSWeights;

	FTimerHandle TrainingAbilityTimerHandle;
	FTimerHandle ManaRegenTimerHandle;

	/** Cached AI controller — set in PossessedBy, avoids per-action Cast */
	TWeakObjectPtr<AAIController> CachedAIController;


private:
	/** GAS ability instances (created in BeginPlay from AbilityData) */
	UPROPERTY(Transient)
	TObjectPtr<UDEGA_Attack> AttackAbility;

	UPROPERTY(Transient)
	TObjectPtr<UDEGA_Heal> HealAbility;

	/** Has died flag (prevents double-death) */
	bool bHasDied = false;

	/** Initialize GAS abilities from AbilityData */
	void InitializeGASAbilities();

	/** One-time world scan to find the MatchManager matching this agent's EnvID */
	ADEMatchManager* FindMatchManagerForEnv() const;


public:
	//============= Event Delegates ===================

	FOnAgentDeathEvent	OnAgentDeathEvent_Delegate;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnAgentDied		OnAgentDied_Delegate;

	// Compatibility delegates (for systems that subscribed to DEHealthComponent events)
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDamageTakenDel, const FDEDamageEventData&, DamageEvent, float, CurrentHealth);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChangedDel, float, CurrentHealth, float, MaxHealth);

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnDamageTakenDel OnDamageTaken_Delegate;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnHealthChangedDel OnHealthChanged_Delegate;
};
