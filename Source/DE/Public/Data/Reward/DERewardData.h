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

	/** Fraction of team average reward mixed into individual reward.
	 *  Raised to strengthen cooperative signal — agents need to care about teammates winning. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Common")
	float TeamRewardMixingRatio = 0.3f;

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
	float SupportAllyProximityThreshold = 1200.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float VanguardHealthThreshold = 0.7f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
	float SupportHealthThreshold = 0.8f;

	// ==================== Reward Normalization & Clamping ====================
	// Note: RewardScale (global reward scale) is inherited from UDynamicEQSRewardData.

	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float SparseRewardScale = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float StepRewardClampMin = -15.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
	float StepRewardClampMax = 15.0f;

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

	// ==================== Isolation Mode ====================

	UPROPERTY(EditAnywhere, Category = "Rewards|Isolation")
	float IsolationApproachMultiplier = 5.0f;

	UPROPERTY(EditAnywhere, Category = "Rewards|Isolation")
	int32 IsolationDebounceSteps = 30;

	// ==================== Stagnation & Loitering ====================

	/** Steps without objective approach before stagnation penalty begins escalating. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Stagnation")
	int32 StagnationThresholdSteps = 20;

	/** Per-step penalty once stagnation threshold is exceeded. Escalates linearly. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Stagnation")
	float StagnationPenaltyPerStep = 0.15f;

	/** Maximum stagnation penalty per step (caps the linear escalation). */
	UPROPERTY(EditAnywhere, Category = "Rewards|Stagnation")
	float StagnationPenaltyMax = 3.0f;

	/** Per-step penalty for loitering on an already-captured (friendly) point
	 *  when at least one non-friendly point remains. Pushes agents to advance. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Stagnation")
	float FriendlyZoneLoiterPenalty = 1.5f;

	/** Steps on friendly zone before loiter penalty activates (brief grace for regrouping). */
	UPROPERTY(EditAnywhere, Category = "Rewards|Stagnation")
	int32 FriendlyZoneLoiterGraceSteps = 10;

	/** Radius (cm) from the team's spawn area center within which an agent is considered loitering at base. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Stagnation")
	float BaseLoiterRadius = 1500.0f;

	/** Per-step penalty for loitering inside the team's own spawn base.
	 *  Unlike FriendlyZoneLoiterPenalty, this fires regardless of enemy proximity. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Stagnation")
	float BaseLoiterPenalty = 1.5f;

	/** Steps inside the spawn base before BaseLoiterPenalty activates. */
	UPROPERTY(EditAnywhere, Category = "Rewards|Stagnation")
	int32 BaseLoiterGraceSteps = 10;

	// ==================== Per-Class Settings ====================
	UPROPERTY(EditAnywhere, Category = "Rewards|Class")
	FDEStrikeRewardSettings StrikeReward;

	UPROPERTY(EditAnywhere, Category = "Rewards|Class")
	FDEVanguardRewardSettings VanguardReward;

	UPROPERTY(EditAnywhere, Category = "Rewards|Class")
	FDESupportRewardSettings SupportReward;

};
