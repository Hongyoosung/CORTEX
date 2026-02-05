// Copyright Epic Games, Inc. All Rights Reserved.

#include "RL/Components/RewardCalculator.h"
#include "Team/ObjectiveActor.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"


URewardCalculator::URewardCalculator()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f; // Update at 10Hz
}

void URewardCalculator::BeginPlay()
{
	Super::BeginPlay();

	// Reset state
	AccumulatedIndividualReward = 0.0f;
	AccumulatedCoordinationReward = 0.0f;

	// v9.0: Removed explicit objective tracking (now uses observation fields)
	// Strategy-specific rewards use HostileObjectiveDistance/FriendlyObjectiveDistance instead
	PreviousCaptureProgress = 0.0f;
	bCaptureCompletionRewarded = false;
}

FRewardComponentBreakdown URewardCalculator::ComputeCurrentReward(const FObservationElement& PrevObs, const FObservationElement& CurrentObs, EStrategyType CurrentStrategy, const FTacticalParameters& CurrentParams)
{
	// 1. 순수 로직 계산 (주입받은 Obs와 Strategy 사용)
	FRewardComponentBreakdown Breakdown = CalculateUnifiedReward(
		CurrentStrategy, // 멤버 변수 대신 인자 사용 권장
		PrevObs,
		CurrentObs
	);

	// 2. 누적된 이벤트 보상 합산 (OnKill, OnDamage 등으로 쌓인 값)
	float EventRewards = AccumulatedIndividualReward + AccumulatedCoordinationReward;
	Breakdown.Total += EventRewards;

	// 3. 누적 변수 초기화
	AccumulatedIndividualReward = 0.0f;
	AccumulatedCoordinationReward = 0.0f;
	// ... 기타 초기화 ...

	return Breakdown;
}





float URewardCalculator::CalculateReward(
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs,
	const FMacroAction& Action)
{
	// v8.0 NOTE: FMacroAction now contains tactical parameters, not strategy
	// Strategy is assigned by MCTS, RL outputs tactical parameters
	// This function delegates to CalculateUnifiedReward()

	FRewardComponentBreakdown Breakdown = CalculateUnifiedReward(
		CurrentStrategy,
		PrevObs,
		CurrentObs
	);

	// Total reward from component breakdown
	float TotalReward = Breakdown.Total;

	// Reset accumulators
	AccumulatedIndividualReward = 0.0f;
	KillsSinceLastUpdate = 0;
	DamageSinceLastUpdate = 0.0f;
	DamageTakenSinceLastUpdate = 0.0f;

	return TotalReward;
}

//--------------------------------------------------------------------------
// UNIFIED REWARD SYSTEM - Component-Based Calculation
//--------------------------------------------------------------------------

