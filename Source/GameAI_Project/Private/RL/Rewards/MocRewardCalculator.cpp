// Copyright Epic Games, Inc. All Rights Reserved.

#include "RL/Rewards/MocRewardCalculator.h"
#include "Characters/MocCharacter.h"
#include "Actors/CapturePoint.h"
#include "Team/MatchManager.h"
#include "Schola/ScholaEnvironment.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"


UMocRewardCalculator::UMocRewardCalculator()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMocRewardCalculator::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<AMocCharacter>(GetOwner());
	CacheCapturePoints();


	// Subscribe to match state changes for win/loss terminal reward (multi-env: bind to owning ScholaEnvironment)
	const int32 MyEnvID = OwnerCharacter ? OwnerCharacter->GetEnvID_Implementation() : 0;
	for (TActorIterator<AScholaEnvironment> It(GetWorld()); It; ++It)
	{
		if ((*It)->ScholaEnvID == MyEnvID)
		{
			(*It)->OnEnvMatchStateChanged.AddDynamic(this, &UMocRewardCalculator::OnMatchStateChanged);
			break;
		}
	}
}

// ==================== Internal Helpers ====================

float UMocRewardCalculator::GetStrategyScale(EStrategyType Strategy, float AssaultScale, float DefendScale, float SupportScale) const
{
	switch (Strategy)
	{
	case EStrategyType::Assault: return AssaultScale;
	case EStrategyType::Defend:  return DefendScale;
	case EStrategyType::Support: return SupportScale;
	default:                     return 1.0f;
	}
}

float UMocRewardCalculator::ApplyAndLogReward(ERewardEventType EventType, EStrategyType Strategy, float RewardValue)
{
	AddReward(RewardValue);
	return RewardValue;
}

void UMocRewardCalculator::AddReward(float Value)
{
	CumulativeReward += Value;
}

float UMocRewardCalculator::DrainSparseReward()
{
	const float Drained = CumulativeReward * SparseRewardScale;
	CumulativeReward = 0.0f;
	return Drained;
}



void UMocRewardCalculator::OnMatchStateChanged(EMocMatchState NewState)
{
	if (!OwnerCharacter) return;

	const int32 MyTeam = OwnerCharacter->GetTeamID_Implementation();

	bool bTeamWon = false;
	bool bMatchOver = false;

	switch (NewState)
	{
	case EMocMatchState::RedTeamWon:
		bTeamWon = (MyTeam == 0);
		bMatchOver = true;
		break;
	case EMocMatchState::BlueTeamWon:
		bTeamWon = (MyTeam == 1);
		bMatchOver = true;
		break;
	case EMocMatchState::TimeExpired:
		{
			// On time expiry, check scores via the owning ScholaEnvironment
			const int32 MyEnvID2 = OwnerCharacter ? OwnerCharacter->GetEnvID_Implementation() : 0;
			for (TActorIterator<AScholaEnvironment> It(GetWorld()); It; ++It)
			{

			}
			bMatchOver = true;
		}
		break;
	default:
		break;
	}

}


// ==================== Event-Driven Sparse Rewards ====================

float UMocRewardCalculator::CalculateKillReward(EStrategyType ActiveStrategy)
{
	bSparseKillFiredThisStep = true;
	float Scale = GetStrategyScale(ActiveStrategy,
		AssaultReward.KillRewardScale, DefendReward.KillRewardScale, SupportReward.KillRewardScale);
	return ApplyAndLogReward(ERewardEventType::Kill, ActiveStrategy, KillReward * Scale);
}

float UMocRewardCalculator::CalculateAssistReward(EStrategyType ActiveStrategy, float DamageDealt)
{
	// DamageDealt is in [0, 100] (normalized damage contribution)
	float DamageNorm = FMath::Clamp(DamageDealt / 100.0f, 0.0f, 1.0f);
	float Scale = GetStrategyScale(ActiveStrategy,
		AssaultReward.KillRewardScale, DefendReward.KillRewardScale, SupportReward.KillRewardScale);
	return ApplyAndLogReward(ERewardEventType::Assist, ActiveStrategy, KillReward * AssistRewardScale * DamageNorm * Scale);
}

