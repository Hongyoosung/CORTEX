// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/Reward/DESupportReward.h"
#include "Data/Reward/DERewardData.h"
#include "Types/DERewardTypes.h"
#include "Types/DEObservationTypes.h"
#include "Characters/DECharacter.h"
#include "Actors/DECapturePoint.h"
#include "Team/DEMatchManager.h"

float DEComputeSupportStepReward(
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
	float Reward = Settings->SupportBaselineReward;

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
		TArray<ADECharacter*> AllyChars;
		if (ADEMatchManager* MatchMgr = Agent->GetMatchManager())
			AllyChars = MatchMgr->GetTeamAgents(MyTeamID);

		bool bAnyNonSupportAlive = false;
		for (ADECharacter* Ally : AllyChars)
		{
			if (Ally && Ally != Agent && Ally->IsAlive() &&
				Ally->GetCommandedStrategy() != EDEStrategyType::Support)
			{ bAnyNonSupportAlive = true; break; }
		}

		float HighestAccumDamage = 0.0f;
		float LowestHP = FLT_MAX;
		int32 NewInjuredIdx = -1;

		for (int32 i = 0; i < AllyCount; ++i)
		{
			const float AllyHP = Current.AllyHealths[i];
			if (AllyHP <= 0.0f) continue;
			if (bAnyNonSupportAlive && i < AllyChars.Num() && AllyChars[i] &&
				AllyChars[i]->GetCommandedStrategy() == EDEStrategyType::Support)
				continue;

			// Primary: most accumulated recent damage
			if (i < InOutState.AllyAccumulatedDamage.Num() &&
				InOutState.AllyAccumulatedDamage[i] > HighestAccumDamage)
			{
				HighestAccumDamage = InOutState.AllyAccumulatedDamage[i];
				NewInjuredIdx = i;
			}
			// Fallback: lowest current HP (when no recent damage recorded)
			else if (HighestAccumDamage == 0.0f && AllyHP < LowestHP)
			{
				LowestHP = AllyHP;
				NewInjuredIdx = i;
			}
		}

		InOutState.CachedInjuredAllyIdx = NewInjuredIdx;
		InOutState.InjuredAllyStalenessCounter = 0;
	}

	// ---- Proximity and approach reward toward injured ally ----
	const int32 InjuredAllyIdx = InOutState.CachedInjuredAllyIdx;
	if (InjuredAllyIdx >= 0 && InjuredAllyIdx < Current.AllyPositions.Num())
	{
		const float CurrAllyDist = FVector::Dist(Current.Position, Current.AllyPositions[InjuredAllyIdx]);
		if (CurrAllyDist <= Settings->SupportAllyProximityThreshold)
		{
			const float AllyHP = Current.AllyHealths[InjuredAllyIdx];
			if (AllyHP > 0.0f && AllyHP < Settings->SupportReward.AllyInjuryThreshold)
			{
				Reward += Settings->SupportReward.AllyProximityBonus;
				if (AllyHP < 0.3f) Reward += Settings->SupportReward.AllyProximityBonus * 0.5f;
			}
			if (Current.Health > Settings->SupportHealthThreshold)
				Reward += Settings->SupportReward.HealthBonus;
		}

		if (!bIsRespawnStep && InjuredAllyIdx < Prev.AllyPositions.Num())
		{
			const float PrevAllyDist = FVector::Dist(Prev.Position, Prev.AllyPositions[InjuredAllyIdx]);
			Reward += Settings->SupportReward.AllyApproachReward * FMath::Max(PrevAllyDist - CurrAllyDist, 0.0f);
		}
	}
	else if (bIsolated && !bIsRespawnStep)
	{
		// Isolated: approach nearest enemy objective as last resort
		if (Current.Health > Settings->SupportHealthThreshold)
			Reward += Settings->SupportReward.HealthBonus * 0.5f;

		float PrevNearestObjDist = FLT_MAX, CurrNearestObjDist = FLT_MAX;
		for (ADECapturePoint* CP : EnvCapturePoints)
		{
			if (!CP || CP->GetTeamID_Implementation() == MyTeamID) continue;
			PrevNearestObjDist = FMath::Min(PrevNearestObjDist, FVector::Dist(Prev.Position, CP->GetActorLocation()));
			CurrNearestObjDist = FMath::Min(CurrNearestObjDist, FVector::Dist(Current.Position, CP->GetActorLocation()));
		}
		if (PrevNearestObjDist < FLT_MAX && CurrNearestObjDist < FLT_MAX)
			Reward += Settings->AssaultReward.ObjectiveProgressReward * Settings->IsolationApproachMultiplier * (PrevNearestObjDist - CurrNearestObjDist);

		if (PositionChange < Settings->AssaultIdleMovementThreshold)
			Reward -= Settings->AssaultReward.IdlePenalty;
	}
	else
	{
		Reward += Settings->SupportReward.PositionReward * 0.5f;
	}

	// ---- Ally formation / isolation rewards ----
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
				Reward += Settings->SupportReward.AllyFormationBonus;
			if (NearestAllyDist > Settings->SupportReward.AllyIsolationDistance)
				Reward -= Settings->SupportReward.AllyIsolationPenalty;
		}
	}

	// ---- Heal tick reward ----
	if (Agent && Agent->GetLastTickHealAmount() > 0.0f)
		Reward += Settings->SupportReward.HealTickReward;

	// ---- Rear-guard positioning rewards ----
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
				Reward += Settings->SupportReward.RearGuardBonus;
			}
			else
			{
				Reward -= Settings->SupportReward.FrontlinePenalty;
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
						Reward += Settings->SupportReward.AllyShieldBonus;
						break;
					}
				}
			}
		}
	}

	return Reward;
}
