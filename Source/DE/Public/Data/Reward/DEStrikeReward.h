// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DEStrikeReward.generated.h"

class ADEAgent;
class ADECapturePoint;
class UDERewardData;
struct FDERewardState;
struct FDEAgentSnapshot;


USTRUCT(BlueprintType)
struct FDEStrikeRewardSettings
{
	GENERATED_BODY()

	//========== Combat Properties =============

	UPROPERTY(EditAnywhere, Category = "Combat")
	float SurvivalRewardScale = 1.5f;

	/** Strike: +3 per kill (base KillReward=10 × 0.3) — reduced to prevent kill-farming over capping */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float KillRewardScale = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float HealthPenalty = 0.0f;

	/** Strike: -10 per death (base DeathPenaltyReward=100 × 0.1) — kept within StepRewardClampMin to avoid impulse spikes. */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float DeathScale = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float IdlePenalty = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float TimePenalty = 0.01f;


	//========== Capture Properties =============

	/** Strike: +15 per capture (base CaptureReward=100 × 0.15) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float CaptureRewardScale = 0.15f;

	/** Strike: -25 per loss (base LossCaptureReward=-100 × 0.25) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float LossCaptureRewardScale = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Capture")
	float ObjectiveProgressReward = 0.15f;

	/** Strike: reduced capture bonuses — range discipline takes priority over capping. */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float ZonePresenceBonus = 3.0f;

	/** Per-step bonus scaled by capture progress [0,1] while actively capping a non-friendly point. */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float ActiveCappingBonus = 4.0f;

	UPROPERTY(EditAnywhere, Category = "Capture")
	float PostCaptureMomentumBonus = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Capture")
	int32 PostCaptureMomentumDuration = 90;

	UPROPERTY(EditAnywhere, Category = "Capture")
	float PostCaptureMomentumMinMove = 100.0f;


	//========== Ranged Combat =============

	/** Minimum combat range for Strike. Penalty fires each step a visible enemy is closer than this. */
	UPROPERTY(EditAnywhere, Category = "RangedCombat")
	float MinCombatRange = 800.0f;

	/** Per-step penalty when any visible enemy is within MinCombatRange.
	 *  Suppressed while actively capping a non-friendly zone (range discipline
	 *  is irrelevant when the agent must stand on the point). */
	UPROPERTY(EditAnywhere, Category = "RangedCombat")
	float TooCloseEnemyPenalty = 6.0f;

	/** Per-step bonus when a visible enemy is between MinCombatRange and MaxEngagementRange.
	 *  Encourages maintaining optimal attack distance. */
	UPROPERTY(EditAnywhere, Category = "RangedCombat")
	float OptimalRangeBonus = 5.0f;

	/** Maximum distance (cm) for OptimalRangeBonus to fire. */
	UPROPERTY(EditAnywhere, Category = "RangedCombat")
	float MaxEngagementRange = 1500.0f;

	/** Per-hit reward when Strike deals damage while at optimal range (beyond melee, within max range). */
	UPROPERTY(EditAnywhere, Category = "RangedCombat")
	float InRangeHitReward = 3.0f;


	//========== Movement Properties =============

	UPROPERTY(EditAnywhere, Category = "Movement")
	float PenaltyPerMeterScale = 1.0f;
};


/**
 * Strike Class: Ranged damage dealer.
 * Captures enemy bases, attacks from distance, penalized for entering melee range.
 */
float DEComputeStrikeStepReward(
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
