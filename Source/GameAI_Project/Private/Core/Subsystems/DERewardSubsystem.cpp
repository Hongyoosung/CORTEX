// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/Subsystems/DERewardSubsystem.h"
#include "Data/DERewardData.h"
#include "Characters/DECharacter.h"
#include "Types/DERewardTypes.h"
#include "Actors/DECapturePoint.h"
#include "Team/DEMatchManager.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

// ==================== UDynamicEQSRewardCalculatorBase Overrides ====================

float UDERewardSubsystem::CalculateStepReward(const FDynamicEQSStepContext& Context)
{
	// Full per-step reward requires game context (character, snapshot, strategy).
	// Use ComputeStepReward() directly. This override satisfies the base class contract.
	return 0.0f;
}

float UDERewardSubsystem::CalculateTerminalReward(bool bWon)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	return bWon ? Settings->TerminalWinReward : Settings->TerminalLossReward;
}

void UDERewardSubsystem::Reset()
{
	Super::Reset();
}

// ==================== Internal Helpers ====================

const UDERewardData* UDERewardSubsystem::GetSettings() const
{
	return Cast<UDERewardData>(RewardData);
}

float UDERewardSubsystem::GetStrategyScale(EDEStrategyType Strategy, float AssaultScale, float DefendScale, float SupportScale) const
{
	switch (Strategy)
	{
	case EDEStrategyType::Assault: return AssaultScale;
	case EDEStrategyType::Defend:  return DefendScale;
	case EDEStrategyType::Support: return SupportScale;
	default:                       return 1.0f;
	}
}


float UDERewardSubsystem::DrainSparseReward(FDERewardState& InOutState, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	const float Drained = InOutState.CumulativeReward * Settings->SparseRewardScale;
	InOutState.CumulativeReward = 0.0f;
	return Drained;
}

// ==================== Event-Driven Sparse Rewards ====================

float UDERewardSubsystem::CalculateKillReward(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	InOutState.bSparseKillFiredThisStep = true;
	float Scale = GetStrategyScale(ActiveStrategy, Settings->AssaultReward.KillRewardScale, Settings->DefendReward.KillRewardScale, Settings->SupportReward.KillRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::Kill, ActiveStrategy, Settings->KillReward * Scale, AgentID);
}

float UDERewardSubsystem::CalculateAssistReward(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, float DamageDealt, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	float DamageNorm = FMath::Clamp(DamageDealt / 100.0f, 0.0f, 1.0f);
	float Scale = GetStrategyScale(ActiveStrategy, Settings->AssaultReward.KillRewardScale, Settings->DefendReward.KillRewardScale, Settings->SupportReward.KillRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::Assist, ActiveStrategy, Settings->KillReward * Settings->AssistRewardScale * DamageNorm * Scale, AgentID);
}

float UDERewardSubsystem::CalculateDeathPenalty(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	float Scale = GetStrategyScale(ActiveStrategy, Settings->AssaultReward.DeathScale, Settings->DefendReward.DeathScale, Settings->SupportReward.DeathScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::Death, ActiveStrategy, -Settings->DeathPenaltyReward * Scale, AgentID);
}

float UDERewardSubsystem::CalculateTeamWipePenalty(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	float Scale = GetStrategyScale(ActiveStrategy, Settings->AssaultReward.DeathScale, Settings->DefendReward.DeathScale, Settings->SupportReward.DeathScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::TeamWipe, ActiveStrategy, -Settings->TeamWipePenalty * Scale, AgentID);
}

float UDERewardSubsystem::CalculateCaptureReward(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	float Scale = GetStrategyScale(ActiveStrategy, Settings->AssaultReward.CaptureRewardScale, Settings->DefendReward.CaptureRewardScale, Settings->SupportReward.CaptureRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::DECapturePoint, ActiveStrategy, Settings->CaptureReward * Scale, AgentID);
}

float UDERewardSubsystem::CalculateLosePointPenalty(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	float Scale = GetStrategyScale(ActiveStrategy, Settings->AssaultReward.LossCaptureRewardScale, Settings->DefendReward.LossCaptureRewardScale, Settings->SupportReward.LossCaptureRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::LosePoint, ActiveStrategy, Settings->LossCaptureReward * Scale, AgentID);
}

float UDERewardSubsystem::CalculateSurvivalReward(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, float CurrentHP, float MaxHP, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings || MaxHP <= 0.0f) return 0.0f;
	if (CurrentHP / MaxHP < Settings->SurvivalHPThreshold) return 0.0f;
	const float Scale = GetStrategyScale(ActiveStrategy,
		Settings->AssaultReward.SurvivalRewardScale,
		Settings->DefendReward.SurvivalRewardScale,
		Settings->SupportReward.SurvivalRewardScale);
	return ApplyAndLogReward(InOutState, EDERewardEventType::Survival, ActiveStrategy, Settings->SurvivalBonus * Scale, AgentID);
}

