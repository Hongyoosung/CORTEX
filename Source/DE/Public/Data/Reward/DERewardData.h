// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Reward/DynamicEQSRewardData.h"
#include "Data/Reward/DEAssaultReward.h"
#include "Data/Reward/DEDefendReward.h"
#include "Data/Reward/DESupportReward.h"
#include "DERewardData.generated.h"


/**
 * Data Asset holding all reward configuration for a match environment.
 * Assigned once on ADEMatchManager; shared by all agents via UDERewardSubsystem.
 */
UCLASS(BlueprintType)
class DE_API UDERewardData : public UDynamicEQSRewardData
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

	/** Bonus awarded to each surviving enemy agent when they achieve a team wipe */
	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float TeamWipeBonus = 30.0f;

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
	float SupportMinMoveThreshold = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportMaxMoveThreshold = 500.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportAllyProximityThreshold = 1200.0f;

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

	// ==================== Assault Combat Range ====================
	// Per-step penalty and kill-scale for Assault staying at range.
	// Configured via AssaultReward.MinCombatRange / AssaultReward.TooCloseEnemyPenalty.
	// Defend (melee tank) has no minimum range penalty — it gets MeleeRangeBonus instead.

	/** Distance threshold (cm) below which Assault kill/assist sparse rewards are scaled down. */
	UPROPERTY(EditAnywhere, Category = "Rewards|CombatRange")
	float CloseRangeKillThreshold = 400.0f;

	/** Multiplier applied to Assault sparse kill rewards when within CloseRangeKillThreshold.
	 *  0.0 = full penalty; 1.0 = disabled. */
	UPROPERTY(EditAnywhere, Category = "Rewards|CombatRange")
	float CloseRangeKillPenaltyScale = 0.0f;

	// ==================== Zone Control Reward ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlRewardPerBase = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlAssaultScale = 0.1f;

	/** Defend is now a frontline tank that captures bases — same scale as Assault. */
	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlDefendScale = 0.1f;

	/** Support should NOT be incentivized to capture — keep very low. */
	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlSupportScale = 0.02f;

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

	/** Per-step bonus when agent is near a friendly base that also has enemies nearby.
	 *  Incentivizes defending contested bases instead of ignoring approaching threats. */
	UPROPERTY(EditAnywhere, Category = "Rewards|BaseCooperation")
	float ContestedBaseDefenseReward = 3.0f;
};
