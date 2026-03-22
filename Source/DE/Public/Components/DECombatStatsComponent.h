// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DECombatStatsComponent.generated.h"

/**
 * UDECombatStatsComponent — Tracks per-agent combat statistics.
 *
 * Extracted from ADEAgent to follow SRP.
 * Manages: damage dealt/taken, kill count, damage contributors (for assist rewards),
 * and last-damage-instigator tracking (for death attribution).
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class DE_API UDECombatStatsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDECombatStatsComponent();

	//========================================
	// Damage Tracking
	//========================================

	/** Record damage dealt to a victim */
	void NotifyDamageDealt(AActor* Victim, float DamageAmount);

	/** Record a confirmed kill */
	void NotifyKillConfirmed(AActor* Victim, float TotalDamageDealt);

	/** Record damage taken (updates instigator tracking and contributor map) */
	void RecordDamageTaken(float ActualDamage, AActor* DamageInstigator);

	/** Reset all stats for a new episode */
	void ResetStats();

	//========================================
	// Accessors
	//========================================

	UFUNCTION(BlueprintPure, Category = "Combat|Stats")
	float GetTotalDamageTaken() const { return TotalDamageTaken; }

	UFUNCTION(BlueprintPure, Category = "Combat|Stats")
	float GetTotalDamageDealt() const { return TotalDamageDealt; }

	UFUNCTION(BlueprintPure, Category = "Combat|Stats")
	int32 GetKillCount() const { return KillCount; }

	/** Get the last actor that dealt damage (for death attribution) */
	AActor* GetLastDamageInstigator() const { return LastDamageInstigator.Get(); }

	/** Get the last damage amount */
	float GetLastDamageAmount() const { return LastDamageAmount; }

	/** Get damage dealt since last reset (per-step accumulator for reward system) */
	float GetStepDamageDealt() const { return StepDamageDealt; }

	/** Reset per-step damage accumulator (called after reward computation) */
	void ResetStepDamage() { StepDamageDealt = 0.0f; }

	/** Get the damage contributors map (for assist rewards) */
	const TMap<AActor*, float>& GetDamageContributors() const { return DamageContributors; }

	//========================================
	// Delegates
	//========================================

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDamageDealtDel, AActor*, Victim, float, DamageAmount);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnKillConfirmedDel, AActor*, Victim, float, DamageDealt);

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnDamageDealtDel OnDamageDealt_Delegate;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnKillConfirmedDel OnKillConfirmed_Delegate;

private:
	UPROPERTY(BlueprintReadOnly, Category = "Combat|Stats", meta = (AllowPrivateAccess = "true"))
	float TotalDamageTaken = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Stats", meta = (AllowPrivateAccess = "true"))
	float TotalDamageDealt = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|Stats", meta = (AllowPrivateAccess = "true"))
	int32 KillCount = 0;

	TWeakObjectPtr<AActor> LastDamageInstigator;
	float LastDamageAmount = 0.0f;

	TMap<AActor*, float> DamageContributors;

	/** Per-step damage dealt accumulator — reset by ResetStepDamage() after reward computation */
	float StepDamageDealt = 0.0f;
};
