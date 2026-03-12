// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Reward/DynamicEQSRewardData.h"
#include "DERewardData.generated.h"


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


	//========== Movement Properties =============

	UPROPERTY(EditAnywhere, Category = "Movement")
	float PenaltyPerMeterScale = 1.0f;
};


USTRUCT(BlueprintType)
struct FDEDefendRewardSettings
{
	GENERATED_BODY()

	//========== Combat Properties =============

	UPROPERTY(EditAnywhere, Category = "Combat")
	float SurvivalRewardScale = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float PositionReward = 0.01f;

	UPROPERTY(EditAnywhere, Category = "Combat")
	float HealthBonus = 1.0f;

	/** Defend: +1 per kill (base KillReward=10 × 0.1) */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float KillRewardScale = 0.1f;

	/** Defend: -15 per death (base DeathPenaltyReward=100 × 0.15) */
	UPROPERTY(EditAnywhere, Category = "Combat")
	float DeathScale = 0.15f;

	//========== Capture Properties =============

	/** Defend: +20 per capture — core objective (base CaptureReward=100 × 0.20) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float CaptureRewardScale = 0.20f;

	/** Defend: -30 per loss — critical failure (base LossCaptureReward=-100 × 0.30) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float LossCaptureRewardScale = 0.30f;

	/** Flat bonus awarded each step the agent is physically inside a friendly capture zone */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float ZonePresenceBonus = 5.0f;

	/** Additional bonus per step when an enemy is actively contesting a friendly zone. */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float ThreatResponseBonus = 6.0f;

	/** Per-cm shaping reward for approaching the nearest friendly capture zone (when outside it). */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float ZoneApproachReward = 0.15f;

	/** Penalty scale applied per unit normalized distance when outside any friendly zone.
	 *  Replaces the hardcoded 0.3f constant — increase to discourage corner camping. */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float OutOfZonePenaltyScale = 2.0f;


	//========== Zone Defense Skills =============

	/** Reward per normalized HP absorbed (0–1) while standing inside a friendly capture zone. */
	UPROPERTY(EditAnywhere, Category = "ZoneDefense")
	float ZoneDurabilityBonus = 2.5f;

	/** Extra kill reward when the killed enemy was within ZoneGuardRadius × CaptureRadius of any friendly capture point. */
	UPROPERTY(EditAnywhere, Category = "ZoneDefense")
	float ZoneGuardKillBonus = 3.0f;

	/** Multiplier on CaptureRadius used to define "near the zone" for ZoneGuardKillBonus. */
	UPROPERTY(EditAnywhere, Category = "ZoneDefense")
	float ZoneGuardRadius = 2.0f;


	//========== Movement Properties =============

	UPROPERTY(EditAnywhere, Category = "Movement")
	float PenaltyPerMeterScale = 1.0f;
};


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

	/** Support: +10 per capture (base CaptureReward=100 × 0.10) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float CaptureRewardScale = 0.10f;

	/** Support: -15 per loss (base LossCaptureReward=-100 × 0.15) */
	UPROPERTY(EditAnywhere, Category = "Capture")
	float LossCaptureRewardScale = 0.15f;


	//========== Heal Properties =============

	/** Per-tick reward when actively healing an ally */
	UPROPERTY(EditAnywhere, Category = "Heal")
	float HealTickReward = 0.5f;

	//========== Positioning Properties =============

	/** Per-step bonus for being farther from the nearest visible enemy than the nearest ally. */
	UPROPERTY(EditAnywhere, Category = "Positioning")
	float RearGuardBonus = 0.3f;

	/** Maximum distance to nearest visible enemy (cm) for RearGuardBonus to fire. */
	UPROPERTY(EditAnywhere, Category = "Positioning")
	float RearGuardMaxEnemyDist = 3000.0f;


	//========== Movement Properties =============

	UPROPERTY(EditAnywhere, Category = "Movement")
	float PenaltyPerMeterScale = 1.5f;
};


/**
 * Data Asset holding all reward configuration for a match environment.
 * Assigned once on ADEMatchManager; shared by all agents via UDERewardSubsystem.
 */
UCLASS(BlueprintType)
class GAMEAI_PROJECT_API UDERewardData : public UDynamicEQSRewardData
{
	GENERATED_BODY()

public:
	// ==================== Common Base Rewards ====================
	// Note: StepPenalty (per-step time penalty, negative), TerminalWinReward,
	// TerminalLossReward, SurvivalBonus, RewardScale are inherited from UDynamicEQSRewardData.

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float KillReward = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float DeathPenaltyReward = 100.0f;

	/** Fraction of team average reward mixed into individual reward. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float TeamRewardMixingRatio = 0.2f;

	/** Fraction of KillReward awarded for an assist (scaled by normalized damage contribution) */
	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float AssistRewardScale = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float TeamWipePenalty = 50.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float CaptureReward = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float LossCaptureReward = -100.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float PenaltyPerMeter = -0.01f;

