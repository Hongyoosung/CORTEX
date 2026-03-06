// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/Subsystems/MocRewardSubsystem.h"
#include "Characters/MocCharacter.h"
#include "Actors/CapturePoint.h"
#include "Team/MatchManager.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

// ==================== Internal Helpers ====================

float UMocRewardSubsystem::GetStrategyScale(EStrategyType Strategy, float AssaultScale, float DefendScale, float SupportScale) const
{
	switch (Strategy)
	{
	case EStrategyType::Assault: return AssaultScale;
	case EStrategyType::Defend:  return DefendScale;
	case EStrategyType::Support: return SupportScale;
	default:                     return 1.0f;
	}
}



float UMocRewardSubsystem::DrainSparseReward(FRewardState& InOutState, const URewardSettingsDataAsset* Settings, int32 AgentID)
{
	if (!Settings) return 0.0f;
	const float Drained = InOutState.CumulativeReward * Settings->SparseRewardScale;
	InOutState.CumulativeReward = 0.0f;
	return Drained;
}

// ==================== Event-Driven Sparse Rewards ====================

float UMocRewardSubsystem::CalculateKillReward(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, int32 AgentID)
{
	if (!Settings) return 0.0f;
	InOutState.bSparseKillFiredThisStep = true;
	float Scale = GetStrategyScale(ActiveStrategy, Settings->AssaultReward.KillRewardScale, Settings->DefendReward.KillRewardScale, Settings->SupportReward.KillRewardScale);
	return ApplyAndLogReward(InOutState, ERewardEventType::Kill, ActiveStrategy, Settings->KillReward * Scale, AgentID);
}

float UMocRewardSubsystem::CalculateAssistReward(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, float DamageDealt, int32 AgentID)
{
	if (!Settings) return 0.0f;
	float DamageNorm = FMath::Clamp(DamageDealt / 100.0f, 0.0f, 1.0f);
	float Scale = GetStrategyScale(ActiveStrategy, Settings->AssaultReward.KillRewardScale, Settings->DefendReward.KillRewardScale, Settings->SupportReward.KillRewardScale);
	return ApplyAndLogReward(InOutState, ERewardEventType::Assist, ActiveStrategy, Settings->KillReward * Settings->AssistRewardScale * DamageNorm * Scale, AgentID);
}

float UMocRewardSubsystem::CalculateDeathPenalty(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, int32 AgentID)
{
	if (!Settings) return 0.0f;
	float Scale = GetStrategyScale(ActiveStrategy, Settings->AssaultReward.DeathScale, Settings->DefendReward.DeathScale, Settings->SupportReward.DeathScale);
	return ApplyAndLogReward(InOutState, ERewardEventType::Death, ActiveStrategy, -Settings->DeathPenaltyReward * Scale, AgentID);
}

float UMocRewardSubsystem::CalculateTeamWipePenalty(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, int32 AgentID)
{
	if (!Settings) return 0.0f;
	float Scale = GetStrategyScale(ActiveStrategy, Settings->AssaultReward.DeathScale, Settings->DefendReward.DeathScale, Settings->SupportReward.DeathScale);
	return ApplyAndLogReward(InOutState, ERewardEventType::TeamWipe, ActiveStrategy, -Settings->TeamWipePenalty * Scale, AgentID);
}

float UMocRewardSubsystem::CalculateCaptureReward(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, int32 AgentID)
{
	if (!Settings) return 0.0f;
	float Scale = GetStrategyScale(ActiveStrategy, Settings->AssaultReward.CaptureRewardScale, Settings->DefendReward.CaptureRewardScale, Settings->SupportReward.CaptureRewardScale);
	return ApplyAndLogReward(InOutState, ERewardEventType::CapturePoint, ActiveStrategy, Settings->CaptureReward * Scale, AgentID);
}

float UMocRewardSubsystem::CalculateLosePointPenalty(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, int32 AgentID)
{
	if (!Settings) return 0.0f;
	float Scale = GetStrategyScale(ActiveStrategy, Settings->AssaultReward.LossCaptureRewardScale, Settings->DefendReward.LossCaptureRewardScale, Settings->SupportReward.LossCaptureRewardScale);
	return ApplyAndLogReward(InOutState, ERewardEventType::LosePoint, ActiveStrategy, Settings->LossCaptureReward * Scale, AgentID);
}

float UMocRewardSubsystem::CalculateSurvivalReward(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, float CurrentHP, float MaxHP, int32 AgentID)
{
	return 0.0f;
}

float UMocRewardSubsystem::CalculateDistanceShaping(const URewardSettingsDataAsset* Settings, FRewardState& InOutState, EStrategyType ActiveStrategy, float DistanceToTarget, int32 AgentID)
{
	return 0.0f;
}



// ==================== Dense Per-Step Reward ====================

