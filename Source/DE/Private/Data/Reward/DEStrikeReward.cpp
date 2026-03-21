// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/Reward/DEStrikeReward.h"
#include "Data/Reward/DERewardData.h"
#include "Types/DERewardTypes.h"
#include "Types/DEObservationTypes.h"
#include "Characters/DEAgent.h"
#include "Actors/DECapturePoint.h"

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
			Reward -= Settings->StrikeReward.TooCloseEnemyPenalty;
			InOutState.bWasTooCloseAtKill = true;
		}
		else if (bAtOptimalRange)
		{
			Reward += Settings->StrikeReward.OptimalRangeBonus;
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
		bool bInNonFriendlyZone = false, bInFriendlyZoneStrike = false;
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
			else if (CurrDistSq <= CaptureRadiusSq)
			{
				bInFriendlyZoneStrike = true;
			}
		}

		const int32 NewCaptures = CurrFriendlyPoints - PrevFriendlyPoints;
		if (NewCaptures > 0)
		{
			InOutState.PostCaptureMomentumStepsRemaining = Settings->StrikeReward.PostCaptureMomentumDuration;
			float NearestFriendlyDistSq = FLT_MAX;
			for (ADECapturePoint* CP : EnvCapturePoints)
			{
				if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;
				const float DSq = FVector::DistSquared(Current.Position, CP->GetActorLocation());
				if (DSq < NearestFriendlyDistSq)
				{
					NearestFriendlyDistSq = DSq;
					InOutState.LastCapturedPointLocation = CP->GetActorLocation();
				}
			}
		}

		if (PrevNearestDistSq < FLT_MAX && CurrNearestDistSq < FLT_MAX)
		{
			const float ApproachScale = bIsolated ? Settings->IsolationApproachMultiplier : 1.0f;
			const float ApproachDelta = FMath::Sqrt(PrevNearestDistSq) - FMath::Sqrt(CurrNearestDistSq);
			const float EffectiveDelta = ApproachDelta >= 0.0f ? ApproachDelta : ApproachDelta * 0.5f;
			Reward += Settings->StrikeReward.ObjectiveProgressReward * ApproachScale * EffectiveDelta;
		}

		if (bInNonFriendlyZone)
		{
			InOutState.StrikeZoneStepsAfterCapture = 0;
			// Scale down zone bonus when too close to enemy — don't reward rushing in blindly
			const float ZoneScale = bEnemyTooClose ? 0.3f : 1.0f;
			Reward += Settings->StrikeReward.ZonePresenceBonus * ZoneScale;
			Reward += Settings->StrikeReward.ActiveCappingBonus * ActiveCappingProgress * ZoneScale;
		}
		else if (bInFriendlyZoneStrike && Settings->StrikeCapturedZoneDecaySteps > 0.0f)
		{
			InOutState.StrikeZoneStepsAfterCapture++;
			const float DecayFactor = FMath::Max(0.0f, 1.0f - (float)InOutState.StrikeZoneStepsAfterCapture / Settings->StrikeCapturedZoneDecaySteps);
			Reward += Settings->StrikeReward.ZonePresenceBonus * DecayFactor;
		}

		if (InOutState.PostCaptureMomentumStepsRemaining > 0)
		{
			InOutState.PostCaptureMomentumStepsRemaining--;
			if (PositionChange >= Settings->StrikeReward.PostCaptureMomentumMinMove &&
				FVector::DistSquared(Current.Position, InOutState.LastCapturedPointLocation) > CaptureRadiusSq)
			{
				Reward += Settings->StrikeReward.PostCaptureMomentumBonus;
			}
		}

		if (PositionChange < Settings->StrikeIdleMovementThreshold && !bInNonFriendlyZone)
		{
			const float IdlePenalty = (CurrNearestDistSq == FLT_MAX || bIsolated)
				? Settings->StrikeReward.IdlePenalty
				: Settings->StrikeReward.IdlePenalty * 0.5f;
			Reward -= IdlePenalty;
		}
	}

	return Reward;
}
