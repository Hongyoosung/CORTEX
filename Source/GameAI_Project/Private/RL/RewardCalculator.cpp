// Copyright Epic Games, Inc. All Rights Reserved.

#include "RL/RewardCalculator.h"
#include "Team/FollowerAgentComponent.h"
#include "Team/TeamLeaderComponent.h"
#include "Combat/HealthComponent.h"
#include "Team/Objective.h"
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
	AccumulatedObjectiveReward = 0.0f;
	LastObjectiveProgress = 0.0f;
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

	// Add objective context to observation
	if (CurrentObjective)
	{
		CurrentObs.ObjectiveContext = FollowerComponent->BuildObjectiveContext(CurrentObjective);
	}

	float StepReward = 0.0f;

	// === CONTINUOUS REWARDS (positioning, progress, strategy bonuses) ===
	if (CurrentObjective && CurrentObjective->IsActive())
	{
		// Objective progress rewards (moving closer, holding position, etc.)
		StepReward += CalculateObjectiveProgressReward(
			CurrentObs.ObjectiveContext.Type,
			PreviousObservation,
			CurrentObs
		);

		// Alignment bonus (strategy matches objective)
		StepReward += CalculateAlignmentBonus(CurrentStrategy, CurrentObs.ObjectiveContext.Type);
	}

	// Strategy-specific continuous rewards (position holding, health recovery, etc.)
	float strategyReward = CalculateStrategyReward(CurrentStrategy, PreviousObservation, CurrentObs);
	StepReward += strategyReward;

	// === EVENT-BASED REWARDS (kills, damage, death accumulated since last tick) ===
	float eventRewards = AccumulatedIndividualReward + AccumulatedCoordinationReward + AccumulatedObjectiveReward;
	StepReward += eventRewards;

	// Forward total reward to FollowerComponent (CRITICAL FIX)
	if (FMath::Abs(StepReward) > 0.01f) // Avoid spam for tiny rewards
	{
		FollowerComponent->ProvideReward(StepReward);

		UE_LOG(LogTemp, Display, TEXT("[REWARD TICK] '%s': Total=%.2f (Events=%.2f, Continuous=%.2f, Strategy=%.2f)"),
			*GetOwner()->GetName(),
			StepReward,
			eventRewards,
			StepReward - eventRewards - strategyReward,
			strategyReward);
	}

	// Reset event accumulators (events are one-time, continuous rewards recalculate every tick)
	AccumulatedIndividualReward = 0.0f;
	AccumulatedCoordinationReward = 0.0f;
	AccumulatedObjectiveReward = 0.0f;
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
// v6.0: OBJECTIVE-AWARE REWARD CALCULATION
//--------------------------------------------------------------------------

float URewardCalculator::CalculateReward(
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs,
	const FMacroAction& Action)
{
	float Reward = 0.0f;

	EStrategyType Strategy = Action.Strategy;
	EObjectiveType Objective = CurrentObs.ObjectiveContext.Type;

	// Strategy-specific rewards (base combat rewards)
	Reward += CalculateStrategyReward(Strategy, PrevObs, CurrentObs);

	// Objective-aware modifiers (CRITICAL for MCTS-RL alignment)
	Reward += CalculateObjectiveProgressReward(Objective, PrevObs, CurrentObs);

	// Alignment bonus: Strategy matches objective
	Reward += CalculateAlignmentBonus(Strategy, Objective);

	// Add accumulated global events (objective completion, death)
	Reward += AccumulatedIndividualReward;
	Reward += AccumulatedObjectiveReward;

	// Reset accumulators
	AccumulatedIndividualReward = 0.0f;
	AccumulatedObjectiveReward = 0.0f;
	KillsSinceLastUpdate = 0;
	DamageSinceLastUpdate = 0.0f;
	DamageTakenSinceLastUpdate = 0.0f;

	return Reward;
}

