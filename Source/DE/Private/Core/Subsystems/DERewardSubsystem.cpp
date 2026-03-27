// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/Subsystems/DERewardSubsystem.h"
#include "Data/Reward/DEStrikeReward.h"
#include "Data/Reward/DEVanguardReward.h"
#include "Data/Reward/DESupportReward.h"
#include "Data/Reward/DERewardData.h"
#include "Characters/DEAgent.h"
#include "Types/DERewardTypes.h"
#include "Actors/DECapturePoint.h"
#include "Team/DEMatchManager.h"

namespace { static const TArray<ADECapturePoint*> EmptyCapturePoints; }

// ==================== UDynamicEQSRewardCalculatorBase Overrides ====================

float UDERewardSubsystem::CalculateStepReward(const FDynamicEQSStepContext& Context)
{
	// Full per-step reward requires game context (character, snapshot, class).
	// Use ComputeStepReward() directly. This override satisfies the base class contract.
	return 0.0f;
}

float UDERewardSubsystem::CalculateTerminalReward(bool bWon)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	return bWon ? Settings->TerminalWinReward : Settings->TerminalLossReward;
}

void UDERewardSubsystem::Reset()
{
	Super::Reset();
}

// ==================== Internal Helpers ====================

const UDERewardData* UDERewardSubsystem::GetSettings() const
{
	return Cast<UDERewardData>(RewardData);
}

float UDERewardSubsystem::GetClassScale(EDEClassType Class, float StrikeScale, float VanguardScale, float SupportScale) const
{
	switch (Class)
	{
	case EDEClassType::Strike: return StrikeScale;
	case EDEClassType::Vanguard:  return VanguardScale;
	case EDEClassType::Support: return SupportScale;
	default:                       return 1.0f;
	}
}


float UDERewardSubsystem::DrainSparseReward(FDERewardState& InOutState, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	const float Drained = InOutState.CumulativeReward * Settings->SparseRewardScale;
	InOutState.CumulativeReward = 0.0f;
	return Drained;
}

// ==================== Event-Driven Sparse Rewards ====================

float UDERewardSubsystem::CalculateKillReward(FDERewardState& InOutState, EDEClassType ActiveClass, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	InOutState.bSparseKillFiredThisStep = true;
	float Scale = GetClassScale(ActiveClass, Settings->StrikeReward.KillRewardScale, Settings->VanguardReward.KillRewardScale, Settings->SupportReward.KillRewardScale);
	float KillValue = Settings->KillReward * Scale;

	// Halve Strike kill reward when the kill happened at too-close range,
	// discouraging melee-range kill-farming for a ranged class.
	if (ActiveClass == EDEClassType::Strike && InOutState.bWasTooCloseAtKill)
		KillValue *= 0.5f;

	return ApplyAndLogReward(InOutState, EDERewardEventType::Kill, ActiveClass, KillValue, AgentID);
}

float UDERewardSubsystem::CalculateAssistReward(FDERewardState& InOutState, EDEClassType ActiveClass, float DamageDealt, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	float DamageNorm = FMath::Clamp(DamageDealt / 100.0f, 0.0f, 1.0f);
	float Scale = GetClassScale(ActiveClass, Settings->StrikeReward.KillRewardScale, Settings->VanguardReward.KillRewardScale, Settings->SupportReward.KillRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::Assist, ActiveClass, Settings->KillReward * Settings->AssistRewardScale * DamageNorm * Scale, AgentID);
}

float UDERewardSubsystem::CalculateDeathPenalty(FDERewardState& InOutState, EDEClassType ActiveClass, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	float Scale = GetClassScale(ActiveClass, Settings->StrikeReward.DeathScale, Settings->VanguardReward.DeathScale, Settings->SupportReward.DeathScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::Death, ActiveClass, -Settings->DeathPenaltyReward * Scale, AgentID);
}

float UDERewardSubsystem::CalculateTeamWipePenalty(FDERewardState& InOutState, EDEClassType ActiveClass, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	float Scale = GetClassScale(ActiveClass, Settings->StrikeReward.DeathScale, Settings->VanguardReward.DeathScale, Settings->SupportReward.DeathScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::TeamWipe, ActiveClass, -Settings->TeamWipePenalty * Scale, AgentID);
}

float UDERewardSubsystem::CalculateTeamWipeBonus(FDERewardState& InOutState, EDEClassType ActiveClass, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	return ApplyAndLogReward(InOutState, EDERewardEventType::TeamWipe, ActiveClass, Settings->TeamWipeBonus, AgentID);
}