FRewardComponentBreakdown URewardCalculator::CalculateUnifiedReward(
	EStrategyType Strategy,
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	FRewardComponentBreakdown Breakdown;

	// Get strategy-specific weights
	const RewardConfig::FStrategyWeights& Weights = RewardConfig::GetWeightsForStrategy(Strategy);

	// Normalize EACH component BEFORE strategy weighting
	// This prevents any single component from dominating the value function

	// Component 1: Objective Progress
	float objRaw = CalculateObjectiveProgressComponent(PrevObs, CurrentObs, Strategy);
	float objNormalized = RewardConfig::OBJECTIVE_NORM.Normalize(objRaw);
	Breakdown.ObjectiveProgress = objNormalized * Weights.ObjectiveProgress;

	// Component 2: Combat Effectiveness
	float combatRaw = CalculateCombatEffectivenessComponent(CurrentObs);
	float combatNormalized = RewardConfig::COMBAT_NORM.Normalize(combatRaw);
	Breakdown.CombatEffectiveness = combatNormalized * Weights.CombatEffectiveness;

	// Component 3: Survival
	float survivalRaw = CalculateSurvivalComponent(CurrentObs);
	float survivalNormalized = RewardConfig::SURVIVAL_NORM.Normalize(survivalRaw);
	Breakdown.Survival = survivalNormalized * Weights.Survival;

	// Component 4: Cover Usage
	float coverRaw = CalculateCoverUsageComponent(CurrentObs);
	float coverNormalized = RewardConfig::COVER_NORM.Normalize(coverRaw);
	Breakdown.CoverUsage = coverNormalized * Weights.CoverUsage;


	// Component 6: Tactical Parameter Effectiveness
	// Creates tight feedback loop: RL parameters → EQS positioning → outcome → reward
	float tacticalRaw = CalculateTacticalParameterEffectivenessComponent(CurrentObs, CurrentTacticalParams);
	float tacticalNormalized = RewardConfig::TACTICAL_NORM.Normalize(tacticalRaw);
	Breakdown.TacticalEffectiveness = tacticalNormalized * Weights.TacticalEffectiveness;

	// Total reward (sum all weighted components)
	float RawTotal = Breakdown.ObjectiveProgress +
	                 Breakdown.CombatEffectiveness +
	                 Breakdown.Survival +
	                 Breakdown.CoverUsage +
	                 Breakdown.TeamCoordination +
	                 Breakdown.TacticalEffectiveness;

	// Soft tanh scaling instead of hard clamp
	// tanh provides smooth gradients at boundaries, better for learning
	// Dividing by 4.0 before tanh keeps typical rewards in linear region
	// Multiplying by 5.0 after tanh gives output range [-5, 5] (softer than v8.10's [-10, 10])
	Breakdown.Total = FMath::Tanh(RawTotal / 4.0f) * 5.0f;

	// v9.0 FIX: Per-component logging to detect identical rewards across agents
	static TMap<URewardCalculator*, int32> LogCounters;
	int32& LogCounter = LogCounters.FindOrAdd(this, 0);

	if (++LogCounter % 100 == 0)
	{
		// Enhanced logging with observation context to verify differentiation
		UE_LOG(LogTemp, Warning, TEXT("🎯 [REWARD v9.0] %s '%s' (#%d):"),
			*UEnum::GetValueAsString(Strategy),
			*GetOwner()->GetName(),
			LogCounter);

		UE_LOG(LogTemp, Warning, TEXT("   Obj=%.2f, Combat=%.2f, Surv=%.2f, Cover=%.2f, Coord=%.2f, Tact=%.2f → Total=%.2f"),
			Breakdown.ObjectiveProgress,
			Breakdown.CombatEffectiveness,
			Breakdown.Survival,
			Breakdown.CoverUsage,
			Breakdown.TeamCoordination,
			Breakdown.TacticalEffectiveness,
			Breakdown.Total);

		// v9.0: Show observation context to verify differentiation
		UE_LOG(LogTemp, Display, TEXT("   Obs: FriendlyDist=%.3f, HostileDist=%.3f, EnemyDist=%.3f, AllyDist=%.3f"),
			CurrentObs.FriendlyObjectiveDistance,
			CurrentObs.HostileObjectiveDistance,
			CurrentObs.DistanceToNearestEnemy,
			CurrentObs.AllyDistance);

		// Detect if using default observations (indicates problem)
		if (CurrentObs.FriendlyObjectiveDistance >= 0.99f && CurrentObs.HostileObjectiveDistance >= 0.99f)
		{
			UE_LOG(LogTemp, Error, TEXT("   ⚠️ WARNING: Using default objective distances (1.0)! This will cause identical rewards."));
		}
	}

	return Breakdown;
}

//--------------------------------------------------------------------------
// v8.0: REWARD COMPONENT IMPLEMENTATIONS (Strategy-Agnostic)
//--------------------------------------------------------------------------

float URewardCalculator::CalculateObjectiveProgressComponent(
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs,
	EStrategyType CurrentStrategy)
{

	switch (CurrentStrategy)
	{
	case EStrategyType::Assault:
		return CalculateAssaultReward(PrevObs, CurrentObs);
	case EStrategyType::Defend:
		return CalculateDefendReward(PrevObs, CurrentObs);
	default:
		return 0.0f;
	}
}

//--------------------------------------------------------------------------
// v9.0: STRATEGY-SPECIFIC REWARD FUNCTIONS
//--------------------------------------------------------------------------

