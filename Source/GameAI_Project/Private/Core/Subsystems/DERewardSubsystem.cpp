// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/Subsystems/DERewardSubsystem.h"
#include "Data/DERewardData.h"
#include "Characters/DECharacter.h"
#include "Types/DERewardTypes.h"
#include "Actors/DECapturePoint.h"
#include "Team/DEMatchManager.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

// ==================== Internal Helpers ====================

float UDERewardSubsystem::GetStrategyScale(EDEStrategyType Strategy, float AssaultScale, float DefendScale, float SupportScale) const
{
	switch (Strategy)
	{
	case EDEStrategyType::Assault: return AssaultScale;
	case EDEStrategyType::Defend:  return DefendScale;
	case EDEStrategyType::Support: return SupportScale;
	default:                     return 1.0f;
	}
}


float UDERewardSubsystem::DrainSparseReward(FDERewardState& InOutState, int32 AgentID)
{
	if (!CachedRewardData) return 0.0f;
	const float Drained = InOutState.CumulativeReward * CachedRewardData->SparseRewardScale;
	InOutState.CumulativeReward = 0.0f;
	return Drained;
}

// ==================== Event-Driven Sparse Rewards ====================

float UDERewardSubsystem::CalculateKillReward(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID)
{
	if (!CachedRewardData) return 0.0f;
	InOutState.bSparseKillFiredThisStep = true;
	float Scale = GetStrategyScale(ActiveStrategy, CachedRewardData->AssaultReward.KillRewardScale, CachedRewardData->DefendReward.KillRewardScale, CachedRewardData->SupportReward.KillRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::Kill, ActiveStrategy, CachedRewardData->KillReward * Scale, AgentID);
}

float UDERewardSubsystem::CalculateAssistReward(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, float DamageDealt, int32 AgentID)
{
	if (!CachedRewardData) return 0.0f;
	float DamageNorm = FMath::Clamp(DamageDealt / 100.0f, 0.0f, 1.0f);
	float Scale = GetStrategyScale(ActiveStrategy, CachedRewardData->AssaultReward.KillRewardScale, CachedRewardData->DefendReward.KillRewardScale, CachedRewardData->SupportReward.KillRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::Assist, ActiveStrategy, CachedRewardData->KillReward * CachedRewardData->AssistRewardScale * DamageNorm * Scale, AgentID);
}

float UDERewardSubsystem::CalculateDeathPenalty(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID)
{
	if (!CachedRewardData) return 0.0f;
	float Scale = GetStrategyScale(ActiveStrategy, CachedRewardData->AssaultReward.DeathScale, CachedRewardData->DefendReward.DeathScale, CachedRewardData->SupportReward.DeathScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::Death, ActiveStrategy, -CachedRewardData->DeathPenaltyReward * Scale, AgentID);
}

float UDERewardSubsystem::CalculateTeamWipePenalty(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID)
{
	if (!CachedRewardData) return 0.0f;
	float Scale = GetStrategyScale(ActiveStrategy, CachedRewardData->AssaultReward.DeathScale, CachedRewardData->DefendReward.DeathScale, CachedRewardData->SupportReward.DeathScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::TeamWipe, ActiveStrategy, -CachedRewardData->TeamWipePenalty * Scale, AgentID);
}

float UDERewardSubsystem::CalculateCaptureReward(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID)
{
	if (!CachedRewardData) return 0.0f;
	float Scale = GetStrategyScale(ActiveStrategy, CachedRewardData->AssaultReward.CaptureRewardScale, CachedRewardData->DefendReward.CaptureRewardScale, CachedRewardData->SupportReward.CaptureRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::DECapturePoint, ActiveStrategy, CachedRewardData->CaptureReward * Scale, AgentID);
}

float UDERewardSubsystem::CalculateLosePointPenalty(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID)
{
	if (!CachedRewardData) return 0.0f;
	float Scale = GetStrategyScale(ActiveStrategy, CachedRewardData->AssaultReward.LossCaptureRewardScale, CachedRewardData->DefendReward.LossCaptureRewardScale, CachedRewardData->SupportReward.LossCaptureRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::LosePoint, ActiveStrategy, CachedRewardData->LossCaptureReward * Scale, AgentID);
}

