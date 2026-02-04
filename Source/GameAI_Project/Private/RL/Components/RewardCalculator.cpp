// Copyright Epic Games, Inc. All Rights Reserved.

#include "RL/Components/RewardCalculator.h"
#include "Actor/FollowerCharacter.h"
#include "Actor/LeaderCharacter.h"
#include "Team/ObjectiveActor.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
// v9.0 PHASE 5: Use FollowerCharacter API (Character-as-Central-Hub)
#include "Actor/FollowerCharacter.h"

URewardCalculator::URewardCalculator()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f; // Update at 10Hz
}

void URewardCalculator::BeginPlay()
{
	Super::BeginPlay();

	// v9.0 Phase 4: Use character API instead of FindComponentByClass
	// Character injects component references via InitializeComponents()
	AFollowerCharacter* FollowerChar = Cast<AFollowerCharacter>(GetOwner());
	if (!FollowerChar)
	{
		UE_LOG(LogTemp, Error, TEXT("[RewardCalculator v9.0] RewardCalculator must be attached to FollowerCharacter! Owner: %s"),
			*GetOwner()->GetName());
		return;
	}

	// Get components through character wrapper API (no FindComponentByClass)
	FollowerAgent = FollowerChar;

	if (!FollowerAgent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RewardCalculator v9.0] No FollowerAgent found on %s"), *FollowerChar->GetName());
	}
	else
	{
		// Initialize previous observation for delta calculations using character API
		PreviousObservation = FollowerChar->BuildLocalObservation();
		UE_LOG(LogTemp, Log, TEXT("[REWARD INIT v9.0] '%s': PreviousObservation initialized via character API"), *FollowerChar->GetName());
	}

	// Reset state
	AccumulatedIndividualReward = 0.0f;
	AccumulatedCoordinationReward = 0.0f;

	// v9.0: Removed explicit objective tracking (now uses observation fields)
	// Strategy-specific rewards use HostileObjectiveDistance/FriendlyObjectiveDistance instead
	PreviousCaptureProgress = 0.0f;
	bCaptureCompletionRewarded = false;
}

void URewardCalculator::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Hybrid reward calculation: continuous (positioning) + event-driven (kills, damage)
	// All rewards forwarded to FollowerAgent for Schola integration

	if (!FollowerAgent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[REWARD TICK] No FollowerAgent - cannot forward rewards!"));
		return;
	}

	// Skip reward calculation for dead agents to prevent negative impact on RL training
	if (!FollowerAgent->IsAlive())
	{
		return;
	}

	// v9.0 Phase 4: Build current observation using character API
	AFollowerCharacter* FollowerChar = Cast<AFollowerCharacter>(GetOwner());
	if (!FollowerChar)
	{
		return;
	}
	FObservationElement CurrentObs = FollowerChar->BuildLocalObservation();

	float StepReward = 0.0f;

	// === v8.0 UNIFIED REWARD CALCULATION ===
	// Calculate reward using unified component-based approach
	FRewardComponentBreakdown Breakdown = CalculateUnifiedReward(
		CurrentStrategy,
		PreviousObservation,
		CurrentObs
	);

	StepReward = Breakdown.Total;

	// Cache breakdown for TensorBoard logging
	LastRewardBreakdown = Breakdown;

	// === EVENT-BASED REWARDS (kills, damage, death accumulated since last tick) ===
	float eventRewards = AccumulatedIndividualReward + AccumulatedCoordinationReward;
	StepReward += eventRewards;

	// Forward total reward to FollowerAgent
	if (FMath::Abs(StepReward) > 0.01f) // Avoid spam for tiny rewards
	{
		FollowerAgent->ProvideReward(StepReward);

		// v8.0: Log with component breakdown
		/*UE_LOG(LogTemp, Display,
			TEXT("[REWARD TICK v8.0] '%s' (%s): Total=%.2f | %s | Events=%.2f"),
			*GetOwner()->GetName(),
			*UEnum::GetValueAsString(CurrentStrategy),
			StepReward + eventRewards,
			*Breakdown.ToString(),
			eventRewards);*/
	}

	// Reset event accumulators (events are one-time, continuous rewards recalculate every tick)
	AccumulatedIndividualReward = 0.0f;
	AccumulatedCoordinationReward = 0.0f;
	KillsSinceLastUpdate = 0;
	DamageSinceLastUpdate = 0.0f;
	DamageTakenSinceLastUpdate = 0.0f;

	// Update previous observation for next tick
	PreviousObservation = CurrentObs;

	// Clean up old combined fire records
	float CurrentTime = GetWorld()->GetTimeSeconds();
	RecentCombinedFires.RemoveAll([CurrentTime, this](const FCombinedFireRecord& Record) {
		return (CurrentTime - Record.Timestamp) > CombinedFireWindow;
	});
} 