float URewardCalculator::CalculateAssaultReward(
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	// v9.0 GRADIENT-BASED: Assault focuses on approaching hostile objective
	// Uses continuous distance gradient instead of binary thresholds
	float Reward = 0.0f;
	float Distance = CurrentObs.HostileObjectiveDistance;  // [0, 1] normalized

	// [DEBUG CHECK] Verify observation is populated
	if (Distance >= 0.99f)
	{
		static int32 WarningCount = 0;
		if (++WarningCount % 100 == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[ASSAULT REWARD] Agent %s: HostileObjectiveDistance=1.0 (default). ObsBuilder not populating!"), *GetOwner()->GetName());
		}
		return 0.0f;  // No reward if observation invalid
	}

	// ========================================
	// GRADIENT REWARD: Closer to hostile objective = higher reward
	// ========================================
	// Distance 0.0 (at objective) → reward = 10.0
	// Distance 0.5 (halfway) → reward = 5.0
	// Distance 1.0 (max distance) → reward = 0.0
	Reward = (1.0f - Distance) * 10.0f;

	// ========================================
	// BONUS: Very close to hostile objective (<10% distance)
	// ========================================
	if (Distance < 0.1f)
	{
		Reward += 15.0f;  // Large bonus for reaching objective
	}

	// ========================================
	// BONUS: Combat engagement near objective
	// ========================================
	// Reward aggressive combat near the hostile objective
	if (CurrentObs.VisibleEnemyCount > 0 && Distance < 0.2f)
	{
		Reward += 5.0f * CurrentObs.VisibleEnemyCount;  // +5.0 per enemy engaged near objective
	}

	// v9.0 DEBUG: Log gradient reward calculation
	static TMap<URewardCalculator*, int32> AssaultLogCounts;
	int32& LogCount = AssaultLogCounts.FindOrAdd(this, 0);
	if (++LogCount % 100 == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("🔵 [ASSAULT GRADIENT] %s: Dist=%.3f → BaseReward=%.2f, Bonuses=%.2f → Total=%.2f"),
			*GetOwner()->GetName(),
			Distance,
			(1.0f - Distance) * 10.0f,
			Reward - ((1.0f - Distance) * 10.0f),
			Reward
		);
	}

	return Reward;
}

float URewardCalculator::CalculateDefendReward(
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	// v9.0 GRADIENT-BASED: Defend focuses on staying near friendly objective
	// Uses continuous distance gradient instead of binary volume checks
	float Reward = 0.0f;
	float Distance = CurrentObs.FriendlyObjectiveDistance;  // [0, 1] normalized

	// ========================================
	// GRADIENT REWARD: Closer to friendly objective = higher reward
	// ========================================
	// Distance 0.0 (at objective) → reward = 10.0
	// Distance 0.5 (halfway) → reward = 5.0
	// Distance 1.0 (max distance) → reward = 0.0
	Reward = (1.0f - Distance) * 10.0f;

	// ========================================
	// BONUS: Inside defensive perimeter (<20% distance)
	// ========================================
	if (Distance < 0.2f)
	{
		Reward += 10.0f;  // Bonus for tight perimeter defense
	}

	// ========================================
	// BONUS: Defending against enemies near objective
	// ========================================
	// Reward active defense when enemies are nearby
	if (CurrentObs.VisibleEnemyCount > 0 && Distance < 0.2f)
	{
		Reward += 8.0f * CurrentObs.VisibleEnemyCount;  // +8.0 per enemy while defending
	}

	// v9.0 DEBUG: Log gradient reward calculation
	static TMap<URewardCalculator*, int32> DefendLogCounts;
	int32& LogCount = DefendLogCounts.FindOrAdd(this, 0);
	if (++LogCount % 100 == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("🟢 [DEFEND GRADIENT] %s: Dist=%.3f → BaseReward=%.2f, Bonuses=%.2f → Total=%.2f"),
			*GetOwner()->GetName(),
			Distance,
			(1.0f - Distance) * 10.0f,
			Reward - ((1.0f - Distance) * 10.0f),
			Reward
		);
	}

	return Reward;
}


