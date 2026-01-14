// Copyright Epic Games, Inc. All Rights Reserved.

#include "RL/RewardCalculator.h"
#include "Team/FollowerAgentComponent.h"
#include "Team/TeamLeaderComponent.h"
#include "Combat/HealthComponent.h"
#include "Team/Mission.h"
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
			// Initialize previous observation (v6.0 fix)
			PreviousObservation = FollowerComponent->BuildLocalObservation();
			UE_LOG(LogTemp, Log, TEXT("[REWARD INIT] '%s': PreviousObservation initialized"), *Owner->GetName());
		}
	}

	// Reset state
	AccumulatedIndividualReward = 0.0f;
	AccumulatedCoordinationReward = 0.0f;
	AccumulatedMissionReward = 0.0f;
	LastMissionProgress = 0.0f;
}

void URewardCalculator::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// CRITICAL FIX (v6.0): Hybrid reward calculation (continuous + event-driven)
	// This ensures ALL rewards (kills, damage, positioning, progress) are forwarded to FollowerComponent

	if (!FollowerComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[REWARD TICK] No FollowerComponent - cannot forward rewards!"));
		return;
	}

	// Build current observation
	FObservationElement CurrentObs = FollowerComponent->BuildLocalObservation();

	// Add Mission context to observation
	if (CurrentMission)
	{
		CurrentObs.MissionContext = FollowerComponent->BuildMissionContext(CurrentMission);
	}

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
	float eventRewards = AccumulatedIndividualReward + AccumulatedCoordinationReward + AccumulatedMissionReward;
	StepReward += eventRewards;

	// Forward total reward to FollowerComponent
	if (FMath::Abs(StepReward) > 0.01f) // Avoid spam for tiny rewards
	{
		FollowerComponent->ProvideReward(StepReward);

		// v8.0: Log with component breakdown
		UE_LOG(LogTemp, Display,
			TEXT("[REWARD TICK v8.0] '%s' (%s): Total=%.2f | %s | Events=%.2f"),
			*GetOwner()->GetName(),
			*UEnum::GetValueAsString(CurrentStrategy),
			StepReward + eventRewards,
			*Breakdown.ToString(),
			eventRewards);
	}

	// Reset event accumulators (events are one-time, continuous rewards recalculate every tick)
	AccumulatedIndividualReward = 0.0f;
	AccumulatedCoordinationReward = 0.0f;
	AccumulatedMissionReward = 0.0f;
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

	// Add accumulated global events (Mission completion)
	float TotalReward = Breakdown.Total + AccumulatedMissionReward;

	// Reset accumulators
	AccumulatedIndividualReward = 0.0f;
	AccumulatedMissionReward = 0.0f;
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

	// Component 1: Objective Progress
	float objComponent = CalculateObjectiveProgressComponent(PrevObs, CurrentObs);
	Breakdown.ObjectiveProgress = objComponent * Weights.ObjectiveProgress;

	// Component 2: Combat Effectiveness
	float combatComponent = CalculateCombatEffectivenessComponent(CurrentObs);
	Breakdown.CombatEffectiveness = combatComponent * Weights.CombatEffectiveness;

	// Component 3: Survival
	float survivalComponent = CalculateSurvivalComponent(CurrentObs);
	Breakdown.Survival = survivalComponent * Weights.Survival;

	// Component 4: Cover Usage
	float coverComponent = CalculateCoverUsageComponent(CurrentObs);
	Breakdown.CoverUsage = coverComponent * Weights.CoverUsage;

	// Component 5: Team Coordination
	float coordComponent = CalculateTeamCoordinationComponent(CurrentObs);
	Breakdown.TeamCoordination = coordComponent * Weights.TeamCoordination;

	// Total reward
	Breakdown.Total = Breakdown.ObjectiveProgress +
	                  Breakdown.CombatEffectiveness +
	                  Breakdown.Survival +
	                  Breakdown.CoverUsage +
	                  Breakdown.TeamCoordination;

	// Log breakdown for debugging
	UE_LOG(LogTemp, Verbose, TEXT("[REWARD v8.0] %s: %s"),
		*UEnum::GetValueAsString(Strategy),
		*Breakdown.ToString());

	return Breakdown;
}

//--------------------------------------------------------------------------
// v8.0: REWARD COMPONENT IMPLEMENTATIONS (Strategy-Agnostic)
//--------------------------------------------------------------------------

