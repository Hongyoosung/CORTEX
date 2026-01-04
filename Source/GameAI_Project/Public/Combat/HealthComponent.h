// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

// Forward declarations
class UWeaponComponent;
class AActor;

/**
 * Damage event data
 */
USTRUCT(BlueprintType)
struct FDamageEventData
{
	GENERATED_BODY()

	/** Who caused the damage */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	AActor* Instigator = nullptr;

	/** Who dealt the damage (e.g., weapon owner) */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	AActor* DamageCauser = nullptr;

	/** Amount of damage dealt */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float DamageAmount = 0.0f;

	/** Hit location in world space */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector HitLocation = FVector::ZeroVector;

	/** Hit normal */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector HitNormal = FVector::ZeroVector;

	/** Was this a critical hit? */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bCriticalHit = false;

	/** Damage type identifier */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FName DamageType = NAME_None;

	FDamageEventData() {}

	FDamageEventData(AActor* InInstigator, AActor* InDamageCauser, float InDamageAmount,
		const FVector& InHitLocation = FVector::ZeroVector, const FVector& InHitNormal = FVector::ZeroVector)
		: Instigator(InInstigator)
		, DamageCauser(InDamageCauser)
		, DamageAmount(InDamageAmount)
		, HitLocation(InHitLocation)
		, HitNormal(InHitNormal)
	{}
};

/**
 * Death event data
 */
USTRUCT(BlueprintType)
struct FDeathEventData
{
	GENERATED_BODY()

	/** The actor that died */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	AActor* DeadActor = nullptr;

	/** Who killed this actor */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	AActor* Killer = nullptr;

	/** Final damage amount that caused death */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float FinalDamage = 0.0f;

	/** Time of death */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float TimeOfDeath = 0.0f;

	FDeathEventData() {}

	FDeathEventData(AActor* InDeadActor, AActor* InKiller, float InFinalDamage, float InTimeOfDeath)
		: DeadActor(InDeadActor)
		, Killer(InKiller)
		, FinalDamage(InFinalDamage)
		, TimeOfDeath(InTimeOfDeath)
	{}
};

/**
 * Delegates for health events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDamageTaken, const FDamageEventData&, DamageEvent, float, CurrentHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDamageDealt, AActor*, Victim, float, DamageAmount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeath, const FDeathEventData&, DeathEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChanged, float, CurrentHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnKillConfirmed, AActor*, Victim, float, DamageDealt);

/**
 * UHealthComponent - AAA-Level Health Management System
 *
 * Features:
 * - Robust damage handling with validation
 * - Death state management
 * - Damage mitigation (armor, shields)
 * - Healing support
 * - Comprehensive event system for RL integration
 * - Thread-safe damage queuing
 * - Network replication ready (stub)
 *
 * v5.0 RL Reward Integration Architecture:
 * 1. HealthComponent broadcasts combat events (OnDamageTaken, OnDamageDealt, OnDeath, OnKillConfirmed)
 * 2. FollowerAgentComponent subscribes to these events
 * 3. FollowerAgentComponent forwards events to RewardCalculator
 * 4. RewardCalculator applies strategy-specific reward logic (Assault/Defend/Support/Retreat)
 * 5. FollowerAgentComponent aggregates rewards each tick and sends to RL training
 *
 * IMPORTANT: HealthComponent should NEVER directly call RewardCalculator to avoid double-counting!
 * The event broadcast pattern ensures proper separation of concerns.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//--------------------------------------------------------------------------
	// DAMAGE HANDLING
	//--------------------------------------------------------------------------

	/**
	 * Apply damage to this actor
	 * @param DamageAmount Raw damage amount
	 * @param Instigator Actor who initiated the damage
	 * @param DamageCauser Actor/component that dealt the damage
	 * @param HitLocation Location of hit in world space
	 * @param HitNormal Normal of hit surface
	 * @return Actual damage dealt after mitigation
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	float TakeDamage(float DamageAmount, AActor* Instigator, AActor* DamageCauser,
		const FVector& HitLocation = FVector::ZeroVector, const FVector& HitNormal = FVector::ZeroVector);

	/**
	 * Notify this component that it dealt damage to another actor
	 * Called by WeaponComponent when projectile hits
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	void NotifyDamageDealt(AActor* Victim, float DamageAmount);

	/**
	 * Notify this component that it killed another actor
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	void NotifyKillConfirmed(AActor* Victim, float InTotalDamageDealt);

	//--------------------------------------------------------------------------
	// HEALTH MANAGEMENT
	//--------------------------------------------------------------------------

	/** Heal this actor */
	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	float Heal(float HealAmount);

	/** Set health to a specific value */
	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	void SetHealth(float NewHealth);

	/** Reset health to max (e.g., respawn) */
	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	void ResetHealth();

	/** Kill this actor instantly */
	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	void Kill(AActor* Killer = nullptr);

	//--------------------------------------------------------------------------
	// QUERIES
	//--------------------------------------------------------------------------

	/** Get current health */
	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	/** Get maximum health */
	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	float GetMaxHealth() const { return MaxHealth; }

	/** Get health percentage (0.0 - 1.0) */
	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	float GetHealthPercentage() const { return MaxHealth > 0.0f ? CurrentHealth / MaxHealth : 0.0f; }

	/** Is alive? */
	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	bool IsAlive() const { return bIsAlive && CurrentHealth > 0.0f; }

	/** Is dead? */
	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	bool IsDead() const { return !bIsAlive || CurrentHealth <= 0.0f; }

	/** Can take damage? */
	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	bool CanTakeDamage() const { return bIsAlive && !bIsInvulnerable; }

	/** Get time since last damage taken */
	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	float GetTimeSinceLastDamage() const;

	//--------------------------------------------------------------------------
	// DAMAGE MITIGATION
	//--------------------------------------------------------------------------

	/** Set invulnerability */
	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	void SetInvulnerable(bool bInvulnerable) { bIsInvulnerable = bInvulnerable; }

	/** Is invulnerable? */
	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	bool IsInvulnerable() const { return bIsInvulnerable; }

	/** Set armor value (reduces incoming damage) */
	UFUNCTION(BlueprintCallable, Category = "Combat|Health")
	void SetArmor(float NewArmor) { Armor = FMath::Clamp(NewArmor, 0.0f, MaxArmor); }

	/** Get current armor */
	UFUNCTION(BlueprintPure, Category = "Combat|Health")
	float GetArmor() const { return Armor; }

	//--------------------------------------------------------------------------
	// STATS TRACKING
	//--------------------------------------------------------------------------

	/** Get total damage taken this life */
	UFUNCTION(BlueprintPure, Category = "Combat|Stats")
	float GetTotalDamageTaken() const { return TotalDamageTaken; }

	/** Get total damage dealt to others */
	UFUNCTION(BlueprintPure, Category = "Combat|Stats")
	float GetTotalDamageDealt() const { return TotalDamageDealt; }

	/** Get kill count */
	UFUNCTION(BlueprintPure, Category = "Combat|Stats")
	int32 GetKillCount() const { return KillCount; }

	/** Reset combat stats */
	UFUNCTION(BlueprintCallable, Category = "Combat|Stats")
	void ResetCombatStats();

