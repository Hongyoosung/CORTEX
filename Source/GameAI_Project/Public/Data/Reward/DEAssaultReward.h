// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DEAssaultReward.generated.h"

class ADECharacter;
class ADECapturePoint;
class UDERewardData;
struct FDERewardState;
struct FDEAgentSnapshot;


USTRUCT(BlueprintType)
struct FDEAssaultRewardSettings
{
	GENERATED_BODY()

	//========== Combat Properties =============

	UPROPERTY(EditAnywhere, Category = "Combat")
	float SurvivalRewardScale = 1.5f;

	/** Assault: +3 per kill (base KillReward=10 × 0.3) — reduced to prevent kill-farming over capping */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float KillRewardScale = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float HealthPenalty = 0.0f;

	/** Assault: -20 per death (base DeathPenaltyReward=100 × 0.2) */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float DeathScale = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float IdlePenalty = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float TimePenalty = 0.01f;


	//========== Capture Properties =============

	/** Assault: +15 per capture (base CaptureReward=100 × 0.15) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float CaptureRewardScale = 0.15f;

	/** Assault: -25 per loss (base LossCaptureReward=-100 × 0.25) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float LossCaptureRewardScale = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Capture")
	float ObjectiveProgressReward = 0.15f;

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


	//========== Ranged Combat =============

	/** Minimum combat range for Assault. Penalty fires each step a visible enemy is closer than this. */
	UPROPERTY(EditAnywhere, Category = "RangedCombat")
	float MinCombatRange = 800.0f;

	/** Per-step penalty when any visible enemy is within MinCombatRange. */
	UPROPERTY(EditAnywhere, Category = "RangedCombat")
	float TooCloseEnemyPenalty = 2.0f;


	//========== Movement Properties =============

	UPROPERTY(EditAnywhere, Category = "Movement")
	float PenaltyPerMeterScale = 1.0f;
};


/**
 * Assault Strategy: Ranged damage dealer.
 * Captures enemy bases, attacks from distance, penalized for entering melee range.
 */
float DEComputeAssaultStepReward(
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