float URewardCalculator::CalculateStrategyReward(
	EStrategyType Strategy,
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	float Reward = 0.0f;

	// Base combat rewards (applies to all strategies)
	Reward += KillsSinceLastUpdate * RewardConfig::KILL_REWARD;
	Reward += DamageSinceLastUpdate * 0.05f; // +5 per 100 damage

	// Strategy-specific modifiers
	switch (Strategy)
	{
		case EStrategyType::Assault:
			// Aggressive combat bonus
			if (KillsSinceLastUpdate > 0)
			{
				Reward += 5.0f; // Extra bonus for kills during assault
			}
			break;

		case EStrategyType::Defend:
			// Holding position bonus
			if (IsOnObjective())
			{
				Reward += 2.0f; // Reward for maintaining defensive position
			}
			break;

		case EStrategyType::Support:
			// Ally protection bonus
			if (DamageTakenSinceLastUpdate > 0.0f)
			{
				Reward += 3.0f; // Reward for drawing fire (protecting allies)
			}
			break;

		case EStrategyType::Retreat:
			// Survival bonus
			if (CurrentObs.AgentHealth > PrevObs.AgentHealth)
			{
				Reward += 5.0f; // Reward for recovering health
			}
			break;

		default:
			break;
	}

	return Reward;
}

float URewardCalculator::CalculateObjectiveProgressReward(
	EObjectiveType Objective,
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	float Reward = 0.0f;

	// Get objective context from observations
	const FObjectiveContext& PrevCtx = PrevObs.ObjectiveContext;
	const FObjectiveContext& CurrentCtx = CurrentObs.ObjectiveContext;

	float DistancePrev = PrevCtx.Distance;
	float DistanceCurrent = CurrentCtx.Distance;
	float DistanceDelta = DistancePrev - DistanceCurrent; // Positive = moving closer

	switch (Objective)
	{
		case EObjectiveType::Capture:
		{
			// Reward for getting closer to objective
			if (DistanceDelta > 0.0f)
			{
				Reward += DistanceDelta * RewardConfig::PROGRESS_PER_METER;
			}

			// Bonus for reaching objective
			if (DistanceCurrent < 0.2f) // Within normalized threshold
			{
				Reward += 10.0f;
			}
			break;
		}

		case EObjectiveType::Defend:
		{
			// Reward for staying near objective
			if (DistanceCurrent < 0.2f) // Within 10m (normalized)
			{
				Reward += 3.0f; // Continuous holding bonus
			}
			else
			{
				// Small penalty for leaving position
				Reward -= 1.0f;
			}
			break;
		}

		case EObjectiveType::Support:
		{
			// Reward for being near protected ally
			if (DistanceCurrent < 0.3f) // Within support range
			{
				Reward += 2.0f;
			}

			// Check if protected ally health improved
			if (CurrentCtx.TargetActor && PrevCtx.TargetActor)
			{
				UHealthComponent* AllyHealth = CurrentCtx.TargetActor->FindComponentByClass<UHealthComponent>();
				if (AllyHealth)
				{
					float currentAllyHealth = AllyHealth->GetCurrentHealth() / AllyHealth->GetMaxHealth();
					// If ally survived and maintained health, bonus
					if (currentAllyHealth >= ProtectedAllyLastHealth && currentAllyHealth > 0.0f)
					{
						Reward += 5.0f;
					}
					ProtectedAllyLastHealth = currentAllyHealth;
				}
			}
			break;
		}

		case EObjectiveType::Retreat:
		{
			// Reward for increasing distance from danger
			if (DistanceDelta < 0.0f) // Moving away (distance increasing)
			{
				Reward += -DistanceDelta * RewardConfig::PROGRESS_PER_METER;
			}

			// Bonus for reaching safe distance
			if (DistanceCurrent > 0.8f) // Far from danger
			{
				Reward += 10.0f;
			}
			break;
		}

		case EObjectiveType::None:
		default:
			// No objective-specific rewards
			break;
	}

	return Reward;
}