float UDERewardSubsystem::CalculateDistanceShaping(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, float DistanceToTarget, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	const float Scale = GetStrategyScale(ActiveStrategy,
		Settings->AssaultReward.PenaltyPerMeterScale,
		Settings->DefendReward.PenaltyPerMeterScale,
		Settings->SupportReward.PenaltyPerMeterScale);
	const float Reward = Settings->PenaltyPerMeter * Scale * (DistanceToTarget / 100.0f);
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
	if (!Agent) {
		UE_LOG(LogTemp, Error, TEXT("DERewardSubsystem: Invalid Agent reference, returning 0 reward"));
		return 0.0f;
	}
	const UDERewardData* Settings = GetSettings();
	if (!Settings)
	{
		UE_LOG(LogTemp, Error, TEXT("DERewardSubsystem: Missing RewardData, returning 0 reward"));
		return 0.0f;
	}

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
					const float DistPenalty = FMath::Min(CurrNearestFriendlyDist / 10000.0f, 1.0f) * Settings->DefendReward.OutOfZonePenaltyScale;
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

				// Threat elimination kill bonus (enemy was within ThreatZoneRadius of any friendly base)
				bool bEnemyWasThreateningBase = false;
				for (int32 i = 0; i < Prev.EnemyPositions.Num() && !bEnemyWasThreateningBase; ++i)
				{
					if (i < Prev.EnemyVisible.Num() && Prev.EnemyVisible[i])
					{
						for (ADECapturePoint* CP : EnvCapturePoints)
						{
							if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;
							if (FVector::Dist(Prev.EnemyPositions[i], CP->GetActorLocation()) <= CP->CaptureRadius * Settings->DefendReward.ThreatZoneRadius)
							{
								bEnemyWasThreateningBase = true;
								break;
							}
						}
					}
				}
				if (bEnemyWasThreateningBase)
				{
					Reward += Settings->DefendReward.ThreatEliminationKillBonus;
				}
			}

			// --- Threat Elimination: approach enemies threatening friendly bases (no-Assault fallback) ---
			if (!bIsRespawnStep && MyTeamID >= 0)
			{
				// Check if any Assault allies are alive
				bool bAnyAssaultAlive = false;
				if (ADEMatchManager* MatchMgr = Agent->GetMatchManager())
				{
					for (ADECharacter* Ally : MatchMgr->GetTeamAgents(MyTeamID))
					{
						if (Ally && Ally != Agent && Ally->IsAlive() &&
							Ally->GetCommandedStrategy() == EDEStrategyType::Assault)
						{ bAnyAssaultAlive = true; break; }
					}
				}

				if (!bAnyAssaultAlive)
				{
					// Find the nearest enemy that's within ThreatZoneRadius of any friendly base
					float BestEnemyDist = FLT_MAX;
					float PrevBestEnemyDist = FLT_MAX;
					for (int32 i = 0; i < Current.EnemyPositions.Num(); ++i)
					{
						if (i >= Current.EnemyVisible.Num() || !Current.EnemyVisible[i]) continue;

						// Check if this enemy threatens a friendly base
						bool bThreatsBase = false;
						for (ADECapturePoint* CP : EnvCapturePoints)
						{
							if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;
							if (FVector::Dist(Current.EnemyPositions[i], CP->GetActorLocation()) <= CP->CaptureRadius * Settings->DefendReward.ThreatZoneRadius)
							{ bThreatsBase = true; break; }
						}
						if (!bThreatsBase) continue;

						const float CurrDist = FVector::Dist(Current.Position, Current.EnemyPositions[i]);
						if (CurrDist < BestEnemyDist)
						{
							BestEnemyDist = CurrDist;
							if (i < Prev.EnemyPositions.Num())
								PrevBestEnemyDist = FVector::Dist(Prev.Position, Prev.EnemyPositions[i]);
						}
					}

					if (BestEnemyDist < FLT_MAX)
					{
						// Approach shaping
						if (PrevBestEnemyDist < FLT_MAX)
						{
							const float ApproachDelta = PrevBestEnemyDist - BestEnemyDist;
							Reward += Settings->DefendReward.ThreatApproachReward * FMath::Max(ApproachDelta, 0.0f);
						}
						// Engagement bonus when within weapon range of the threatening enemy
						if (BestEnemyDist <= 2000.0f)
						{
							Reward += Settings->DefendReward.ThreatEngagementBonus;
						}
					}
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
				// Collect live ally characters to check strategy (prevents support-on-support heal loops)
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

				float LowestAllyHealth = FLT_MAX;
				int32 NewInjuredIdx = -1;
				for (int32 i = 0; i < Current.AllyHealths.Num(); ++i)
				{
					const float AllyHP = Current.AllyHealths[i];
					if (AllyHP <= 0.0f) continue;
					// Skip support allies when non-support allies are alive
					if (bAnyNonSupportAlive && i < AllyChars.Num() && AllyChars[i] &&
						AllyChars[i]->GetCommandedStrategy() == EDEStrategyType::Support)
						continue;
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
					// Find nearest alive ally distance to enemy
					float NearestAllyToEnemyDist = FLT_MAX;
					float NearestAllyToSelfDist = FLT_MAX;
					for (int32 a = 0; a < Current.AllyPositions.Num(); ++a)
					{
						if (Current.AllyPositions[a].IsZero()) continue;
						if (a < Current.AllyHealths.Num() && Current.AllyHealths[a] <= 0.0f) continue;
						NearestAllyToEnemyDist = FMath::Min(NearestAllyToEnemyDist, FVector::Dist(Current.AllyPositions[a], NearestEnemyPos));
						NearestAllyToSelfDist = FMath::Min(NearestAllyToSelfDist, FVector::Dist(Current.AllyPositions[a], Current.Position));
					}

					// Rear guard bonus: Support is farther from enemy than nearest ally
					if (NearestEnemyDist > NearestAllyToEnemyDist)
					{
						Reward += Settings->SupportReward.RearGuardBonus;
					}
					else
					{
						// Frontline penalty: Support is closer to enemy than nearest ally
						Reward -= Settings->SupportReward.FrontlinePenalty;
					}

					// Ally shield bonus: at least one ally is between Support and the nearest enemy
					if (NearestAllyToEnemyDist < FLT_MAX)
					{
						// Check if any ally is roughly between us and the enemy
						for (int32 a = 0; a < Current.AllyPositions.Num(); ++a)
						{
							if (Current.AllyPositions[a].IsZero()) continue;
							if (a < Current.AllyHealths.Num() && Current.AllyHealths[a] <= 0.0f) continue;
							const float AllyToEnemy = FVector::Dist(Current.AllyPositions[a], NearestEnemyPos);
							const float AllyToSelf = FVector::Dist(Current.AllyPositions[a], Current.Position);
							// Ally is "between" if closer to enemy than we are AND closer to us than the enemy is
							if (AllyToEnemy < NearestEnemyDist && AllyToSelf < NearestEnemyDist)
							{
								Reward += Settings->SupportReward.AllyShieldBonus;
								break;
							}
						}
					}
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

	// Minimum combat range penalty
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
					break;
				}
			}
		}
	}
	// Close-range kill tracking
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

	// Cooperative base occupation shaping
	Reward += ComputeBaseCooperationReward(Agent, InOutState, Strategy);

	// Step penalty — StepPenalty is negative (inherited from UDynamicEQSRewardData)
	const float AssaultStepPenalty = Settings->AssaultReward.TimePenalty;
	const float EffectiveStepPenalty = (Strategy == EDEStrategyType::Assault) ? AssaultStepPenalty : -Settings->StepPenalty;
	Reward -= EffectiveStepPenalty;

	float DrainedSparse = DrainSparseReward(InOutState, Agent->AgentID);
	if (InOutState.bWasTooCloseAtKill && InOutState.bSparseKillFiredThisStep)
	{
		DrainedSparse *= Settings->CloseRangeKillPenaltyScale;
	}
	Reward += DrainedSparse;
	Reward *= Settings->RewardScale;
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

