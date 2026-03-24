// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/Reward/DESupportReward.h"
#include "Data/Reward/DERewardData.h"
#include "Types/DERewardTypes.h"
#include "Types/DEObservationTypes.h"
#include "Characters/DEAgent.h"
#include "Actors/DECapturePoint.h"
#include "Team/DEMatchManager.h"
#include "GAS/Abilities/DEGA_Heal.h"

float DEComputeSupportStepReward(
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
	float Reward = Settings->SupportBaselineReward;

	// Two separate reward channels to prevent full stacking (mutual weighting).
	// ProximityReward = bonuses for being near allies and positioning correctly.
	// HealReward      = bonuses for actively healing.
	// Combined at the end via: max(a,b) + 0.3 * min(a,b).
	float ProximityReward = 0.0f;
	float HealReward = 0.0f;

	// Movement reward: encourage steady repositioning without sprinting
	if (PositionChange > Settings->SupportMinMoveThreshold && PositionChange < Settings->SupportMaxMoveThreshold)
		Reward += Settings->SupportReward.PositionReward;

	// ---- Update ally accumulated damage tracker ----
	// Ensures the array matches the current ally count
	const int32 AllyCount = Current.AllyHealths.Num();
	if (InOutState.AllyAccumulatedDamage.Num() < AllyCount)
		InOutState.AllyAccumulatedDamage.SetNum(AllyCount, EAllowShrinking::No);

	for (int32 i = 0; i < AllyCount; ++i)
	{
		// Accumulate damage taken by each ally this step
		const float PrevHP = (i < Prev.AllyHealths.Num()) ? Prev.AllyHealths[i] : Current.AllyHealths[i];
		const float DmgThisStep = FMath::Max(0.0f, PrevHP - Current.AllyHealths[i]);
		InOutState.AllyAccumulatedDamage[i] += DmgThisStep;
		// Decay toward zero each step
		InOutState.AllyAccumulatedDamage[i] *= Settings->SupportReward.AllyDamageDecayRate;
	}

	// ---- Target selection: ally with most accumulated recent damage ----
	++InOutState.InjuredAllyStalenessCounter;
	const bool bShouldReevalTarget =
		(InOutState.CachedInjuredAllyIdx < 0) ||
		(InOutState.InjuredAllyStalenessCounter >= 5) ||
		(InOutState.CachedInjuredAllyIdx < AllyCount && Current.AllyHealths[InOutState.CachedInjuredAllyIdx] <= 0.0f);

	if (bShouldReevalTarget)
	{
		TArray<ADEAgent*> AllyChars;
		if (ADEMatchManager* MatchMgr = Agent->GetMatchManager())
			AllyChars = MatchMgr->GetTeamAgents(MyTeamID);

		bool bAnyNonSupportAlive = false;
		for (ADEAgent* Ally : AllyChars)
		{
			if (Ally && Ally != Agent && Ally->IsAlive() &&
				Ally->GetCommandedClass() != EDEClassType::Support)
			{ bAnyNonSupportAlive = true; break; }
		}

		// Target selection: blend urgency (low current HP) with recent damage activity.
		// Score = (1 - HP) * 2 + normalized_accum * 0.3, so a critically low ally
		// always outranks one who merely took recent chip damage.
		float BestScore = -1.0f;
		int32 NewInjuredIdx = -1;

		for (int32 i = 0; i < AllyCount; ++i)
		{
			const float AllyHP = Current.AllyHealths[i];
			if (AllyHP <= 0.0f) continue;
			if (bAnyNonSupportAlive && i < AllyChars.Num() && AllyChars[i] &&
				AllyChars[i]->GetCommandedClass() == EDEClassType::Support)
				continue;

			const float AccumNorm = (i < InOutState.AllyAccumulatedDamage.Num())
				? FMath::Min(InOutState.AllyAccumulatedDamage[i] / 50.0f, 1.0f)
				: 0.0f;
			const float UrgencyScore = (1.0f - AllyHP) * 2.0f + AccumNorm * 0.3f;
			if (UrgencyScore > BestScore)
			{
				BestScore     = UrgencyScore;
				NewInjuredIdx = i;
			}
		}

		InOutState.CachedInjuredAllyIdx = NewInjuredIdx;
		InOutState.InjuredAllyStalenessCounter = 0;
	}

	// ---- Proximity and approach reward toward injured ally → ProximityReward ----
	const int32 InjuredAllyIdx = InOutState.CachedInjuredAllyIdx;
	if (InjuredAllyIdx >= 0 && InjuredAllyIdx < Current.AllyPositions.Num())
	{
		const float CurrAllyDist = FVector::Dist(Current.Position, Current.AllyPositions[InjuredAllyIdx]);
		if (CurrAllyDist <= Settings->SupportAllyProximityThreshold)
		{
			const float AllyHP = Current.AllyHealths[InjuredAllyIdx];
			if (AllyHP > 0.0f && AllyHP < Settings->SupportReward.AllyInjuryThreshold)
			{
				ProximityReward += Settings->SupportReward.AllyProximityBonus;
				if (AllyHP < 0.3f) ProximityReward += Settings->SupportReward.AllyProximityBonus * 0.5f;
			}
			if (Current.Health > Settings->SupportHealthThreshold)
				ProximityReward += Settings->SupportReward.HealthBonus;
		}

		if (!bIsRespawnStep && InjuredAllyIdx < Prev.AllyPositions.Num())
		{
			const float PrevAllyDist = FVector::Dist(Prev.Position, Prev.AllyPositions[InjuredAllyIdx]);
			ProximityReward += Settings->SupportReward.AllyApproachReward * FMath::Max(PrevAllyDist - CurrAllyDist, 0.0f);
		}
	}
	else if (bIsolated && !bIsRespawnStep)
	{
		// Isolated: approach nearest friendly base to regroup (NOT enemy objectives)
		if (Current.Health > Settings->SupportHealthThreshold)
			ProximityReward += Settings->SupportReward.HealthBonus * 0.5f;

		float PrevNearestFriendlyDist = FLT_MAX, CurrNearestFriendlyDist = FLT_MAX;
		for (ADECapturePoint* CP : EnvCapturePoints)
		{
			if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;
			PrevNearestFriendlyDist = FMath::Min(PrevNearestFriendlyDist, FVector::Dist(Prev.Position, CP->GetActorLocation()));
			CurrNearestFriendlyDist = FMath::Min(CurrNearestFriendlyDist, FVector::Dist(Current.Position, CP->GetActorLocation()));
		}
		if (PrevNearestFriendlyDist < FLT_MAX && CurrNearestFriendlyDist < FLT_MAX)
			ProximityReward += Settings->SupportReward.AllyApproachReward * Settings->IsolationApproachMultiplier * (PrevNearestFriendlyDist - CurrNearestFriendlyDist);
	}
	else
	{
		ProximityReward += Settings->SupportReward.PositionReward * 0.5f;
	}

	// ---- Ally formation / isolation → ProximityReward (bonus) / Reward (penalty) ----
	if (!bIsRespawnStep && Current.AllyPositions.Num() > 0)
	{
		float NearestAllyDist = FLT_MAX;
		for (int32 a = 0; a < Current.AllyPositions.Num(); ++a)
		{
			if (Current.AllyPositions[a].IsZero()) continue;
			if (a < Current.AllyHealths.Num() && Current.AllyHealths[a] <= 0.0f) continue;
			const float D = FVector::Dist(Current.Position, Current.AllyPositions[a]);
			if (D < NearestAllyDist)
				NearestAllyDist = D;
		}
		if (NearestAllyDist < FLT_MAX)
		{
			if (NearestAllyDist <= Settings->SupportAllyProximityThreshold)
				ProximityReward += Settings->SupportReward.AllyFormationBonus;
			if (NearestAllyDist > Settings->SupportReward.AllyIsolationDistance)
				Reward -= Settings->SupportReward.AllyIsolationPenalty;  // penalties stay additive
		}
	}

	// ---- Heal tick reward → HealReward ----
	if (Agent && Agent->GetLastTickHealAmount() > 0.0f)
	{
		HealReward += Settings->SupportReward.HealTickReward;

		// Bonus for healing a low-HP ally
		UDEGA_Heal* HealAbility = Agent->GetHealAbility();
		if (HealAbility)
		{
			ADEAgent* HealTarget = HealAbility->GetCurrentTarget();
			if (HealTarget && HealTarget->GetHealthPercentage() < Settings->SupportReward.HealLowHPThreshold)
			{
				HealReward += Settings->SupportReward.HealOnLowHPReward;
			}
		}
	}

	// ---- Near injured ally reward → HealReward ----
	if (Agent)
	{
		UDEGA_Heal* HealAbility = Agent->GetHealAbility();
		if (HealAbility && HealAbility->HasInjuredAllyInRange(Settings->SupportReward.NearInjuredAllyHPThreshold))
		{
			HealReward += Settings->SupportReward.NearInjuredAllyReward;
		}
	}

	// ---- Rear-guard positioning → ProximityReward (bonus) / Reward (penalty) ----
	if (!bIsRespawnStep && Current.EnemyPositions.Num() > 0 && Current.AllyPositions.Num() > 0)
	{
		float NearestEnemyDist = FLT_MAX;
		FVector NearestEnemyPos = FVector::ZeroVector;
		for (int32 i = 0; i < Current.EnemyPositions.Num(); ++i)
		{
			if (i < Current.EnemyVisible.Num() && Current.EnemyVisible[i])
			{
				const float D = FVector::Dist(Current.Position, Current.EnemyPositions[i]);
				if (D < NearestEnemyDist)
				{
					NearestEnemyDist = D;
					NearestEnemyPos = Current.EnemyPositions[i];
				}
			}
		}

		if (NearestEnemyDist < FLT_MAX && NearestEnemyDist <= Settings->SupportReward.RearGuardMaxEnemyDist)
		{
			float NearestAllyToEnemyDist = FLT_MAX;
			for (int32 a = 0; a < Current.AllyPositions.Num(); ++a)
			{
				if (Current.AllyPositions[a].IsZero()) continue;
				if (a < Current.AllyHealths.Num() && Current.AllyHealths[a] <= 0.0f) continue;
				NearestAllyToEnemyDist = FMath::Min(NearestAllyToEnemyDist,
					FVector::Dist(Current.AllyPositions[a], NearestEnemyPos));
			}

			if (NearestEnemyDist > NearestAllyToEnemyDist)
			{
				ProximityReward += Settings->SupportReward.RearGuardBonus;
			}
			else
			{
				Reward -= Settings->SupportReward.FrontlinePenalty;  // penalties stay additive
			}

			// Ally shield bonus: at least one ally is between Support and the nearest enemy
			if (NearestAllyToEnemyDist < FLT_MAX)
			{
				for (int32 a = 0; a < Current.AllyPositions.Num(); ++a)
				{
					if (Current.AllyPositions[a].IsZero()) continue;
					if (a < Current.AllyHealths.Num() && Current.AllyHealths[a] <= 0.0f) continue;
					const float AllyToEnemy = FVector::Dist(Current.AllyPositions[a], NearestEnemyPos);
					const float AllyToSelf  = FVector::Dist(Current.AllyPositions[a], Current.Position);
					if (AllyToEnemy < NearestEnemyDist && AllyToSelf < NearestEnemyDist)
					{
						ProximityReward += Settings->SupportReward.AllyShieldBonus;
						break;
					}
				}
			}
		}
	}

	// ---- Independent channels: proximity and healing are distinct role behaviours ----
	// Both contribute fully so that performing both simultaneously (staying near an
	// injured ally AND actively healing them) is correctly rewarded.
	Reward += ProximityReward;
	Reward += HealReward;

	return Reward;
}
