// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/Reward/DESupportReward.h"
#include "Data/Reward/DERewardData.h"
#include "Types/DERewardTypes.h"
#include "Types/DEObservationTypes.h"
#include "Characters/DEAgent.h"
#include "Actors/DECapturePoint.h"
#include "Team/DEMatchManager.h"
#include "GAS/Abilities/DEGA_Heal.h"
#include "Actors/DESpawnArea.h"

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
	const FDESupportRewardSettings& S = Settings->SupportReward;
	float Reward = Settings->SupportBaselineReward;

	// ================================================================
	// 1. HEAL REWARDS — the primary objective
	// ================================================================

	if (Agent && Agent->GetLastTickHealAmount() > 0.0f)
	{
		Reward += S.HealTickReward;

		UDEGA_Heal* HealAbility = Agent->GetHealAbility();
		if (HealAbility)
		{
			ADEAgent* HealTarget = HealAbility->GetCurrentTarget();
			if (HealTarget && HealTarget->GetHealthPercentage() < S.HealLowHPThreshold)
				Reward += S.HealOnLowHPReward;
		}
	}

	// ================================================================
	// 2. ALLY FOLLOW — stay within heal range of allies
	// ================================================================

	// Fetch ally list to distinguish Support from non-Support
	TArray<ADEAgent*> AllyChars;
	if (ADEMatchManager* MatchMgr = Agent ? Agent->GetMatchManager() : nullptr)
		AllyChars = MatchMgr->GetTeamAgents(MyTeamID);

	if (!bIsRespawnStep && Current.AllyPositions.Num() > 0)
	{
		// Find nearest alive NON-SUPPORT ally distance.
		// Excludes other Supports so two Supports can't farm InHealRangeBonus off each other.
		float NearestAllyDist = FLT_MAX;
		int32 NearestAllyIdx = -1;
		for (int32 a = 0; a < Current.AllyPositions.Num(); ++a)
		{
			if (Current.AllyPositions[a].IsZero()) continue;
			if (a < Current.AllyHealths.Num() && Current.AllyHealths[a] <= 0.0f) continue;
			if (a < AllyChars.Num() && AllyChars[a] &&
				AllyChars[a]->GetCommandedClass() == EDEClassType::Support)
				continue;
			const float D = FVector::Dist(Current.Position, Current.AllyPositions[a]);
			if (D < NearestAllyDist)
			{
				NearestAllyDist = D;
				NearestAllyIdx = a;
			}
		}

		if (NearestAllyIdx >= 0)
		{
			// Heal range from config is 800cm; use SupportAllyProximityThreshold as the reward threshold
			// since it's slightly larger (1200cm) giving a comfortable margin
			const float HealRangeThreshold = Settings->SupportAllyProximityThreshold;

			if (NearestAllyDist <= HealRangeThreshold)
			{
				// In heal range — flat bonus
				Reward += S.InHealRangeBonus;
			}
			else
			{
				// Outside heal range — approach shaping
				if (NearestAllyIdx < Prev.AllyPositions.Num())
				{
					const float PrevDist = FVector::Dist(Prev.Position, Prev.AllyPositions[NearestAllyIdx]);
					const float Approach = PrevDist - NearestAllyDist;
					if (Approach > 0.0f)
						Reward += S.AllyApproachReward * Approach;
				}
			}
		}
	}

	// ================================================================
	// 3. ENEMY AVOIDANCE — farther from enemies = more reward
	// ================================================================

	if (!bIsRespawnStep)
	{
		float NearestEnemyDist = FLT_MAX;
		for (int32 i = 0; i < Current.EnemyPositions.Num(); ++i)
		{
			if (i < Current.EnemyVisible.Num() && Current.EnemyVisible[i])
			{
				const float D = FVector::Dist(Current.Position, Current.EnemyPositions[i]);
				NearestEnemyDist = FMath::Min(NearestEnemyDist, D);
			}
		}

		if (NearestEnemyDist < FLT_MAX)
		{
			// Continuous reward: more distance = more reward, clamped at EnemyDistanceNorm
			const float NormDist = FMath::Clamp(NearestEnemyDist / S.EnemyDistanceNorm, 0.0f, 1.0f);
			Reward += S.EnemyDistanceBonus * NormDist;
		}
	}

	// ================================================================
	// 4. ALLY CAPTURE — contest CPs with allies when no enemies nearby
	// ================================================================

	if (!bIsRespawnStep)
	{
		// Check if any enemies are visible
		bool bEnemiesVisible = false;
		for (int32 i = 0; i < Current.EnemyVisible.Num(); ++i)
		{
			if (Current.EnemyVisible[i])
			{
				bEnemiesVisible = true;
				break;
			}
		}

		if (!bEnemiesVisible)
		{
			for (ADECapturePoint* CP : EnvCapturePoints)
			{
				if (!CP || CP->GetTeamID_Implementation() == MyTeamID) continue;
				if (FVector::DistSquared(Current.Position, CP->GetActorLocation()) > CaptureRadiusSq) continue;

				// Check if at least one alive ally is also in the capture zone
				bool bAllyPresent = false;
				for (int32 a = 0; a < Current.AllyPositions.Num(); ++a)
				{
					if (a < Current.AllyHealths.Num() && Current.AllyHealths[a] <= 0.0f) continue;
					if (FVector::DistSquared(Current.AllyPositions[a], CP->GetActorLocation()) > CaptureRadiusSq) continue;
					bAllyPresent = true;
					break;
				}

				if (bAllyPresent)
				{
					Reward += S.AllyCaptureBonus;
					break;
				}
			}
		}
	}

	// ================================================================
	// 5. BASE LOITER PENALTY — shared with other roles, don't camp spawn
	// ================================================================

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