float URewardCalculator::CalculateCombatEffectivenessComponent(
	const FObservationElement& CurrentObs)
{
	float Reward = 0.0f;

	// ✅ LOG: Combat activity tracking
	static int32 TickCounter = 0;
	TickCounter++;

	if (TickCounter % 100 == 0)  // Log every 100 ticks (~10 seconds)
	{
		UE_LOG(LogTemp, Warning, TEXT("[COMBAT CHECK] Agent=%s | DamageDealt=%.1f | Kills=%d"),
			*GetOwner()->GetName(),
			DamageSinceLastUpdate,
			KillsSinceLastUpdate
		);
	}

	// Damage dealt
	if (DamageSinceLastUpdate > 0.0f)
	{
		Reward += DamageSinceLastUpdate * RewardConfig::DAMAGE_REWARD_SCALE;  // +0.1 per 1 damage
	}

	// Kills (v8.0: includes efficient target selection bonus)
	if (KillsSinceLastUpdate > 0)
	{
		if (bLastKillWasLowestHP)
		{
			Reward += RewardConfig::KILL_REWARD_EFFICIENT;  // +12.0 for efficient targeting
		}
		else
		{
			Reward += RewardConfig::KILL_REWARD_BASE;  // +10.0 base kill reward
		}
	}

	return Reward;
}

float URewardCalculator::CalculateSurvivalComponent(
	const FObservationElement& CurrentObs)
{
	// Death penalty (will be accumulated in AccumulatedIndividualReward via OnDeath())
	// This component is for continuous survival rewards, death handled separately
	return 0.0f;
}

float URewardCalculator::CalculateCoverUsageComponent(
	const FObservationElement& CurrentObs)
{
	float Reward = 0.0f;

	// Cover bonus (continuous while in cover)
	if (CurrentObs.bHasCover)
	{
		
		Reward += RewardConfig::COVER_BONUS;  // +0.1 per step
	}

	return Reward;
}