float UDERewardSubsystem::CalculateSurvivalReward(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, float CurrentHP, float MaxHP, int32 AgentID)
{
	if (!CachedRewardData || MaxHP <= 0.0f) return 0.0f;
	if (CurrentHP / MaxHP < CachedRewardData->SurvivalHPThreshold) return 0.0f;
	const float Scale = GetStrategyScale(ActiveStrategy,
		CachedRewardData->AssaultReward.SurvivalRewardScale,
		CachedRewardData->DefendReward.SurvivalRewardScale,
		CachedRewardData->SupportReward.SurvivalRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::Survival, ActiveStrategy, CachedRewardData->SurvivalReward * Scale, AgentID);
}

float UDERewardSubsystem::CalculateDistanceShaping(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, float DistanceToTarget, int32 AgentID)
{
	if (!CachedRewardData) return 0.0f;
	const float Scale = GetStrategyScale(ActiveStrategy,
		CachedRewardData->AssaultReward.PenaltyPerMeterScale,
		CachedRewardData->DefendReward.PenaltyPerMeterScale,
		CachedRewardData->SupportReward.PenaltyPerMeterScale);
	const float Reward = CachedRewardData->PenaltyPerMeter * Scale * (DistanceToTarget / 100.0f);
	return ApplyAndLogReward(InOutState, EDERewardEventType::DistanceShaping, ActiveStrategy, Reward, AgentID);
}



// ==================== Dense Per-Step Reward ====================

