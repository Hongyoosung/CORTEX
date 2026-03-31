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

	/** Support: 0 per kill — kills are irrelevant to role */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float KillRewardScale = 0.0f;

	/** Support: -12 per death (base DeathPenaltyReward=100 × 0.12) */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float DeathScale = 0.12f;


	//========== Shared Sparse Event Scales =============

	/** Scale for capture reward events (shared subsystem) — raised so Support cares about team winning objectives */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float CaptureRewardScale = 0.05f;

	/** Scale for losing a capture point (shared subsystem) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float LossCaptureRewardScale = 0.15f;

	/** Scale for distance-to-objective shaping (shared subsystem) */
	UPROPERTY(EditAnywhere, Category = "Movement")
	float PenaltyPerMeterScale = 1.5f;


	//========== Heal Properties =============

	/** Per-tick reward when actively healing an ally */
	UPROPERTY(EditAnywhere, Category = "Heal")
	float HealTickReward = 4.0f;

	/** Bonus reward when healing an ally whose HP is below HealLowHPThreshold */
	UPROPERTY(EditAnywhere, Category = "Heal")
	float HealOnLowHPReward = 2.0f;

	/** HP fraction threshold for HealOnLowHPReward to fire */
	UPROPERTY(EditAnywhere, Category = "Heal")
	float HealLowHPThreshold = 0.5f;


	//========== Ally Follow Properties =============

	/** Per-step bonus for being within heal range of any alive ally.
	 *  This is the core positioning reward — stay in heal range. */
	UPROPERTY(EditAnywhere, Category = "AllyFollow")
	float InHealRangeBonus = 1.5f;

	/** Per-cm shaping reward for approaching the nearest ally when outside heal range. */
	UPROPERTY(EditAnywhere, Category = "AllyFollow")
	float AllyApproachReward = 0.003f;


	//========== Enemy Avoidance Properties =============

	/** Continuous bonus scaled by distance to nearest visible enemy.
	 *  Reward = EnemyDistanceBonus * clamp(dist / EnemyDistanceNorm, 0, 1).
	 *  Farther from enemies = more reward. */
	UPROPERTY(EditAnywhere, Category = "EnemyAvoidance")
	float EnemyDistanceBonus = 1.0f;

	/** Distance (cm) at which EnemyDistanceBonus reaches its maximum.
	 *  Beyond this distance the bonus is clamped at EnemyDistanceBonus. */
	UPROPERTY(EditAnywhere, Category = "EnemyAvoidance")
	float EnemyDistanceNorm = 2000.0f;


	//========== Capture Properties =============

	/** Bonus for contesting an enemy CP alongside at least one alive ally,
	 *  when no enemies are visible. Encourages objective play during downtime. */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float AllyCaptureBonus = 2.0f;
};


/**
 * Support Class: Healer / rear-guard.
 * Two goals:
 *   1. Heal allies from as far away from enemies as possible, staying within heal range.
 *   2. Capture objectives together with allies when no enemies are nearby.
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
