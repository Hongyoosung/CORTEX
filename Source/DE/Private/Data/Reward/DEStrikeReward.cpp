// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/Reward/DEStrikeReward.h"
#include "Data/Reward/DERewardData.h"
#include "Types/DERewardTypes.h"
#include "Types/DEObservationTypes.h"
#include "Characters/DEAgent.h"
#include "Actors/DECapturePoint.h"
#include "Team/DEMatchManager.h"
#include "Actors/DESpawnArea.h"

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
	int32 MyTeamID)
{
	float Reward = Settings->StrikeBaselineReward;

	// Health-loss penalty
	const float HealthLoss = Prev.Health - Current.Health;
	if (HealthLoss > Settings->StrikeHealthLossThreshold)
		Reward -= Settings->StrikeReward.HealthPenalty * HealthLoss;

	// Early zone check: TooCloseEnemyPenalty is reduced (not suppressed) while actively capping.
	// Strike should still prefer range even on-point, but at 30% penalty to avoid
	// over-penalising agents holding a capture zone under pressure.
	bool bInNonFriendlyZoneEarly = false;
	if (!bIsRespawnStep && EnvCapturePoints.Num() > 0)
	{
		for (const ADECapturePoint* CP : EnvCapturePoints)
		{
			if (!CP || CP->GetTeamID_Implementation() == MyTeamID) continue;
			if (FVector::DistSquared(Current.Position, CP->GetActorLocation()) <= CaptureRadiusSq)
			{
				bInNonFriendlyZoneEarly = true;
				break;
			}
		}
	}

	// Ranged combat: penalise too close, reward optimal range
	bool bEnemyTooClose = false;
	{
		const float MinRangeSq = FMath::Square(Settings->StrikeReward.MinCombatRange);
		const float MaxRangeSq = FMath::Square(Settings->StrikeReward.MaxEngagementRange);
		bool bTooClose = false;
		bool bAtOptimalRange = false;
		for (int32 i = 0; i < Current.EnemyPositions.Num(); ++i)
		{
			if (i < Current.EnemyVisible.Num() && Current.EnemyVisible[i])
			{
				const float DistSq = FVector::DistSquared(Current.Position, Current.EnemyPositions[i]);
				if (DistSq < MinRangeSq)
				{
					bTooClose = true;
				}
				else if (DistSq <= MaxRangeSq)
				{
					bAtOptimalRange = true;
				}
			}
		}
		if (bTooClose)
		{
			bEnemyTooClose = true;
			// Reduce (not suppress) penalty while capping — Strike should still prefer range,
			// but not be punished as harshly for holding a point under pressure.
			const float RangePenaltyScale = bInNonFriendlyZoneEarly ? 0.3f : 1.0f;
			Reward -= Settings->StrikeReward.TooCloseEnemyPenalty * RangePenaltyScale;
			InOutState.bWasTooCloseAtKill = true;
		}
		else if (bAtOptimalRange)
		{
			Reward += Settings->StrikeReward.OptimalRangeBonus;
		}

		// In-range hit reward: reward actual damage output at optimal range
		if (bAtOptimalRange && Agent->GetStepDamageDealt() > 0.0f)
		{
			Reward += Settings->StrikeReward.InRangeHitReward;
		}
	}

	if (!bIsRespawnStep && EnvCapturePoints.Num() > 0)
	{
		int32 PrevFriendlyPoints = 0, CurrFriendlyPoints = 0;
		for (int32 i = 0; i < Prev.CapturePointStatuses.Num(); ++i)
		{
			if (Prev.CapturePointStatuses[i] > 0.5f) PrevFriendlyPoints++;
			if (i < Current.CapturePointStatuses.Num() && Current.CapturePointStatuses[i] > 0.5f) CurrFriendlyPoints++;
		}

		float PrevNearestDistSq = FLT_MAX, CurrNearestDistSq = FLT_MAX;
		bool bInNonFriendlyZone = false;
		bool bInFriendlyZone = false;
		bool bAnyNonFriendlyPointExists = false;
		float ActiveCappingProgress = 0.0f;

		for (ADECapturePoint* CP : EnvCapturePoints)
		{
			if (!CP) continue;
			const float PrevDistSq = FVector::DistSquared(Prev.Position, CP->GetActorLocation());
			const float CurrDistSq = FVector::DistSquared(Current.Position, CP->GetActorLocation());

			if (CP->GetTeamID_Implementation() != MyTeamID)
			{
				bAnyNonFriendlyPointExists = true;
				PrevNearestDistSq = FMath::Min(PrevNearestDistSq, PrevDistSq);
				CurrNearestDistSq = FMath::Min(CurrNearestDistSq, CurrDistSq);
				if (CurrDistSq <= CaptureRadiusSq)
				{
					bInNonFriendlyZone = true;
					ActiveCappingProgress = FMath::Max(ActiveCappingProgress, CP->GetCaptureProgress());
				}
			}
			else if (CurrDistSq <= CaptureRadiusSq)
			{
				bInFriendlyZone = true;
			}
		}

		if (PrevNearestDistSq < FLT_MAX && CurrNearestDistSq < FLT_MAX)
		{
			const float ApproachScale = bIsolated ? Settings->IsolationApproachMultiplier : 1.0f;
			const float ApproachDelta = FMath::Sqrt(PrevNearestDistSq) - FMath::Sqrt(CurrNearestDistSq);
			const float EffectiveDelta = ApproachDelta >= 0.0f ? ApproachDelta : ApproachDelta * 0.5f;
			Reward += Settings->StrikeReward.ObjectiveProgressReward * ApproachScale * EffectiveDelta;

			// Stagnation tracking: reset if making meaningful approach, increment otherwise
			if (ApproachDelta > 10.0f)
				InOutState.StagnationSteps = 0;
			else
				InOutState.StagnationSteps++;
		}

		if (bInNonFriendlyZone)
		{
			Reward += Settings->StrikeReward.ZonePresenceBonus;
			Reward += Settings->StrikeReward.ActiveCappingBonus * ActiveCappingProgress;
			InOutState.FriendlyZoneLoiterSteps = 0;
		}

		// Friendly-zone loitering penalty: discourage camping on already-captured points.
		// Suppressed when enemies are nearby — the agent may be defending.
		const bool bAnyEnemyVisible = Current.EnemyVisible.ContainsByPredicate([](bool b){ return b; });
		if (bInFriendlyZone && !bInNonFriendlyZone && bAnyNonFriendlyPointExists && !bAnyEnemyVisible)
		{
			InOutState.FriendlyZoneLoiterSteps++;
			if (InOutState.FriendlyZoneLoiterSteps > Settings->FriendlyZoneLoiterGraceSteps)
			{
				Reward -= Settings->FriendlyZoneLoiterPenalty;
			}
		}
		else if (!bInFriendlyZone)
		{
			InOutState.FriendlyZoneLoiterSteps = 0;
		}

		if (PositionChange < Settings->StrikeIdleMovementThreshold && !bInNonFriendlyZone)
		{
			const float IdlePenalty = (CurrNearestDistSq == FLT_MAX || bIsolated)
				? Settings->StrikeReward.IdlePenalty
				: Settings->StrikeReward.IdlePenalty * 0.5f;
			Reward -= IdlePenalty;
		}
	}

	// Base loiter penalty: always applies when the agent is inside their own spawn base,
	// regardless of enemy proximity — agents should leave the base and push forward.
	if (!bIsRespawnStep && Agent)
	{
		if (ADEMatchManager* MatchMgr = Agent->GetMatchManager())
		{
			const FDETeamConfiguration TeamCfg = MatchMgr->GetTeamConfiguration(MyTeamID);
			if (TeamCfg.DESpawnArea)
			{
				const float BaseRadiusSq = FMath::Square(Settings->BaseLoiterRadius);
				if (FVector::DistSquared(Current.Position, TeamCfg.DESpawnArea->GetActorLocation()) <= BaseRadiusSq)
				{
					InOutState.BaseLoiterSteps++;
					if (InOutState.BaseLoiterSteps > Settings->BaseLoiterGraceSteps)
						Reward -= Settings->BaseLoiterPenalty;
				}
				else
				{
					InOutState.BaseLoiterSteps = 0;
				}
			}
		}
	}

	return Reward;
}