float UDERewardSubsystem::ComputeStepReward(
	ADECharacter* Agent,
	FDERewardState& InOutState,
	EDEStrategyType Strategy,
	const FDEAgentSnapshot& Prev,
	const FDEAgentSnapshot& Current,
	const FDEEQSWeightParameters& Action)
{
	if (!Agent || !CachedRewardData) return 0.0f;

	const UDERewardData* Settings = CachedRewardData;

	float Reward = 0.0f;
	const float PositionChange = FVector::Dist(Prev.Position, Current.Position);
	const int32 MyTeamID = Agent->GetTeamID_Implementation();
	const int32 MyEnvID = Agent->GetEnvID_Implementation();
	const bool bIsRespawnStep = !Prev.bIsAlive;

	TArray<ADECapturePoint*> EnvCapturePoints;
	for (TActorIterator<ADEMatchManager> It(GetWorld()); It; ++It)
	{
		if ((*It)->GetEnvID() == MyEnvID)
		{
			EnvCapturePoints = (*It)->GetCapturePoints();
			break;
		}
	}

	float CaptureRadiusSq_Cached = 250000.0f;
	if (EnvCapturePoints.Num() > 0 && EnvCapturePoints[0])
	{
		CaptureRadiusSq_Cached = FMath::Square(EnvCapturePoints[0]->CaptureRadius);
	}

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

	switch (Strategy)
	{
	case EDEStrategyType::Assault:
		{
			Reward += Settings->AssaultBaselineReward;
			float HealthLoss = Prev.Health - Current.Health;
			if (HealthLoss > Settings->AssaultHealthLossThreshold)
				Reward -= Settings->AssaultReward.HealthPenalty * HealthLoss;

			if (!bIsRespawnStep && EnvCapturePoints.Num() > 0)
			{
				int32 PrevFriendlyPoints = 0, CurrFriendlyPoints = 0;
				for (int32 i = 0; i < Prev.CapturePointStatuses.Num(); ++i)
				{
					if (Prev.CapturePointStatuses[i] > 0.5f) PrevFriendlyPoints++;
					if (i < Current.CapturePointStatuses.Num() && Current.CapturePointStatuses[i] > 0.5f) CurrFriendlyPoints++;
				}

				float PrevNearestDistSq = FLT_MAX, CurrNearestDistSq = FLT_MAX;
				bool bInNonFriendlyZone = false, bInFriendlyZoneAssault = false;
				float ActiveCappingProgress = 0.0f;

				for (ADECapturePoint* CP : EnvCapturePoints)
				{
					if (!CP) continue;
					float PrevDistSq = FVector::DistSquared(Prev.Position, CP->GetActorLocation());
					float CurrDistSq = FVector::DistSquared(Current.Position, CP->GetActorLocation());

					if (CP->GetTeamID_Implementation() != MyTeamID)
					{
						PrevNearestDistSq = FMath::Min(PrevNearestDistSq, PrevDistSq);
						CurrNearestDistSq = FMath::Min(CurrNearestDistSq, CurrDistSq);
						if (CurrDistSq <= CaptureRadiusSq_Cached)
						{
							bInNonFriendlyZone = true;
							ActiveCappingProgress = FMath::Max(ActiveCappingProgress, CP->GetCaptureProgress());
						}
					}
					else if (CurrDistSq <= CaptureRadiusSq_Cached)
					{
						bInFriendlyZoneAssault = true;
					}
				}

				int32 NewCaptures = CurrFriendlyPoints - PrevFriendlyPoints;
				if (NewCaptures > 0)
				{
					InOutState.PostCaptureMomentumStepsRemaining = Settings->AssaultReward.PostCaptureMomentumDuration;
					float NearestFriendlyDistSq = FLT_MAX;
					for (ADECapturePoint* CP : EnvCapturePoints)
					{
						if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;
						float DSq = FVector::DistSquared(Current.Position, CP->GetActorLocation());
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
					float ApproachDelta = FMath::Sqrt(PrevNearestDistSq) - FMath::Sqrt(CurrNearestDistSq);
					const float EffectiveDelta = ApproachDelta >= 0.0f ? ApproachDelta : ApproachDelta * 0.5f;
					Reward += Settings->AssaultReward.ObjectiveProgressReward * ApproachScale * EffectiveDelta;
				}

				if (bInNonFriendlyZone)
				{
					InOutState.AssaultZoneStepsAfterCapture = 0;
					Reward += Settings->AssaultReward.ZonePresenceBonus;
					Reward += Settings->AssaultReward.ActiveCappingBonus * ActiveCappingProgress;
				}
				else if (bInFriendlyZoneAssault && Settings->AssaultCapturedZoneDecaySteps > 0.0f)
				{
					InOutState.AssaultZoneStepsAfterCapture++;
					const float DecayFactor = FMath::Max(0.0f, 1.0f - (float)InOutState.AssaultZoneStepsAfterCapture / Settings->AssaultCapturedZoneDecaySteps);
					Reward += Settings->AssaultReward.ZonePresenceBonus * DecayFactor;
				}

				if (InOutState.PostCaptureMomentumStepsRemaining > 0)
				{
					InOutState.PostCaptureMomentumStepsRemaining--;
					if (PositionChange >= Settings->AssaultReward.PostCaptureMomentumMinMove &&
						FVector::DistSquared(Current.Position, InOutState.LastCapturedPointLocation) > CaptureRadiusSq_Cached)
					{
						Reward += Settings->AssaultReward.PostCaptureMomentumBonus;
					}
				}

				if (PositionChange < Settings->AssaultIdleMovementThreshold && !bInNonFriendlyZone)
				{
					float IdlePenalty = (CurrNearestDistSq == FLT_MAX || bIsolated) ? Settings->AssaultReward.IdlePenalty : Settings->AssaultReward.IdlePenalty * 0.5f;
					Reward -= IdlePenalty;
				}
			}
		}
		break;

	case EDEStrategyType::Defend:
		{
			Reward += Settings->DefendBaselineReward;
			if (MyTeamID >= 0 && EnvCapturePoints.Num() > 0)
			{
				float CurrNearestFriendlyDist = FLT_MAX, PrevNearestFriendlyDist = FLT_MAX;
				InOutState.bInFriendlyZone = false;

				for (ADECapturePoint* CP : EnvCapturePoints)
				{
					if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;
					const float CurrDist = FVector::Dist(Current.Position, CP->GetActorLocation());
					const float PrevDist = FVector::Dist(Prev.Position, CP->GetActorLocation());
					CurrNearestFriendlyDist = FMath::Min(CurrNearestFriendlyDist, CurrDist);
					PrevNearestFriendlyDist = FMath::Min(PrevNearestFriendlyDist, PrevDist);
					if (CurrDist <= CP->CaptureRadius) InOutState.bInFriendlyZone = true;
				}

				if (CurrNearestFriendlyDist == FLT_MAX && !bIsRespawnStep)
				{
					float PrevNonFriendlyDist = FLT_MAX, CurrNonFriendlyDist = FLT_MAX;
					bool bInNonFriendlyZone = false;
					float ActiveCappingProgress = 0.0f;
					for (ADECapturePoint* NonFriendlyCP : EnvCapturePoints)
					{
						if (!NonFriendlyCP || NonFriendlyCP->GetTeamID_Implementation() == MyTeamID) continue;
						const float PrevDist = FVector::Dist(Prev.Position, NonFriendlyCP->GetActorLocation());
						const float CurrDist = FVector::Dist(Current.Position, NonFriendlyCP->GetActorLocation());
						PrevNonFriendlyDist = FMath::Min(PrevNonFriendlyDist, PrevDist);
						CurrNonFriendlyDist = FMath::Min(CurrNonFriendlyDist, CurrDist);
						if (CurrDist <= NonFriendlyCP->CaptureRadius)
						{
							bInNonFriendlyZone = true;
							ActiveCappingProgress = FMath::Max(ActiveCappingProgress, NonFriendlyCP->GetCaptureProgress());
						}
					}

					if (PrevNonFriendlyDist < FLT_MAX && CurrNonFriendlyDist < FLT_MAX)
					{
						const float ApproachDelta = PrevNonFriendlyDist - CurrNonFriendlyDist;
						const float EffectiveDelta = ApproachDelta >= 0.0f ? ApproachDelta : ApproachDelta * 0.5f;
						const float ApproachScale = bIsolated ? Settings->IsolationApproachMultiplier : 1.0f;
						Reward += Settings->DefendReward.ZoneApproachReward * ApproachScale * EffectiveDelta;
					}

					if (bInNonFriendlyZone)
					{
						InOutState.bInFriendlyZone = false;
						Reward += Settings->DefendReward.ZonePresenceBonus;
						Reward += Settings->AssaultReward.ActiveCappingBonus * ActiveCappingProgress;
					}
					else if (PositionChange < Settings->AssaultIdleMovementThreshold)
					{
						Reward -= Settings->AssaultReward.IdlePenalty * 0.5f;
					}
				}

				if (InOutState.bInFriendlyZone)
				{
					Reward += Settings->DefendReward.ZonePresenceBonus;
					// Reward free movement within the zone; remove stationary center bias
					if (PositionChange >= Settings->ZoneMovementMinThreshold)
						Reward += Settings->ZoneMovementBonus;
					if (Current.Health > Settings->DefendHealthThreshold) Reward += Settings->DefendReward.HealthBonus;

					for (int32 i = 0; i < Current.EnemyPositions.Num(); ++i)
					{
						if (i < Current.EnemyVisible.Num() && Current.EnemyVisible[i])
						{
							for (ADECapturePoint* CP : EnvCapturePoints)
							{
								if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;
								if (FVector::Dist(Current.EnemyPositions[i], CP->GetActorLocation()) <= CP->CaptureRadius)
								{
									Reward += Settings->DefendReward.ThreatResponseBonus;
									goto ThreatResponseApplied;
								}
							}
						}
					}
				ThreatResponseApplied:;
				}
				else if (CurrNearestFriendlyDist < FLT_MAX && PrevNearestFriendlyDist < FLT_MAX)
				{
					if (!bIsRespawnStep)
					{
						const float ApproachDelta = PrevNearestFriendlyDist - CurrNearestFriendlyDist;
						const float DefendApproachScale = bIsolated ? Settings->IsolationApproachMultiplier : 1.0f;
						Reward += Settings->DefendReward.ZoneApproachReward * DefendApproachScale * FMath::Max(ApproachDelta, 0.0f);
					}
					const float DistPenalty = FMath::Min(CurrNearestFriendlyDist / 10000.0f, 1.0f) * 0.3f;
					Reward -= DistPenalty;
				}
			}

			if (!bIsRespawnStep && InOutState.bInFriendlyZone)
			{
				const float DamageTaken = Prev.Health - Current.Health;
				if (DamageTaken > 0.0f)
				{
					Reward += Settings->DefendReward.ZoneDurabilityBonus * DamageTaken;
				}
			}

			if (InOutState.bSparseKillFiredThisStep)
			{
				bool bEnemyWasNearZone = false;
				for (int32 i = 0; i < Prev.EnemyPositions.Num() && !bEnemyWasNearZone; ++i)
				{
					if (i < Prev.EnemyVisible.Num() && Prev.EnemyVisible[i])
					{
						for (ADECapturePoint* CP : EnvCapturePoints)
						{
							if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;
							if (FVector::Dist(Prev.EnemyPositions[i], CP->GetActorLocation()) <= CP->CaptureRadius * Settings->DefendReward.ZoneGuardRadius)
							{
								bEnemyWasNearZone = true;
								break;
							}
						}
					}
				}
				if (bEnemyWasNearZone)
				{
					Reward += Settings->DefendReward.ZoneGuardKillBonus;
				}
			}
		}
		break;

	case EDEStrategyType::Support:
		{
			Reward += Settings->SupportBaselineReward;
			if (PositionChange > Settings->SupportMinMoveThreshold && PositionChange < Settings->SupportMaxMoveThreshold)
				Reward += Settings->SupportReward.PositionReward;

			++InOutState.InjuredAllyStalenessCounter;
			bool bShouldReevalTarget = (InOutState.CachedInjuredAllyIdx < 0) || (InOutState.InjuredAllyStalenessCounter >= 5) ||
				(InOutState.CachedInjuredAllyIdx < Current.AllyHealths.Num() && Current.AllyHealths[InOutState.CachedInjuredAllyIdx] <= 0.0f);

			if (bShouldReevalTarget)
			{
				float LowestAllyHealth = FLT_MAX;
				int32 NewInjuredIdx = -1;
				for (int32 i = 0; i < Current.AllyHealths.Num(); ++i)
				{
					const float AllyHP = Current.AllyHealths[i];
					if (AllyHP <= 0.0f) continue;
					if (AllyHP < LowestAllyHealth)
					{
						LowestAllyHealth = AllyHP;
						NewInjuredIdx = i;
					}
				}
				InOutState.CachedInjuredAllyIdx = NewInjuredIdx;
				InOutState.InjuredAllyStalenessCounter = 0;
			}

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
					{
						Reward += Settings->SupportReward.HealthBonus;
					}
				}

				if (!bIsRespawnStep && InjuredAllyIdx < Prev.AllyPositions.Num())
				{
					const float PrevAllyDist = FVector::Dist(Prev.Position, Prev.AllyPositions[InjuredAllyIdx]);
					const float ApproachDelta = PrevAllyDist - CurrAllyDist;
					Reward += Settings->SupportReward.AllyApproachReward * FMath::Max(ApproachDelta, 0.0f);
				}
			}
			else if (bIsolated && !bIsRespawnStep)
			{
				if (Current.Health > Settings->SupportHealthThreshold) Reward += Settings->SupportReward.HealthBonus * 0.5f;

				float PrevNearestObjDist = FLT_MAX, CurrNearestObjDist = FLT_MAX;
				for (ADECapturePoint* CP : EnvCapturePoints)
				{
					if (!CP || CP->GetTeamID_Implementation() == MyTeamID) continue;
					PrevNearestObjDist = FMath::Min(PrevNearestObjDist, FVector::Dist(Prev.Position, CP->GetActorLocation()));
					CurrNearestObjDist = FMath::Min(CurrNearestObjDist, FVector::Dist(Current.Position, CP->GetActorLocation()));
				}
				if (PrevNearestObjDist < FLT_MAX && CurrNearestObjDist < FLT_MAX)
					Reward += Settings->AssaultReward.ObjectiveProgressReward * Settings->IsolationApproachMultiplier * (PrevNearestObjDist - CurrNearestObjDist);

				if (PositionChange < Settings->AssaultIdleMovementThreshold) Reward -= Settings->AssaultReward.IdlePenalty;
			}
			else
			{
				Reward += Settings->SupportReward.PositionReward * 0.5f;
			}

			if (Agent && Agent->GetLastTickHealAmount() > 0.0f)
			{
				Reward += Settings->SupportReward.HealTickReward;
			}

			if (InOutState.bSparseKillFiredThisStep)
			{
				bool bAllyInjured = false;
				for (float AllyHP : Current.AllyHealths)
				{
					if (AllyHP > 0.0f && AllyHP < 0.5f) { bAllyInjured = true; break; }
				}
				if (bAllyInjured)
				{
					Reward -= Settings->SupportReward.RoleBreakPenalty;
				}
			}

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
					for (const FVector& AllyPos : Current.AllyPositions)
					{
						if (AllyPos.IsZero()) continue;
						NearestAllyToEnemyDist = FMath::Min(NearestAllyToEnemyDist, FVector::Dist(AllyPos, NearestEnemyPos));
					}
					if (NearestEnemyDist > NearestAllyToEnemyDist) Reward += Settings->SupportReward.RearGuardBonus;
				}
			}
		}
		break;
	}


	// Survival reward (common to all strategies)
	if (!bIsRespawnStep && Current.bIsAlive)
	{
		CalculateSurvivalReward(InOutState, Strategy, Current.Health, 1.0f, Agent->AgentID);
	}

	// Minimum combat range penalty (common to all strategies)
	// Penalizes agents when a visible enemy is within MinCombatRange to discourage point-blank combat
	// and reinforce the CombatRange EQS weight as a meaningful positioning signal.
	if (!bIsRespawnStep && Settings->MinCombatRange > 0.0f && Settings->TooCloseEnemyPenalty > 0.0f)
	{
		const float MinRangeSq = FMath::Square(Settings->MinCombatRange);
		for (int32 i = 0; i < Current.EnemyPositions.Num(); ++i)
		{
			if (i < Current.EnemyVisible.Num() && Current.EnemyVisible[i])
			{
				if (FVector::DistSquared(Current.Position, Current.EnemyPositions[i]) < MinRangeSq)
				{
					Reward -= Settings->TooCloseEnemyPenalty;
					break; // one penalty per step regardless of how many close enemies
				}
			}
		}
	}
	// Close-range kill tracking — flags whether the agent was too close when a kill fired this step
	if (!bIsRespawnStep && Settings->CloseRangeKillThreshold > 0.0f)
	{
		const float KillRangeSq = FMath::Square(Settings->CloseRangeKillThreshold);
		for (int32 i = 0; i < Current.EnemyPositions.Num(); ++i)
		{
			if (i < Current.EnemyVisible.Num() && Current.EnemyVisible[i])
			{
				if (FVector::DistSquared(Current.Position, Current.EnemyPositions[i]) < KillRangeSq)
				{
					InOutState.bWasTooCloseAtKill = true;
					break;
				}
			}
		}
	}

	// Zone Control Reward
	if (MyTeamID >= 0 && EnvCapturePoints.Num() > 0)
	{
		int32 FriendlyBases = 0, EnemyBases = 0, NeutralBases = 0;
		for (const ADECapturePoint* CP : EnvCapturePoints)
		{
			if (!CP) continue;
			const int32 OwnerTeam = CP->GetTeamID_Implementation();
			if (OwnerTeam == MyTeamID) FriendlyBases++;
			else if (OwnerTeam >= 0) EnemyBases++;
			else NeutralBases++;
		}
		const float NetControl = static_cast<float>(FriendlyBases - EnemyBases) - NeutralBases * 0.5f;
		const float ZoneControlScale = GetStrategyScale(Strategy, Settings->ZoneControlAssaultScale, Settings->ZoneControlDefendScale, Settings->ZoneControlSupportScale);
		Reward += Settings->ZoneControlRewardPerBase * ZoneControlScale * NetControl;
	}

	// Cooperative base occupation shaping (Phase 5)
	Reward += ComputeBaseCooperationReward(Agent, InOutState);

	float EffectiveTimePenalty = (Strategy == EDEStrategyType::Assault) ? Settings->AssaultReward.TimePenalty : Settings->TimePenalty;
	Reward -= EffectiveTimePenalty;

	float DrainedSparse = DrainSparseReward(InOutState, Agent->AgentID);
	if (InOutState.bWasTooCloseAtKill && InOutState.bSparseKillFiredThisStep)
	{
		DrainedSparse *= Settings->CloseRangeKillPenaltyScale;
	}
	Reward += DrainedSparse;
	Reward *= Settings->GlobalRewardScale;
	Reward = FMath::Clamp(Reward, Settings->StepRewardClampMin, Settings->StepRewardClampMax);

	InOutState.bSparseKillFiredThisStep = false;
	InOutState.bWasTooCloseAtKill = false;

	// Team reward mixing
	const float CurrentIndividualReward = Reward;
	if (Settings->TeamRewardMixingRatio > 0.0f && Agent)
	{
		if (ADEMatchManager* MatchMgr = Agent->GetMatchManager())
		{
			const TArray<ADECharacter*>& Teammates = MatchMgr->GetTeamAgents(MyTeamID);
			float TeamRewardSum = 0.0f;
			int32 TeamCount = 0;
			for (ADECharacter* Mate : Teammates)
			{
				if (Mate)
				{
					TeamRewardSum += Mate->RewardState.LastIndividualStepReward;
					TeamCount++;
				}
			}
			if (TeamCount > 0)
			{
				const float TeamAvg = TeamRewardSum / static_cast<float>(TeamCount);
				Reward = (1.0f - Settings->TeamRewardMixingRatio) * CurrentIndividualReward + Settings->TeamRewardMixingRatio * TeamAvg;
			}
		}
	}
	InOutState.LastIndividualStepReward = CurrentIndividualReward;

	return Reward;
}

