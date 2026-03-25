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
	bool /*bIsolated*/,
	int32 MyTeamID)
{
	float Reward = Settings->SupportBaselineReward;
	float ProximityReward = 0.0f;
	float HealReward = 0.0f;

	// ---- Update ally accumulated damage tracker ----
	const int32 AllyCount = Current.AllyHealths.Num();
	if (InOutState.AllyAccumulatedDamage.Num() < AllyCount)
		InOutState.AllyAccumulatedDamage.SetNum(AllyCount, EAllowShrinking::No);

	for (int32 i = 0; i < AllyCount; ++i)
	{
		const float PrevHP = (i < Prev.AllyHealths.Num()) ? Prev.AllyHealths[i] : Current.AllyHealths[i];
		const float DmgThisStep = FMath::Max(0.0f, PrevHP - Current.AllyHealths[i]);
		InOutState.AllyAccumulatedDamage[i] += DmgThisStep;
		InOutState.AllyAccumulatedDamage[i] *= Settings->SupportReward.AllyDamageDecayRate;
	}

	// ---- Fetch ally list (needed for target selection and co-stack penalty) ----
	TArray<ADEAgent*> AllyChars;
	if (ADEMatchManager* MatchMgr = Agent ? Agent->GetMatchManager() : nullptr)
		AllyChars = MatchMgr->GetTeamAgents(MyTeamID);

	// ---- Target selection ----
	// Always resolves to a target ally:
	//   Primary  — non-Support ally with highest urgency (injured + recent damage).
	//              Floor score of 0.1 ensures healthy allies are still selected.
	//   Fallback — nearest alive Support ally when no non-Support allies remain.
	//   None     — CachedInjuredAllyIdx = -1 (truly alone, e.g. respawn).
	++InOutState.InjuredAllyStalenessCounter;
	const bool bShouldReevalTarget =
		(InOutState.CachedInjuredAllyIdx < 0) ||
		(InOutState.InjuredAllyStalenessCounter >= 5) ||
		(InOutState.CachedInjuredAllyIdx < AllyCount && Current.AllyHealths[InOutState.CachedInjuredAllyIdx] <= 0.0f);

	if (bShouldReevalTarget)
	{
		bool  bAnyNonSupportAlive = false;
		float BestScore  = -1.0f;
		int32 NewTargetIdx = -1;

		// Pass 1: non-Support allies
		for (int32 i = 0; i < AllyCount; ++i)
		{
			const float AllyHP = Current.AllyHealths[i];
			if (AllyHP <= 0.0f) continue;
			if (i < AllyChars.Num() && AllyChars[i] &&
				AllyChars[i]->GetCommandedClass() == EDEClassType::Support)
				continue;

			bAnyNonSupportAlive = true;
			const float AccumNorm = (i < InOutState.AllyAccumulatedDamage.Num())
				? FMath::Min(InOutState.AllyAccumulatedDamage[i] / 50.0f, 1.0f)
				: 0.0f;
			// Floor of 0.1 ensures a fully-healthy frontliner is still selected over nobody
			const float Score = FMath::Max((1.0f - AllyHP) * 2.0f + AccumNorm * 0.3f, 0.1f);
			if (Score > BestScore)
			{
				BestScore    = Score;
				NewTargetIdx = i;
			}
		}

		// Pass 2 (fallback): nearest alive Support ally
		if (!bAnyNonSupportAlive)
		{
			float NearestSupportDist = FLT_MAX;
			for (int32 i = 0; i < AllyCount; ++i)
			{
				if (Current.AllyHealths[i] <= 0.0f) continue;
				if (i >= AllyChars.Num() || !AllyChars[i]) continue;
				if (AllyChars[i]->GetCommandedClass() != EDEClassType::Support) continue;
				if (i >= Current.AllyPositions.Num()) continue;

				const float D = FVector::Dist(Current.Position, Current.AllyPositions[i]);
				if (D < NearestSupportDist)
				{
					NearestSupportDist = D;
					NewTargetIdx       = i;
				}
			}
		}

		InOutState.CachedInjuredAllyIdx      = NewTargetIdx;
		InOutState.bFallbackToSupportTarget  = !bAnyNonSupportAlive;
		InOutState.InjuredAllyStalenessCounter = 0;
	}

	// ---- Detect nearest visible enemy ----
	float  NearestEnemyDist = FLT_MAX;
	FVector NearestEnemyPos = FVector::ZeroVector;
	for (int32 i = 0; i < Current.EnemyPositions.Num(); ++i)
	{
		if (i < Current.EnemyVisible.Num() && Current.EnemyVisible[i])
		{
			const float D = FVector::Dist(Current.Position, Current.EnemyPositions[i]);
			if (D < NearestEnemyDist)
			{
				NearestEnemyDist = D;
				NearestEnemyPos  = Current.EnemyPositions[i];
			}
		}
	}
	const bool bEnemiesNearby = (NearestEnemyDist <= Settings->SupportReward.RearGuardMaxEnemyDist);

	// ---- Absolute enemy proximity penalty (both modes) ----
	if (NearestEnemyDist < FLT_MAX && NearestEnemyDist < Settings->SupportReward.MinEnemyDistancePenaltyThreshold)
		Reward -= Settings->SupportReward.TooCloseEnemyPenalty;

	// ---- Target ally following (both modes) ----
	// Approach shaping is 2× stronger in advance mode to pull Support toward the objective.
	const int32 TargetIdx = InOutState.CachedInjuredAllyIdx;
	if (TargetIdx >= 0 && TargetIdx < Current.AllyPositions.Num())
	{
		const float CurrTargetDist = FVector::Dist(Current.Position, Current.AllyPositions[TargetIdx]);

		if (CurrTargetDist <= Settings->SupportAllyProximityThreshold)
		{
			const float AllyHP = Current.AllyHealths[TargetIdx];
			if (AllyHP > 0.0f && AllyHP < Settings->SupportReward.AllyInjuryThreshold)
			{
				ProximityReward += Settings->SupportReward.AllyProximityBonus;
				if (AllyHP < 0.3f)
					ProximityReward += Settings->SupportReward.AllyProximityBonus * 0.5f;
			}
			ProximityReward += Settings->SupportReward.AllyFormationBonus;
		}

		if (Current.Health > Settings->SupportHealthThreshold)
			ProximityReward += Settings->SupportReward.HealthBonus;

		if (!bIsRespawnStep && TargetIdx < Prev.AllyPositions.Num())
		{
			const float PrevTargetDist = FVector::Dist(Prev.Position, Prev.AllyPositions[TargetIdx]);
			const float ApproachScale  = bEnemiesNearby ? 1.0f : 2.0f;
			ProximityReward += Settings->SupportReward.AllyApproachReward * ApproachScale
				* FMath::Max(PrevTargetDist - CurrTargetDist, 0.0f);
		}
	}
	else if (!bIsRespawnStep)
	{
		// Truly alone: approach nearest non-friendly CP to maintain objective presence
		float PrevBestDist = FLT_MAX, CurrBestDist = FLT_MAX;
		for (ADECapturePoint* CP : EnvCapturePoints)
		{
			if (!CP || CP->GetTeamID_Implementation() == MyTeamID) continue;
			PrevBestDist = FMath::Min(PrevBestDist, FVector::Dist(Prev.Position,    CP->GetActorLocation()));
			CurrBestDist = FMath::Min(CurrBestDist, FVector::Dist(Current.Position, CP->GetActorLocation()));
		}
		if (PrevBestDist < FLT_MAX && CurrBestDist < FLT_MAX)
			ProximityReward += Settings->SupportReward.AllyApproachReward
				* Settings->IsolationApproachMultiplier
				* (PrevBestDist - CurrBestDist);
	}

	// ---- Advance mode: bonus for contesting a CP alongside an ally ----
	// Fires only when no enemies are visible, encouraging Support to participate in
	// objective play rather than loitering at a safe distance.
	// The CP-with-ally check prevents solo-capping — Support earns this only when
	// at least one alive ally shares the capture zone.
	// When non-Support allies are all dead (bFallbackToSupportTarget), any alive
	// ally (Support) counts so the group can still contest.
	if (!bEnemiesNearby && !bIsRespawnStep)
	{
		for (ADECapturePoint* CP : EnvCapturePoints)
		{
			if (!CP || CP->GetTeamID_Implementation() == MyTeamID) continue;
			if (FVector::DistSquared(Current.Position, CP->GetActorLocation()) > CaptureRadiusSq) continue;

			bool bAllyPresent = false;
			for (int32 a = 0; a < Current.AllyPositions.Num(); ++a)
			{
				if (a < Current.AllyHealths.Num() && Current.AllyHealths[a] <= 0.0f) continue;
				if (FVector::DistSquared(Current.AllyPositions[a], CP->GetActorLocation()) > CaptureRadiusSq) continue;

				// In normal mode require a non-Support ally; in fallback any ally counts
				if (!InOutState.bFallbackToSupportTarget &&
					a < AllyChars.Num() && AllyChars[a] &&
					AllyChars[a]->GetCommandedClass() == EDEClassType::Support)
					continue;

				bAllyPresent = true;
				break;
			}

			if (bAllyPresent)
			{
				ProximityReward += Settings->SupportReward.CaptureRewardScale * Settings->CaptureReward;
				break; // one bonus per step regardless of CP count
			}
		}
	}

	// ---- Isolation penalty ----
	if (!bIsRespawnStep && Current.AllyPositions.Num() > 0)
	{
		float NearestAllyDist = FLT_MAX;
		for (int32 a = 0; a < Current.AllyPositions.Num(); ++a)
		{
			if (Current.AllyPositions[a].IsZero()) continue;
			if (a < Current.AllyHealths.Num() && Current.AllyHealths[a] <= 0.0f) continue;
			NearestAllyDist = FMath::Min(NearestAllyDist, FVector::Dist(Current.Position, Current.AllyPositions[a]));
		}
		if (NearestAllyDist > Settings->SupportReward.AllyIsolationDistance)
			Reward -= Settings->SupportReward.AllyIsolationPenalty;
	}

	// ---- Support co-stack penalty ----
	// Breaks the Support-Support deadlock where two Supports satisfy each other's
	// formation requirement and stop following frontline agents.
	// Suppressed when bFallbackToSupportTarget: rallying with other Supports is
	// correct behavior when the entire frontline has been wiped.
	if (!InOutState.bFallbackToSupportTarget && !bIsRespawnStep)
	{
		for (int32 a = 0; a < AllyCount; ++a)
		{
			if (a >= AllyChars.Num() || !AllyChars[a]) continue;
			if (AllyChars[a]->GetCommandedClass() != EDEClassType::Support) continue;
			if (a >= Current.AllyPositions.Num() || Current.AllyHealths[a] <= 0.0f) continue;
			if (FVector::Dist(Current.Position, Current.AllyPositions[a]) <= Settings->SupportAllyProximityThreshold)
			{
				Reward -= Settings->SupportReward.SupportCoStackPenalty;
				break; // one penalty regardless of how many Supports are nearby
			}
		}
	}

	// ---- Combat mode: rear-guard positioning ----
	// Only fires when enemies are visible — Support should stay behind allies in a fight,
	// but position freely (following objective) when no threat is present.
	if (bEnemiesNearby && !bIsRespawnStep && Current.AllyPositions.Num() > 0)
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
			ProximityReward += Settings->SupportReward.RearGuardBonus;
		else
			Reward -= Settings->SupportReward.FrontlinePenalty;

	}

	// ---- Heal rewards (fire in both modes when healing is occurring) ----
	if (Agent && Agent->GetLastTickHealAmount() > 0.0f)
	{
		HealReward += Settings->SupportReward.HealTickReward;

		UDEGA_Heal* HealAbility = Agent->GetHealAbility();
		if (HealAbility)
		{
			ADEAgent* HealTarget = HealAbility->GetCurrentTarget();
			if (HealTarget && HealTarget->GetHealthPercentage() < Settings->SupportReward.HealLowHPThreshold)
				HealReward += Settings->SupportReward.HealOnLowHPReward;
		}
	}

	if (Agent)
	{
		UDEGA_Heal* HealAbility = Agent->GetHealAbility();
		if (HealAbility && HealAbility->HasInjuredAllyInRange(Settings->SupportReward.NearInjuredAllyHPThreshold))
			HealReward += Settings->SupportReward.NearInjuredAllyReward;
	}

	Reward += ProximityReward;
	Reward += HealReward;
	return Reward;
}
