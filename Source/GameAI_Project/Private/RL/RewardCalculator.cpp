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

	// Check objective compliance (MCTS-RL alignment)
	CheckObjectiveCompliance();
}

//--------------------------------------------------------------------------
// CORE REWARD CALCULATION
//--------------------------------------------------------------------------

float URewardCalculator::CalculateTotalReward(float DeltaTime)
{
	float TotalReward = 0.0f;

	// 1. Individual rewards
	float IndividualReward = CalculateIndividualReward();
	TotalReward += IndividualReward * IndividualRewardWeight;

	// 2. Coordination rewards
	float CoordinationReward = CalculateCoordinationReward();
	TotalReward += CoordinationReward * CoordinationRewardWeight;

	// 3. Objective rewards
	float ObjectiveReward = CalculateObjectiveReward();
	TotalReward += ObjectiveReward * ObjectiveRewardWeight;

	// 4. Efficiency penalties
	float EfficiencyPenalty = CalculateEfficiencyPenalty(DeltaTime);
	TotalReward += EfficiencyPenalty;

	// 5. Cover usage rewards (Sprint 6)
	float CoverReward = CalculateCoverReward();
	TotalReward += CoverReward;

	// Reset accumulators
	AccumulatedIndividualReward = 0.0f;
	AccumulatedCoordinationReward = 0.0f;
	AccumulatedObjectiveReward = 0.0f;
	KillsSinceLastUpdate = 0;
	DamageSinceLastUpdate = 0.0f;
	DamageTakenSinceLastUpdate = 0.0f;

	return TotalReward;
}

float URewardCalculator::CalculateIndividualReward()
{
	float Reward = AccumulatedIndividualReward;

	// Add accumulated event rewards
	Reward += KillsSinceLastUpdate * 10.0f;  // +10 per kill
	Reward += DamageSinceLastUpdate * 0.05f; // +5 per 100 damage
	Reward -= DamageTakenSinceLastUpdate * 0.05f; // -5 per 100 damage taken

	// Penalty for wasted ammo (firing with no visible targets) - v4.0 macro actions
	// REDUCED from -2.0 to -0.5 to allow exploration during early training
	// Too high penalty causes agents to never explore firing actions
	if (FollowerComponent)
	{
		FObservationElement Obs = FollowerComponent->GetLocalObservation();
		bool bFiredThisTick = (FollowerComponent->LastTacticalAction.MacroAction.FireMode != EFireMode::HoldFire);

		if (bFiredThisTick && Obs.VisibleEnemyCount == 0)
		{
			Reward -= 0.5f; // -0.5 per wasted shot (reduced to allow exploration)
		}

		// Exploration bonus: small reward for taking ANY action other than default Hold
		// Encourages agents to explore movement and tactical positions during early training
		FMacroAction CurrentAction = FollowerComponent->LastTacticalAction.MacroAction;
		bool bTookAction = (CurrentAction.PositionChoice != ETacticalPosition::Hold) ||
		                   (CurrentAction.FireMode != EFireMode::HoldFire) ||
		                   (CurrentAction.Stance != EStance::Stand);

		if (bTookAction)
		{
			Reward += 0.1f; // +0.1 for any action (encourages exploration)
		}
	}

	return Reward;
}

float URewardCalculator::CalculateCoordinationReward()
{
	float Reward = AccumulatedCoordinationReward;

	// Combined fire bonus
	int32 CombinedFireCount = RecentCombinedFires.Num();
	if (CombinedFireCount >= 2) // At least 2 agents hitting same target
	{
		Reward += 10.0f; // +10 for coordinated attack
	}

	// Formation maintenance bonus
	if (IsInFormation())
	{
		Reward += 0.5f; // +0.5 per tick in formation (~5/sec at 10Hz)
	}

	// Objective disobedience penalty
	if (bDisobeyedObjective)
	{
		Reward -= 15.0f; // -15 for disobeying objective
		bDisobeyedObjective = false; // Reset flag
	}

	// On-objective bonus for kills (already tracked in OnKillEnemy)
	// This is just the accumulator

	return Reward;
}

float URewardCalculator::CalculateObjectiveReward()
{
	float Reward = AccumulatedObjectiveReward;

	// Track objective progress
	if (CurrentObjective && CurrentObjective->IsActive())
	{
		float CurrentProgress = CurrentObjective->GetProgress();
		float ProgressDelta = CurrentProgress - LastObjectiveProgress;

		if (ProgressDelta > 0.0f)
		{
			// Reward incremental progress
			Reward += ProgressDelta * 10.0f; // +10 per 100% progress
		}

		LastObjectiveProgress = CurrentProgress;
	}

	return Reward;
}

float URewardCalculator::CalculateEfficiencyPenalty(float DeltaTime)
{
	float Penalty = 0.0f;

	// Time inefficiency for objectives with time limits
	if (CurrentObjective && CurrentObjective->TimeLimit > 0.0f)
	{
		float TimeRatio = CurrentObjective->TimeRemaining / CurrentObjective->TimeLimit;
		if (TimeRatio < 0.3f) // Less than 30% time remaining
		{
			Penalty -= 0.05f * DeltaTime; // Small time pressure penalty
		}
	}

	// Health inefficiency (being at low health)
	if (HealthComponent)
	{
		float HealthRatio = HealthComponent->GetCurrentHealth() / HealthComponent->GetMaxHealth();
		if (HealthRatio < 0.3f) // Below 30% health
		{
			Penalty -= 0.02f * DeltaTime; // Small low-health penalty
		}
	}

	return Penalty;
}