float URewardCalculator::CalculateTacticalParameterEffectivenessComponent(
	const FObservationElement& CurrentObs,
	const FTacticalParameters& TacticalParams)
{
	// v9.0: Gradient-based tactical parameter rewards (continuous, not binary)
	// Replaces binary thresholds with smooth gradient functions
	// Expected: 2-3× faster convergence due to continuous feedback
	float TotalReward = 0.0f;

	// ========================================
	// 1. AGGRESSION: Target distance based on aggression level
	// ========================================
	// High aggression (0.8) → target distance 0.2 (20% of max)
	// Medium aggression (0.5) → target distance 0.5 (50% of max)
	// Low aggression (0.2) → target distance 0.8 (80% of max)
	{
		float TargetDistance = 0.8f - (TacticalParams.Aggression * 0.6f);  // Range [0.2, 0.8]
		float ActualDistance = CurrentObs.DistanceToNearestEnemy;
		float DistanceError = FMath::Abs(TargetDistance - ActualDistance);

		// Gaussian-like reward: peak at target, falls off with distance
		float AggressionReward = FMath::Exp(-DistanceError * 3.0f);  // Range [0, 1]
		TotalReward += AggressionReward * 0.3f;  // Weight: 30%
	}

	// ========================================
	// 2. COVER: Probabilistic alignment + penalty under fire
	// ========================================
	// High CoverPreference → should be in cover when enemies nearby
	// Low CoverPreference → can be exposed
	{
		float CoverReward = 0.0f;
		bool bEnemiesNearby = CurrentObs.DistanceToNearestEnemy < 0.5f;

		if (CurrentObs.bHasCover)
		{
			// In cover: Reward proportional to CoverPreference
			CoverReward = TacticalParams.CoverPreference;

			// Extra reward if taking fire (cover is critical)
			if (bEnemiesNearby)
			{
				CoverReward += 0.5f;
			}
		}
		else
		{
			// Not in cover: Reward if preference is low
			CoverReward = 1.0f - TacticalParams.CoverPreference;

			// Penalty if taking fire without cover
			if (bEnemiesNearby && TacticalParams.CoverPreference > 0.6f)
			{
				CoverReward -= 0.5f;  // Should be in cover!
			}
		}

		TotalReward += FMath::Clamp(CoverReward, -0.5f, 1.0f) * 0.2f;  // Weight: 20%
	}

	// ========================================
	// 3. SPREAD: Target ally distance based on spread parameter
	// ========================================
	// Low spread (0.0) → tight formation, target 0.1 (10% of max distance)
	// High spread (1.0) → dispersed, target 0.6 (60% of max distance)
	if (CurrentObs.AllyDistance > 0.01f)  // Has ally data
	{
		float TargetAllyDistance = 0.1f + (TacticalParams.SpreadDistance * 0.5f);  // Range [0.1, 0.6]
		float ActualAllyDistance = CurrentObs.AllyDistance;
		float DistanceError = FMath::Abs(TargetAllyDistance - ActualAllyDistance);

		// Gaussian-like reward
		float SpreadReward = FMath::Exp(-DistanceError * 4.0f);  // Range [0, 1]
		TotalReward += SpreadReward * 0.25f;  // Weight: 25%
	}

	// ========================================
	// 4. RISK: Gradient threat level × tolerance
	// ========================================
	// Threat level = (1 - health) × (1 - enemyDistance) × enemyCount
	// High risk tolerance → can survive high threat
	// Low risk tolerance → should avoid threat
	{
		float ThreatLevel = (1.0f - CurrentObs.AgentHealth) *
		                    (1.0f - CurrentObs.DistanceToNearestEnemy) *
		                    FMath::Min(CurrentObs.VisibleEnemyCount / 3.0f, 1.0f);  // Normalized [0, 1]

		// Acceptable threat = RiskTolerance (high tolerance → can handle high threat)
		float ThreatError = FMath::Abs(ThreatLevel - TacticalParams.RiskTolerance);

		// Reward for matching risk tolerance to actual threat exposure
		float RiskReward = FMath::Exp(-ThreatError * 2.0f);  // Range [0, 1]
		TotalReward += RiskReward * 0.15f;  // Weight: 15%
	}

	// ========================================
	// 5. TEMPORAL CONSISTENCY: Penalize erratic parameter changes
	// ========================================
	// Reward smooth parameter adjustments over time
	{
		// Compare current params to previous params
		float ParamDelta = FMath::Abs(TacticalParams.Aggression - PreviousTacticalParams.Aggression) +
		                   FMath::Abs(TacticalParams.CoverPreference - PreviousTacticalParams.CoverPreference) +
		                   FMath::Abs(TacticalParams.SpreadDistance - PreviousTacticalParams.SpreadDistance) +
		                   FMath::Abs(TacticalParams.RiskTolerance - PreviousTacticalParams.RiskTolerance);

		// Reward small deltas (smooth control), penalize large deltas (erratic)
		float ConsistencyReward = FMath::Exp(-ParamDelta * 2.0f);  // Range [0, 1]
		TotalReward += ConsistencyReward * 0.1f;  // Weight: 10%

		// Update previous params
		PreviousTacticalParams = TacticalParams;
	}

	// Scale total reward by effectiveness bonus
	return TotalReward * RewardConfig::TACTICAL_EFFECTIVENESS_BONUS;  // × 0.5
}

void URewardCalculator::SetCurrentTacticalParameters(const FTacticalParameters& Params)
{
	// v9.0: Store previous params for temporal consistency reward
	PreviousTacticalParams = CurrentTacticalParams;
	CurrentTacticalParams = Params;
}


//--------------------------------------------------------------------------
// EVENT TRACKING
//--------------------------------------------------------------------------

void URewardCalculator::OnKillEnemy(AActor* Enemy)
{
	// Delegate to priority-aware version (assume not lowest HP if not specified)
	OnKillEnemyWithPriority(Enemy, false);
}

void URewardCalculator::OnKillEnemyWithPriority(AActor* Enemy, bool bWasLowestHP)
{
	KillsSinceLastUpdate++;
	bLastKillWasLowestHP = bWasLowestHP;

	// Kill reward calculated in CalculateCombatEffectivenessComponent
	// Base: +10.0, Efficient (lowest HP): +12.0

	UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT v8.0] '%s': Kill (LowestHP=%s) → +%.1f in next tick"),
		*GetOwner()->GetName(),
		bWasLowestHP ? TEXT("true") : TEXT("false"),
		bWasLowestHP ? RewardConfig::KILL_REWARD_EFFICIENT : RewardConfig::KILL_REWARD_BASE);
}

