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

	// Clean up old combined fire records
	float CurrentTime = GetWorld()->GetTimeSeconds();
	RecentCombinedFires.RemoveAll([CurrentTime, this](const FCombinedFireRecord& Record) {
		return (CurrentTime - Record.Timestamp) > CombinedFireWindow;
	});

	// v8.0: Objective compliance checking DISABLED
	// Action masking in Python environment now prevents invalid actions
	// CheckObjectiveCompliance();
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

	// Bonus for kill while on objective
	if (IsOnObjective())
	{
		AccumulatedCoordinationReward += 15.0f; // +15 for objective kill
		UE_LOG(LogTemp, Log, TEXT("[REWARD] Kill on objective: +15 (total bonus: %.1f)"), AccumulatedCoordinationReward);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[REWARD] Kill: +10"));
	}
}

void URewardCalculator::OnDealDamage(float Damage, AActor* Target)
{
	DamageSinceLastUpdate += Damage;

	// Register for combined fire tracking
	RegisterCombinedFire(Target);
}

void URewardCalculator::OnTakeDamage(float Damage)
{
	DamageTakenSinceLastUpdate += Damage;
}

void URewardCalculator::OnDeath()
{
	// v6.0: Unified death penalty (CRITICAL: Must be < objective rewards)
	// This ensures agents will sacrifice themselves to complete objectives
	AccumulatedIndividualReward += RewardConfig::DEATH_PENALTY;

	UE_LOG(LogTemp, Warning, TEXT("[REWARD v6.0] Death: %.1f (acceptable if objective achieved)"),
		RewardConfig::DEATH_PENALTY);
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
	UE_LOG(LogTemp, Warning, TEXT("[REWARD v6.0] Objective complete (%s): +%.1f"),
		*UEnum::GetValueAsString(Objective->Type), CompletionReward);
}

void URewardCalculator::OnObjectiveFailed(UObjective* Objective)
{
	// v6.0: Objective failure penalty (but still less than completion reward)
	AccumulatedObjectiveReward -= 30.0f;

	if (Objective)
	{
		UE_LOG(LogTemp, Warning, TEXT("[REWARD v6.0] Objective failed (%s): -30"),
			*UEnum::GetValueAsString(Objective->Type));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[REWARD v6.0] Objective failed: -30"));
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