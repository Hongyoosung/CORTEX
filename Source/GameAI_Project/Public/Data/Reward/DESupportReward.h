// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DESupportReward.generated.h"

class ADECharacter;
class ADECapturePoint;
class UDERewardData;
struct FDERewardState;
struct FDEAgentSnapshot;


USTRUCT(BlueprintType)
struct FDESupportRewardSettings
{
	GENERATED_BODY()

	//========== Combat Properties =============

	UPROPERTY(EditAnywhere, Category = "Combat")
	float SurvivalRewardScale = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float PositionReward = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float HealthBonus = 0.3f;

	/** Support: 0 per kill — kills are irrelevant to role */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float KillRewardScale = 0.0f;

	/** Penalty applied when support agent gets a kill while any ally is below 50% HP. */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float RoleBreakPenalty = 3.0f;

	/** Support: -10 per death (base DeathPenaltyReward=100 × 0.10) */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float DeathScale = 0.10f;


	//========== Ally Proximity Properties =============

	/** Flat bonus per step for being within SupportAllyProximityThreshold of the most-injured ally. */
	UPROPERTY(EditAnywhere, Category = "AllyProximity")
	float AllyProximityBonus = 2.0f;

	/** HP fraction (0-1) below which the most-injured ally is considered to need support. */
	UPROPERTY(EditAnywhere, Category = "AllyProximity")
	float AllyInjuryThreshold = 0.9f;

	/** Per-cm shaping reward for approaching the most-injured ally. */
	UPROPERTY(EditAnywhere, Category = "AllyProximity")
	float AllyApproachReward = 0.05f;


	//========== Capture Properties =============

	/** Support: +2 per capture (base CaptureReward=100 × 0.02) — low to discourage solo capping */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float CaptureRewardScale = 0.02f;

	/** Support: -15 per loss (base LossCaptureReward=-100 × 0.15) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float LossCaptureRewardScale = 0.15f;


	//========== Ally Damage Tracking =============

	/** Per-step decay multiplier for AllyAccumulatedDamage (0–1).
	 *  Lower = shorter damage memory; 0.85 ≈ 6-step half-life. */
	UPROPERTY(EditAnywhere, Category = "AllyProximity", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AllyDamageDecayRate = 0.85f;

	//========== Heal Properties =============

	/** Per-tick reward when actively healing an ally */
	UPROPERTY(EditAnywhere, Category = "Heal")
	float HealTickReward = 5.0f;

	//========== Positioning Properties =============

	/** Per-step bonus for being farther from the nearest visible enemy than the nearest ally. */
	UPROPERTY(EditAnywhere, Category = "Positioning")
	float RearGuardBonus = 1.5f;

	/** Maximum distance to nearest visible enemy (cm) for RearGuardBonus to fire. */
	UPROPERTY(EditAnywhere, Category = "Positioning")
	float RearGuardMaxEnemyDist = 3000.0f;

	/** Per-step penalty when Support is closer to the nearest visible enemy than the nearest alive ally.
	 *  Discourages frontline positioning — Support should stay behind teammates. */
	UPROPERTY(EditAnywhere, Category = "Positioning")
	float FrontlinePenalty = 2.0f;

	/** Per-step bonus when at least one alive ally is between Support and the nearest visible enemy. */
	UPROPERTY(EditAnywhere, Category = "Positioning")
	float AllyShieldBonus = 1.0f;


	//========== Movement Properties =============

	UPROPERTY(EditAnywhere, Category = "Movement")
	float PenaltyPerMeterScale = 1.5f;
};


/**
 * Support Strategy: Healer / rear-guard.
 * Follows and heals the ally with the most accumulated recent damage.
 * No offensive capability — attack branch is blocked in the Behavior Tree.
 */
float DEComputeSupportStepReward(
	ADECharacter* Agent,
	FDERewardState& InOutState,
	const FDEAgentSnapshot& Prev,
	const FDEAgentSnapshot& Current,
	const TArray<ADECapturePoint*>& EnvCapturePoints,
	const UDERewardData* Settings,
	float CaptureRadiusSq,
	float PositionChange,
	bool bIsRespawnStep,
	bool bIsolated,
	int32 MyTeamID);