float UMocRewardCalculator::CalculateDeathPenalty(EStrategyType ActiveStrategy)
{
	float Scale = GetStrategyScale(ActiveStrategy,
		AssaultReward.DeathScale, DefendReward.DeathScale, SupportReward.DeathScale);
	return ApplyAndLogReward(ERewardEventType::Death, ActiveStrategy, -DeathPenaltyReward * Scale);
}

float UMocRewardCalculator::CalculateTeamWipePenalty(EStrategyType ActiveStrategy)
{
	float Scale = GetStrategyScale(ActiveStrategy,
		AssaultReward.DeathScale, DefendReward.DeathScale, SupportReward.DeathScale);
	return ApplyAndLogReward(ERewardEventType::TeamWipe, ActiveStrategy, -TeamWipePenalty * Scale);
}

float UMocRewardCalculator::CalculateCaptureReward(EStrategyType ActiveStrategy)
{
	float Scale = GetStrategyScale(ActiveStrategy,
		AssaultReward.CaptureRewardScale, DefendReward.CaptureRewardScale, SupportReward.CaptureRewardScale);
	return ApplyAndLogReward(ERewardEventType::CapturePoint, ActiveStrategy, CaptureReward * Scale);
}

float UMocRewardCalculator::CalculateLosePointPenalty(EStrategyType ActiveStrategy)
{
	// LossCaptureReward is already negative; scale amplifies/reduces magnitude per role
	float Scale = GetStrategyScale(ActiveStrategy,
		AssaultReward.LossCaptureRewardScale, DefendReward.LossCaptureRewardScale, SupportReward.LossCaptureRewardScale);
	return ApplyAndLogReward(ERewardEventType::LosePoint, ActiveStrategy, LossCaptureReward * Scale);
}

float UMocRewardCalculator::CalculateSurvivalReward(EStrategyType ActiveStrategy, float CurrentHP, float MaxHP)
{
	return 0.0f;
}

float UMocRewardCalculator::CalculateDistanceShaping(EStrategyType ActiveStrategy, float DistanceToTarget)
{

	float Scale = GetStrategyScale(ActiveStrategy,
		AssaultReward.PenaltyPerMeterScale, DefendReward.PenaltyPerMeterScale, SupportReward.PenaltyPerMeterScale);
	float Penalty = PenaltyPerMeter * Scale * DistanceToTarget;
	return ApplyAndLogReward(ERewardEventType::DistanceShaping, ActiveStrategy, Penalty);
}


// ==================== Dense Per-Step Reward ====================