float URewardCalculator::CalculateAlignmentBonus(
	EStrategyType Strategy,
	EObjectiveType Objective)
{
	// Bonus for strategy matching objective intent
	if ((Strategy == EStrategyType::Assault && Objective == EObjectiveType::Capture) ||
		(Strategy == EStrategyType::Defend && Objective == EObjectiveType::Defend) ||
		(Strategy == EStrategyType::Support && Objective == EObjectiveType::Support) ||
		(Strategy == EStrategyType::Retreat && Objective == EObjectiveType::Retreat))
	{
		return 1.0f; // Small bonus for perfect alignment
	}

	return 0.0f;
}


//--------------------------------------------------------------------------
// EVENT TRACKING
//--------------------------------------------------------------------------

void URewardCalculator::OnKillEnemy(AActor* Enemy)
{
	KillsSinceLastUpdate++;

	// Base kill reward (will be added in CalculateStrategyReward during tick)
	float killReward = RewardConfig::KILL_REWARD; // +15.0

	// Bonus for kill while on objective
	if (IsOnObjective())
	{
		AccumulatedCoordinationReward += 15.0f; // +15 for objective kill
		UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Kill on objective → +15.0 coordination bonus (base +15.0 kill)"),
			*GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Kill → base +15.0 (will be added in tick)"),
			*GetOwner()->GetName());
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
	// v6.0: Unified death penalty (CRITICAL: Must be < objective rewards)
	// This ensures agents will sacrifice themselves to complete objectives
	AccumulatedIndividualReward += RewardConfig::DEATH_PENALTY;

	UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Death penalty %.1f (acceptable if objective achieved)"),
		*GetOwner()->GetName(), RewardConfig::DEATH_PENALTY);
}

void URewardCalculator::OnObjectiveComplete(UObjective* Objective)
{
	if (!Objective)
	{
		return;
	}

	// v6.0: Objective-specific completion rewards
	float CompletionReward = 0.0f;

	switch (Objective->Type)
	{
		case EObjectiveType::Capture:
			CompletionReward = RewardConfig::OBJECTIVE_CAPTURE_REWARD;
			break;
		case EObjectiveType::Defend:
			CompletionReward = RewardConfig::OBJECTIVE_DEFEND_REWARD;
			break;
		case EObjectiveType::Support:
			CompletionReward = RewardConfig::OBJECTIVE_SUPPORT_REWARD;
			break;
		case EObjectiveType::Retreat:
			CompletionReward = RewardConfig::OBJECTIVE_RETREAT_REWARD;
			break;
		default:
			CompletionReward = 50.0f; // Legacy default
			break;
	}

	AccumulatedObjectiveReward += CompletionReward;
	UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Objective complete (%s) → +%.1f"),
		*GetOwner()->GetName(), *UEnum::GetValueAsString(Objective->Type), CompletionReward);
}

void URewardCalculator::OnObjectiveFailed(UObjective* Objective)
{
	// v6.0: Objective failure penalty (but still less than completion reward)
	AccumulatedObjectiveReward -= 30.0f;

	if (Objective)
	{
		UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Objective failed (%s) → -30.0"),
			*GetOwner()->GetName(), *UEnum::GetValueAsString(Objective->Type));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Objective failed → -30.0"),
			*GetOwner()->GetName());
	}
}

void URewardCalculator::SetCurrentObjective(UObjective* Objective)
{
	if (CurrentObjective != Objective)
	{
		CurrentObjective = Objective;
		LastObjectiveProgress = Objective ? Objective->GetProgress() : 0.0f;
		UE_LOG(LogTemp, Log, TEXT("[REWARD] Objective updated: %s"),
			Objective ? *UEnum::GetValueAsString(Objective->Type) : TEXT("None"));
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

bool URewardCalculator::IsOnObjective() const
{
	if (!CurrentObjective || !CurrentObjective->IsActive())
	{
		return false;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	// Check distance to objective location
	FVector OwnerLocation = Owner->GetActorLocation();
	FVector ObjectiveLocation = CurrentObjective->TargetLocation;
	float Distance = FVector::Dist(OwnerLocation, ObjectiveLocation);

	return Distance <= ObjectiveRadiusThreshold;
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