float URewardCalculator::CalculateObjectiveProgressComponent(
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	float Reward = 0.0f;

	// Get objective actor from mission context
	const FMissionContext& CurrentCtx = CurrentObs.MissionContext;
	AObjectiveActor* ObjectiveActor = Cast<AObjectiveActor>(CurrentCtx.TargetActor);

	if (!ObjectiveActor || !GetOwner())
	{
		return 0.0f;
	}

	// Check if agent is in objective volume
	bool bIsInVolume = ObjectiveActor->IsAgentInVolume(GetOwner());

	// Volume retention reward (continuous while inside)
	if (bIsInVolume)
	{
		Reward += RewardConfig::OBJECTIVE_VOLUME_REWARD;  // +0.1 per step
	}

	float currentDistance = FVector::Dist(CurrentObs.Position, ObjectiveActor->GetActorLocation());
	float prevDistance = FVector::Dist(PrevObs.Position, ObjectiveActor->GetActorLocation());
	if ((currentDistance < prevDistance) && !bIsInVolume)
	{
		Reward += RewardConfig::OBJECTIVE_ADVANCE_REWARD;  // +0.1 per step when advancing
	}

	return Reward;
}

float URewardCalculator::CalculateCombatEffectivenessComponent(
	const FObservationElement& CurrentObs)
{
	float Reward = 0.0f;

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
	
	// Formation bonus (optimal support range: 200cm to 800cm)
	if (ProtectedAlly && CurrentObs.AllyDistance > 0.0f)
	{
		float distance = CurrentObs.AllyDistance;

		if (distance > 200.0f && distance < 800.0f)
		{
			Reward += RewardConfig::FORMATION_BONUS;  // +0.1 per step in formation
		}
	}

	return Reward;
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

	// Bonus for kill while on Mission
	if (IsOnMission())
	{
		AccumulatedCoordinationReward += 15.0f; // +15 for Mission kill
		UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT v8.0] '%s': Kill on Mission (LowestHP=%s) → +15.0 coordination bonus"),
			*GetOwner()->GetName(),
			bWasLowestHP ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT v8.0] '%s': Kill (LowestHP=%s) → +%.1f in next tick"),
			*GetOwner()->GetName(),
			bWasLowestHP ? TEXT("true") : TEXT("false"),
			bWasLowestHP ? RewardConfig::KILL_REWARD_EFFICIENT : RewardConfig::KILL_REWARD_BASE);
	}
}

void URewardCalculator::OnDealDamage(float Damage, AActor* Target)
{
	DamageSinceLastUpdate += Damage;

	// Register for combined fire tracking
	RegisterCombinedFire(Target);

	UE_LOG(LogTemp, Verbose, TEXT("[REWARD EVENT] '%s': Dealt %.1f damage (accumulated: %.1f)"),
		*GetOwner()->GetName(), Damage, DamageSinceLastUpdate);
}

void URewardCalculator::OnTakeDamage(float Damage)
{
	DamageTakenSinceLastUpdate += Damage;

	UE_LOG(LogTemp, Verbose, TEXT("[REWARD EVENT] '%s': Took %.1f damage (accumulated: %.1f)"),
		*GetOwner()->GetName(), Damage, DamageTakenSinceLastUpdate);
}

void URewardCalculator::OnDeath()
{
	// v6.0: Unified death penalty (CRITICAL: Must be < Mission rewards)
	// This ensures agents will sacrifice themselves to complete Missions
	AccumulatedIndividualReward += RewardConfig::DEATH_PENALTY;

	UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Death penalty %.1f (acceptable if Mission achieved)"),
		*GetOwner()->GetName(), RewardConfig::DEATH_PENALTY);
}


void URewardCalculator::SetCurrentMission(UMission* Mission)
{
	if (CurrentMission != Mission)
	{
		CurrentMission = Mission;
		LastMissionProgress = Mission ? Mission->GetProgress() : 0.0f;
		UE_LOG(LogTemp, Log, TEXT("[REWARD] Mission updated: %s"),
			Mission ? *UEnum::GetValueAsString(Mission->Type) : TEXT("None"));
	}
}

void URewardCalculator::SetCurrentStrategy(EStrategyType Strategy)
{
	if (CurrentStrategy != Strategy)
	{
		EStrategyType PreviousStrategy = CurrentStrategy;
		CurrentStrategy = Strategy;

		UE_LOG(LogTemp, Log, TEXT("[REWARD] Strategy updated: %s → %s (reward weights may change)"),
			*UEnum::GetValueAsString(PreviousStrategy),
			*UEnum::GetValueAsString(Strategy));
	}
}

//--------------------------------------------------------------------------
// COORDINATION TRACKING
//--------------------------------------------------------------------------

bool URewardCalculator::IsOnMission() const
{
	if (!CurrentMission || !CurrentMission->IsActive())
	{
		return false;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	// Check distance to Mission location
	FVector OwnerLocation = Owner->GetActorLocation();
	FVector MissionLocation = CurrentMission->TargetLocation;
	float Distance = FVector::Dist(OwnerLocation, MissionLocation);

	return Distance <= MissionRadiusThreshold;
}

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
		AccumulatedCoordinationReward += 10.0f; // +10 for combined fire
		UE_LOG(LogTemp, Log, TEXT("[REWARD] Combined fire on %s: +10"), *Target->GetName());
	}

	// Add this record
	FCombinedFireRecord NewRecord;
	NewRecord.Target = Target;
	NewRecord.Timestamp = CurrentTime;
	RecentCombinedFires.Add(NewRecord);
}