float UMocRewardCalculator::ComputeStepReward(
	EStrategyType Strategy,
	const FObservation& Prev,
	const FObservation& Current,
	const FEQSWeightParameters& Action)
{
	float Reward = 0.0f;
	const float PositionChange = FVector::Dist(Prev.Position, Current.Position);
	const int32 MyTeamID = (OwnerCharacter) ? OwnerCharacter->GetTeamID_Implementation() : -1;

	// Skip position-based dense rewards on respawn transitions.
	// When Prev.bIsAlive=false the agent was dead and has just respawned,
	// so PositionChange reflects the teleport distance (death→spawn point),
	// not actual movement. Applying movement/objective rewards on this
	// step would produce a spurious ±80 spike per step.
	const bool bIsRespawnStep = !Prev.bIsAlive;

	// Isolation: true only after all allies have been dead for IsolationDebounceSteps
	// consecutive steps. Filters out normal respawn windows where AllyHealths briefly
	// reads all-zero between death and respawn, which would otherwise corrupt rewards.
	{
		bool bAllAlliesDead = true;
		for (const float HP : Current.AllyHealths)
		{
			if (HP > 0.0f) { bAllAlliesDead = false; break; }
		}
		if (bAllAlliesDead)
			IsolatedConsecutiveSteps = FMath::Min(IsolatedConsecutiveSteps + 1, IsolationDebounceSteps);
		else
			IsolatedConsecutiveSteps = 0;
	}
	const bool bIsolated = (IsolatedConsecutiveSteps >= IsolationDebounceSteps);

	switch (Strategy)
	{
	case EStrategyType::Assault:
		{
			Reward += AssaultBaselineReward;

			float HealthLoss = Prev.Health - Current.Health;
			if (HealthLoss > AssaultHealthLossThreshold)
			{
				Reward -= AssaultReward.HealthPenalty * HealthLoss;
			}

			if (!bIsRespawnStep && OwnerCharacter && CachedCapturePoints.Num() > 0)
			{
				int32 PrevFriendlyPoints = 0;
				int32 CurrFriendlyPoints = 0;
				for (int32 i = 0; i < Prev.CapturePointStatuses.Num(); ++i)
				{
					if (Prev.CapturePointStatuses[i] > 0.5f) PrevFriendlyPoints++;
					if (i < Current.CapturePointStatuses.Num() && Current.CapturePointStatuses[i] > 0.5f) CurrFriendlyPoints++;
				}

				// 단일 루프로 통합 및 DistSquared 사용
				float PrevNearestDistSq = FLT_MAX;
				float CurrNearestDistSq = FLT_MAX;
				bool  bInNonFriendlyZone = false;
				bool  bInFriendlyZoneAssault = false;
				float ActiveCappingProgress = 0.0f;

				for (ACapturePoint* CP : CachedCapturePoints)
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
					PostCaptureMomentumStepsRemaining = AssaultReward.PostCaptureMomentumDuration;
					float NearestFriendlyDistSq = FLT_MAX;
					for (ACapturePoint* CP : CachedCapturePoints)
					{
						if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;
						float DSq = FVector::DistSquared(Current.Position, CP->GetActorLocation());
						if (DSq < NearestFriendlyDistSq)
						{
							NearestFriendlyDistSq = DSq;
							LastCapturedPointLocation = CP->GetActorLocation();
						}
					}
				}

				// 루프가 끝난 뒤에만 Sqrt를 적용하여 정확한 스칼라 거리 보상 부여
				if (PrevNearestDistSq < FLT_MAX && CurrNearestDistSq < FLT_MAX)
				{
					const float ApproachScale = bIsolated ? IsolationApproachMultiplier : 1.0f;
					float ApproachDelta = FMath::Sqrt(PrevNearestDistSq) - FMath::Sqrt(CurrNearestDistSq);
					// Bidirectional: reward approach (+), penalise retreat at half-strength (−).
					// Previously clamped to ≥0 — moving away from objectives gave zero signal,
					// allowing agents to walk toward walls or corners without any gradient cost.
					const float EffectiveDelta = ApproachDelta >= 0.0f ? ApproachDelta : ApproachDelta * 0.5f;
					Reward += AssaultReward.ObjectiveProgressReward * ApproachScale * EffectiveDelta;
				}

				// 루프 통합으로 간결해진 Zone Presence 처리 로직
				if (bInNonFriendlyZone)
				{
					AssaultZoneStepsAfterCapture = 0;
					Reward += AssaultReward.ZonePresenceBonus;
					Reward += AssaultReward.ActiveCappingBonus * ActiveCappingProgress;
				}
				else
				{
					if (bInFriendlyZoneAssault && AssaultCapturedZoneDecaySteps > 0.0f)
					{
						AssaultZoneStepsAfterCapture++;
						const float DecayFactor = FMath::Max(0.0f, 1.0f - (float)AssaultZoneStepsAfterCapture / AssaultCapturedZoneDecaySteps);
						Reward += AssaultReward.ZonePresenceBonus * DecayFactor;
					}
				}

				if (PostCaptureMomentumStepsRemaining > 0)
				{
					PostCaptureMomentumStepsRemaining--;
					if (PositionChange >= AssaultReward.PostCaptureMomentumMinMove)
					{
						if (FVector::DistSquared(Current.Position, LastCapturedPointLocation) > CaptureRadiusSq_Cached)
						{
							Reward += AssaultReward.PostCaptureMomentumBonus;
						}
					}
				}

				if (PositionChange < AssaultIdleMovementThreshold && !bInNonFriendlyZone)
				{
					float IdlePenalty = (CurrNearestDistSq == FLT_MAX || bIsolated)
						? AssaultReward.IdlePenalty
						: AssaultReward.IdlePenalty * 0.5f;
					// Do NOT halve the penalty inside a captured friendly zone — camping in
					// an already-secured zone should be equally discouraged so agents push forward.
					Reward -= IdlePenalty;
				}
			}
		}
		break;

	case EStrategyType::Defend:
		{
			// B1: Unconditional baseline — offsets expected negative dense reward for untrained agents.
			// Without this, random policies accumulate massive negatives from distance penalties,
			// preventing the critic from fitting the reward distribution.
			Reward += DefendBaselineReward;

			if (MyTeamID >= 0 && CachedCapturePoints.Num() > 0)
			{
				float CurrNearestFriendlyDist = FLT_MAX;
				float PrevNearestFriendlyDist = FLT_MAX;
				bInFriendlyZone = false;

				for (ACapturePoint* CP : CachedCapturePoints)
				{
					if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;

					const float CurrDist = FVector::Dist(Current.Position, CP->GetActorLocation());
					const float PrevDist = FVector::Dist(Prev.Position, CP->GetActorLocation());
					CurrNearestFriendlyDist = FMath::Min(CurrNearestFriendlyDist, CurrDist);
					PrevNearestFriendlyDist = FMath::Min(PrevNearestFriendlyDist, PrevDist);

					if (CurrDist <= CP->CaptureRadius)
					{
						bInFriendlyZone = true;
					}
				}

				// Cold-start: no friendly CPs exist yet (all neutral or enemy at episode start).
				// Behave like Assault — approach and contest the nearest non-friendly CP to
				// establish a base before switching to pure defense mode.
				if (CurrNearestFriendlyDist == FLT_MAX && !bIsRespawnStep)
				{
					float PrevNonFriendlyDist = FLT_MAX;
					float CurrNonFriendlyDist = FLT_MAX;
					bool  bInNonFriendlyZone  = false;
					float ActiveCappingProgress = 0.0f;

					for (ACapturePoint* NonFriendlyCP : CachedCapturePoints)
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

					// Approach reward toward nearest non-friendly CP (bidirectional: half-penalty for retreat)
					if (PrevNonFriendlyDist < FLT_MAX && CurrNonFriendlyDist < FLT_MAX)
					{
						const float ApproachDelta = PrevNonFriendlyDist - CurrNonFriendlyDist;
						const float EffectiveDelta = ApproachDelta >= 0.0f ? ApproachDelta : ApproachDelta * 0.5f;
						const float ApproachScale = bIsolated ? IsolationApproachMultiplier : 1.0f;
						Reward += DefendReward.ZoneApproachReward * ApproachScale * EffectiveDelta;
					}

					// Zone presence and active-capping bonuses while inside a non-friendly CP
					if (bInNonFriendlyZone)
					{
						bInFriendlyZone = false; // not a friendly zone — skip defend-zone-specific logic below
						Reward += DefendReward.ZonePresenceBonus;
						// Reuse AssaultReward.ActiveCappingBonus to reward actively converting the point
						Reward += AssaultReward.ActiveCappingBonus * ActiveCappingProgress;
					}

					// Idle penalty when outside any zone (mirrors Assault behaviour)
					if (!bInNonFriendlyZone && PositionChange < AssaultIdleMovementThreshold)
					{
						Reward -= AssaultReward.IdlePenalty * 0.5f;
					}
				}

				// Proximity-proportional zone bonus: ramps from 0 at 3× radius to full at edge.
			const float ZoneRampRadius = CaptureRadius_Cached * 3.0f;
			const float ZoneProximityFactor = (CurrNearestFriendlyDist < FLT_MAX)
				? FMath::Clamp(1.0f - (CurrNearestFriendlyDist / ZoneRampRadius), 0.0f, 1.0f)
				: 0.0f;

			if (bInFriendlyZone)
			{
				// Inside zone: full defending rewards
				Reward += DefendReward.ZonePresenceBonus;
				if (PositionChange < DefendStationaryThreshold)
				{
					Reward += DefendReward.PositionReward;
				}
				if (Current.Health > DefendHealthThreshold)
				{
					Reward += DefendReward.HealthBonus;
				}

				// Threat response: bonus for being in zone when an enemy is actively contesting it
				for (int32 i = 0; i < Current.EnemyPositions.Num(); ++i)
				{
					if (i < Current.EnemyVisible.Num() && Current.EnemyVisible[i])
					{
						for (ACapturePoint* CP : CachedCapturePoints)
						{
							if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;
							if (FVector::Dist(Current.EnemyPositions[i], CP->GetActorLocation()) <= CP->CaptureRadius)
							{
								Reward += DefendReward.ThreatResponseBonus;
								goto ThreatResponseApplied; // only award once per step
							}
						}
					}
				}
				ThreatResponseApplied:;
			}
			// Removed: partial zone bonus for being near but outside zone.
			// It rewarded loitering outside the zone without actually entering.

			if (!bInFriendlyZone && CurrNearestFriendlyDist < FLT_MAX && PrevNearestFriendlyDist < FLT_MAX)
				{
					if (!bIsRespawnStep)
					{
						// Outside zone: only reward APPROACHING, clamp negative to 0.
						// This prevents random movement from accumulating massive negatives.
						// Amplified when isolated to push the last survivor back to zone faster.
						const float ApproachDelta = PrevNearestFriendlyDist - CurrNearestFriendlyDist;
						const float DefendApproachScale = bIsolated ? IsolationApproachMultiplier : 1.0f;
						Reward += DefendReward.ZoneApproachReward * DefendApproachScale * FMath::Max(ApproachDelta, 0.0f);
					}

					// Distance penalty: stronger pull toward zone (0.3 from 0.2).
					const float DistPenalty = FMath::Min(CurrNearestFriendlyDist / 10000.0f, 1.0f) * 0.3f;
					Reward -= DistPenalty;
				}
			}
			// --- Zone Durability Bonus ---
			// Reward for absorbing damage while physically inside a friendly zone.
			// Prevents the agent from fleeing the zone when under fire.
			if (!bIsRespawnStep && bInFriendlyZone)
			{
				const float DamageTaken = Prev.Health - Current.Health;
				if (DamageTaken > 0.0f)
				{
					const float DurabilityReward = DefendReward.ZoneDurabilityBonus * DamageTaken;
					Reward += DurabilityReward;
					
				}
			}

			// --- Zone Guard Kill Bonus ---
			// Extra kill reward if the enemy was threatening a friendly zone the step before the kill.
			// Uses Prev positions (enemy is dead in Current) to check zone proximity.
			// Awards once per step regardless of how many zone-threats were present.
			if (bSparseKillFiredThisStep)
			{
				bool bEnemyWasNearZone = false;
				for (int32 i = 0; i < Prev.EnemyPositions.Num() && !bEnemyWasNearZone; ++i)
				{
					if (i < Prev.EnemyVisible.Num() && Prev.EnemyVisible[i])
					{
						for (ACapturePoint* CP : CachedCapturePoints)
						{
							if (!CP || CP->GetTeamID_Implementation() != MyTeamID) continue;
							const float EnemyDistToZone = FVector::Dist(Prev.EnemyPositions[i], CP->GetActorLocation());
							if (EnemyDistToZone <= CP->CaptureRadius * DefendReward.ZoneGuardRadius)
							{
								bEnemyWasNearZone = true;
								break;
							}
						}
					}
				}
				if (bEnemyWasNearZone)
				{
					Reward += DefendReward.ZoneGuardKillBonus;
					
				}
			}
		}
		break;

	case EStrategyType::Support:
		{
			// B2: Unconditional baseline
			Reward += SupportBaselineReward;

			if (PositionChange > SupportMinMoveThreshold && PositionChange < SupportMaxMoveThreshold)
			{
				Reward += SupportReward.PositionReward;
			}

			// --- Stable injured-ally target selection ---
			++InjuredAllyStalenessCounter;
			bool bShouldReevalTarget =
				(CachedInjuredAllyIdx < 0) ||
				(InjuredAllyStalenessCounter >= InjuredAllyReevalInterval) ||
				(CachedInjuredAllyIdx < Current.AllyHealths.Num() &&
					Current.AllyHealths[CachedInjuredAllyIdx] <= 0.0f); // cached ally died

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
				CachedInjuredAllyIdx = NewInjuredIdx;
				InjuredAllyStalenessCounter = 0;
			}

			const int32 InjuredAllyIdx = CachedInjuredAllyIdx;

			if (InjuredAllyIdx >= 0 && InjuredAllyIdx < Current.AllyPositions.Num())
			{
				const float CurrAllyDist = FVector::Dist(Current.Position, Current.AllyPositions[InjuredAllyIdx]);

				// 체력 보너스를 아군 근처에 있을 때만 주도록 변경
				if (CurrAllyDist <= SupportAllyProximityThreshold)
				{
					// Proximity bonus only fires when ally is actually injured.
					// Unconditional bonus caused support to camp with healthy allies indefinitely.
					const float AllyHP = Current.AllyHealths[InjuredAllyIdx];
					if (AllyHP > 0.0f && AllyHP < SupportReward.AllyInjuryThreshold)
					{
						Reward += SupportReward.AllyProximityBonus;

						// Extra bonus for critically injured ally (HP < 30%)
						if (AllyHP < 0.3f)
						{
							Reward += SupportReward.AllyProximityBonus * 0.5f;
						}
					}

					// Health bonus for support staying alive near the team
					if (Current.Health > SupportHealthThreshold)
					{
						Reward += SupportReward.HealthBonus;
					}
				}
				// 아군과 너무 멀리 떨어져서 꿀을 빨고 있다면, 체력 보너스는 없고 오히려 접근 보상만 존재하게 됨

				// Approach shaping: only reward APPROACHING (clamp negative to 0).
				if (!bIsRespawnStep && InjuredAllyIdx < Prev.AllyPositions.Num())
				{
					const float PrevAllyDist = FVector::Dist(Prev.Position, Prev.AllyPositions[InjuredAllyIdx]);
					const float ApproachDelta = PrevAllyDist - CurrAllyDist;
					Reward += SupportReward.AllyApproachReward * FMath::Max(ApproachDelta, 0.0f);
				}
			}
			else if (bIsolated && !bIsRespawnStep)
			{
				// 고립 시 체력 보너스를 절반만 지급하여 생존은 격려하되 숨는 것보다 목표로 가는 것을 우선시함
				if (Current.Health > SupportHealthThreshold)
				{
					Reward += SupportReward.HealthBonus * 0.5f;
				}

				// All allies dead: redirect to objective approach instead of passive baseline.
				float PrevNearestObjDist = FLT_MAX;
				float CurrNearestObjDist = FLT_MAX;
				for (ACapturePoint* CP : CachedCapturePoints)
				{
					if (!CP || CP->GetTeamID_Implementation() == MyTeamID) continue;
					PrevNearestObjDist = FMath::Min(PrevNearestObjDist, FVector::Dist(Prev.Position, CP->GetActorLocation()));
					CurrNearestObjDist = FMath::Min(CurrNearestObjDist, FVector::Dist(Current.Position, CP->GetActorLocation()));
				}
				if (PrevNearestObjDist < FLT_MAX && CurrNearestObjDist < FLT_MAX)
				{
					Reward += AssaultReward.ObjectiveProgressReward * IsolationApproachMultiplier
						* (PrevNearestObjDist - CurrNearestObjDist);
				}
				if (PositionChange < AssaultIdleMovementThreshold)
				{
					Reward -= AssaultReward.IdlePenalty;
				}
			}
			else
			{
				// No valid ally target (all dead or no allies): provide a small baseline
				Reward += SupportReward.PositionReward * 0.5f;
			}


			// --- Heal tick reward ---
			if (OwnerCharacter && OwnerCharacter->GetLastTickHealAmount() > 0.0f)
			{
				Reward += SupportReward.HealTickReward;
				
			}

			// --- Role-break penalty: killed an enemy while an ally needed healing ---
			// Fires when a Kill event was logged THIS STEP and at least one ally is below 50% HP.
			// FIX: Only check the latest sparse reward drain, not the full EventLog history.
			// Previous code scanned the append-only EventLog, causing the penalty to fire
			// every step after the first kill for the rest of the episode.
			{
				bool bKilledThisStep = bSparseKillFiredThisStep;
				if (bKilledThisStep)
				{
					bool bAllyInjured = false;
					for (float AllyHP : Current.AllyHealths)
					{
						if (AllyHP > 0.0f && AllyHP < 0.5f)
						{
							bAllyInjured = true;
							break;
						}
					}
					if (bAllyInjured)
					{
						Reward -= SupportReward.RoleBreakPenalty;
						
					}
				}
			}

			// --- Rear-guard positioning bonus ---
			// Reward agent for being farther from nearest visible enemy than its nearest ally.
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

				// Only fire when enemy is within RearGuardMaxEnemyDist — prevents rewarding far-corner hiding.
				if (NearestEnemyDist < FLT_MAX && NearestEnemyDist <= SupportReward.RearGuardMaxEnemyDist)
				{
					float NearestAllyToEnemyDist = FLT_MAX;
					for (const FVector& AllyPos : Current.AllyPositions)
					{
						if (AllyPos.IsZero()) continue;
						NearestAllyToEnemyDist = FMath::Min(NearestAllyToEnemyDist, FVector::Dist(AllyPos, NearestEnemyPos));
					}

					if (NearestEnemyDist > NearestAllyToEnemyDist)
					{
						Reward += SupportReward.RearGuardBonus;
					}
				}
			}
		}
		break;
	}

	// Zone control reward: per-step signal proportional to (friendly_bases - enemy_bases).
	// Positive when the team holds more territory, negative when the enemy dominates.
	// The competitive differential discourages passive corner-hiding — an agent idling
	// in a corner while the enemy captures bases will accumulate a negative signal here,
	// creating pressure to engage. Scale differs by role: Defend agents benefit most from
	// retaining zones; Assault agents are primarily driven by the capture/approach rewards.
	if (MyTeamID >= 0 && CachedCapturePoints.Num() > 0)
	{
		int32 FriendlyBases = 0;
		int32 EnemyBases = 0;
		int32 NeutralBases = 0;
		for (const ACapturePoint* CP : CachedCapturePoints)
		{
			if (!CP) continue;
			const int32 OwnerTeam = CP->GetTeamID_Implementation();
			if (OwnerTeam == MyTeamID)    FriendlyBases++;
			else if (OwnerTeam >= 0)      EnemyBases++;
			else                          NeutralBases++; // -1 = neutral
		}
		// Treat uncaptured neutral CPs as 0.5 enemy so capturing a neutral gives the
		// same ZoneControl incentive as capturing an enemy point (both swing NetControl by +1.5 net).
		// Previously neutral=+1 vs enemy=+2 caused agents to skip nearby neutrals in favour of
		// routing to enemy-held points for the larger reward swing.
		const float NetControl = static_cast<float>(FriendlyBases - EnemyBases) - NeutralBases * 0.5f;
		const float ZoneControlScale = GetStrategyScale(Strategy,
			ZoneControlAssaultScale, ZoneControlDefendScale, ZoneControlSupportScale);
		Reward += ZoneControlRewardPerBase * ZoneControlScale * NetControl;
	}

	// Time penalty (higher for Assault to discourage camping)
	float EffectiveTimePenalty = (Strategy == EStrategyType::Assault) ? AssaultReward.TimePenalty : TimePenalty;
	Reward -= EffectiveTimePenalty;

	// Drain any sparse rewards accumulated by event callbacks (kills, deaths, captures) this step
	Reward += DrainSparseReward();

	// Global reward normalization: scale down so VF can fit discounted returns.
	// Applied BEFORE clamping so sparse events (±20) become ±2 and dense (0.3) becomes 0.03.
	Reward *= GlobalRewardScale;

	// B3: Clamp total step reward to prevent catastrophic accumulation.
	Reward = FMath::Clamp(Reward, StepRewardClampMin, StepRewardClampMax);

	// Reset per-step sparse flags (set by callbacks between ComputeStepReward calls)
	bSparseKillFiredThisStep = false;

	// Team reward mixing: blend individual reward with teammates' previous-step average.
	// Uses 1-step lag to avoid ordering dependency (all agents compute in same frame).
	const float CurrentIndividualReward = Reward;
	if (TeamRewardMixingRatio > 0.0f && OwnerCharacter)
	{
		AMatchManager* MatchMgr = OwnerCharacter->GetMatchManager();
		if (MatchMgr)
		{
			const int32 TeamID = OwnerCharacter->GetTeamID_Implementation();
			// [수정] 가능하면 const TArray<AMocCharacter*>& 로 반환받도록 처리해야 복사가 발생하지 않습니다.
			// (만약 MatchManager의 GetTeamAgents가 값 복사(TArray)를 반환한다면 엔진 구조상 메모리 할당이 발생합니다.
			// 향후 TeamMgr->GetTeamAgents() 반환형 자체를 참조 반환으로 바꾸는 것을 강력히 권장합니다.)
			const TArray<AMocCharacter*>& Teammates = MatchMgr->GetTeamAgents(TeamID);

			float TeamRewardSum = 0.0f;
			int32 TeamCount = 0;


			if (TeamCount > 0)
			{
				const float TeamAvg = TeamRewardSum / static_cast<float>(TeamCount);
				Reward = (1.0f - TeamRewardMixingRatio) * CurrentIndividualReward + TeamRewardMixingRatio * TeamAvg;
			}
		}
	}
	LastIndividualStepReward = CurrentIndividualReward;

	return Reward;
}

