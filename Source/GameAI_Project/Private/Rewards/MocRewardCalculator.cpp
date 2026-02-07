// Copyright Epic Games, Inc. All Rights Reserved.

#include "RL/Rewards/MocRewardCalculator.h"
#include "Characters/MocCharacter.h"


UMocRewardCalculator::UMocRewardCalculator()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UMocRewardCalculator::CalculateKillReward(EStrategyType ActiveStrategy)
{
	float Reward = 0.0f;

	switch (ActiveStrategy)
	{
	case EStrategyType::Assault:
		Reward = 10.0f;
		break;
	case EStrategyType::Defend:
		Reward = 5.0f;
		break;
	case EStrategyType::Support:
		Reward = 3.0f;
		break;
	}

	LogRewardEvent(ERewardEventType::Kill, ActiveStrategy, Reward);
	AddReward(Reward);
	return Reward;
}

float UMocRewardCalculator::CalculateAssistReward(EStrategyType ActiveStrategy, float DamageDealt)
{
	float BaseReward = 0.0f;

	switch (ActiveStrategy)
	{
	case EStrategyType::Assault:
		BaseReward = 3.0f;
		break;
	case EStrategyType::Defend:
		BaseReward = 2.0f;
		break;
	case EStrategyType::Support:
		BaseReward = 4.0f; 
		break;
	}

	// Scale by damage contribution (normalize to 0-1 range)
	float Reward = BaseReward * FMath::Clamp(DamageDealt / 100.0f, 0.0f, 1.0f);

	LogRewardEvent(ERewardEventType::Assist, ActiveStrategy, Reward);
	AddReward(Reward);
	return Reward;
}

float UMocRewardCalculator::CalculateDeathPenalty(EStrategyType ActiveStrategy)
{
	float Penalty = 0.0f;

	switch (ActiveStrategy)
	{
	case EStrategyType::Assault:
		Penalty = -20.0f;
		break;
	case EStrategyType::Defend:
		Penalty = -15.0f;
		break;
	case EStrategyType::Support:
		Penalty = -10.0f;
		break;
	}

	LogRewardEvent(ERewardEventType::Death, ActiveStrategy, Penalty);
	AddReward(Penalty);
	return Penalty;
}

float UMocRewardCalculator::CalculateCaptureReward(EStrategyType ActiveStrategy)
{
	float Reward = 0.0f;

	switch (ActiveStrategy)
	{
	case EStrategyType::Assault:
		Reward = 15.0f;
		break;
	case EStrategyType::Defend:
		Reward = 20.0f; // Core objective for defenders
		break;
	case EStrategyType::Support:
		Reward = 10.0f;
		break;
	}

	LogRewardEvent(ERewardEventType::CapturePoint, ActiveStrategy, Reward);
	AddReward(Reward);
	return Reward;
}

float UMocRewardCalculator::CalculateLosePointPenalty(EStrategyType ActiveStrategy)
{
	float Penalty = 0.0f;

	switch (ActiveStrategy)
	{
	case EStrategyType::Assault:
		Penalty = -25.0f;
		break;
	case EStrategyType::Defend:
		Penalty = -30.0f; // Critical failure for defenders
		break;
	case EStrategyType::Support:
		Penalty = -15.0f;
		break;
	}

	LogRewardEvent(ERewardEventType::LosePoint, ActiveStrategy, Penalty);
	AddReward(Penalty);
	return Penalty;
}

float UMocRewardCalculator::CalculateHealAllyReward(EStrategyType ActiveStrategy, float HPRestored)
{
	float Reward = 0.0f;

	if (ActiveStrategy == EStrategyType::Support && HPRestored >= 40.0f)
	{
		Reward = 12.0f; // Core objective for support
	}
	else if (ActiveStrategy == EStrategyType::Defend && HPRestored >= 40.0f)
	{
		Reward = 3.0f;
	}

	LogRewardEvent(ERewardEventType::HealAlly, ActiveStrategy, Reward);
	AddReward(Reward);
	return Reward;
}