float UDERewardSubsystem::ComputeBaseCooperationReward(ADECharacter* Agent, FDERewardState& InOutState, EDEStrategyType Strategy)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings || !Agent) return 0.0f;

	ADEMatchManager* MatchMgr = Agent->GetMatchManager();
	if (!MatchMgr) return 0.0f;

	const TArray<ADECapturePoint*>& CPs = MatchMgr->GetCapturePoints();
	if (CPs.Num() == 0) return 0.0f;

	const int32 MyTeamID = Agent->GetTeamID_Implementation();
	const FVector MyPos = Agent->GetActorLocation();
	const float Radius = Settings->BaseOccupationRadius;
	const float RadiusSq = Radius * Radius;

	TArray<ADECharacter*> Teammates = MatchMgr->GetTeamAgents(MyTeamID);

	// =========================================================
	// 1. 전선(Frontline) 거점 식별 로직
	// 적/중립 거점과 가장 가까운 아군 거점들을 찾아냅니다.
	// =========================================================
	TArray<ADECapturePoint*> FriendlyBases;
	TArray<ADECapturePoint*> HostileBases;

	for (ADECapturePoint* CP : CPs)
	{
		if (!CP) continue;
		if (CP->GetTeamID_Implementation() == MyTeamID) FriendlyBases.Add(CP);
		else HostileBases.Add(CP);
	}

	TArray<ADECapturePoint*> FrontlineBases;
	if (HostileBases.Num() > 0 && FriendlyBases.Num() > 0)
	{
		float MinDistSqToFront = FLT_MAX;

		// 가장 가까운 적/중립 거점과의 최소 거리 탐색
		for (ADECapturePoint* FBase : FriendlyBases)
		{
			for (ADECapturePoint* HBase : HostileBases)
			{
				const float DistSq = FVector::DistSquared(FBase->GetActorLocation(), HBase->GetActorLocation());
				if (DistSq < MinDistSqToFront) MinDistSqToFront = DistSq;
			}
		}

		// 최소 거리 기준, 약간의 여유(약 1.2배 거리) 내에 있는 거점들을 모두 전선으로 간주
		const float FrontlineMarginSq = MinDistSqToFront * 1.44f;
		for (ADECapturePoint* FBase : FriendlyBases)
		{
			for (ADECapturePoint* HBase : HostileBases)
			{
				if (FVector::DistSquared(FBase->GetActorLocation(), HBase->GetActorLocation()) <= FrontlineMarginSq)
				{
					FrontlineBases.Add(FBase);
					break; // 이 아군 거점은 전선임
				}
			}
		}
	}
	else
	{
		// 적 거점이 없거나(승리 직전) 아군 거점이 없다면 모든 아군 거점을 동일하게 취급
		FrontlineBases = FriendlyBases;
	}

	// =========================================================
	// 2. 역할별 보상 계산 로직
	// =========================================================
	float Reward = 0.0f;

	for (int32 i = 0; i < CPs.Num(); ++i)
	{
		ADECapturePoint* CP = CPs[i];
		if (!CP) continue;

		const FVector CPPos = CP->GetActorLocation();
		const int32   Owner = CP->GetTeamID_Implementation();
		const float   DistSq = FVector::DistSquared(MyPos, CPPos);
		const bool    bNearMe = (DistSq <= RadiusSq);
		const bool    bIsFrontline = FrontlineBases.Contains(CP);

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
			// 1) 내가 거점 안에 있을 때의 보상/페널티
			if (Owner != MyTeamID && AlliesNear == 0)
			{
				// 적/중립 거점에 진입 (Assault가 주로 받을 보상)
				Reward += Settings->BaseOccupationReward;
			}
			else if (Owner == MyTeamID && Strategy == EDEStrategyType::Defend)
			{
				// 아군 거점 방어 (Defend 전용)
				if (bIsFrontline && AlliesNear == 0)
				{
					Reward += Settings->BaseOccupationReward; // 전선 방어 보상
				}
				else if (!bIsFrontline)
				{
					Reward -= Settings->BaseOccupationReward * 0.5f; // 후방 캠핑 페널티
				}
			}

			// 2) 겹침 페널티 (CoOccupationPenalty)
			if (AlliesNear >= 1)
			{
				if (Strategy == EDEStrategyType::Defend)
				{
					Reward -= Settings->CoOccupationPenalty; // 수비수는 넓게 퍼져야 함
				}
				else if (Strategy == EDEStrategyType::Assault && Owner == MyTeamID)
				{
					Reward -= Settings->CoOccupationPenalty * 0.5f; // 강습조도 본진에 뭉쳐있으면 페널티
				}
			}

			// 3) 할당된 베이스 도달 보상
			if (!InOutState.bHasReachedAssignedBase && Agent->AssignedBaseIndex == i)
			{
				InOutState.bHasReachedAssignedBase = true;
				Reward += Settings->AssignedBaseReachReward;
			}
		}

		// 3) 빈집 페널티 (UndefendedBasePenalty)
		// 오직 Defend 전략을 가진 에이전트에게만, 그리고 '전선(Frontline)' 거점이 비었을 때만 부여합니다.
		if (Owner == MyTeamID && AlliesNear == 0 && !bNearMe)
		{
			if (Strategy == EDEStrategyType::Defend && bIsFrontline)
			{
				Reward -= Settings->UndefendedBasePenalty;
			}
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
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return;

	for (ADECharacter* Agent : AllAgents)
	{
		if (!Agent) continue;

		const int32 AgentTeam = Agent->GetTeamID_Implementation();

		if (WinnerTeamID == -1)
		{
			// Tie — no terminal reward
			continue;
		}

		const float TerminalReward = CalculateTerminalReward(AgentTeam == WinnerTeamID);
		Agent->RewardState.CumulativeReward += TerminalReward;

		UE_LOG(LogTemp, Log, TEXT("[DERewardSubsystem] Match end: Agent %d (Team %d) gets %.1f (%s)"),
			Agent->AgentID, AgentTeam, TerminalReward,
			(AgentTeam == WinnerTeamID) ? TEXT("WIN") : TEXT("LOSS"));
	}
}