FRewardBreakdown UMocRewardCalculator::ComputeRewardBreakdown(
	EStrategyType Strategy,
	const FObservation& Prev,
	const FObservation& Current) const
{
	FRewardBreakdown Breakdown;

	const float PositionChange = FVector::Dist(Prev.Position, Current.Position);
	const float HealthLoss = Prev.Health - Current.Health;

	Breakdown.TimePenaltyComponent = (Strategy == EStrategyType::Assault) ? -AssaultReward.TimePenalty : -TimePenalty;

	switch (Strategy)
	{
	case EStrategyType::Assault:
		Breakdown.PositionComponent = 0.0f; // MovementReward removed; approach shaping handled in ComputeStepReward
		if (HealthLoss > AssaultHealthLossThreshold)
		{
			Breakdown.HealthComponent = -AssaultReward.HealthPenalty * HealthLoss;
		}
		break;

	case EStrategyType::Defend:
		if (PositionChange < DefendStationaryThreshold) Breakdown.PositionComponent = DefendReward.PositionReward;
		if (Current.Health > DefendHealthThreshold)     Breakdown.HealthComponent = DefendReward.HealthBonus;
		// Zone anchoring approximation (ObjectiveComponent) — full world query omitted in breakdown
		Breakdown.ObjectiveComponent = 0.0f;
		break;

	case EStrategyType::Support:
		if (PositionChange > SupportMinMoveThreshold && PositionChange < SupportMaxMoveThreshold)
			Breakdown.PositionComponent = SupportReward.PositionReward;
		if (Current.Health > SupportHealthThreshold)
			Breakdown.HealthComponent = SupportReward.HealthBonus;
		// Ally proximity approximation (ObjectiveComponent) — ally world query omitted in breakdown
		Breakdown.ObjectiveComponent = 0.0f;
		break;
	}

	bool bJustDied = Prev.bIsAlive && !Current.bIsAlive;
	if (bJustDied)
	{
		float DeathScale = GetStrategyScale(Strategy,
			AssaultReward.DeathScale, DefendReward.DeathScale, SupportReward.DeathScale);
		Breakdown.DeathPenaltyComponent = -DeathPenaltyReward * DeathScale;
	}

	Breakdown.StrategyReward = Breakdown.PositionComponent + Breakdown.HealthComponent;
	Breakdown.Total = Breakdown.StrategyReward
		+ Breakdown.ObjectiveComponent
		+ Breakdown.DeathPenaltyComponent
		+ Breakdown.TimePenaltyComponent;

	return Breakdown;
}

