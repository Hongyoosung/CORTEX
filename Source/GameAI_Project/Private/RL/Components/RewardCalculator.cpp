// Copyright Epic Games, Inc. All Rights Reserved.

#include "RL/Components/RewardCalculator.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Combat/Components/HealthComponent.h"
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

	// Find component references
	AActor* Owner = GetOwner();
	if (Owner)
	{
		FollowerComponent = Owner->FindComponentByClass<UFollowerAgentComponent>();
		HealthComponent = Owner->FindComponentByClass<UHealthComponent>();

		if (!FollowerComponent)
		{
			UE_LOG(LogTemp, Warning, TEXT("RewardCalculator: No FollowerAgentComponent found on %s"), *Owner->GetName());
		}
		else
		{
			// Initialize previous observation for delta calculations
			PreviousObservation = FollowerComponent->BuildLocalObservation();
			UE_LOG(LogTemp, Log, TEXT("[REWARD INIT] '%s': PreviousObservation initialized"), *Owner->GetName());
		}
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
	// All rewards forwarded to FollowerComponent for Schola integration

	if (!FollowerComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[REWARD TICK] No FollowerComponent - cannot forward rewards!"));
		return;
	}

	// Skip reward calculation for dead agents to prevent negative impact on RL training
	if (!FollowerComponent->GetIsAlive())
	{
		return;
	}

	// Build current observation
	FObservationElement CurrentObs = FollowerComponent->BuildLocalObservation();

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

	// Forward total reward to FollowerComponent
	if (FMath::Abs(StepReward) > 0.01f) // Avoid spam for tiny rewards
	{
		FollowerComponent->ProvideReward(StepReward);

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
// v8.0: UNIFIED REWARD CALCULATION
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

	// v8.0: Total reward from component breakdown
	float TotalReward = Breakdown.Total;

	// Reset accumulators
	AccumulatedIndividualReward = 0.0f;
	KillsSinceLastUpdate = 0;
	DamageSinceLastUpdate = 0.0f;
	DamageTakenSinceLastUpdate = 0.0f;

	return TotalReward;
}

//--------------------------------------------------------------------------
// v8.0: UNIFIED REWARD SYSTEM - Component-Based Calculation
//--------------------------------------------------------------------------

FRewardComponentBreakdown URewardCalculator::CalculateUnifiedReward(
	EStrategyType Strategy,
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	FRewardComponentBreakdown Breakdown;

	// Get strategy-specific weights
	const RewardConfig::FStrategyWeights& Weights = RewardConfig::GetWeightsForStrategy(Strategy);

	// v9.0: Normalize EACH component BEFORE strategy weighting
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

	// Component 6: v8.0 Tactical Parameter Effectiveness
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

	// v9.0: Soft tanh scaling instead of hard clamp
	// tanh provides smooth gradients at boundaries, better for learning
	// Dividing by 4.0 before tanh keeps typical rewards in linear region
	// Multiplying by 5.0 after tanh gives output range [-5, 5] (softer than v8.10's [-10, 10])
	Breakdown.Total = FMath::Tanh(RawTotal / 4.0f) * 5.0f;

	// Log breakdown for debugging (every 100 steps to reduce spam)
	static int32 LogCounter = 0;
	if (++LogCounter % 100 == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[REWARD v9.0] %s | Agent=%s | Obj=%.2f | Combat=%.2f | Survival=%.2f | Cover=%.2f | Coord=%.2f | Tactical=%.2f | Total=%.2f"),
			*UEnum::GetValueAsString(Strategy),
			*GetOwner()->GetName(),
			Breakdown.ObjectiveProgress,
			Breakdown.CombatEffectiveness,
			Breakdown.Survival,
			Breakdown.CoverUsage,
			Breakdown.TeamCoordination,
			Breakdown.TacticalEffectiveness,
			Breakdown.Total);
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
	if (!FollowerComponent) return 0.0f;

	CurrentStrategy = FollowerComponent->GetAssignedStrategy();

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
	// Assault: Incentivize approaching hostile objective
	float Reward = 0.0f;
	float Dist = CurrentObs.HostileObjectiveDistance;
	float PrevDist = PrevObs.HostileObjectiveDistance;

	// [DEBUG CHECK] 거리가 1.0f(초기값)에서 변하지 않는지 확인
	if (Dist >= 0.99f && PrevDist >= 0.99f)
	{
		// 100틱마다 한 번만 경고 로그 (스팸 방지)
		static int32 WarningCount = 0;
		if (++WarningCount % 100 == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[REWARD ALERT] Agent %s has MAX Hostile Distance (1.0). ObsBuilder update required!"), *GetOwner()->GetName());
		}
	}

	// Reward for getting closer to hostile objective
	float DistanceDelta = PrevObs.HostileObjectiveDistance - CurrentObs.HostileObjectiveDistance;
	if (DistanceDelta > 0.0f)
	{
		// Approaching hostile objective
		Reward += DistanceDelta * 10.0f;  // Scale by progress
	}

	// Extra reward for being very close to hostile objective (<20% distance)
	if (CurrentObs.HostileObjectiveDistance < 0.2f)
	{
		Reward += RewardConfig::OBJECTIVE_VOLUME_REWARD;  // +1.0 per step near objective
	}

	return Reward;
}

float URewardCalculator::CalculateDefendReward(
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	// Defend: Incentivize staying near friendly objective
	float Reward = 0.0f;

	// Reward for being close to friendly objective (defensive perimeter)
	if (CurrentObs.FriendlyObjectiveDistance < 0.3f)
	{
		Reward += RewardConfig::OBJECTIVE_VOLUME_REWARD;  // +1.0 per step in perimeter
	}

	// Penalty for moving away from friendly objective
	float DistanceDelta = CurrentObs.FriendlyObjectiveDistance - PrevObs.FriendlyObjectiveDistance;
	if (DistanceDelta > 0.0f)
	{
		// Moving away from friendly objective (bad for defense)
		Reward -= DistanceDelta * 5.0f;
	}

	return Reward;
}

float URewardCalculator::CalculateSupportReward(
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	// Support: Incentivize proximity to allies (uses existing AllyDistance field)
	float Reward = 0.0f;

	// Reward for being close to ally
	if (CurrentObs.bAllyNeedsHelp && CurrentObs.AllyDistance < 0.2f)
	{
		Reward += RewardConfig::SUPPORT_CRITICAL_BONUS;  // +5.0 for reaching ally in need
	}

	// Reward for approaching ally
	float AllyDistanceDelta = PrevObs.AllyDistance - CurrentObs.AllyDistance;
	if (AllyDistanceDelta > 0.0f && CurrentObs.bAllyNeedsHelp)
	{
		Reward += AllyDistanceDelta * RewardConfig::SUPPORT_PROXIMITY_BONUS;  // Scale by progress
	}

	return Reward;
}

float URewardCalculator::CalculateRetreatReward(
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	// Retreat: Incentivize distance from enemies (uses DistanceToNearestEnemy)
	float Reward = 0.0f;

	// Reward for increasing distance from nearest enemy
	float EnemyDistanceDelta = CurrentObs.DistanceToNearestEnemy - PrevObs.DistanceToNearestEnemy;
	if (EnemyDistanceDelta > 0.0f)
	{
		// Successfully retreating (getting further from enemies)
		Reward += EnemyDistanceDelta * 8.0f;  // High reward for escape
	}

	// Bonus for being far from enemies (>80% distance)
	if (CurrentObs.DistanceToNearestEnemy > 0.8f)
	{
		Reward += 1.0f;  // Safe distance bonus
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
	EStrategyType AssignedStrategy = FollowerComponent ? FollowerComponent->GetAssignedStrategy() : EStrategyType::Assault;

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
	// v8.0: Delegate to priority-aware version (assume not lowest HP if not specified)
	OnKillEnemyWithPriority(Enemy, false);
}

void URewardCalculator::OnKillEnemyWithPriority(AActor* Enemy, bool bWasLowestHP)
{
	KillsSinceLastUpdate++;
	bLastKillWasLowestHP = bWasLowestHP;

	// v8.0: Kill reward calculated in CalculateCombatEffectivenessComponent
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

		UE_LOG(LogTemp, Log, TEXT("[REWARD v9.0] Strategy updated: %s → %s"),
			*UEnum::GetValueAsString(PreviousStrategy),
			*UEnum::GetValueAsString(Strategy));
	}
}

//--------------------------------------------------------------------------
// COORDINATION TRACKING
//--------------------------------------------------------------------------

bool URewardCalculator::IsInFormation() const
{
	if (!FollowerComponent)
	{
		return false;
	}

	// Check if near teammates
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	UTeamLeaderComponent* TeamLeader = FollowerComponent->GetTeamLeader();
	if (!TeamLeader)
	{
		return false;
	}

	// Get team members
	TArray<AActor*> TeamMembers = TeamLeader->GetFollowers();
	if (TeamMembers.Num() <= 1)
	{
		return false; // No teammates
	}

	// Count nearby teammates
	FVector OwnerLocation = Owner->GetActorLocation();
	int32 NearbyCount = 0;

	for (AActor* MemberActor : TeamMembers)
	{
		if (MemberActor == Owner || !MemberActor)
		{
			continue;
		}

		float Distance = FVector::Dist(OwnerLocation, MemberActor->GetActorLocation());
		if (Distance <= FormationDistanceThreshold)
		{
			NearbyCount++;
		}
	}

	// Consider "in formation" if at least 1 teammate nearby
	return NearbyCount >= 1;
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