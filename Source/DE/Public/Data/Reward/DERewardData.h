// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Reward/DynamicEQSRewardData.h"
#include "Data/Reward/DEStrikeReward.h"
#include "Data/Reward/DEVanguardReward.h"
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

	/** Reduced: team wipe is not episode-ending, just a modest signal. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float TeamWipePenalty = 10.0f;

	/** Reduced: team wipe is not episode-ending, just a modest signal. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float TeamWipeBonus = 10.0f;

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
	float StrikeHealthLossThreshold = 0.05f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float StrikeIdleMovementThreshold = 50.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float StrikeCapturedZoneDecaySteps = 15.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportMinMoveThreshold = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportMaxMoveThreshold = 500.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportAllyProximityThreshold = 1200.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float VanguardHealthThreshold = 0.7f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportHealthThreshold = 0.8f;

	// ==================== Reward Normalization & Clamping ====================
	// Note: RewardScale (global reward scale) is inherited from UDynamicEQSRewardData.

	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float SparseRewardScale = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float StepRewardClampMin = -10.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float StepRewardClampMax = 10.0f;

	// ==================== Capture Loss Cooldown ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|Capture")
	float CaptureLossCooldownSeconds = 10.0f;

	// ==================== Class Baselines ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|Class")
	float StrikeBaselineReward = 0.01f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Class")
	float VanguardBaselineReward = 0.01f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Class")
	float SupportBaselineReward = 0.01f;

	// ==================== Strike Combat Range ====================
	// Per-step penalty and kill-scale for Strike staying at range.
	// Configured via StrikeReward.MinCombatRange / StrikeReward.TooCloseEnemyPenalty.
	// Vanguard (melee tank) has no minimum range penalty — it gets MeleeRangeBonus instead.

	/** Distance threshold (cm) below which Strike kill/assist sparse rewards are scaled down. */
	UPROPERTY(EditAnywhere, Category = "Rewards|CombatRange")
	float CloseRangeKillThreshold = 400.0f;

	/** Multiplier applied to Strike sparse kill rewards when within CloseRangeKillThreshold.
	 *  0.0 = full penalty; 1.0 = disabled. */
	UPROPERTY(EditAnywhere, Category = "Rewards|CombatRange")
	float CloseRangeKillPenaltyScale = 0.0f;

	// ==================== Zone Control Reward ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlRewardPerBase = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlStrikeScale = 0.1f;

	/** Vanguard is now a frontline tank that captures bases — same scale as Strike. */
	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlVanguardScale = 0.1f;

	/** Support should NOT be incentivized to capture — keep very low. */
	UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
	float ZoneControlSupportScale = 0.02f;

	// ==================== Isolation Mode ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|Isolation")
	float IsolationApproachMultiplier = 5.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Isolation")
	int32 IsolationDebounceSteps = 30;

	// ==================== Per-Class Settings ====================
	UPROPERTY(EditAnywhere, Category = "Rewards|Class")
	FDEStrikeRewardSettings StrikeReward;

	UPROPERTY(EditAnywhere, Category = "Rewards|Class")
	FDEVanguardRewardSettings VanguardReward;

	UPROPERTY(EditAnywhere, Category = "Rewards|Class")
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

	/** Radius (cm) used to determine proximity for base cooperation rewards. */
	UPROPERTY(EditAnywhere, Category = "Rewards|BaseCooperation")
	float BaseOccupationRadius = 2000.0f;

};