float UMocRewardSubsystem::ComputeStepReward(
	AMocCharacter* Agent,
	const URewardSettingsDataAsset* Settings,
	FRewardState& InOutState,
	EStrategyType Strategy,
	const FObservation& Prev,
	const FObservation& Current,
	const FEQSWeightParameters& Action)
{
	if (!Agent || !Settings) return 0.0f;

	float Reward = 0.0f;
	const float PositionChange = FVector::Dist(Prev.Position, Current.Position);
	const int32 MyTeamID = Agent->GetTeamID_Implementation();
	const int32 MyEnvID = Agent->GetEnvID_Implementation();
	const bool bIsRespawnStep = !Prev.bIsAlive;

	// 환경 거점 캐싱 (Subsystem은 캐시를 들고 있지 않으므로 매 스텝 또는 참조를 받아야 함)
	TArray<ACapturePoint*> EnvCapturePoints;
	for (TActorIterator<AMatchManager> It(GetWorld()); It; ++It)
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

	// Isolation 모드 계산
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
	case EStrategyType::Assault:
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

				for (ACapturePoint* CP : EnvCapturePoints)
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
					for (ACapturePoint* CP : EnvCapturePoints)
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

	case EStrategyType::Defend:
		{
			Reward += Settings->DefendBaselineReward;
			if (MyTeamID >= 0 && EnvCapturePoints.Num() > 0)
			{
				float CurrNearestFriendlyDist = FLT_MAX, PrevNearestFriendlyDist = FLT_MAX;
				InOutState.bInFriendlyZone = false;

				for (ACapturePoint* CP : EnvCapturePoints)
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
					for (ACapturePoint* NonFriendlyCP : EnvCapturePoints)
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
					if (PositionChange < Settings->DefendStationaryThreshold) Reward += Settings->DefendReward.PositionReward;
					if (Current.Health > Settings->DefendHealthThreshold)     Reward += Settings->DefendReward.HealthBonus;
					
					// Threat response
					for (int32 i = 0; i < Current.EnemyPositions.Num(); ++i)
					{
						if (i < Current.EnemyVisible.Num() && Current.EnemyVisible[i])
						{
							for (ACapturePoint* CP : EnvCapturePoints)
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
					const float DurabilityReward = Settings->DefendReward.ZoneDurabilityBonus * DamageTaken;
					Reward += DurabilityReward;
					ApplyAndLogReward(InOutState, ERewardEventType::ZoneDurability, Strategy, DurabilityReward, Agent->AgentID);
				}
			}

			if (InOutState.bSparseKillFiredThisStep)
			{
				bool bEnemyWasNearZone = false;
				for (int32 i = 0; i < Prev.EnemyPositions.Num() && !bEnemyWasNearZone; ++i)
				{
					if (i < Prev.EnemyVisible.Num() && Prev.EnemyVisible[i])
					{
						for (ACapturePoint* CP : EnvCapturePoints)
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
					ApplyAndLogReward(InOutState, ERewardEventType::ZoneGuardKill, Strategy, Settings->DefendReward.ZoneGuardKillBonus, Agent->AgentID);
				}
			}
		}
		break;

	case EStrategyType::Support:
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
				for (ACapturePoint* CP : EnvCapturePoints)
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
				ApplyAndLogReward(InOutState, ERewardEventType::HealAlly, Strategy, Settings->SupportReward.HealTickReward, Agent->AgentID);
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
					ApplyAndLogReward(InOutState, ERewardEventType::Kill, Strategy, -Settings->SupportReward.RoleBreakPenalty, Agent->AgentID);
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

	// Zone Control Reward
	if (MyTeamID >= 0 && EnvCapturePoints.Num() > 0)
	{
		int32 FriendlyBases = 0, EnemyBases = 0, NeutralBases = 0;
		for (const ACapturePoint* CP : EnvCapturePoints)
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

	float EffectiveTimePenalty = (Strategy == EStrategyType::Assault) ? Settings->AssaultReward.TimePenalty : Settings->TimePenalty;
	Reward -= EffectiveTimePenalty;

	// 희소 보상 Drain 및 정규화
	Reward += DrainSparseReward(InOutState, Settings, Agent->AgentID);
	Reward *= Settings->GlobalRewardScale;
	Reward = FMath::Clamp(Reward, Settings->StepRewardClampMin, Settings->StepRewardClampMax);

	InOutState.bSparseKillFiredThisStep = false;

	// Team reward mixing
	const float CurrentIndividualReward = Reward;
	if (Settings->TeamRewardMixingRatio > 0.0f && Agent)
	{
		if (AMatchManager* MatchMgr = Agent->GetMatchManager())
		{
			const TArray<AMocCharacter*>& Teammates = MatchMgr->GetTeamAgents(MyTeamID);
			float TeamRewardSum = 0.0f;
			int32 TeamCount = 0;
			for (AMocCharacter* Mate : Teammates)
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

float UMocRewardSubsystem::ApplyAndLogReward(FRewardState& InOutAgentState, ERewardEventType EventType, EStrategyType Strategy, float RewardValue, int32 AgentID)
{
	float Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	

	return RewardValue;
}