float UMocRewardCalculator::CalculateRevealEnemyReward(EStrategyType ActiveStrategy)
{
	float Reward = 0.0f;

	switch (ActiveStrategy)
	{
	case EStrategyType::Assault:
		Reward = 2.0f;
		break;
	case EStrategyType::Defend:
		Reward = 1.0f;
		break;
	case EStrategyType::Support:
		Reward = 1.0f;
		break;
	}

	LogRewardEvent(ERewardEventType::RevealEnemy, ActiveStrategy, Reward);
	AddReward(Reward);
	return Reward;
}

float UMocRewardCalculator::CalculatePickupDenyReward(EStrategyType ActiveStrategy)
{
	float Reward = 0.0f;

	switch (ActiveStrategy)
	{
	case EStrategyType::Assault:
		Reward = 1.0f;
		break;
	case EStrategyType::Defend:
		Reward = 1.0f;
		break;
	case EStrategyType::Support:
		Reward = 2.0f;
		break;
	}

	LogRewardEvent(ERewardEventType::PickupDeny, ActiveStrategy, Reward);
	AddReward(Reward);
	return Reward;
}

float UMocRewardCalculator::CalculateSurvivalReward(EStrategyType ActiveStrategy, float CurrentHP, float MaxHP)
{
	float HPRatio = CurrentHP / MaxHP;
	if (HPRatio > 0.3f)
	{
		return 0.0f; // Only reward survival when HP < 30%
	}

	float Reward = 0.0f;

	switch (ActiveStrategy)
	{
	case EStrategyType::Assault:
		Reward = -5.0f; // Should be more aggressive, not surviving
		break;
	case EStrategyType::Defend:
		Reward = -3.0f;
		break;
	case EStrategyType::Support:
		Reward = -2.0f;
		break;
	}

	LogRewardEvent(ERewardEventType::Survival, ActiveStrategy, Reward);
	AddReward(Reward);
	return Reward;
}

float UMocRewardCalculator::CalculateDistanceShaping(EStrategyType ActiveStrategy, float DistanceToTarget)
{
	if (!bUseDenseRewards)
	{
		return 0.0f; // Disabled in Phase 1b
	}

	float PenaltyPerMeter = 0.0f;

	switch (ActiveStrategy)
	{
	case EStrategyType::Assault:
		PenaltyPerMeter = -0.01f;
		break;
	case EStrategyType::Defend:
		PenaltyPerMeter = -0.01f;
		break;
	case EStrategyType::Support:
		PenaltyPerMeter = -0.015f;
		break;
	}

	float Penalty = PenaltyPerMeter * DistanceToTarget;

	LogRewardEvent(ERewardEventType::DistanceShaping, ActiveStrategy, Penalty);
	AddReward(Penalty);
	return Penalty;
}

float UMocRewardCalculator::CalculateStrategyDiversityBonus(int32 UniqueStrategiesCount)
{
	float Bonus = (UniqueStrategiesCount >= 3) ? 2.0f : 0.0f;

	if (Bonus > 0.0f)
	{
		LogRewardEvent(ERewardEventType::StrategyDiversity, EStrategyType::Assault, Bonus);
		AddReward(Bonus);
	}

	return Bonus;
}

float UMocRewardCalculator::CalculateObjectiveMajorityBonus(int32 ControlledPoints)
{
	float Bonus = (ControlledPoints >= 3) ? 0.5f : 0.0f;

	if (Bonus > 0.0f)
	{
		AddReward(Bonus);
	}

	return Bonus;
}

void UMocRewardCalculator::LogRewardEvent(ERewardEventType EventType, EStrategyType Strategy, float Reward)
{
	if (!OwnerCharacter)
	{
		OwnerCharacter = Cast<AMocCharacter>(GetOwner());
	}

	float Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	int32 AgentID = OwnerCharacter ? OwnerCharacter->AgentID : -1;

	EventLog.Add(FRewardEvent(EventType, Strategy, Reward, Timestamp, AgentID));
}

void UMocRewardCalculator::AddReward(float Value)
{
	CumulativeReward += Value;
}
