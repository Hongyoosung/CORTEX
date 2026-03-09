// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DEEventTypes.generated.h"


UENUM(BlueprintType)
enum class EDECriticalEventType : uint8
{
	AllyKilled UMETA(DisplayName = "Ally Killed"),
	EnemyKilled UMETA(DisplayName = "Enemy Killed"),
	ObjectiveCaptured UMETA(DisplayName = "Objective Captured"),
	ObjectiveLost UMETA(DisplayName = "Objective Lost"),
	HealthCritical UMETA(DisplayName = "Team Health Critical"), // Team average < 30%
	COUNT UMETA(Hidden)
};


/**
 * Damage event data
 */
USTRUCT(BlueprintType)
struct FDEDamageEventData
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

	FDEDamageEventData() {}

	FDEDamageEventData(AActor* InInstigator, AActor* InDamageCauser, float InDamageAmount,
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
struct FDEDeathEventData
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

	FDEDeathEventData() {}

	FDEDeathEventData(AActor* InDeadActor, AActor* InKiller, float InFinalDamage, float InTimeOfDeath)
		: DeadActor(InDeadActor)
		, Killer(InKiller)
		, FinalDamage(InFinalDamage)
		, TimeOfDeath(InTimeOfDeath)
	{}
};