void URewardCalculator::OnDealDamage(float Damage, AActor* Target)
{
	DamageSinceLastUpdate += Damage;

	// Register for combined fire tracking
	RegisterCombinedFire(Target);

	// ✅ LOG: Upgraded to Warning level for visibility
	UE_LOG(LogTemp, Warning, TEXT("💥 [DAMAGE EVENT] '%s' dealt %.1f damage to '%s' (accumulated: %.1f)"),
		*GetOwner()->GetName(), Damage, Target ? *Target->GetName() : TEXT("NULL"), DamageSinceLastUpdate);
}

void URewardCalculator::OnTakeDamage(float Damage)
{
	DamageTakenSinceLastUpdate += Damage;

	UE_LOG(LogTemp, Verbose, TEXT("[REWARD EVENT] '%s': Took %.1f damage (accumulated: %.1f)"),
		*GetOwner()->GetName(), Damage, DamageTakenSinceLastUpdate);
}

void URewardCalculator::OnDeath()
{
	// Death penalty scaled by strategy survival weight
	AccumulatedIndividualReward += RewardConfig::DEATH_PENALTY;

	UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Death penalty %.1f"),
		*GetOwner()->GetName(), RewardConfig::DEATH_PENALTY);
}


void URewardCalculator::SetCurrentStrategy(EStrategyType Strategy)
{
	if (CurrentStrategy != Strategy)
	{
		EStrategyType PreviousStrategy = CurrentStrategy;
		CurrentStrategy = Strategy;

		// v9.0: Removed explicit objective tracking
		// Strategy-specific rewards use observation fields instead
		PreviousCaptureProgress = 0.0f;
		bCaptureCompletionRewarded = false;

		// v9.0 FIX: Enhanced logging to track strategy changes
		UE_LOG(LogTemp, Display, TEXT("✅ [SET CURRENT STRATEGY] '%s': %s → %s (CurrentStrategy now = %s)"),
			*GetOwner()->GetName(),
			*UEnum::GetValueAsString(PreviousStrategy),
			*UEnum::GetValueAsString(Strategy),
			*UEnum::GetValueAsString(CurrentStrategy));
	}
	else
	{
		// v9.0 DEBUG: Log when SetCurrentStrategy is called but value doesn't change
		static TMap<URewardCalculator*, int32> NoChangeCount;
		int32& Count = NoChangeCount.FindOrAdd(this, 0);
		if (++Count % 100 == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("ℹ️ [SET CURRENT STRATEGY] '%s': Already %s (no change, count=%d)"),
				*GetOwner()->GetName(),
				*UEnum::GetValueAsString(Strategy),
				Count);
		}
	}
}


void URewardCalculator::RegisterCombinedFire(AActor* Target)
{
	if (!Target)
	{
		return;
	}

	// Record this fire event
	float CurrentTime = GetWorld()->GetTimeSeconds();

	// Check if this target was recently fired upon by others
	int32 ExistingCount = 0;
	for (const FCombinedFireRecord& Record : RecentCombinedFires)
	{
		if (Record.Target == Target && (CurrentTime - Record.Timestamp) <= CombinedFireWindow)
		{
			ExistingCount++;
		}
	}

	// If 2+ agents firing at same target, award bonus
	if (ExistingCount >= 1) // This agent + 1 other = combined fire
	{
		AccumulatedCoordinationReward += RewardConfig::COMBINED_FIRE_REWARD; // +20.0 for combined fire (was 10.0)
		UE_LOG(LogTemp, Log, TEXT("[REWARD v8.0] Combined fire on %s: +%.1f"), *Target->GetName(), RewardConfig::COMBINED_FIRE_REWARD);
	}

	// Add this record
	FCombinedFireRecord NewRecord;
	NewRecord.Target = Target;
	NewRecord.Timestamp = CurrentTime;
	RecentCombinedFires.Add(NewRecord);
}