//--------------------------------------------------------------------------
// UNIFIED REWARD CALCULATION
//--------------------------------------------------------------------------

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
	float objRaw = CalculateObjectiveProgressComponent(PrevObs, CurrentObs);
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

	// Component 5: Team Coordination
	float coordRaw = CalculateTeamCoordinationComponent(CurrentObs);
	float coordNormalized = RewardConfig::COORD_NORM.Normalize(coordRaw);
	Breakdown.TeamCoordination = coordNormalized * Weights.TeamCoordination;

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
	const FObservationElement& CurrentObs)
{
	// v9.0: Strategy-specific objective reward functions
	// No explicit objective assignment - rewards based on observation fields
	if (!FollowerAgent) return 0.0f;

	// v9.0 FIX CRITICAL: DO NOT lazy sync - trust SetCurrentStrategy() value!
	// Lazy sync was OVERWRITING correct strategies with stale data from TacticalState.
	// Now that SetCurrentStrategy() is called explicitly, we TRUST that value.

	// Diagnostic only: Check if FollowerAgent has different strategy (should NOT happen)
	if (FollowerAgent)
	{
		EStrategyType FollowerReportedStrategy = FollowerAgent->GetAssignedStrategy();

		if (FollowerReportedStrategy != CurrentStrategy)
		{
			static TMap<URewardCalculator*, int32> MismatchCounts;
			int32& Count = MismatchCounts.FindOrAdd(this, 0);

			if (++Count <= 3)  // Log first 3 mismatches only
			{
				UE_LOG(LogTemp, Error, TEXT("❌ [STRATEGY MISMATCH v9.0] '%s': FollowerAgent reports '%s', but CurrentStrategy is '%s' (occurrence %d)"),
					*GetOwner()->GetName(),
					*UEnum::GetValueAsString(FollowerReportedStrategy),
					*UEnum::GetValueAsString(CurrentStrategy),
					Count);
				UE_LOG(LogTemp, Error, TEXT("   └─ USING CurrentStrategy '%s' (from SetCurrentStrategy). FollowerAgent data is STALE."),
					*UEnum::GetValueAsString(CurrentStrategy));

				if (Count == 3)
				{
					UE_LOG(LogTemp, Error, TEXT("   └─ Further mismatch warnings suppressed. This indicates TacticalState is not syncing with MCTS assignments."));
				}
			}

			// DO NOT SYNC - trust SetCurrentStrategy value!
			// CurrentStrategy stays as-is (correct value from MCTS → SetCurrentStrategy)
		}
	}

	switch (CurrentStrategy)
	{
		case EStrategyType::Assault:
			return CalculateAssaultReward(PrevObs, CurrentObs);
		case EStrategyType::Defend:
			return CalculateDefendReward(PrevObs, CurrentObs);
		case EStrategyType::Support:
			return CalculateSupportReward(PrevObs, CurrentObs);
		case EStrategyType::Retreat:
			return CalculateRetreatReward(PrevObs, CurrentObs);
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

float URewardCalculator::CalculateSupportReward(
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	// v9.0 GRADIENT-BASED: Support focuses on proximity to allies in need
	// Uses continuous distance gradient when ally needs help
	float Reward = 0.0f;

	// Only provide support rewards if ally actually needs help
	if (!CurrentObs.bAllyNeedsHelp)
	{
		return 0.0f;
	}

	float Distance = CurrentObs.AllyDistance;  // [0, 1] normalized

	// ========================================
	// GRADIENT REWARD: Closer to ally = higher reward
	// ========================================
	// Distance 0.0 (at ally) → reward = 12.0
	// Distance 0.5 (halfway) → reward = 6.0
	// Distance 1.0 (max distance) → reward = 0.0
	// Higher base multiplier (12.0 vs 10.0) because support is critical
	Reward = (1.0f - Distance) * 12.0f;

	// ========================================
	// BONUS: Very close support (<5% distance = ~250cm)
	// ========================================
	if (Distance < 0.05f)
	{
		Reward += 15.0f;  // Large bonus for reaching ally in need
	}

	// ========================================
	// BONUS: Covering ally under fire
	// ========================================
	// Reward staying close while enemies are nearby (protective support)
	if (CurrentObs.VisibleEnemyCount > 0 && Distance < 0.08f)
	{
		Reward += 5.0f;  // Bonus for providing cover fire
	}

	// v9.0 DEBUG: Log gradient reward calculation
	static TMap<URewardCalculator*, int32> SupportLogCounts;
	int32& LogCount = SupportLogCounts.FindOrAdd(this, 0);
	if (++LogCount % 100 == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("🟡 [SUPPORT GRADIENT] %s: AllyDist=%.3f, NeedsHelp=%d → BaseReward=%.2f, Bonuses=%.2f → Total=%.2f"),
			*GetOwner()->GetName(),
			Distance,
			CurrentObs.bAllyNeedsHelp ? 1 : 0,
			(1.0f - Distance) * 12.0f,
			Reward - ((1.0f - Distance) * 12.0f),
			Reward
		);
	}

	return Reward;
}

float URewardCalculator::CalculateRetreatReward(
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	// v9.0 GRADIENT-BASED: Retreat focuses on maximizing distance from enemies
	// Uses continuous distance gradient instead of delta-only rewards
	float Reward = 0.0f;
	float Distance = CurrentObs.DistanceToNearestEnemy;  // [0, 1] normalized

	// ========================================
	// GRADIENT REWARD: Farther from enemies = higher reward
	// ========================================
	// Distance 1.0 (max distance) → reward = 10.0
	// Distance 0.5 (halfway) → reward = 5.0
	// Distance 0.0 (touching enemy) → reward = 0.0
	Reward = Distance * 10.0f;

	// ========================================
	// BONUS: Safe distance achieved (>80% max distance)
	// ========================================
	if (Distance > 0.8f)
	{
		Reward += 10.0f;  // Large bonus for reaching safety
	}

	// ========================================
	// PENALTY: Surrounded by multiple enemies
	// ========================================
	// Penalize failed retreat (still near many enemies)
	if (CurrentObs.VisibleEnemyCount > 2)
	{
		Reward -= 5.0f;  // Penalty for being surrounded
	}

	// v9.0 DEBUG: Log gradient reward calculation
	static TMap<URewardCalculator*, int32> RetreatLogCounts;
	int32& LogCount = RetreatLogCounts.FindOrAdd(this, 0);
	if (++LogCount % 100 == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("🔴 [RETREAT GRADIENT] %s: EnemyDist=%.3f, EnemyCount=%d → BaseReward=%.2f, Modifiers=%.2f → Total=%.2f"),
			*GetOwner()->GetName(),
			Distance,
			CurrentObs.VisibleEnemyCount,
			Distance * 10.0f,
			Reward - (Distance * 10.0f),
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

float URewardCalculator::CalculateTeamCoordinationComponent(
	const FObservationElement& CurrentObs)
{
	float Reward = 0.0f;

	// ✅ v8.20 ENHANCEMENT: Strategy-aware coordination rewards
	EStrategyType AssignedStrategy = FollowerAgent ? FollowerAgent->GetAssignedStrategy() : EStrategyType::Assault;

	// [1] Support Strategy: Reward approaching low-health allies
	if (AssignedStrategy == EStrategyType::Support)
	{
		// Check if observation contains ally health info
		float AllyHP = CurrentObs.AllyHealth;  // Normalized [0,1]
		float AllyDist = CurrentObs.AllyDistance;  // Normalized [0,1]

		if (AllyHP > 0.0f && AllyHP < 0.6f && AllyDist > 0.0f)  // Ally has <60% HP
		{
			// Reward inversely proportional to distance (closer = better)
			float ProximityReward = (1.0f - AllyDist) * RewardConfig::SUPPORT_PROXIMITY_BONUS;  // +2.0 at 0 dist
			Reward += ProximityReward;

			// Additional bonus if ally is critically low (<30% HP) and very close (<200cm)
			if (AllyHP < 0.3f && AllyDist < 0.04f)
			{
				Reward += RewardConfig::SUPPORT_CRITICAL_BONUS;  // +5.0 for critical support
			}

			// ✅ LOG: Support behavior tracking
			static int32 SupportLogCounter = 0;
			if (++SupportLogCounter % 50 == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[SUPPORT REWARD] Agent=%s | AllyHP=%.1f%% | AllyDist=%.2f | Reward=+%.2f"),
					*GetOwner()->GetName(),
					AllyHP * 100.0f,
					AllyDist,
					ProximityReward
				);
			}
		}
	}
	// [2] Generic formation bonus for all other strategies
	else if (ProtectedAlly && CurrentObs.AllyDistance > 0.0f)
	{
		float normalizedDist = CurrentObs.AllyDistance;
		constexpr float MIN_FORMATION_DIST = 200.0f / RLConfig::MAX_DISTANCE_NORMALIZATION;  // ~0.04
		constexpr float MAX_FORMATION_DIST = 800.0f / RLConfig::MAX_DISTANCE_NORMALIZATION;  // ~0.16

		if (normalizedDist > MIN_FORMATION_DIST && normalizedDist < MAX_FORMATION_DIST)
		{
			Reward += RewardConfig::FORMATION_BONUS;  // +0.1 per step in formation
		}
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