// ==================== Episode Management ====================

void UMocRewardCalculator::ResetEpisodeState()
{
	PostCaptureMomentumStepsRemaining = 0;
	LastCapturedPointLocation = FVector::ZeroVector;
	AssaultZoneStepsAfterCapture = 0;
	CachedInjuredAllyIdx = -1;
	InjuredAllyStalenessCounter = 0;
	LastCaptureLossPenaltyTime.Empty();
	bSparseKillFiredThisStep = false;
	LastIndividualStepReward = 0.0f;
}




// ==================== Internals ====================

void UMocRewardCalculator::CacheCapturePoints()
{
	CachedCapturePoints.Empty();

	// 월드를 순회하지 않고, 에이전트가 속한 환경을 찾습니다.
	if (!OwnerCharacter) return;

	const int32 MyEnvID = OwnerCharacter->GetEnvID_Implementation();

	// 환경을 찾아서 그 환경이 소유한 거점 목록만 가져옵니다.
	for (TActorIterator<AScholaEnvironment> It(GetWorld()); It; ++It)
	{
		if ((*It)->GetEnvId() == MyEnvID)
		{
			// ScholaEnvironment가 소유한 거점 목록을 그대로 사용
			
			break;
		}
	}

	if (CachedCapturePoints.Num() > 0 && CachedCapturePoints[0])
	{
		CaptureRadius_Cached = CachedCapturePoints[0]->CaptureRadius;
		CaptureRadiusSq_Cached = CaptureRadius_Cached * CaptureRadius_Cached;
	}
}