	// ==================== Tunable Thresholds ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SurvivalHPThreshold = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float AssaultHealthLossThreshold = 0.05f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float AssaultIdleMovementThreshold = 50.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float AssaultCapturedZoneDecaySteps = 15.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float DefendStationaryThreshold = 200.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportMinMoveThreshold = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportMaxMoveThreshold = 500.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportAllyProximityThreshold = 1500.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float DefendHealthThreshold = 0.7f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportHealthThreshold = 0.8f;

	// ==================== Reward Normalization & Clamping ====================
	// Note: RewardScale (global reward scale) is inherited from UDynamicEQSRewardData.

	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float SparseRewardScale = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float StepRewardClampMin = -2.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float StepRewardClampMax = 2.0f;

	// ==================== Capture Loss Cooldown ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|Capture")
	float CaptureLossCooldownSeconds = 10.0f;

	// ==================== Strategy Baselines ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	float AssaultBaselineReward = 0.01f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	float DefendBaselineReward = 0.01f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	float SupportBaselineReward = 0.01f;

	// ==================== Defend Zone Movement ====================

	/** Minimum movement (cm) per step to qualify for the in-zone movement bonus.
	 *  Replaces the old stationary bonus so defenders can roam freely inside the zone. */
	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneMovement")
	float ZoneMovementMinThreshold = 50.0f;

	/** Bonus awarded each step that the defender moves more than ZoneMovementMinThreshold inside a friendly zone. */
	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneMovement")
	float ZoneMovementBonus = 0.05f;

	// ==================== Minimum Combat Range ====================

	/** Minimum combat range (cm). Penalty fires when a visible enemy is closer than this. Set to 0 to allow melee. */
	UPROPERTY(EditAnywhere, Category = "Rewards|CombatRange")
	float MinCombatRange = 0.0f;

	/** Per-step penalty applied when any visible enemy is within MinCombatRange. */
	UPROPERTY(EditAnywhere, Category = "Rewards|CombatRange")
	float TooCloseEnemyPenalty = 1.5f;

	/** Distance threshold (cm) below which kill/assist sparse rewards are scaled down.
	 *  Agents must maintain this range while attacking to receive full kill credit. */
	UPROPERTY(EditAnywhere, Category = "Rewards|CombatRange")
	float CloseRangeKillThreshold = 400.0f;

	/** Multiplier applied to sparse kill/assist rewards when the agent was within CloseRangeKillThreshold.
	 *  0.0 = no reward for close-range kills; 1.0 = no penalty (disabled). */
	UPROPERTY(EditAnywhere, Category = "Rewards|CombatRange")
	float CloseRangeKillPenaltyScale = 0.0f;

	// ==================== Zone Control Reward ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlRewardPerBase = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlAssaultScale = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlDefendScale = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlSupportScale = 1.0f;

	// ==================== Isolation Mode ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|Isolation")
	float IsolationApproachMultiplier = 5.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Isolation")
	int32 IsolationDebounceSteps = 30;

	// ==================== Per-Strategy Settings ====================
	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	FDEAssaultRewardSettings AssaultReward;

	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	FDEDefendRewardSettings DefendReward;

	UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
	FDESupportRewardSettings SupportReward;

	// ==================== Cooperative Base Occupation (Phase 5) ====================

	/** Per-step bonus when this agent is the ONLY ally within BaseOccupationRadius
	 *  of an uncontrolled (neutral or enemy) capture point. */
	UPROPERTY(EditAnywhere, Category = "Rewards|BaseCooperation")
	float BaseOccupationReward = 2.0f;

	/** Per-step penalty when 2+ allies stack on the same base. */
	UPROPERTY(EditAnywhere, Category = "Rewards|BaseCooperation")
	float CoOccupationPenalty = 0.5f;

	/** Sparse reward for the agent that flipped a base's ownership. */
	UPROPERTY(EditAnywhere, Category = "Rewards|BaseCooperation")
	float BaseCaptureCreditReward = 5.0f;

	/** Per-step shared penalty for each friendly base with no ally guarding it. */
	UPROPERTY(EditAnywhere, Category = "Rewards|BaseCooperation")
	float UndefendedBasePenalty = 1.0f;

	/** Sparse reward (once per episode) for first reaching the assigned base. */
	UPROPERTY(EditAnywhere, Category = "Rewards|BaseCooperation")
	float AssignedBaseReachReward = 1.0f;

	/** Radius (cm) used to determine proximity for base cooperation rewards. */
	UPROPERTY(EditAnywhere, Category = "Rewards|BaseCooperation")
	float BaseOccupationRadius = 2000.0f;
};
