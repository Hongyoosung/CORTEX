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

	// === CONTINUOUS REWARDS (positioning, progress, strategy bonuses) ===
	if (CurrentMission && CurrentMission->IsActive())
	{
		// Mission progress rewards (moving closer, holding position, etc.)
		StepReward += CalculateMissionProgressReward(
			CurrentObs.MissionContext.Type,
			PreviousObservation,
			CurrentObs
		);

		// Alignment bonus (strategy matches Mission)
		StepReward += CalculateAlignmentBonus(CurrentStrategy, CurrentObs.MissionContext.Type);
	}

	// Strategy-specific continuous rewards (position holding, health recovery, etc.)
	float strategyReward = CalculateStrategyReward(CurrentStrategy, PreviousObservation, CurrentObs);
	StepReward += strategyReward;

	// === EVENT-BASED REWARDS (kills, damage, death accumulated since last tick) ===
	float eventRewards = AccumulatedIndividualReward + AccumulatedCoordinationReward + AccumulatedMissionReward;
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
// v6.0: Mission-AWARE REWARD CALCULATION
//--------------------------------------------------------------------------

float URewardCalculator::CalculateReward(
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs,
	const FMacroAction& Action)
{
	// v8.0 NOTE: FMacroAction no longer contains Strategy field
	// Strategy is assigned by MCTS via Mission type, RL outputs tactical parameters
	// This function may be DEPRECATED in v8.0 - rewards are calculated per-component via CalculateStrategyReward()

	float Reward = 0.0f;

	EMissionType Mission = CurrentObs.MissionContext.Type;

	// Mission-aware modifiers (CRITICAL for MCTS-RL alignment)
	Reward += CalculateMissionProgressReward(Mission, PrevObs, CurrentObs);

	// Add accumulated global events (Mission completion, death)
	Reward += AccumulatedIndividualReward;
	Reward += AccumulatedMissionReward;

	// Reset accumulators
	AccumulatedIndividualReward = 0.0f;
	AccumulatedMissionReward = 0.0f;
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
			if (IsOnMission())
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

float URewardCalculator::CalculateMissionProgressReward(
	EMissionType Mission,
	const FObservationElement& PrevObs,
	const FObservationElement& CurrentObs)
{
	float Reward = 0.0f;

	// v7.0: Volume-based rewards for durability system
	// Remove distance-based logic, use capture volume presence

	// Get Mission context from observations
	const FMissionContext& CurrentCtx = CurrentObs.MissionContext;

	// Check if we have a valid objective actor
	AObjectiveActor* ObjectiveActor = Cast<AObjectiveActor>(CurrentCtx.TargetActor);
	if (!ObjectiveActor)
	{
		// v7.0 INTEGRATION INCOMPLETE: TargetActor should be AObjectiveActor, but legacy missions pass nullptr
		// TODO: Complete v7.0 integration - update mission creation to use ObjectiveManager
		UE_LOG(LogTemp, Warning, TEXT("[REWARD] No ObjectiveActor found in mission context - using fallback reward (v7.0 integration incomplete)"));
		return 0.0f; // No objective-based rewards until v7.0 integration complete
	}

	// v7.0: Volume-based rewards for ObjectiveActor
	AActor* OwnerAgent = GetOwner();
	if (!OwnerAgent)
	{
		return 0.0f;
	}

	// Check if agent is in capture volume
	bool bIsInVolume = ObjectiveActor->IsAgentInVolume(OwnerAgent);

	// Determine if this is friendly or hostile objective
	UFollowerAgentComponent* Follower = OwnerAgent->FindComponentByClass<UFollowerAgentComponent>();
	if (!Follower)
	{
		return 0.0f;
	}

	bool bIsFriendlyObjective = ObjectiveActor->IsFriendlyTo(Follower->GetTeamID());

	if (bIsFriendlyObjective)
	{
		// === DEFENSE ROLE ===

		// Defense Volume Retention Reward: Continuous reward while inside friendly volume
		if (bIsInVolume)
		{
			Reward += 0.05f; // +0.05 per step (~0.5/sec at 10Hz)
		}

		// Defense Kill Reward: Bonus for killing enemy within friendly volume
		if (KillsSinceLastUpdate > 0 && bIsInVolume)
		{
			Reward += 5.0f; // +2.0 to +5.0 bonus per spec
		}
	}
	else
	{
		// === ASSAULT ROLE ===

		// Assault Volume Retention Reward: Continuous reward while inside enemy volume
		if (bIsInVolume)
		{
			Reward += 0.1f; // +0.1 per step (~1.0/sec at 10Hz)
		}

		// Base Destruction Reward: Large reward for defeating enemy objective
		if (ObjectiveActor->IsDefeated())
		{
			Reward += 100.0f; // +50.0 to +100.0 per spec
			UE_LOG(LogTemp, Warning, TEXT("[REWARD] '%s': ENEMY BASE DESTROYED → +100.0"),
				*OwnerAgent->GetName());
		}
	}

	return Reward;
}

float URewardCalculator::CalculateAlignmentBonus(
	EStrategyType Strategy,
	EMissionType Mission)
{
	// v7.0: Removed Capture type - now Assault/Defend Missions determined by team ownership
	// Bonus for strategy matching Mission intent
	if ((Strategy == EStrategyType::Assault && Mission == EMissionType::Assault) ||
		(Strategy == EStrategyType::Defend && Mission == EMissionType::Defend) ||
		(Strategy == EStrategyType::Support && Mission == EMissionType::Support) ||
		(Strategy == EStrategyType::Retreat && Mission == EMissionType::Retreat))
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

	// Bonus for kill while on Mission
	if (IsOnMission())
	{
		AccumulatedCoordinationReward += 15.0f; // +15 for Mission kill
		UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Kill on Mission → +15.0 coordination bonus (base +15.0 kill)"),
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
	// v6.0: Unified death penalty (CRITICAL: Must be < Mission rewards)
	// This ensures agents will sacrifice themselves to complete Missions
	AccumulatedIndividualReward += RewardConfig::DEATH_PENALTY;

	UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Death penalty %.1f (acceptable if Mission achieved)"),
		*GetOwner()->GetName(), RewardConfig::DEATH_PENALTY);
}

void URewardCalculator::OnMissionComplete(UMission* Mission)
{
	if (!Mission)
	{
		return;
	}

	// v7.0: Mission-specific completion rewards (Capture replaced with Assault)
	float CompletionReward = 0.0f;

	switch (Mission->Type)
	{
		case EMissionType::Assault:
			CompletionReward = RewardConfig::OBJECTIVE_CAPTURE_REWARD; // Reuse capture reward for assault
			break;
		case EMissionType::Defend:
			CompletionReward = RewardConfig::OBJECTIVE_DEFEND_REWARD;
			break;
		case EMissionType::Support:
			CompletionReward = RewardConfig::SUPPORT_REWARD;
			break;
		case EMissionType::Retreat:
			CompletionReward = RewardConfig::RETREAT_REWARD;
			break;
		default:
			CompletionReward = 50.0f; // Legacy default
			break;
	}

	AccumulatedMissionReward += CompletionReward;
	UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Mission complete (%s) → +%.1f"),
		*GetOwner()->GetName(), *UEnum::GetValueAsString(Mission->Type), CompletionReward);
}

void URewardCalculator::OnMissionFailed(UMission* Mission)
{
	// v6.0: Mission failure penalty (but still less than completion reward)
	AccumulatedMissionReward -= 30.0f;

	if (Mission)
	{
		UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Mission failed (%s) → -30.0"),
			*GetOwner()->GetName(), *UEnum::GetValueAsString(Mission->Type));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[REWARD EVENT] '%s': Mission failed → -30.0"),
			*GetOwner()->GetName());
	}
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