float URewardCalculator::CalculateCoverReward()
{
	float Reward = 0.0f;

	if (!FollowerComponent || !HealthComponent)
	{
		return Reward;
	}

	// Get current observation (includes cover info)
	FObservationElement Obs = FollowerComponent->GetLocalObservation();

	// Check if agent is in cover (within threshold distance)
	bool bInCover = Obs.bHasCover && (Obs.NearestCoverDistance < CoverDistanceThreshold);

	// Track under fire status (taking damage recently)
	bool bUnderFire = DamageTakenSinceLastUpdate > 0.0f;

	// Reward for using cover when under fire
	if (bInCover && bUnderFire)
	{
		Reward += CoverUnderFireReward;
	}

	// Penalty for being exposed when enemies are visible
	if (!bInCover && Obs.VisibleEnemyCount > 0 && Obs.DistanceToNearestEnemy < 2000.0f)
	{
		Reward += ExposedPenalty; // Negative value
	}

	// Bonus for crouching/proning in cover (defensive behavior) - v4.0 macro actions
	// Note: We check the action from the last tactical action
	EStance CurrentStance = FollowerComponent->LastTacticalAction.MacroAction.Stance;
	if (bInCover && (CurrentStance == EStance::Crouch || CurrentStance == EStance::Prone))
	{
		Reward += CrouchInCoverReward;
	}

	// v4.0 NOTE: Removed continuous movement-toward-cover reward
	// Discrete position choices (Hold, Forward, Retreat, Flank) don't provide directional vectors
	// Cover-seeking behavior is now implicit in position selection via EQS/NavMesh

	// Update tracking state
	bWasInCover = bInCover;
	bWasUnderFire = bUnderFire;

	return Reward;
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
	AccumulatedIndividualReward -= 10.0f; // -10 for death
	UE_LOG(LogTemp, Warning, TEXT("[REWARD] Death: -10"));
}

void URewardCalculator::OnObjectiveComplete(UObjective* Objective)
{
	AccumulatedObjectiveReward += 50.0f; // +50 for objective completion
	UE_LOG(LogTemp, Warning, TEXT("[REWARD] Objective complete: +50"));
}

void URewardCalculator::OnObjectiveFailed(UObjective* Objective)
{
	AccumulatedObjectiveReward -= 30.0f; // -30 for objective failure
	UE_LOG(LogTemp, Warning, TEXT("[REWARD] Objective failed: -30"));
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

void URewardCalculator::CheckObjectiveCompliance()
{
	if (!CurrentObjective || !FollowerComponent)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	FVector AgentLocation = Owner->GetActorLocation();
	FObservationElement Obs = FollowerComponent->GetLocalObservation();

	switch (CurrentObjective->Type)
	{
		case EObjectiveType::Eliminate:
		{
			// Disobey if moving AWAY from target enemy
			if (CurrentObjective->TargetActor)
			{
				FVector ToTarget = CurrentObjective->TargetActor->GetActorLocation() - AgentLocation;
				FVector Velocity = Owner->GetVelocity();

				if (Velocity.SizeSquared() > 100.0f) // Agent is moving (threshold: 10 cm/s)
				{
					float Alignment = FVector::DotProduct(Velocity.GetSafeNormal(), ToTarget.GetSafeNormal());
					if (Alignment < -0.5f) // Moving away (dot product < -0.5 = > 120 degrees)
					{
						bDisobeyedObjective = true;
					}
				}
			}
			break;
		}

		case EObjectiveType::DefendObjective:
		case EObjectiveType::CaptureObjective:
		{
			// Disobey if leaving objective zone
			float DistToObjective = FVector::Dist(AgentLocation, CurrentObjective->TargetLocation);
			if (DistToObjective > ObjectiveRadiusThreshold * 1.5f) // 50% buffer
			{
				bDisobeyedObjective = true;
			}
			break;
		}

		case EObjectiveType::Retreat:
		{
			// Disobey if moving TOWARD enemies instead of away
			if (Obs.VisibleEnemyCount > 0 && Obs.NearbyEnemies.Num() > 0)
			{
				// Get direction to nearest enemy
				float EnemyYawAngle = Obs.NearbyEnemies[0].RelativeAngle;
				FVector ToEnemy = FRotator(0.0f, EnemyYawAngle, 0.0f).Vector();
				ToEnemy.Normalize();

				FVector Velocity = Owner->GetVelocity();

				if (Velocity.SizeSquared() > 100.0f)
				{
					float Alignment = FVector::DotProduct(Velocity.GetSafeNormal(), ToEnemy);
					if (Alignment > 0.5f) // Moving toward enemies (dot product > 0.5 = < 60 degrees)
					{
						bDisobeyedObjective = true;
					}
				}
			}
			break;
		}

		case EObjectiveType::SupportAlly:
		{
			// Disobey if NOT moving toward ally or NOT providing cover - v4.0 macro actions
			if (CurrentObjective->TargetActor)
			{
				float DistToAlly = FVector::Dist(AgentLocation, CurrentObjective->TargetActor->GetActorLocation());
				bool bProvidingCover = (FollowerComponent->LastTacticalAction.MacroAction.FireMode != EFireMode::HoldFire);
				if (DistToAlly > 3000.0f && !bProvidingCover)
				{
					// Too far from ally (>30m) and not providing covering fire
					bDisobeyedObjective = true;
				}
			}
			break;
		}

		case EObjectiveType::FormationMove:
		{
			// Disobey if breaking formation
			if (!IsInFormation())
			{
				bDisobeyedObjective = true;
			}
			break;
		}

		default:
			break;
	}
}
