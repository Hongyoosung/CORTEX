// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/Reward/DEVanguardReward.h"
#include "Data/Reward/DERewardData.h"
#include "Types/DERewardTypes.h"
#include "Types/DEObservationTypes.h"
#include "Characters/DEAgent.h"
#include "Actors/DECapturePoint.h"

float DEComputeVanguardStepReward(
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
	float Reward = Settings->VanguardBaselineReward;

	// ---- Objective: approach and capture enemy/neutral bases (identical to Strike) ----
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
		float ActiveCappingProgress = 0.0f;

		for (ADECapturePoint* CP : EnvCapturePoints)
		{
			if (!CP) continue;
			const float PrevDistSq = FVector::DistSquared(Prev.Position, CP->GetActorLocation());
			const float CurrDistSq = FVector::DistSquared(Current.Position, CP->GetActorLocation());

			if (CP->GetTeamID_Implementation() != MyTeamID)
			{
				PrevNearestDistSq = FMath::Min(PrevNearestDistSq, PrevDistSq);
				CurrNearestDistSq = FMath::Min(CurrNearestDistSq, CurrDistSq);
				if (CurrDistSq <= CaptureRadiusSq)
				{
					bInNonFriendlyZone = true;
					ActiveCappingProgress = FMath::Max(ActiveCappingProgress, CP->GetCaptureProgress());
				}
			}
		}

		// Approach shaping toward nearest non-friendly base
		if (PrevNearestDistSq < FLT_MAX && CurrNearestDistSq < FLT_MAX)
		{
			const float ApproachScale = bIsolated ? Settings->IsolationApproachMultiplier : 1.0f;
			const float ApproachDelta = FMath::Sqrt(PrevNearestDistSq) - FMath::Sqrt(CurrNearestDistSq);
			const float EffectiveDelta = ApproachDelta >= 0.0f ? ApproachDelta : ApproachDelta * 0.5f;
			Reward += Settings->VanguardReward.ObjectiveProgressReward * ApproachScale * EffectiveDelta;
		}

		if (bInNonFriendlyZone)
		{
			Reward += Settings->VanguardReward.ZonePresenceBonus;
			Reward += Settings->VanguardReward.ActiveCappingBonus * ActiveCappingProgress;
		}
		else if (PositionChange < Settings->StrikeIdleMovementThreshold)
		{
			const float IdlePenalty = (CurrNearestDistSq == FLT_MAX || bIsolated)
				? Settings->VanguardReward.IdlePenalty
				: Settings->VanguardReward.IdlePenalty * 0.5f;
			Reward -= IdlePenalty;
		}

	}

	// ---- Melee Range Bonus + Enemy Approach Shaping ----
	if (!bIsRespawnStep)
	{
		const float MeleeRangeSq = FMath::Square(Settings->VanguardReward.MeleeRangeDistance);
		const float ApproachMaxRangeSq = FMath::Square(Settings->VanguardReward.EnemyApproachMaxRange);
		float NearestVisibleEnemyDist = FLT_MAX;
		float PrevNearestVisibleEnemyDist = FLT_MAX;
		bool bInMeleeRange = false;

		for (int32 i = 0; i < Current.EnemyPositions.Num(); ++i)
		{
			if (i < Current.EnemyVisible.Num() && Current.EnemyVisible[i])
			{
				const float CurrDistSq = FVector::DistSquared(Current.Position, Current.EnemyPositions[i]);
				if (CurrDistSq <= MeleeRangeSq)
				{
					bInMeleeRange = true;
				}
				const float CurrDist = FMath::Sqrt(CurrDistSq);
				if (CurrDist < NearestVisibleEnemyDist)
					NearestVisibleEnemyDist = CurrDist;
				// Approximate previous distance using prev position
				if (i < Prev.EnemyPositions.Num())
				{
					const float PrevDist = FVector::Dist(Prev.Position, Prev.EnemyPositions[i]);
					if (PrevDist < PrevNearestVisibleEnemyDist)
						PrevNearestVisibleEnemyDist = PrevDist;
				}
			}
		}

		if (bInMeleeRange)
		{
			Reward += Settings->VanguardReward.MeleeRangeBonus;
		}

		// Enemy approach shaping: reward closing distance to nearest visible enemy
		if (NearestVisibleEnemyDist < Settings->VanguardReward.EnemyApproachMaxRange &&
			PrevNearestVisibleEnemyDist < FLT_MAX)
		{
			const float ApproachDelta = PrevNearestVisibleEnemyDist - NearestVisibleEnemyDist;
			if (ApproachDelta > 0.0f)
				Reward += Settings->VanguardReward.EnemyApproachReward * ApproachDelta;
		}
	}

	// ---- Health Bonus: small incentive to stay healthy enough to keep fighting ----
	if (!bIsRespawnStep && Current.Health > Settings->VanguardHealthThreshold)
		Reward += Settings->VanguardReward.HealthBonus;

	return Reward;
}
