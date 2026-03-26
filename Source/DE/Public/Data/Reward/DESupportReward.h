// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DESupportReward.generated.h"

class ADEAgent;
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
	float HealthBonus = 0.3f;

	/** Support: 0 per kill — kills are irrelevant to role */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float KillRewardScale = 0.0f;

	/** Support: -20 per death (base DeathPenaltyReward=100 × 0.20) */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float DeathScale = 0.20f;


	//========== Ally Proximity Properties =============

	/** Flat bonus per step for being within SupportAllyProximityThreshold of the most-injured ally. */
	UPROPERTY(EditAnywhere, Category = "AllyProximity")
	float AllyProximityBonus = 1.0f;

	/** HP fraction (0-1) below which the most-injured ally is considered to need support.
	 *  0.70 = fires when ally has taken ~30% damage (common in active combat).
	 *  Was 0.55 — too sparse, created a binary reward cliff that destabilized the critic. */
	UPROPERTY(EditAnywhere, Category = "AllyProximity")
	float AllyInjuryThreshold = 0.70f;

	/** Per-cm shaping reward for approaching the most-injured ally. */
	UPROPERTY(EditAnywhere, Category = "AllyProximity")
	float AllyApproachReward = 0.05f;

	/** Per-step bonus for being within SupportAllyProximityThreshold of ANY alive ally,
	 *  regardless of their health. Provides a stable reward floor that does not
	 *  erode as ally survivability improves. Reduced to prevent Support-Support deadlock. */
	UPROPERTY(EditAnywhere, Category = "AllyProximity")
	float AllyFormationBonus = 0.8f;

	/** Per-step penalty when Support is farther than this distance from the nearest alive ally. */
	UPROPERTY(EditAnywhere, Category = "AllyProximity")
	float AllyIsolationPenalty = 1.5f;

	/** Distance (cm) beyond which AllyIsolationPenalty fires.
	 *  Relaxed from 1500 to give Support more slack when following a dynamic frontline. */
	UPROPERTY(EditAnywhere, Category = "AllyProximity")
	float AllyIsolationDistance = 2000.0f;

	/** Per-step penalty when Support is near other Supports but the nearest non-Support
	 *  alive ally is beyond AllyIsolationDistance. Prevents two Supports from satisfying
	 *  each other's isolation check while ignoring the frontline.
	 *  Only fires when at least one non-Support ally is alive (suppressed during bFallbackToSupportTarget). */
	UPROPERTY(EditAnywhere, Category = "AllyProximity")
	float FrontlinerIsolationPenalty = 1.0f;

	/** Per-step penalty when 2+ Support agents are within SupportAllyProximityThreshold
	 *  of each other. Breaks the Support-Support deadlock where two supports satisfy
	 *  each other's formation requirements and stop following frontline agents.
	 *  Increased from 0.8 to clearly dominate AllyFormationBonus (0.8). */
	UPROPERTY(EditAnywhere, Category = "AllyProximity")
	float SupportCoStackPenalty = 1.5f;


	//========== Capture Properties =============

	/** Support: +2 per capture (base CaptureReward=100 × 0.02) — low to discourage solo capping */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float CaptureRewardScale = 0.005f;

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
	float HealTickReward = 4.0f;

	/** Bonus reward when healing an ally whose HP is below HealLowHPThreshold */
	UPROPERTY(EditAnywhere, Category = "Heal")
	float HealOnLowHPReward = 1.5f;

	/** HP fraction threshold for HealOnLowHPReward to fire */
	UPROPERTY(EditAnywhere, Category = "Heal")
	float HealLowHPThreshold = 0.6f;

	/** Per-step reward when within heal range of any ally below NearInjuredAllyHPThreshold */
	UPROPERTY(EditAnywhere, Category = "Heal")
	float NearInjuredAllyReward = 0.5f;

	/** HP fraction threshold for NearInjuredAllyReward */
	UPROPERTY(EditAnywhere, Category = "Heal")
	float NearInjuredAllyHPThreshold = 0.7f;

	//========== Positioning Properties =============

	/** Per-step bonus for being farther from the nearest visible enemy than the nearest ally. */
	UPROPERTY(EditAnywhere, Category = "Positioning")
	float RearGuardBonus = 0.5f;

	/** Maximum distance to nearest visible enemy (cm) for RearGuardBonus to fire. */
	UPROPERTY(EditAnywhere, Category = "Positioning")
	float RearGuardMaxEnemyDist = 3000.0f;

	/** Per-step penalty when Support is closer to the nearest visible enemy than the nearest alive ally.
	 *  Discourages frontline positioning — Support should stay behind teammates. */
	UPROPERTY(EditAnywhere, Category = "Positioning")
	float FrontlinePenalty = 2.0f;

	/** Hard minimum distance (cm) Support must maintain from any visible enemy.
	 *  Fires regardless of ally positions — prevents Support from charging into melee range
	 *  while following an injured frontline ally. */
	UPROPERTY(EditAnywhere, Category = "Positioning")
	float MinEnemyDistancePenaltyThreshold = 1000.0f;

	/** Per-step penalty when Support is closer than MinEnemyDistancePenaltyThreshold to any visible enemy.
	 *  Reduced from 4.0: already stacks with FrontlinePenalty (2.0), combined -4.5 is sufficient. */
	UPROPERTY(EditAnywhere, Category = "Positioning")
	float TooCloseEnemyPenalty = 2.5f;


	//========== Movement Properties =============

	UPROPERTY(EditAnywhere, Category = "Movement")
	float PenaltyPerMeterScale = 1.5f;
};


/**
 * Support Class: Healer / rear-guard.
 * Follows and heals the ally with the most accumulated recent damage.
 * No offensive capability — attack branch is blocked in the Behavior Tree.
 */
float DEComputeSupportStepReward(
	ADEAgent* Agent,
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
