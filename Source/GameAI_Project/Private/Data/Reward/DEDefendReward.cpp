// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/Reward/DEDefendReward.h"
#include "Data/Reward/DERewardData.h"
#include "Types/DERewardTypes.h"
#include "Types/DEObservationTypes.h"
#include "Characters/DECharacter.h"
#include "Actors/DECapturePoint.h"

float DEComputeDefendStepReward(
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
	int32 MyTeamID)
{
	float Reward = Settings->DefendBaselineReward;

	// ---- Objective: approach and capture enemy/neutral bases (identical to Assault) ----
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

		// Capture event: start post-capture momentum
		const int32 NewCaptures = CurrFriendlyPoints - PrevFriendlyPoints;
		if (NewCaptures > 0)
		{
			InOutState.PostCaptureMomentumStepsRemaining = Settings->DefendReward.PostCaptureMomentumDuration;
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

		// Approach shaping toward nearest non-friendly base
		if (PrevNearestDistSq < FLT_MAX && CurrNearestDistSq < FLT_MAX)
		{
			const float ApproachScale = bIsolated ? Settings->IsolationApproachMultiplier : 1.0f;
			const float ApproachDelta = FMath::Sqrt(PrevNearestDistSq) - FMath::Sqrt(CurrNearestDistSq);
			const float EffectiveDelta = ApproachDelta >= 0.0f ? ApproachDelta : ApproachDelta * 0.5f;
			Reward += Settings->DefendReward.ObjectiveProgressReward * ApproachScale * EffectiveDelta;
		}

		if (bInNonFriendlyZone)
		{
			Reward += Settings->DefendReward.ZonePresenceBonus;
			Reward += Settings->DefendReward.ActiveCappingBonus * ActiveCappingProgress;
		}
		else if (PositionChange < Settings->AssaultIdleMovementThreshold)
		{
			const float IdlePenalty = (CurrNearestDistSq == FLT_MAX || bIsolated)
				? Settings->DefendReward.IdlePenalty
				: Settings->DefendReward.IdlePenalty * 0.5f;
			Reward -= IdlePenalty;
		}

		// Post-capture momentum: move to next enemy base after capping
		if (InOutState.PostCaptureMomentumStepsRemaining > 0)
		{
			InOutState.PostCaptureMomentumStepsRemaining--;
			if (PositionChange >= Settings->DefendReward.PostCaptureMomentumMinMove &&
				FVector::DistSquared(Current.Position, InOutState.LastCapturedPointLocation) > CaptureRadiusSq)
			{
				Reward += Settings->DefendReward.PostCaptureMomentumBonus;
			}
		}
	}

	// ---- Melee Range Bonus: reward for engaging at close range (tank role) ----
	if (!bIsRespawnStep)
	{
		const float MeleeRangeSq = FMath::Square(Settings->DefendReward.MeleeRangeDistance);
		for (int32 i = 0; i < Current.EnemyPositions.Num(); ++i)
		{
			if (i < Current.EnemyVisible.Num() && Current.EnemyVisible[i])
			{
				if (FVector::DistSquared(Current.Position, Current.EnemyPositions[i]) <= MeleeRangeSq)
				{
					Reward += Settings->DefendReward.MeleeRangeBonus;
					break;
				}
			}
		}
	}

	// ---- Durability Bonus: reward per HP absorbed (soaking damage for the team) ----
	if (!bIsRespawnStep)
	{
		const float DamageTaken = Prev.Health - Current.Health;
		if (DamageTaken > 0.0f)
			Reward += Settings->DefendReward.ZoneDurabilityBonus * DamageTaken;
	}

	// ---- Health Bonus: small incentive to stay healthy enough to keep fighting ----
	if (!bIsRespawnStep && Current.Health > Settings->DefendHealthThreshold)
		Reward += Settings->DefendReward.HealthBonus;

	// ---- Zone Guard Kill Bonus: extra kill credit when enemy was threatening a friendly zone ----
	if (InOutState.bSparseKillFiredThisStep)
	{
		for (int32 i = 0; i < Prev.EnemyPositions.Num(); ++i)
		{
			if (i >= Prev.EnemyVisible.Num() || !Prev.EnemyVisible[i]) continue;
			for (ADECapturePoint* CP : EnvCapturePoints)
			{
				if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;
				if (FVector::Dist(Prev.EnemyPositions[i], CP->GetActorLocation()) <= CP->CaptureRadius * Settings->DefendReward.ZoneGuardRadius)
				{
					Reward += Settings->DefendReward.ZoneGuardKillBonus;
					goto DefendZoneGuardDone;
				}
			}
		}
		DefendZoneGuardDone:;
	}

	return Reward;
}