public:
	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** Maximum health */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "1.0", ClampMax = "10000.0"))
	float MaxHealth = 100.0f;

	/** Starting health (set to MaxHealth if 0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
	float StartingHealth = 0.0f;

	/** Maximum armor value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float MaxArmor = 50.0f;

	/** Starting armor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float StartingArmor = 0.0f;

	/** Armor damage reduction (0.0 - 1.0, where 1.0 = 100% reduction per armor point) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ArmorEffectiveness = 0.01f;

	/** Regenerate health over time? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config")
	bool bEnableHealthRegen = false;

	/** Health regen rate (HP per second) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (EditCondition = "bEnableHealthRegen", ClampMin = "0.0"))
	float HealthRegenRate = 5.0f;

	/** Delay before health regen starts after taking damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (EditCondition = "bEnableHealthRegen", ClampMin = "0.0"))
	float HealthRegenDelay = 3.0f;

	/** Start invulnerable? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config")
	bool bStartInvulnerable = false;

	/** Enable debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Debug")
	bool bEnableDebugDrawing = false;

	//--------------------------------------------------------------------------
	// STATE
	//--------------------------------------------------------------------------

	/** Current health */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	float CurrentHealth = 100.0f;

	/** Current armor */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	float Armor = 0.0f;

	/** Is alive? */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	bool bIsAlive = true;

	/** Is invulnerable? */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	bool bIsInvulnerable = false;

	/** Last damage event */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	FDamageEventData LastDamageEvent;

	/** Last death event */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	FDeathEventData LastDeathEvent;

	//--------------------------------------------------------------------------
	// STATS
	//--------------------------------------------------------------------------

	/** Total damage taken this life */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Stats")
	float TotalDamageTaken = 0.0f;

	/** Total damage dealt to others */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Stats")
	float TotalDamageDealt = 0.0f;

	/** Number of kills */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Stats")
	int32 KillCount = 0;

	//--------------------------------------------------------------------------
	// EVENTS
	//--------------------------------------------------------------------------

	/** Fired when this actor takes damage */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnDamageTaken OnDamageTaken;

	/** Fired when this actor deals damage to another */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnDamageDealt OnDamageDealt;

	/** Fired when this actor dies */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnDeath OnDeath;

	/** Fired when health changes (damage or heal) */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnHealthChanged OnHealthChanged;

	/** Fired when a kill is confirmed */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnKillConfirmed OnKillConfirmed;

private:
	/** Handle death logic */
	void HandleDeath(AActor* Killer, float FinalDamage);

	/** Apply armor mitigation to damage */
	float ApplyArmorMitigation(float IncomingDamage);

	/** Update health regeneration */
	void UpdateHealthRegen(float DeltaTime);

	/** Draw debug info */
	void DrawDebugInfo();

	/** Time of last damage taken (for regen delay) */
	float TimeOfLastDamage = 0.0f;

	/** Has died this session? (for one-time death logic) */
	bool bHasDied = false;
};