float UDERewardSubsystem::CalculateCaptureReward(FDERewardState& InOutState, EDEClassType ActiveClass, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	float Scale = GetClassScale(ActiveClass, Settings->StrikeReward.CaptureRewardScale, Settings->VanguardReward.CaptureRewardScale, Settings->SupportReward.CaptureRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::DECapturePoint, ActiveClass, Settings->CaptureReward * Scale, AgentID);
}

float UDERewardSubsystem::CalculateLosePointPenalty(FDERewardState& InOutState, EDEClassType ActiveClass, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	float Scale = GetClassScale(ActiveClass, Settings->StrikeReward.LossCaptureRewardScale, Settings->VanguardReward.LossCaptureRewardScale, Settings->SupportReward.LossCaptureRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::LosePoint, ActiveClass, Settings->LossCaptureReward * Scale, AgentID);
}

float UDERewardSubsystem::CalculateSurvivalReward(FDERewardState& InOutState, EDEClassType ActiveClass, float CurrentHP, float MaxHP, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings || MaxHP <= 0.0f) return 0.0f;
	if (CurrentHP / MaxHP < Settings->SurvivalHPThreshold) return 0.0f;
	const float Scale = GetClassScale(ActiveClass,
		Settings->StrikeReward.SurvivalRewardScale,
		Settings->VanguardReward.SurvivalRewardScale,
		Settings->SupportReward.SurvivalRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::Survival, ActiveClass, Settings->SurvivalBonus * Scale, AgentID);
}

float UDERewardSubsystem::CalculateDistanceShaping(FDERewardState& InOutState, EDEClassType ActiveClass, float DistanceToTarget, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	const float Scale = GetClassScale(ActiveClass,
		Settings->StrikeReward.PenaltyPerMeterScale,
		Settings->VanguardReward.PenaltyPerMeterScale,
		Settings->SupportReward.PenaltyPerMeterScale);
	const float Reward = Settings->PenaltyPerMeter * Scale * (DistanceToTarget / 100.0f);
	return ApplyAndLogReward(InOutState, EDERewardEventType::DistanceShaping, ActiveClass, Reward, AgentID);
}



// ==================== Dense Per-Step Reward ====================

