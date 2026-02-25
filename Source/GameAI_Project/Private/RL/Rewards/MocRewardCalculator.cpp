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

	// Bind to every capture point's ownership-change event so sparse rewards fire automatically
	for (ACapturePoint* CP : CachedCapturePoints)
	{
		if (CP)
		{
			CP->OnPointCaptured.AddDynamic(this, &UMocRewardCalculator::OnCapturePointCaptured);
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
	LogRewardEvent(EventType, Strategy, RewardValue);
	AddReward(RewardValue);
	return RewardValue;
}

void UMocRewardCalculator::AddReward(float Value)
{
	CumulativeReward += Value;
}

float UMocRewardCalculator::DrainSparseReward()
{
	const float Drained = CumulativeReward;
	CumulativeReward = 0.0f;
	return Drained;
}

void UMocRewardCalculator::OnCapturePointCaptured(ECapturePointID PointID, ECapturePointOwnership PreviousOwner, ECapturePointOwnership NewOwner)
{
	if (!OwnerCharacter) return;

	const int32 MyTeam = OwnerCharacter->GetTeamID_Implementation();

	auto ToTeam = [](ECapturePointOwnership O) -> int32
	{
		if (O == ECapturePointOwnership::RedTeam)  return 0;
		if (O == ECapturePointOwnership::BlueTeam) return 1;
		return -1;
	};

	const int32 NewOwnerTeam  = ToTeam(NewOwner);
	const int32 PrevOwnerTeam = ToTeam(PreviousOwner);

	const EStrategyType Strategy = OwnerCharacter->GetCommandedStrategy();

	if (NewOwnerTeam == MyTeam)
	{
		CalculateCaptureReward(Strategy);
	}
	else if (PrevOwnerTeam == MyTeam)
	{
		// B4: Cooldown — prevent flip-flop scenarios from flooding sparse penalties.
		// Max 1 loss penalty per capture point per CaptureLossCooldownSeconds.
		const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		const float* LastPenaltyTime = LastCaptureLossPenaltyTime.Find(PointID);
		if (LastPenaltyTime && (CurrentTime - *LastPenaltyTime) < CaptureLossCooldownSeconds)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[MocRewardCalculator] Capture loss cooldown active for CP %d (%.1fs remaining)"),
				static_cast<int32>(PointID), CaptureLossCooldownSeconds - (CurrentTime - *LastPenaltyTime));
			return;
		}
		LastCaptureLossPenaltyTime.Add(PointID, CurrentTime);
		CalculateLosePointPenalty(Strategy);
	}
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
	// B5: Death masking — if agent already died this episode, return 0.
	// Sparse rewards (kill callbacks from other agents) are still drained to prevent
	// accumulation, but not forwarded. This ensures the death penalty isn't drowned
	// out by 200+ steps of baseline reward post-respawn.
	if (bAgentDiedThisEpisode)
	{
		DrainSparseReward(); // drain and discard
		return 0.0f;
	}

	// Detect death on THIS step → set flag for all future steps
	if (Prev.bIsAlive && !Current.bIsAlive)
	{
		bAgentDiedThisEpisode = true;
		// Still compute this step's reward (includes the death penalty from sparse drain)
	}

	float Reward = 0.0f;
	const float PositionChange = FVector::Dist(Prev.Position, Current.Position);
	const int32 MyTeamID = (OwnerCharacter) ? OwnerCharacter->GetTeamID_Implementation() : -1;

	// Skip position-based dense rewards on respawn transitions.
	// When Prev.bIsAlive=false the agent was dead and has just respawned,
	// so PositionChange reflects the teleport distance (death→spawn point),
	// not actual movement. Applying movement/objective rewards on this
	// step would produce a spurious ±80 spike per step.
	const bool bIsRespawnStep = !Prev.bIsAlive;

	switch (Strategy)
	{
	case EStrategyType::Assault:
		{
			// Movement reward — skip on respawn (teleport distance, not real movement)
			if (!bIsRespawnStep)
			{
				Reward += AssaultReward.MovementReward * PositionChange;
			}

			// Health loss penalty
			float HealthLoss = Prev.Health - Current.Health;
			if (HealthLoss > AssaultHealthLossThreshold)
			{
				Reward -= AssaultReward.HealthPenalty * HealthLoss;
			}

			// Objective shaping: capture point progress (skip on respawn — position delta is meaningless)
			if (!bIsRespawnStep && OwnerCharacter && CachedCapturePoints.Num() > 0)
			{
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

				// New captures: trigger momentum window (sparse reward fires via OnCapturePointCaptured)
				int32 NewCaptures = CurrFriendlyPoints - PrevFriendlyPoints;
				if (NewCaptures > 0)
				{
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
			// B1: Unconditional baseline — offsets expected negative dense reward for untrained agents.
			// Without this, random policies accumulate massive negatives from distance penalties,
			// preventing the critic from fitting the reward distribution.
			Reward += DefendBaselineReward;

			if (MyTeamID >= 0 && CachedCapturePoints.Num() > 0)
			{
				float CurrNearestFriendlyDist = FLT_MAX;
				float PrevNearestFriendlyDist = FLT_MAX;
				bool bInFriendlyZone = false;

				for (ACapturePoint* CP : CachedCapturePoints)
				{
					if (!CP || CP->GetOwningTeamID() != MyTeamID) continue;

					const float CurrDist = FVector::Dist(Current.Position, CP->GetActorLocation());
					const float PrevDist = FVector::Dist(Prev.Position, CP->GetActorLocation());
					CurrNearestFriendlyDist = FMath::Min(CurrNearestFriendlyDist, CurrDist);
					PrevNearestFriendlyDist = FMath::Min(PrevNearestFriendlyDist, PrevDist);

					if (CurrDist <= CP->CaptureRadius)
					{
						bInFriendlyZone = true;
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
			}
			else if (CurrNearestFriendlyDist < FLT_MAX)
			{
				// Outside zone but within ramp radius: partial zone bonus
				Reward += DefendReward.ZonePresenceBonus * ZoneProximityFactor * 0.5f;
			}

			if (!bInFriendlyZone && CurrNearestFriendlyDist < FLT_MAX && PrevNearestFriendlyDist < FLT_MAX)
				{
					if (!bIsRespawnStep)
					{
						// Outside zone: only reward APPROACHING, clamp negative to 0.
						// This prevents random movement from accumulating massive negatives.
						const float ApproachDelta = PrevNearestFriendlyDist - CurrNearestFriendlyDist;
						Reward += DefendReward.ZoneApproachReward * FMath::Max(ApproachDelta, 0.0f);
					}

					// Softened distance penalty: reduced magnitude so it doesn't dominate.
					// Original: 0.5 * min(dist/10000, 1.0) → up to -0.5/step.
					// New: 0.2 * min(dist/10000, 1.0) → up to -0.2/step (baseline absorbs this).
					const float DistPenalty = FMath::Min(CurrNearestFriendlyDist / 10000.0f, 1.0f) * 0.2f;
					Reward -= DistPenalty;
				}
			}
		}
		break;

	case EStrategyType::Support:
		{
			// B2: Unconditional baseline — same rationale as Defend.
			Reward += SupportBaselineReward;

			if (Current.Health > SupportHealthThreshold)
			{
				Reward += SupportReward.HealthBonus;
			}
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

				if (CurrAllyDist <= SupportAllyProximityThreshold)
				{
					// Flat proximity bonus when within support range
					Reward += SupportReward.AllyProximityBonus;

					// Extra bonus for being next to a critically injured ally (HP < 30%)
					const float AllyHP = Current.AllyHealths[InjuredAllyIdx];
					if (AllyHP > 0.0f && AllyHP < 0.3f)
					{
						Reward += SupportReward.AllyProximityBonus * 0.5f;
					}
				}
				// B2: Removed distance penalty for being far from allies.
				// Baseline reward provides the offset; approach shaping provides the gradient.
				// No need to punish distance — just reward approaching.

				// Approach shaping: only reward APPROACHING (clamp negative to 0).
				if (!bIsRespawnStep && InjuredAllyIdx < Prev.AllyPositions.Num())
				{
					const float PrevAllyDist = FVector::Dist(Prev.Position, Prev.AllyPositions[InjuredAllyIdx]);
					const float ApproachDelta = PrevAllyDist - CurrAllyDist;
					Reward += SupportReward.AllyApproachReward * FMath::Max(ApproachDelta, 0.0f);
				}
			}
			else
			{
				// No valid ally target (all dead or no allies): provide a small baseline
				Reward += SupportReward.PositionReward * 0.5f;
			}
		}
		break;
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
	ResetCumulativeReward();
	PostCaptureMomentumStepsRemaining = 0;
	LastCapturedPointLocation = FVector::ZeroVector;
	CachedInjuredAllyIdx = -1;
	InjuredAllyStalenessCounter = 0;
	LastCaptureLossPenaltyTime.Empty();
	bAgentDiedThisEpisode = false;
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
