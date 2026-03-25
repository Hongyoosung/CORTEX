// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DEVanguardReward.generated.h"

class ADEAgent;
class ADECapturePoint;
class UDERewardData;
struct FDERewardState;
struct FDEAgentSnapshot;


/**
 * Vanguard = Frontline Melee Tank.
 * Captures enemy bases identically to Strike, but engages at melee range
 * and is rewarded for absorbing damage on behalf of the team.
 */
USTRUCT(BlueprintType)
struct FDEVanguardRewardSettings
{
	GENERATED_BODY()

	//========== Combat Properties =============

	UPROPERTY(EditAnywhere, Category = "Combat")
	float SurvivalRewardScale = 1.5f;

	/** Vanguard: +4 per kill (base KillReward=10 × 0.4) — melee tank kills aggressively */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float KillRewardScale = 0.4f;

	/** Reward per normalized HP absorbed (0–1) while alive — tank soaks damage for the team. */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float ZoneDurabilityBonus = 3.0f;

	/** Vanguard: -20 per death (base DeathPenaltyReward=100 × 0.20) */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float DeathScale = 0.20f;

	/** Small per-step bonus when health is above VanguardHealthThreshold (stay healthy to keep fighting). */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float HealthBonus = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float IdlePenalty = 2.5f;

	//========== Melee Engagement =============

	/** Per-step bonus when a visible enemy is within MeleeRangeDistance. Encourages frontline presence. */
	UPROPERTY(EditAnywhere, Category = "Melee")
	float MeleeRangeBonus = 1.5f;

	/** Distance threshold (cm) for MeleeRangeBonus. */
	UPROPERTY(EditAnywhere, Category = "Melee")
	float MeleeRangeDistance = 1000.0f;

	/** Per-cm shaping reward for approaching a visible enemy. Provides gradient toward melee range.
	 *  Calibrated at ~200 cm/step typical movement → ~0.4 reward/step at full approach. */
	UPROPERTY(EditAnywhere, Category = "Melee")
	float EnemyApproachReward = 0.004f;

	/** Maximum distance (cm) at which EnemyApproachReward activates. */
	UPROPERTY(EditAnywhere, Category = "Melee")
	float EnemyApproachMaxRange = 2500.0f;

	//========== Capture Properties (same tendency as Strike) =============

	/** Vanguard: +15 per capture (base CaptureReward=100 × 0.15) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float CaptureRewardScale = 0.15f;

	/** Vanguard: -25 per loss (base LossCaptureReward=-100 × 0.25) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float LossCaptureRewardScale = 0.25f;

	/** Per-cm shaping toward nearest enemy/neutral base. */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float ObjectiveProgressReward = 0.3f;

	/** Vanguard: moderate capture bonuses — reduced from 8/12 to prevent clamp saturation. */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float ZonePresenceBonus = 3.0f;

	/** Per-step bonus scaled by capture progress [0,1] while actively capping a non-friendly point. */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float ActiveCappingBonus = 4.0f;

	//========== Movement Properties =============

	UPROPERTY(EditAnywhere, Category = "Movement")
	float PenaltyPerMeterScale = 1.0f;
};


/**
 * Vanguard Class: Frontline melee tank.
 * Captures enemy bases identically to Strike, engages at melee range,
 * and is rewarded for absorbing damage on behalf of the team.
 */
float DEComputeVanguardStepReward(
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