float UDERewardSubsystem::ComputeBaseCooperationReward(ADECharacter* Agent, FDERewardState& InOutState)
{
	if (!CachedRewardData || !Agent) return 0.0f;

	ADEMatchManager* MatchMgr = Agent->GetMatchManager();
	if (!MatchMgr) return 0.0f;

	const TArray<ADECapturePoint*>& CPs = MatchMgr->GetCapturePoints();
	if (CPs.Num() == 0) return 0.0f;

	const int32 MyTeamID = Agent->GetTeamID_Implementation();
	const FVector MyPos  = Agent->GetActorLocation();
	const float Radius   = CachedRewardData->BaseOccupationRadius;
	const float RadiusSq = Radius * Radius;

	TArray<ADECharacter*> Teammates = MatchMgr->GetTeamAgents(MyTeamID);

	float Reward = 0.0f;

	// ---- Per-base pass ----
	for (int32 i = 0; i < CPs.Num(); ++i)
	{
		ADECapturePoint* CP = CPs[i];
		if (!CP) continue;

		const FVector CPPos   = CP->GetActorLocation();
		const int32   Owner   = CP->GetTeamID_Implementation();
		const float   DistSq  = FVector::DistSquared(MyPos, CPPos);
		const bool    bNearMe = (DistSq <= RadiusSq);

		// Count allies near this base (excluding self)
		int32 AlliesNear = 0;
		for (ADECharacter* Mate : Teammates)
		{
			if (!Mate || Mate == Agent || !Mate->IsAlive()) continue;
			if (FVector::DistSquared(Mate->GetActorLocation(), CPPos) <= RadiusSq)
			{
				++AlliesNear;
			}
		}

		if (bNearMe)
		{
			// Solo occupation of an uncontrolled base
			if (Owner != MyTeamID && AlliesNear == 0)
			{
				Reward += CachedRewardData->BaseOccupationReward;
			}
			// Co-occupation penalty (2+ allies on same base)
			if (AlliesNear >= 1)
			{
				Reward -= CachedRewardData->CoOccupationPenalty;
			}

			// Assigned-base reach reward (once per episode)
			if (!InOutState.bHasReachedAssignedBase && Agent->AssignedBaseIndex == i)
			{
				InOutState.bHasReachedAssignedBase = true;
				Reward += CachedRewardData->AssignedBaseReachReward;
			}
		}

		// Undefended friendly base penalty (shared, no ally nearby)
		if (Owner == MyTeamID && AlliesNear == 0 && !bNearMe)
		{
			Reward -= CachedRewardData->UndefendedBasePenalty;
		}
	}

	return Reward;
}