float UDERewardSubsystem::ComputeStepReward(
	ADEAgent* Agent,
	FDERewardState& InOutState,
	EDEClassType Class,
	const FDEAgentSnapshot& Prev,
	const FDEAgentSnapshot& Current,
	const FDEEQSWeightParameters& Action,
	ADEMatchManager* MatchManager)
{
	if (!Agent) {
		UE_LOG(LogTemp, Error, TEXT("DERewardSubsystem: Invalid Agent reference, returning 0 reward"));
		return 0.0f;
	}
	const UDERewardData* Settings = GetSettings();
	if (!Settings)
	{
		UE_LOG(LogTemp, Error, TEXT("DERewardSubsystem: Missing RewardData, returning 0 reward"));
		return 0.0f;
	}

	const float PositionChange = FVector::Dist(Prev.Position, Current.Position);
	const int32 MyTeamID = Agent->GetTeamID_Implementation();
	const bool bIsRespawnStep = !Prev.bIsAlive;

	// Use passed MatchManager (cached by caller) instead of per-step world scan
	const TArray<ADECapturePoint*>& EnvCapturePoints = MatchManager
		? MatchManager->GetCapturePoints()
		: EmptyCapturePoints;

	float CaptureRadiusSq = 250000.0f;
	if (EnvCapturePoints.Num() > 0 && EnvCapturePoints[0])
		CaptureRadiusSq = FMath::Square(EnvCapturePoints[0]->CaptureRadius);

	// Isolation mode
	bool bAllAlliesDead = true;
	for (const float HP : Current.AllyHealths)
	{
		if (HP > 0.0f) { bAllAlliesDead = false; break; }
	}
	if (bAllAlliesDead)
		InOutState.IsolatedConsecutiveSteps = FMath::Min(InOutState.IsolatedConsecutiveSteps + 1, Settings->IsolationDebounceSteps);
	else
		InOutState.IsolatedConsecutiveSteps = 0;
	const bool bIsolated = (InOutState.IsolatedConsecutiveSteps >= Settings->IsolationDebounceSteps);

	// ---- Dispatch per-class step reward ----
	float Reward = 0.0f;
	switch (Class)
	{
	case EDEClassType::Strike:
		Reward = DEComputeStrikeStepReward(Agent, InOutState, Prev, Current, EnvCapturePoints, Settings,
			CaptureRadiusSq, PositionChange, bIsRespawnStep, bIsolated, MyTeamID);
		break;

	case EDEClassType::Vanguard:
		Reward = DEComputeVanguardStepReward(Agent, InOutState, Prev, Current, EnvCapturePoints, Settings,
			CaptureRadiusSq, PositionChange, bIsRespawnStep, bIsolated, MyTeamID);
		break;

	case EDEClassType::Support:
		Reward = DEComputeSupportStepReward(Agent, InOutState, Prev, Current, EnvCapturePoints, Settings,
			CaptureRadiusSq, PositionChange, bIsRespawnStep, bIsolated, MyTeamID);
		break;
	}

	// ---- Reset per-step damage accumulator (consumed by Strike reward above) ----
	Agent->ResetStepDamage();

	// ---- Stagnation penalty: escalating penalty for not making objective progress ----
	if (InOutState.StagnationSteps > Settings->StagnationThresholdSteps)
	{
		const int32 ExcessSteps = InOutState.StagnationSteps - Settings->StagnationThresholdSteps;
		const float StagnationPenalty = FMath::Min(
			Settings->StagnationPenaltyPerStep * static_cast<float>(ExcessSteps),
			Settings->StagnationPenaltyMax);
		Reward -= StagnationPenalty;
	}

	// ---- Common: Survival reward ----
	if (!bIsRespawnStep && Current.bIsAlive)
		CalculateSurvivalReward(InOutState, Class, Current.Health, 1.0f, Agent->AgentID);

	// ---- Step (time) penalty ----
	Reward -= -Settings->StepPenalty;

	// ---- Drain and scale sparse rewards ----
	Reward += DrainSparseReward(InOutState, Agent->AgentID);

	Reward *= Settings->RewardScale;
	Reward = FMath::Clamp(Reward, Settings->StepRewardClampMin, Settings->StepRewardClampMax);

	InOutState.bSparseKillFiredThisStep = false;
	InOutState.bWasTooCloseAtKill = false;
	InOutState.LastIndividualStepReward = Reward;

	// ---- Team reward mixing (MAPPO cooperative signal) ----
	// Blend individual reward with team average to strengthen cooperative behavior.
	// TeamRewardMixingRatio=0.2 means 80% individual + 20% team average.
	if (Settings->TeamRewardMixingRatio > 0.0f && MatchManager)
	{
		const TArray<ADEAgent*>& TeamAgents = MatchManager->GetTeamAgents(MyTeamID);
		if (TeamAgents.Num() > 1)
		{
			float TeamRewardSum = 0.0f;
			int32 TeamCount = 0;
			for (const ADEAgent* Ally : TeamAgents)
			{
				if (Ally && Ally != Agent)
				{
					TeamRewardSum += Ally->RewardState.LastIndividualStepReward;
					++TeamCount;
				}
			}
			if (TeamCount > 0)
			{
				const float TeamAvg = TeamRewardSum / TeamCount;
				const float Alpha = Settings->TeamRewardMixingRatio;
				Reward = (1.0f - Alpha) * Reward + Alpha * TeamAvg;
			}
		}
	}

	return Reward;
}

float UDERewardSubsystem::ApplyAndLogReward(FDERewardState& InOutAgentState, EDERewardEventType EventType, EDEClassType Class, float RewardValue, int32 AgentID)
{
	InOutAgentState.CumulativeReward += RewardValue;
	return RewardValue;
}

void UDERewardSubsystem::ApplyMatchEndReward(int32 WinnerTeamID, const TArray<ADEAgent*>& AllAgents)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return;

	for (ADEAgent* Agent : AllAgents)
	{
		if (!Agent) continue;

		const int32 AgentTeam = Agent->GetTeamID_Implementation();

		if (WinnerTeamID == -1)
		{
			// Tie — no terminal reward
			continue;
		}

		const float TerminalReward = CalculateTerminalReward(AgentTeam == WinnerTeamID);
		Agent->RewardState.CumulativeReward += TerminalReward;

		UE_LOG(LogTemp, Log, TEXT("[DERewardSubsystem] Match end: Agent %d (Team %d) gets %.1f (%s)"),
			Agent->AgentID, AgentTeam, TerminalReward,
			(AgentTeam == WinnerTeamID) ? TEXT("WIN") : TEXT("LOSS"));
	}
}
