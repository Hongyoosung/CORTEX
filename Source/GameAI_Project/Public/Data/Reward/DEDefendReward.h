// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DEDefendReward.generated.h"

class ADECharacter;
class ADECapturePoint;
class UDERewardData;
struct FDERewardState;
struct FDEAgentSnapshot;


/**
 * Defend = Frontline Melee Tank.
 * Captures enemy bases identically to Assault, but engages at melee range
 * and is rewarded for absorbing damage on behalf of the team.
 */
USTRUCT(BlueprintType)
struct FDEDefendRewardSettings
{
	GENERATED_BODY()

	//========== Combat Properties =============

	UPROPERTY(EditAnywhere, Category = "Combat")
	float SurvivalRewardScale = 1.5f;

	/** Defend: +4 per kill (base KillReward=10 × 0.4) — melee tank kills aggressively */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float KillRewardScale = 0.4f;

	/** Reward per normalized HP absorbed (0–1) while alive — tank soaks damage for the team. */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float ZoneDurabilityBonus = 3.0f;

	/** Defend: -20 per death (base DeathPenaltyReward=100 × 0.20) */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float DeathScale = 0.20f;

	/** Small per-step bonus when health is above DefendHealthThreshold (stay healthy to keep fighting). */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float HealthBonus = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float IdlePenalty = 1.5f;

	//========== Melee Engagement =============

	/** Per-step bonus when a visible enemy is within MeleeRangeDistance. Encourages frontline presence. */
	UPROPERTY(EditAnywhere, Category = "Melee")
	float MeleeRangeBonus = 1.5f;

	/** Distance threshold (cm) for MeleeRangeBonus. */
	UPROPERTY(EditAnywhere, Category = "Melee")
	float MeleeRangeDistance = 350.0f;

	//========== Capture Properties (same tendency as Assault) =============

	/** Defend: +15 per capture (base CaptureReward=100 × 0.15) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float CaptureRewardScale = 0.15f;

	/** Defend: -25 per loss (base LossCaptureReward=-100 × 0.25) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float LossCaptureRewardScale = 0.25f;

	/** Per-cm shaping toward nearest enemy/neutral base. */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float ObjectiveProgressReward = 0.15f;

	/** Bonus per step while inside an enemy or neutral capture zone. */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float ZonePresenceBonus = 6.0f;

	/** Per-step bonus scaled by capture progress [0,1] while actively capping a non-friendly point. */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float ActiveCappingBonus = 8.0f;

	UPROPERTY(EditAnywhere, Category = "Capture")
	float PostCaptureMomentumBonus = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Capture")
	int32 PostCaptureMomentumDuration = 30;

	UPROPERTY(EditAnywhere, Category = "Capture")
	float PostCaptureMomentumMinMove = 100.0f;

	//========== Zone Guard Kill =============

	/** Extra kill reward when the killed enemy was near a friendly capture point.
	 *  Keeps the tank protecting the team even while pushing forward. */
	UPROPERTY(EditAnywhere, Category = "ZoneGuard")
	float ZoneGuardKillBonus = 3.0f;

	/** Multiplier on CaptureRadius used to define "near the zone" for ZoneGuardKillBonus. */
	UPROPERTY(EditAnywhere, Category = "ZoneGuard")
	float ZoneGuardRadius = 3.0f;

	//========== Movement Properties =============

	UPROPERTY(EditAnywhere, Category = "Movement")
	float PenaltyPerMeterScale = 1.0f;
};


/**
 * Defend Strategy: Frontline melee tank.
 * Captures enemy bases identically to Assault, engages at melee range,
 * and is rewarded for absorbing damage on behalf of the team.
 */
float DEComputeDefendStepReward(
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
