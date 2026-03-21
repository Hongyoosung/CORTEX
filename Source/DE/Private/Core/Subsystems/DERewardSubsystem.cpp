// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/Subsystems/DERewardSubsystem.h"
#include "Data/Reward/DEAssaultReward.h"
#include "Data/Reward/DEDefendReward.h"
#include "Data/Reward/DESupportReward.h"
#include "Data/Reward/DERewardData.h"
#include "Characters/DEAgent.h"
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

float UDERewardSubsystem::CalculateTeamWipeBonus(FDERewardState& InOutState, EDEStrategyType ActiveStrategy, int32 AgentID)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return 0.0f;
	return ApplyAndLogReward(InOutState, EDERewardEventType::TeamWipe, ActiveStrategy, Settings->TeamWipeBonus, AgentID);
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
	ADEAgent* Agent,
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

	const float PositionChange = FVector::Dist(Prev.Position, Current.Position);
	const int32 MyTeamID = Agent->GetTeamID_Implementation();
	const int32 MyEnvID  = Agent->GetEnvID_Implementation();
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

	float CaptureRadiusSq = 250000.0f;
	if (EnvCapturePoints.Num() > 0 && EnvCapturePoints[0])
		CaptureRadiusSq = FMath::Square(EnvCapturePoints[0]->CaptureRadius);

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

	// ---- Dispatch per-strategy step reward ----
	float Reward = 0.0f;
	switch (Strategy)
	{
	case EDEStrategyType::Assault:
		Reward = DEComputeAssaultStepReward(Agent, InOutState, Prev, Current, EnvCapturePoints, Settings,
			CaptureRadiusSq, PositionChange, bIsRespawnStep, bIsolated, MyTeamID);
		break;

	case EDEStrategyType::Defend:
		Reward = DEComputeDefendStepReward(Agent, InOutState, Prev, Current, EnvCapturePoints, Settings,
			CaptureRadiusSq, PositionChange, bIsRespawnStep, bIsolated, MyTeamID);
		break;

	case EDEStrategyType::Support:
		Reward = DEComputeSupportStepReward(Agent, InOutState, Prev, Current, EnvCapturePoints, Settings,
			CaptureRadiusSq, PositionChange, bIsRespawnStep, bIsolated, MyTeamID);
		break;
	}

	// ---- Common: Survival reward ----
	if (!bIsRespawnStep && Current.bIsAlive)
		CalculateSurvivalReward(InOutState, Strategy, Current.Health, 1.0f, Agent->AgentID);

	// ---- Assault-only: close-range kill tracking (for sparse reward scaling) ----
	// Defend (melee) intentionally has no penalty for being close.
	if (!bIsRespawnStep && Strategy == EDEStrategyType::Assault && Settings->CloseRangeKillThreshold > 0.0f)
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

	// ---- Zone Control Reward ----
	if (MyTeamID >= 0 && EnvCapturePoints.Num() > 0)
	{
		int32 FriendlyBases = 0, EnemyBases = 0, NeutralBases = 0;
		for (const ADECapturePoint* CP : EnvCapturePoints)
		{
			if (!CP) continue;
			const int32 OwnerTeam = CP->GetTeamID_Implementation();
			if (OwnerTeam == MyTeamID) FriendlyBases++;
			else if (OwnerTeam >= 0)   EnemyBases++;
			else                       NeutralBases++;
		}
		const float NetControl = static_cast<float>(FriendlyBases - EnemyBases) - NeutralBases * 0.5f;
		const float ZoneControlScale = GetStrategyScale(Strategy,
			Settings->ZoneControlAssaultScale, Settings->ZoneControlDefendScale, Settings->ZoneControlSupportScale);
		Reward += Settings->ZoneControlRewardPerBase * ZoneControlScale * NetControl;
	}

	// ---- Cooperative base occupation shaping ----
	Reward += ComputeBaseCooperationReward(Agent, InOutState, Strategy);

	// ---- Step (time) penalty ----
	const float EffectiveStepPenalty = (Strategy == EDEStrategyType::Assault)
		? Settings->AssaultReward.TimePenalty
		: -Settings->StepPenalty;
	Reward -= EffectiveStepPenalty;

	// ---- Drain and scale sparse rewards ----
	float DrainedSparse = DrainSparseReward(InOutState, Agent->AgentID);
	// For Assault only: penalise kills made at melee range (ranged DPS should stay back)
	if (Strategy == EDEStrategyType::Assault &&
		InOutState.bWasTooCloseAtKill && InOutState.bSparseKillFiredThisStep)
	{
		DrainedSparse *= Settings->CloseRangeKillPenaltyScale;
	}
	Reward += DrainedSparse;

	Reward *= Settings->RewardScale;
	Reward = FMath::Clamp(Reward, Settings->StepRewardClampMin, Settings->StepRewardClampMax);

	InOutState.bSparseKillFiredThisStep = false;
	InOutState.bWasTooCloseAtKill       = false;

	// ---- Team reward mixing ----
	const float CurrentIndividualReward = Reward;
	if (Settings->TeamRewardMixingRatio > 0.0f)
	{
		if (ADEMatchManager* MatchMgr = Agent->GetMatchManager())
		{
			const TArray<ADEAgent*>& Teammates = MatchMgr->GetTeamAgents(MyTeamID);
			float TeamRewardSum = 0.0f;
			int32 TeamCount = 0;
			for (ADEAgent* Mate : Teammates)
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

float UDERewardSubsystem::ComputeBaseCooperationReward(ADEAgent* Agent, FDERewardState& InOutState, EDEStrategyType Strategy)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings || !Agent) return 0.0f;

	ADEMatchManager* MatchMgr = Agent->GetMatchManager();
	if (!MatchMgr) return 0.0f;

	const TArray<ADECapturePoint*>& CPs = MatchMgr->GetCapturePoints();
	if (CPs.Num() == 0) return 0.0f;

	const int32 MyTeamID = Agent->GetTeamID_Implementation();
	const FVector MyPos  = Agent->GetActorLocation();
	const float Radius   = Settings->BaseOccupationRadius;
	const float RadiusSq = Radius * Radius;

	TArray<ADEAgent*> Teammates = MatchMgr->GetTeamAgents(MyTeamID);
	TArray<ADEAgent*> Enemies   = MatchMgr->GetEnemyAgents(MyTeamID);

	// ---- Identify frontline bases (closest friendly bases to any hostile base) ----
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
		for (ADECapturePoint* FBase : FriendlyBases)
			for (ADECapturePoint* HBase : HostileBases)
			{
				const float DistSq = FVector::DistSquared(FBase->GetActorLocation(), HBase->GetActorLocation());
				if (DistSq < MinDistSqToFront) MinDistSqToFront = DistSq;
			}

		const float FrontlineMarginSq = MinDistSqToFront * 1.44f;
		for (ADECapturePoint* FBase : FriendlyBases)
			for (ADECapturePoint* HBase : HostileBases)
				if (FVector::DistSquared(FBase->GetActorLocation(), HBase->GetActorLocation()) <= FrontlineMarginSq)
				{
					FrontlineBases.Add(FBase);
					break;
				}
	}
	else
	{
		FrontlineBases = FriendlyBases;
	}

	// ---- Per-base cooperation reward ----
	float Reward = 0.0f;
	for (int32 i = 0; i < CPs.Num(); ++i)
	{
		ADECapturePoint* CP = CPs[i];
		if (!CP) continue;

		const FVector CPPos  = CP->GetActorLocation();
		const int32   Owner  = CP->GetTeamID_Implementation();
		const float   DistSq = FVector::DistSquared(MyPos, CPPos);
		const bool    bNearMe = (DistSq <= RadiusSq);

		int32 AlliesNear = 0;
		for (ADEAgent* Mate : Teammates)
		{
			if (!Mate || Mate == Agent || !Mate->IsAlive()) continue;
			if (FVector::DistSquared(Mate->GetActorLocation(), CPPos) <= RadiusSq)
				++AlliesNear;
		}

		// Count enemies near this base (for contested-base detection)
		int32 EnemiesNear = 0;
		for (ADEAgent* Foe : Enemies)
		{
			if (!Foe || !Foe->IsAlive()) continue;
			if (FVector::DistSquared(Foe->GetActorLocation(), CPPos) <= RadiusSq)
				++EnemiesNear;
		}

		if (bNearMe)
		{
			// Support should never be incentivized toward capture points — skip all base rewards
			if (Strategy != EDEStrategyType::Support)
			{
				// Assault and Defend get BaseOccupationReward when alone in enemy/neutral zone
				if (Owner != MyTeamID && AlliesNear == 0)
					Reward += Settings->BaseOccupationReward;

				// Stacking penalty: Assault/Defend should spread across bases
				if (AlliesNear >= 1)
					Reward -= Settings->CoOccupationPenalty;

				// Contested-base defense: reward for being near a friendly base that enemies are threatening
				if (Owner == MyTeamID && EnemiesNear > 0)
					Reward += Settings->ContestedBaseDefenseReward;

				// Assigned base first-arrival reward
				if (!InOutState.bHasReachedAssignedBase && Agent->AssignedBaseIndex == i)
				{
					InOutState.bHasReachedAssignedBase = true;
					Reward += Settings->AssignedBaseReachReward;
				}
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

void UDERewardSubsystem::ApplyMatchEndReward(int32 WinnerTeamID, const TArray<ADEAgent*>& AllAgents)
{
	const UDERewardData* Settings = GetSettings();
	if (!Settings) return;

	for (ADEAgent* Agent : AllAgents)
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
