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
// LEGACY REWARD CALCULATION (Deprecated in v6.0)
//--------------------------------------------------------------------------

float URewardCalculator::CalculateTotalReward(float DeltaTime)
{
	float TotalReward = 0.0f;

	// v5.0: Strategy-specific reward calculation
	switch (CurrentStrategy)
	{
		case EStrategyType::Assault:
		{
			// Assault: Kills, damage, advancement
			TotalReward += KillsSinceLastUpdate * Assault_KillReward;
			TotalReward += DamageSinceLastUpdate * Assault_DamageMultiplier;

			// Reward for advancing toward objective
			if (CurrentObjective && FollowerComponent)
			{
				AActor* Owner = GetOwner();
				if (Owner)
				{
					float CurrentDistance = FVector::Dist(Owner->GetActorLocation(), CurrentObjective->TargetLocation);
					if (LastDistanceToObjective > 0.0f)
					{
						float DistanceReduction = LastDistanceToObjective - CurrentDistance;
						if (DistanceReduction > 0.0f)
						{
							TotalReward += Assault_AdvanceReward * DeltaTime;
						}
					}
					LastDistanceToObjective = CurrentDistance;

					// Bonus for reaching objective
					if (CurrentDistance < ObjectiveRadiusThreshold)
					{
						TotalReward += Assault_ObjectiveReachReward * DeltaTime;
					}
				}
			}
			break;
		}

		case EStrategyType::Defend:
		{
			// Defend: Kills, hold position, suppression
			TotalReward += KillsSinceLastUpdate * Defend_KillReward;

			// Reward for holding position
			bool bOnObjective = IsOnObjective();
			if (bOnObjective)
			{
				TotalReward += Defend_HoldPositionReward * DeltaTime;
			}
			else if (bWasOnObjective)
			{
				// Penalty for leaving position
				TotalReward += Defend_LeavePositionPenalty * DeltaTime;
			}
			bWasOnObjective = bOnObjective;

			// v5.0: Bonus for damaging enemies without killing (area denial)
			if (FollowerComponent && DamageSinceLastUpdate > 0.0f && KillsSinceLastUpdate == 0)
			{
				TotalReward += Defend_AreaDenialReward;
			}
			break;
		}

		case EStrategyType::Support:
		{
			// Support: Protect ally, kill threats, maintain distance
			TotalReward += KillsSinceLastUpdate * Support_KillThreatReward;

			// Track protected ally
			if (CurrentObjective && CurrentObjective->TargetActor)
			{
				ProtectedAlly = CurrentObjective->TargetActor;
				UHealthComponent* AllyHealth = ProtectedAlly->FindComponentByClass<UHealthComponent>();
				if (AllyHealth)
				{
					float CurrentHealth = AllyHealth->GetCurrentHealth() / AllyHealth->GetMaxHealth();

					// Reward for maintaining ally health
					if (CurrentHealth > ProtectedAllyLastHealth)
					{
						TotalReward += Support_AllyProtectedReward;
					}

					// Penalty if ally died
					if (CurrentHealth <= 0.0f && ProtectedAllyLastHealth > 0.0f)
					{
						TotalReward += Support_AllyDeathPenalty;
					}

					ProtectedAllyLastHealth = CurrentHealth;

					// Reward for maintaining optimal support distance (5-15m)
					AActor* Owner = GetOwner();
					if (Owner)
					{
						float DistToAlly = FVector::Dist(Owner->GetActorLocation(), ProtectedAlly->GetActorLocation());
						if (DistToAlly >= Support_MinDistance && DistToAlly <= Support_MaxDistance)
						{
							TotalReward += Support_MaintainDistanceReward * DeltaTime;
						}
					}
				}
			}

			// Bonus for drawing enemy fire (taking damage while protecting)
			if (DamageTakenSinceLastUpdate > 0.0f && ProtectedAlly)
			{
				TotalReward += Support_DrawFireReward;
			}
			break;
		}

		case EStrategyType::Retreat:
		{
			// Retreat: Increase distance, reach safety, cover fire
			if (FollowerComponent)
			{
				FObservationElement Obs = FollowerComponent->GetLocalObservation();
				float CurrentDistToEnemy = Obs.DistanceToNearestEnemy;

				// Reward for increasing distance from enemies
				if (LastDistanceToEnemy > 0.0f)
				{
					float DistanceIncrease = CurrentDistToEnemy - LastDistanceToEnemy;
					if (DistanceIncrease > 0.0f)
					{
						TotalReward += Retreat_DistanceIncreaseReward * DeltaTime;
					}
				}
				LastDistanceToEnemy = CurrentDistToEnemy;

				// Reward for reaching safe distance
				bool bInSafeZone = (CurrentDistToEnemy > Retreat_SafeDistance);
				if (bInSafeZone && !bWasInSafeZone)
				{
					TotalReward += Retreat_SafeZoneReward;
				}
				bWasInSafeZone = bInSafeZone;

				// Bonus for covering fire while retreating (suppression)
				if (DamageSinceLastUpdate > 0.0f && KillsSinceLastUpdate == 0)
				{
					TotalReward += Retreat_CoveringFireReward;
				}
			}
			break;
		}

		default:
			// Fallback to legacy reward calculation
			TotalReward += CalculateIndividualReward() * IndividualRewardWeight;
			TotalReward += CalculateCoordinationReward() * CoordinationRewardWeight;
			TotalReward += CalculateObjectiveReward() * ObjectiveRewardWeight;
			TotalReward += CalculateEfficiencyPenalty(DeltaTime);
			TotalReward += CalculateCoverReward();
			break;
	}

	// Add accumulated rewards (global events like objective completion)
	TotalReward += AccumulatedIndividualReward;
	TotalReward += AccumulatedCoordinationReward;
	TotalReward += AccumulatedObjectiveReward;

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
		// v5.0: Firing is indicated by TargetIndex >= 0
		bool bFiredThisTick = (FollowerComponent->LastTacticalAction.MacroAction.TargetIndex >= 0);

		if (bFiredThisTick && Obs.VisibleEnemyCount == 0)
		{
			Reward -= 0.5f; // -0.5 per wasted shot (reduced to allow exploration)
		}

		// Exploration bonus: small reward for taking ANY action other than default Hold
		// Encourages agents to explore movement and tactical positions during early training
		FMacroAction CurrentAction = FollowerComponent->LastTacticalAction.MacroAction;
		// v5.0: Action taken if moving OR firing (TargetIndex >= 0)
		bool bTookAction = (CurrentAction.PositionChoice != ETacticalPosition::Hold) ||
		                   (CurrentAction.TargetIndex >= 0);

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

	// v8.0: Objective disobedience penalty REMOVED
	// Action masking in sbdapm_env.py prevents invalid actions at source
	// Agents can no longer choose actions that conflict with strategic objectives

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

		// Continuous proximity reward - encourages both teams to fight near objectives
		AActor* Owner = GetOwner();
		if (Owner)
		{
			float CurrentDistance = FVector::Dist(Owner->GetActorLocation(), CurrentObjective->TargetLocation);

			// Reward for being close to objective (scales with proximity)
			if (CurrentDistance < ObjectiveRadiusThreshold) // Within 10m (default 1000cm)
			{
				float ProximityRatio = 1.0f - (CurrentDistance / ObjectiveRadiusThreshold);
				Reward += ProximityRatio * 2.0f; // +2.0 at objective center, 0 at threshold
			}
		}
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

	// v4.0: Simplified objective types (Assault, Defend, Support, Retreat)
	switch (CurrentObjective->Type)
	{
		case EObjectiveType::Assault:
		{
			// Disobey if moving AWAY from target (enemy or objective)
			FVector TargetLocation = CurrentObjective->TargetActor ?
				CurrentObjective->TargetActor->GetActorLocation() :
				CurrentObjective->TargetLocation;

			FVector ToTarget = TargetLocation - AgentLocation;
			FVector Velocity = Owner->GetVelocity();

			if (Velocity.SizeSquared() > 100.0f) // Agent is moving (threshold: 10 cm/s)
			{
				float Alignment = FVector::DotProduct(Velocity.GetSafeNormal(), ToTarget.GetSafeNormal());
				if (Alignment < -0.5f) // Moving away (dot product < -0.5 = > 120 degrees)
				{
					bDisobeyedObjective = true;
				}
			}
			break;
		}

		case EObjectiveType::Defend:
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

		case EObjectiveType::Support:
		{
			// Disobey if NOT moving toward ally or NOT providing cover - v4.0 macro actions
			if (CurrentObjective->TargetActor)
			{
				float DistToAlly = FVector::Dist(AgentLocation, CurrentObjective->TargetActor->GetActorLocation());
				// v5.0: Providing cover is indicated by TargetIndex >= 0
				bool bProvidingCover = (FollowerComponent->LastTacticalAction.MacroAction.TargetIndex >= 0);
				if (DistToAlly > 3000.0f && !bProvidingCover)
				{
					// Too far from ally (>30m) and not providing covering fire
					bDisobeyedObjective = true;
				}
			}
			break;
		}

		case EObjectiveType::None:
		{
			// No objective, can't disobey
			break;
		}

		default:
			break;
	}
}
