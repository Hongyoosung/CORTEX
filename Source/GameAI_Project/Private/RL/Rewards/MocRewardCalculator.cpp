// Copyright Epic Games, Inc. All Rights Reserved.

#include "RL/Rewards/MocRewardCalculator.h"
#include "Characters/MocCharacter.h"
#include "Actors/CapturePoint.h"
#include "Kismet/GameplayStatics.h"


UMocRewardCalculator::UMocRewardCalculator()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMocRewardCalculator::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<AMocCharacter>(GetOwner());
	CacheCapturePoints();
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
	LogRewardEvent(EventType, Strategy, RewardValue);
	AddReward(RewardValue);
	return RewardValue;
}

void UMocRewardCalculator::AddReward(float Value)
{
	CumulativeReward += Value;
}

// ==================== Event-Driven Sparse Rewards ====================

float UMocRewardCalculator::CalculateKillReward(EStrategyType ActiveStrategy)
{
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

float UMocRewardCalculator::CalculatePickupDenyReward(EStrategyType ActiveStrategy)
{
	float Scale = GetStrategyScale(ActiveStrategy,
		AssaultReward.PickupDenyRewardScale, DefendReward.PickupDenyRewardScale, SupportReward.PickupDenyRewardScale);
	return ApplyAndLogReward(ERewardEventType::PickupDeny, ActiveStrategy, PickupDenyReward * Scale);
}

float UMocRewardCalculator::CalculateSurvivalReward(EStrategyType ActiveStrategy, float CurrentHP, float MaxHP)
{
	if (MaxHP <= 0.0f || (CurrentHP / MaxHP) > SurvivalHPThreshold) return 0.0f;

	float Scale = GetStrategyScale(ActiveStrategy,
		AssaultReward.SurvivalRewardScale, DefendReward.SurvivalRewardScale, SupportReward.SurvivalRewardScale);
	return ApplyAndLogReward(ERewardEventType::Survival, ActiveStrategy, SurvivalReward * Scale);
}

float UMocRewardCalculator::CalculateDistanceShaping(EStrategyType ActiveStrategy, float DistanceToTarget)
{
	if (!bUseDenseRewards) return 0.0f;

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

	switch (Strategy)
	{
	case EStrategyType::Assault:
		{
			// Movement reward
			Reward += AssaultReward.MovementReward * PositionChange;

			// Health loss penalty
			float HealthLoss = Prev.Health - Current.Health;
			if (HealthLoss > AssaultHealthLossThreshold)
			{
				Reward -= AssaultReward.HealthPenalty * HealthLoss;
			}

			// Objective shaping: capture point progress
			if (OwnerCharacter && CachedCapturePoints.Num() > 0)
			{
				const int32 MyTeamID = OwnerCharacter->GetTeamID_Implementation();

				// Friendly point counts from observation snapshots (consistent source)
				int32 PrevFriendlyPoints = 0;
				int32 CurrFriendlyPoints = 0;
				for (int32 i = 0; i < Prev.CapturePointStatuses.Num(); ++i)
				{
					if (Prev.CapturePointStatuses[i] > 0.5f) PrevFriendlyPoints++;
					if (i < Current.CapturePointStatuses.Num() && Current.CapturePointStatuses[i] > 0.5f) CurrFriendlyPoints++;
				}

				// Distances to nearest non-friendly CP and zone presence (from world state)
				float PrevNearestDist = FLT_MAX;
				float CurrNearestDist = FLT_MAX;
				bool  bInNonFriendlyZone = false;

				for (ACapturePoint* CP : CachedCapturePoints)
				{
					if (!CP || CP->GetOwningTeamID() == MyTeamID) continue;

					float PrevDist = FVector::Dist(Prev.Position, CP->GetActorLocation());
					float CurrDist = FVector::Dist(Current.Position, CP->GetActorLocation());
					PrevNearestDist = FMath::Min(PrevNearestDist, PrevDist);
					CurrNearestDist = FMath::Min(CurrNearestDist, CurrDist);

					if (CurrDist <= CP->CaptureRadius)
					{
						bInNonFriendlyZone = true;
					}
				}

				// New captures trigger a momentum window and bonus
				int32 NewCaptures = CurrFriendlyPoints - PrevFriendlyPoints;
				if (NewCaptures > 0)
				{
					Reward += CaptureReward * NewCaptures;
					PostCaptureMomentumStepsRemaining = AssaultReward.PostCaptureMomentumDuration;

					// Store location of nearest newly-friendly CP for momentum direction
					float NearestFriendlyDist = FLT_MAX;
					for (ACapturePoint* CP : CachedCapturePoints)
					{
						if (!CP || CP->GetOwningTeamID() != MyTeamID) continue;
						float D = FVector::Dist(Current.Position, CP->GetActorLocation());
						if (D < NearestFriendlyDist)
						{
							NearestFriendlyDist = D;
							LastCapturedPointLocation = CP->GetActorLocation();
						}
					}
				}

				// Objective progress: approaching nearest non-friendly CP
				if (PrevNearestDist < FLT_MAX && CurrNearestDist < FLT_MAX)
				{
					Reward += AssaultReward.ObjectiveProgressReward * (PrevNearestDist - CurrNearestDist);
				}

				// Zone presence bonus
				if (bInNonFriendlyZone)
				{
					Reward += AssaultReward.ZonePresenceBonus;
				}

				// Post-capture momentum bonus for advancing away from captured point
				if (PostCaptureMomentumStepsRemaining > 0)
				{
					PostCaptureMomentumStepsRemaining--;
					if (PositionChange >= AssaultReward.PostCaptureMomentumMinMove)
					{
						float DistFromCaptured = FVector::Dist(Current.Position, LastCapturedPointLocation);
						if (DistFromCaptured > CaptureRadius_Cached)
						{
							Reward += AssaultReward.PostCaptureMomentumBonus;
						}
					}
				}

				// Idle penalty: stationary and not contesting a zone
				if (PositionChange < AssaultIdleMovementThreshold && !bInNonFriendlyZone)
				{
					// Stronger penalty when no target in range; lighter when target is just far away
					float IdlePenalty = (CurrNearestDist == FLT_MAX)
						? AssaultReward.IdlePenalty
						: AssaultReward.IdlePenalty * 0.5f;
					Reward -= IdlePenalty;
				}
			}
		}
		break;

	case EStrategyType::Defend:
		{
			if (PositionChange < DefendStationaryThreshold)
			{
				Reward += DefendReward.PositionReward;
			}
			if (Current.Health > DefendHealthThreshold)
			{
				Reward += DefendReward.HealthBonus;
			}
			if (Current.WeaponCooldown < DefendWeaponCooldownThreshold)
			{
				Reward += DefendWeaponReadyBonus;
			}
		}
		break;

	case EStrategyType::Support:
		{
			if (PositionChange > SupportMinMoveThreshold && PositionChange < SupportMaxMoveThreshold)
			{
				Reward += SupportReward.PositionReward;
			}
			if (Current.Health > SupportHealthThreshold)
			{
				Reward += SupportReward.HealthBonus;
			}
		}
		break;
	}

	// Death penalty (role-scaled via UPROPERTY)
	bool bJustDied = Prev.bIsAlive && !Current.bIsAlive;
	if (bJustDied)
	{
		float DeathScale = GetStrategyScale(Strategy,
			AssaultReward.DeathScale, DefendReward.DeathScale, SupportReward.DeathScale);
		Reward -= DeathPenaltyReward * DeathScale;
	}

	// Time penalty (higher for Assault to discourage camping)
	float EffectiveTimePenalty = (Strategy == EStrategyType::Assault) ? AssaultReward.TimePenalty : TimePenalty;
	Reward -= EffectiveTimePenalty;

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
		Breakdown.PositionComponent = AssaultReward.MovementReward * PositionChange;
		if (HealthLoss > AssaultHealthLossThreshold)
		{
			Breakdown.HealthComponent = -AssaultReward.HealthPenalty * HealthLoss;
		}
		break;

	case EStrategyType::Defend:
		if (PositionChange < DefendStationaryThreshold) Breakdown.PositionComponent = DefendReward.PositionReward;
		if (Current.Health > DefendHealthThreshold)     Breakdown.HealthComponent = DefendReward.HealthBonus;
		break;

	case EStrategyType::Support:
		if (PositionChange > SupportMinMoveThreshold && PositionChange < SupportMaxMoveThreshold)
			Breakdown.PositionComponent = SupportReward.PositionReward;
		if (Current.Health > SupportHealthThreshold)
			Breakdown.HealthComponent = SupportReward.HealthBonus;
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
	ResetCumulativeReward();
	PostCaptureMomentumStepsRemaining = 0;
	LastCapturedPointLocation = FVector::ZeroVector;
}

// ==================== Event Log ====================

void UMocRewardCalculator::LogRewardEvent(ERewardEventType EventType, EStrategyType Strategy, float Reward)
{
	float Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	int32 AgentID = OwnerCharacter ? OwnerCharacter->AgentID : -1;
	EventLog.Add(FRewardEvent(EventType, Strategy, Reward, Timestamp, AgentID));
}

// ==================== Internals ====================

void UMocRewardCalculator::CacheCapturePoints()
{
	CachedCapturePoints.Empty();

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ACapturePoint::StaticClass(), Found);

	for (AActor* Actor : Found)
	{
		if (ACapturePoint* CP = Cast<ACapturePoint>(Actor))
		{
			CachedCapturePoints.Add(CP);
		}
	}

	// Assumes all capture points share the same radius; warn if none found
	if (CachedCapturePoints.Num() > 0)
	{
		CaptureRadius_Cached = CachedCapturePoints[0]->CaptureRadius;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MocRewardCalculator] No capture points found — using default radius %.0f"), CaptureRadius_Cached);
	}

	UE_LOG(LogTemp, Log, TEXT("[MocRewardCalculator] Cached %d capture points (radius=%.0f)"),
		CachedCapturePoints.Num(), CaptureRadius_Cached);
}