float UDERewardSubsystem::ApplyAndLogReward(FDERewardState& InOutAgentState, EDERewardEventType EventType, EDEStrategyType Strategy, float RewardValue, int32 AgentID)
{
	InOutAgentState.CumulativeReward += RewardValue;
	return RewardValue;
}

void UDERewardSubsystem::ApplyMatchEndReward(int32 WinnerTeamID, const TArray<ADECharacter*>& AllAgents)
{
	if (!CachedRewardData) return;

	const float WinReward  = CachedRewardData->MatchWinReward;
	const float LossReward = CachedRewardData->MatchLossReward;

	for (ADECharacter* Agent : AllAgents)
	{
		if (!Agent) continue;

		const int32 AgentTeam = Agent->GetTeamID_Implementation();

		if (WinnerTeamID == -1)
		{
			// Tie — no terminal reward
			continue;
		}

		const float TerminalReward = (AgentTeam == WinnerTeamID) ? WinReward : LossReward;
		// Queue into sparse reward buffer so DrainSparseReward() picks it up on the next step
		Agent->RewardState.CumulativeReward += TerminalReward;

		UE_LOG(LogTemp, Log, TEXT("[DERewardSubsystem] Match end: Agent %d (Team %d) gets %.1f (%s)"),
			Agent->AgentID, AgentTeam, TerminalReward,
			(AgentTeam == WinnerTeamID) ? TEXT("WIN") : TEXT("LOSS"));
	}
}
