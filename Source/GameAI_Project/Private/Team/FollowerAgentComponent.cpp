#include "Team/FollowerAgentComponent.h"
#include "Team/TeamLeaderComponent.h"
#include "RL/RLPolicyNetwork.h"
#include "RL/RewardCalculator.h"
#include "Perception/AgentPerceptionComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/WeaponComponent.h"
#include "Core/SimulationManagerGameMode.h"
#include "DrawDebugHelpers.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Team/Objective.h"
#include "Schola/ScholaAgentComponent.h"
#include "Simulation/StateTransition.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UFollowerAgentComponent::UFollowerAgentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;  // Update every 100ms
}

void UFollowerAgentComponent::BeginPlay()
{
	Super::BeginPlay();

	// Initialize RL policy if not set
	if (!TacticalPolicy && bUseRLPolicy)
	{
		TacticalPolicy = NewObject<URLPolicyNetwork>(this);
		FMultiHeadPolicyConfig Config;
		TacticalPolicy->Initialize(Config);

		UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Created RL policy"), *GetOwner()->GetName());
	}

	// Find team leader component
	if (!TeamLeader)
	{
		// Option 1: Get from TeamLeaderActor if specified
		if (TeamLeaderActor)
		{
			TeamLeader = TeamLeaderActor->FindComponentByClass<UTeamLeaderComponent>();
			if (TeamLeader)
			{
				UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Found TeamLeader on specified actor '%s'"),
					*GetOwner()->GetName(), *TeamLeaderActor->GetName());
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("FollowerAgent '%s': TeamLeaderActor '%s' has no TeamLeaderComponent!"),
					*GetOwner()->GetName(), *TeamLeaderActor->GetName());
			}
		}
		// Option 2: Auto-find by tag
		else if (TeamLeaderTag != NAME_None)
		{
			TArray<AActor*> FoundActors;
			UGameplayStatics::GetAllActorsWithTag(GetWorld(), TeamLeaderTag, FoundActors);

			if (FoundActors.Num() > 0)
			{
				TeamLeaderActor = FoundActors[0];
				TeamLeader = TeamLeaderActor->FindComponentByClass<UTeamLeaderComponent>();

				if (TeamLeader)
				{
					UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Auto-found TeamLeader on actor '%s' by tag '%s'"),
						*GetOwner()->GetName(), *TeamLeaderActor->GetName(), *TeamLeaderTag.ToString());
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': Found actor with tag '%s' but no TeamLeaderComponent"),
						*GetOwner()->GetName(), *TeamLeaderTag.ToString());
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': No actor found with tag '%s'"),
					*GetOwner()->GetName(), *TeamLeaderTag.ToString());
			}
		}
	}

	// Auto-register with team leader
	if (bAutoRegisterWithLeader && TeamLeader)
	{
		RegisterWithTeamLeader();
	}

	// Bind to HealthComponent events for RL reward integration
	UHealthComponent* HealthComp = GetOwner()->FindComponentByClass<UHealthComponent>();
	if (HealthComp)
	{
		HealthComp->OnDamageTaken.AddDynamic(this, &UFollowerAgentComponent::OnDamageTakenEvent);
		HealthComp->OnDamageDealt.AddDynamic(this, &UFollowerAgentComponent::OnDamageDealtEvent);
		HealthComp->OnKillConfirmed.AddDynamic(this, &UFollowerAgentComponent::OnKillEvent);
		HealthComp->OnDeath.AddDynamic(this, &UFollowerAgentComponent::OnDeathEvent);

		UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Bound to HealthComponent events for RL rewards"),
			*GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': No HealthComponent found, RL combat rewards disabled"),
			*GetOwner()->GetName());
	}

	// Find or create RewardCalculator (Sprint 4)
	RewardCalculator = GetOwner()->FindComponentByClass<URewardCalculator>();
	if (!RewardCalculator)
	{
		UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': No RewardCalculator found, hierarchical rewards disabled"),
			*GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Found RewardCalculator, hierarchical rewards enabled"),
			*GetOwner()->GetName());
	}

	// [SCHOLA FIX] Create dummy objective for Schola agents BEFORE StateTree starts
	// This ensures the StateTree can enter ExecuteObjective state immediately
	bool bIsScholaAgent = GetOwner()->FindComponentByClass<UScholaAgentComponent>() != nullptr;
	if (bIsScholaAgent && !CurrentObjective)
	{
		// v4.0: Use base UObjective class (no subclasses)
		UObjective* DummyObj = NewObject<UObjective>(this);
		if (DummyObj)
		{
			DummyObj->Type = EObjectiveType::Support; // v4.0: FormationMove → Support
			DummyObj->Status = EObjectiveStatus::Active;
			DummyObj->Priority = 10;
			DummyObj->TimeLimit = 0.0f; // Infinite

			// CRITICAL: Set unreachable target to prevent completion
			// Dummy objective remains active until replaced by MCTS
			// Setting target 100km away ensures it NEVER completes during training
			DummyObj->TargetLocation = GetOwner()->GetActorLocation() + FVector(10000000.0f, 10000000.0f, 0.0f);

			// Add self to AssignedAgents to prevent edge-case completion
			// (CheckCompletion returns true when AssignedAgents.Num() == 0)
			DummyObj->AssignedAgents.Add(GetOwner());

			SetCurrentObjective(DummyObj);

			UE_LOG(LogTemp, Warning, TEXT("🎮 [SCHOLA INIT] '%s': Created dummy FormationMove objective (infinite, never completes)"),
				*GetOwner()->GetName());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("FollowerAgentComponent: Initialized on %s"), *GetOwner()->GetName());
}

void UFollowerAgentComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	// Check if simulation is running
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
	if (SimManager && !SimManager->IsSimulationRunning())
	{
		return;
	}

	// Update tactical action timer (v3.0)
	TimeSinceLastTacticalAction += DeltaTime;

	// Calculate hierarchical rewards if RewardCalculator is available
	if (RewardCalculator)
	{
		float TotalReward = RewardCalculator->CalculateTotalReward(DeltaTime);
		if (FMath::Abs(TotalReward) > 0.01f) // Only provide non-zero rewards
		{
			ProvideReward(TotalReward, false);
		}
	}

	// Draw debug info if enabled
	if (bEnableDebugDrawing)
	{
		DrawDebugInfo();
	}
}

void UFollowerAgentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregister from team leader
	UnregisterFromTeamLeader();

	Super::EndPlay(EndPlayReason);
}

//------------------------------------------------------------------------------
// INITIALIZATION
//------------------------------------------------------------------------------


//------------------------------------------------------------------------------
// TEAM LEADER COMMUNICATION
//------------------------------------------------------------------------------

bool UFollowerAgentComponent::RegisterWithTeamLeader()
{
	if (!TeamLeader)
	{
		UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': No TeamLeader set, cannot register"),
			*GetOwner()->GetName());
		return false;
	}

	bool bSuccess = TeamLeader->RegisterFollower(GetOwner());

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Registered with TeamLeader '%s'"),
			*GetOwner()->GetName(), *TeamLeader->TeamName);

		// Also register with SimulationManager (fix for missing team registration)
		ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
		if (SimManager)
		{
			bool bSimRegistered = SimManager->RegisterTeamMember(TeamLeader->TeamID, GetOwner());
			if (bSimRegistered)
			{
				UE_LOG(LogTemp, Warning, TEXT("✅ Follower '%s': Registered with SimulationManager (TeamID: %d)"),
					*GetOwner()->GetName(), TeamLeader->TeamID);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("❌ Follower '%s': Failed to register with SimulationManager (TeamID: %d)"),
					*GetOwner()->GetName(), TeamLeader->TeamID);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("⚠️ Follower '%s': SimulationManager not found, team member registration skipped"),
				*GetOwner()->GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': Failed to register with TeamLeader"),
			*GetOwner()->GetName());
	}

	return bSuccess;
}

void UFollowerAgentComponent::UnregisterFromTeamLeader()
{
	if (!TeamLeader) return;

	if (IsRegisteredWithLeader())
	{
		TeamLeader->UnregisterFollower(GetOwner());
		UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Unregistered from TeamLeader"),
			*GetOwner()->GetName());
	}
}

void UFollowerAgentComponent::SignalEventToLeader(
	EStrategicEvent Event,
	AActor* Instigator,
	FVector Location,
	int32 Priority)
{
	if (!TeamLeader)
	{
		UE_LOG(LogTemp, Error, TEXT("[FOLLOWER] '%s': ❌ Cannot signal event, no TeamLeader!"),
			*GetOwner()->GetName());
		return;
	}

	// Use owner's location if not specified
	if (Location.IsZero() && GetOwner())
	{
		Location = GetOwner()->GetActorLocation();
	}

	FString EventName = UEnum::GetValueAsString(Event);
	FString InstigatorName = Instigator ? Instigator->GetName() : TEXT("None");

	UE_LOG(LogTemp, Warning, TEXT("[FOLLOWER] '%s': 📡 Signaling event '%s' to Team Leader '%s' (Instigator: %s, Priority: %d)"),
		*GetOwner()->GetName(),
		*EventName,
		*TeamLeader->TeamName,
		*InstigatorName,
		Priority);

	TeamLeader->ProcessStrategicEvent(Event, Instigator, Location, Priority);

	UE_LOG(LogTemp, Display, TEXT("[FOLLOWER] '%s': ✅ Event signaled successfully"),
		*GetOwner()->GetName());

	// Broadcast event
	OnEventSignaled.Broadcast(Event, Instigator, Priority);
}

void UFollowerAgentComponent::ReportObjectiveComplete(bool bSuccess)
{
	if (!HasActiveObjective()) return;

	// Mark objective as completed or failed
	if (bSuccess)
	{
		CurrentObjective->Status = EObjectiveStatus::Completed;
		CurrentObjective->Progress = 1.0f;
	}
	else
	{
		CurrentObjective->Status = EObjectiveStatus::Failed;
	}

	UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Objective %s completed (%s)"),
		*GetOwner()->GetName(),
		*UEnum::GetValueAsString(CurrentObjective->Type),
		bSuccess ? TEXT("Success") : TEXT("Failed"));

	// Signal completion event to leader (low priority)
	if (bSuccess)
	{
		SignalEventToLeader(EStrategicEvent::ObjectiveComplete, GetOwner(), FVector::ZeroVector, 2);
	}
	else
	{
		SignalEventToLeader(EStrategicEvent::ObjectiveFailed, GetOwner(), FVector::ZeroVector, 2);
	}
}

void UFollowerAgentComponent::RequestAssistance(int32 Priority)
{
	SignalEventToLeader(EStrategicEvent::AllyRescueSignal, GetOwner(), FVector::ZeroVector, Priority);

	UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': Requested assistance (Priority: %d)"),
		*GetOwner()->GetName(), Priority);
}

//------------------------------------------------------------------------------
// OBJECTIVE EXECUTION (v3.0)
//------------------------------------------------------------------------------

bool UFollowerAgentComponent::HasActiveObjective() const
{
	return CurrentObjective != nullptr && CurrentObjective->IsActive();
}

//------------------------------------------------------------------------------
// STATE MANAGEMENT (v3.0: StateTree-based, no explicit FSM)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::MarkAsDead()
{
	if (!bIsAlive) return;

	bIsAlive = false;

	// Signal death to team leader
	SignalEventToLeader(EStrategicEvent::AllyKilled, GetOwner(), FVector::ZeroVector, 10);

	// Notify StateTree of death (triggers state transition)
	UFollowerStateTreeComponent* StateTreeComp = GetOwner()->FindComponentByClass<UFollowerStateTreeComponent>();
	if (StateTreeComp)
	{
		StateTreeComp->OnFollowerDied();
	}

	UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': Marked as dead"), *GetOwner()->GetName());
}

void UFollowerAgentComponent::MarkAsAlive()
{
	if (bIsAlive) return;

	bIsAlive = true;

	// Reset health to full
	UHealthComponent* HealthComp = GetOwner()->FindComponentByClass<UHealthComponent>();
	if (HealthComp)
	{
		HealthComp->ResetHealth();
		UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Health reset to %.1f/%.1f"),
			*GetOwner()->GetName(), HealthComp->GetCurrentHealth(), HealthComp->GetMaxHealth());
	}

	// Reset episode (v3.0: objectives are cleared via ObjectiveManager)
	ResetEpisode();

	// Notify StateTree of respawn (this will exit Dead state and re-enable systems)
	// CRITICAL FIX: Only call if StateTree is valid and world is ready
	if (UFollowerStateTreeComponent* StateTreeComp = GetOwner()->FindComponentByClass<UFollowerStateTreeComponent>())
	{
		if (StateTreeComp->IsValidLowLevel() && StateTreeComp->GetWorld())
		{
			StateTreeComp->OnFollowerRespawned();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': StateTree not ready during respawn, skipping restart"), *GetOwner()->GetName());
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': Respawn complete - ready for new commands"), *GetOwner()->GetName());
}

//------------------------------------------------------------------------------
// OBSERVATION
//------------------------------------------------------------------------------

void UFollowerAgentComponent::UpdateLocalObservation(const FObservationElement& NewObservation)
{
	LocalObservation = NewObservation;
}

FObservationElement UFollowerAgentComponent::BuildLocalObservation()
{
	// Start profiling
	double StartTime = FPlatformTime::Seconds();

	FObservationElement Observation;

	AActor* Owner = GetOwner();
	if (!Owner) return Observation;

	// v5.0: Streamlined Agent State (7 features)
	Observation.Position = Owner->GetActorLocation();
	Observation.Velocity = Owner->GetVelocity();

	// v5.0: Health (normalized [0, 1])
	UHealthComponent* HealthComp = Owner->FindComponentByClass<UHealthComponent>();
	if (HealthComp)
	{
		Observation.AgentHealth = HealthComp->GetHealthPercentage();  // Already [0, 1]
	}

	// Get perception component for enemy and raycast info
	UAgentPerceptionComponent* PerceptionComp = Owner->FindComponentByClass<UAgentPerceptionComponent>();
	if (PerceptionComp)
	{
		// Update enemy information from perception
		PerceptionComp->UpdateObservationWithEnemies(Observation);

		// Build raycast hit types (16 rays at 5000cm range)
		Observation.RaycastHitTypes = PerceptionComp->BuildRaycastHitTypes(16, 5000.0f);

		// Calculate raycast distances (normalized)
		const FVector OwnerLocation = Owner->GetActorLocation();
		Observation.RaycastDistances.Init(1.0f, 16);

		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Owner);

		const float MaxRayDistance = 5000.0f;
		const float AngleStep = 360.0f / 16;
		const FRotator OwnerRotation = Owner->GetActorRotation();  // Use local variable for raycast calculation

		for (int32 i = 0; i < 16; ++i)
		{
			const float Angle = i * AngleStep;
			const FRotator RayRotation = OwnerRotation + FRotator(0, Angle, 0);
			const FVector RayDirection = RayRotation.Vector();
			const FVector EndLocation = OwnerLocation + (RayDirection * MaxRayDistance);

			FHitResult HitResult;
			if (GetWorld()->LineTraceSingleByChannel(HitResult, OwnerLocation, EndLocation,
				ECC_Visibility, QueryParams))
			{
				// Normalized distance (0-1)
				Observation.RaycastDistances[i] = FMath::Clamp(HitResult.Distance / MaxRayDistance, 0.0f, 1.0f);
			}
		}
	}
	else
	{
		// Initialize empty if no perception component
		Observation.InitializeRaycasts(16);
	}

	// Get cover information from perception (cached to avoid expensive per-tick queries)
	if (PerceptionComp)
	{
		TArray<AActor*> Enemies = PerceptionComp->GetDetectedEnemies();
		if (Enemies.Num() > 0)
		{
			// Update cover cache if interval elapsed
			float CurrentTime = GetWorld()->GetTimeSeconds();
			if (CurrentTime - LastCoverQueryTime >= CoverQueryInterval)
			{
				FVector CoverLocation;
				float CoverDistance;
				bHasCachedCover = FindNearestCover(CoverLocation, CoverDistance, Enemies);
				if (bHasCachedCover)
				{
					CachedCoverLocation = CoverLocation;
					CachedCoverDistance = CoverDistance;
				}
				LastCoverQueryTime = CurrentTime;
			}

			// Use cached cover data
			const float MaxCoverDistance = 5000.0f;  // v5.0: normalization constant
			if (bHasCachedCover)
			{
				Observation.bHasCover = true;
				Observation.NearestCoverDistance = FMath::Clamp(CachedCoverDistance / MaxCoverDistance, 0.0f, 1.0f);

				// Calculate direction to cover (normalized 2D)
				FVector ToCover = CachedCoverLocation - Owner->GetActorLocation();
				ToCover.Z = 0; // Flatten to 2D
				ToCover.Normalize();
				Observation.CoverDirection = FVector2D(ToCover.X, ToCover.Y);
			}
			else
			{
				Observation.bHasCover = false;
				Observation.NearestCoverDistance = 1.0f;  // v5.0: normalized max distance
				Observation.CoverDirection = FVector2D::ZeroVector;
			}
		}
		else
		{
			// No enemies - no cover needed
			Observation.bHasCover = false;
			Observation.NearestCoverDistance = 1.0f;  // v5.0: normalized max distance
			Observation.CoverDirection = FVector2D::ZeroVector;
		}
	}

	// ========================================
	// v5.0: SUPPORT CONTEXT (4 features)
	// Find ally most in need of help for Support strategy
	// ========================================
	if (TeamLeader)
	{
		const float MaxAllyDistance = 5000.0f;  // Normalization constant
		const float AllyNeedsHelpThreshold = 0.5f;  // Health below 50% triggers help

		AActor* AllyInNeed = nullptr;
		float WorstAllyHealth = 1.0f;
		FVector AllyLocation = FVector::ZeroVector;

		// Find ally with lowest health
		for (AActor* Ally : TeamLeader->GetAliveFollowers())
		{
			if (!Ally || Ally == Owner) continue;

			UHealthComponent* AllyHealthComp = Ally->FindComponentByClass<UHealthComponent>();
			if (AllyHealthComp && AllyHealthComp->IsAlive())
			{
				float AllyHealthPct = AllyHealthComp->GetHealthPercentage();
				if (AllyHealthPct < WorstAllyHealth)
				{
					WorstAllyHealth = AllyHealthPct;
					AllyInNeed = Ally;
					AllyLocation = Ally->GetActorLocation();
				}
			}
		}

		if (AllyInNeed)
		{
			Observation.bAllyNeedsHelp = (WorstAllyHealth < AllyNeedsHelpThreshold);
			Observation.AllyHealth = WorstAllyHealth;

			// Calculate distance and direction to ally
			FVector ToAlly = AllyLocation - Owner->GetActorLocation();
			float Distance = ToAlly.Size();
			Observation.AllyDistance = FMath::Clamp(Distance / MaxAllyDistance, 0.0f, 1.0f);

			// Calculate angle to ally (normalized [-1, 1] based on forward direction)
			ToAlly.Normalize();
			FVector Forward = Owner->GetActorForwardVector();
			float Angle = FMath::Atan2(
				FVector::CrossProduct(Forward, ToAlly).Z,
				FVector::DotProduct(Forward, ToAlly)
			);
			Observation.AllyDirectionAngle = FMath::Clamp(Angle / PI, -1.0f, 1.0f);
		}
		else
		{
			// No ally in need
			Observation.bAllyNeedsHelp = false;
			Observation.AllyHealth = 1.0f;
			Observation.AllyDistance = 0.0f;
			Observation.AllyDirectionAngle = 0.0f;
		}
	}

	// End profiling
	double EndTime = FPlatformTime::Seconds();
	TotalObservationTime += static_cast<float>((EndTime - StartTime) * 1000.0); // Convert to ms

	return Observation;
}

bool UFollowerAgentComponent::FindNearestCover(FVector& OutCoverLocation, float& OutDistance, const TArray<AActor*>& Enemies)
{
	// Start profiling
	double StartTime = FPlatformTime::Seconds();

	AActor* Owner = GetOwner();
	if (!Owner || Enemies.Num() == 0)
	{
		return false;
	}

	const FVector AgentLocation = Owner->GetActorLocation();
	const FVector AgentEyeLevel = AgentLocation + FVector(0, 0, 150.0f);
	UWorld* World = GetWorld();

	// Calculate average enemy direction
	FVector AvgEnemyDirection = FVector::ZeroVector;
	for (AActor* Enemy : Enemies)
	{
		if (Enemy)
		{
			FVector ToEnemy = Enemy->GetActorLocation() - AgentLocation;
			ToEnemy.Z = 0; // Flatten to 2D
			ToEnemy.Normalize();
			AvgEnemyDirection += ToEnemy;
		}
	}
	AvgEnemyDirection.Normalize();

	// Sample points in a circle around the agent (perpendicular to enemy direction preferred)
	float BestScore = -1.0f;
	FVector BestCoverLocation = FVector::ZeroVector;
	const float AngleStep = 360.0f / CoverSearchSamples;

	for (int32 i = 0; i < CoverSearchSamples; ++i)
	{
		float Angle = i * AngleStep;
		FRotator SearchRotation = FRotator(0, Angle, 0);
		FVector SearchDirection = SearchRotation.Vector();
		FVector SearchEnd = AgentLocation + (SearchDirection * CoverSearchRadius);

		// Raycast to find obstacles
		FHitResult HitResult;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Owner);
		Params.bTraceComplex = false;

		if (World->LineTraceSingleByChannel(HitResult, AgentLocation, SearchEnd, ECC_WorldStatic, Params))
		{
			// Found an obstacle - check if it provides cover from enemies
			FVector PotentialCover = HitResult.Location;
			FVector PotentialCoverEye = PotentialCover + FVector(0, 0, 150.0f);

			// Check height (must be tall enough for cover)
			if (HitResult.Normal.Z < -0.5f || FMath::Abs(HitResult.ImpactPoint.Z - AgentLocation.Z) < MinCoverHeight)
			{
				continue; // Ground hit or too low
			}

			// Check if this position blocks LOS to enemies
			int32 BlockedEnemies = 0;
			for (AActor* Enemy : Enemies)
			{
				if (!Enemy) continue;

				FVector EnemyEyeLevel = Enemy->GetActorLocation() + FVector(0, 0, 150.0f);
				FHitResult LOSResult;
				FCollisionQueryParams LOSParams;
				LOSParams.bTraceComplex = false;

				if (World->LineTraceSingleByChannel(LOSResult, PotentialCoverEye, EnemyEyeLevel, ECC_Visibility, LOSParams))
				{
					BlockedEnemies++; // LOS blocked
				}
			}

			// Score this cover position
			float Distance = FVector::Dist(AgentLocation, PotentialCover);
			float CoverRatio = static_cast<float>(BlockedEnemies) / Enemies.Num();
			float DistanceScore = 1.0f - FMath::Clamp(Distance / CoverSearchRadius, 0.0f, 1.0f);

			// Prefer cover that blocks more enemies and is closer
			float Score = (CoverRatio * 0.7f) + (DistanceScore * 0.3f);

			if (Score > BestScore)
			{
				BestScore = Score;
				BestCoverLocation = PotentialCover;
			}
		}
	}

	// End profiling
	double EndTime = FPlatformTime::Seconds();
	TotalCoverQueryTime += static_cast<float>((EndTime - StartTime) * 1000.0); // Convert to ms
	CoverQueriesThisEpisode++;

	// Return best cover if found
	if (BestScore > 0.0f)
	{
		OutCoverLocation = BestCoverLocation;
		OutDistance = FVector::Dist(AgentLocation, BestCoverLocation);
		return true;
	}

	return false;
}

//------------------------------------------------------------------------------
// REINFORCEMENT LEARNING
//------------------------------------------------------------------------------


TArray<float> UFollowerAgentComponent::GetRLActionProbabilities()
{
	// v3.0: No longer using discrete action probabilities, uses atomic actions instead
	// Return empty array for backward compatibility
	TArray<float> EmptyProbs;
	return EmptyProbs;
}

void UFollowerAgentComponent::ProvideReward(float Reward, bool bTerminal)
{
	// Always accumulate reward (independent of RL policy or experience collection)
	AccumulatedReward += Reward;

	UE_LOG(LogTemp, Verbose, TEXT("FollowerAgent '%s': Provided reward %.2f (Accumulated: %.2f, Terminal: %s)"),
		*GetOwner()->GetName(), Reward, AccumulatedReward, bTerminal ? TEXT("Yes") : TEXT("No"));

	// Reset episode if terminal
	if (bTerminal)
	{
		ResetEpisode();
	}
}

void UFollowerAgentComponent::ResetEpisode()
{
	AccumulatedReward = 0.0f;
	PreviousObservation = FObservationElement();

	// Reset profiling stats (Sprint 6)
	TotalObservationTime = 0.0f;
	TotalCoverQueryTime = 0.0f;
	CoverQueriesThisEpisode = 0;

	// CRITICAL FIX (Issue #2): Clear visible enemies from StateTree context
	// When episode resets, agents must forget previously detected enemies
	UFollowerStateTreeComponent* StateTreeComp = GetOwner()->FindComponentByClass<UFollowerStateTreeComponent>();
	if (StateTreeComp)
	{
		FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
		SharedContext.VisibleEnemies.Empty();
		SharedContext.PrimaryTarget = nullptr;
		SharedContext.DistanceToPrimaryTarget = 99999.0f;

		UE_LOG(LogTemp, Warning, TEXT("[EPISODE RESET] '%s': Cleared VisibleEnemies (%d enemies forgotten)"),
			*GetOwner()->GetName(), SharedContext.VisibleEnemies.Num());
	}

	// Also clear perception component's tracked enemies
	UAgentPerceptionComponent* PerceptionComp = GetOwner()->FindComponentByClass<UAgentPerceptionComponent>();
	if (PerceptionComp)
	{
		// Perception component will rebuild enemy list on next tick via ScanForEnemies
		UE_LOG(LogTemp, Warning, TEXT("[EPISODE RESET] '%s': Perception will rebuild enemy list on next scan"),
			*GetOwner()->GetName());
	}

	// FIX (Issue #4): Clear team leader's known enemies (team-based enemy sharing)
	// Only one follower needs to do this (avoid duplicate clears), so check if we're the first follower
	if (TeamLeader)
	{
		TArray<AActor*> AllFollowers = TeamLeader->GetFollowers();
		if (AllFollowers.Num() > 0 && AllFollowers[0] == GetOwner())
		{
			// First follower clears team knowledge
			TeamLeader->ClearKnownEnemies();
			UE_LOG(LogTemp, Warning, TEXT("[EPISODE RESET] '%s': Cleared team knowledge (first follower)"),
				*GetOwner()->GetName());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Episode reset"), *GetOwner()->GetName());
}

void UFollowerAgentComponent::OnEpisodeEnded(float EpisodeReward)
{
	// Add episode reward to accumulated reward
	AccumulatedReward += EpisodeReward;

	// Provide terminal reward to RL policy
	ProvideReward(EpisodeReward, true); // bTerminal = true

	UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Episode ended - EpisodeReward=%.2f, TotalAccumulated=%.2f"),
		*GetOwner()->GetName(), EpisodeReward, AccumulatedReward);
}

//------------------------------------------------------------------------------
// UTILITY
//------------------------------------------------------------------------------

bool UFollowerAgentComponent::IsRegisteredWithLeader() const
{
	if (!TeamLeader) return false;

	return TeamLeader->IsFollowerRegistered(GetOwner());
}


//------------------------------------------------------------------------------
// DEBUG VISUALIZATION
//------------------------------------------------------------------------------

void UFollowerAgentComponent::DrawDebugInfo()
{
	if (!GetOwner()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FVector FollowerPos = GetOwner()->GetActorLocation();

	// Draw objective info above follower (v3.0)
	FString ObjectiveStr = CurrentObjective ? UEnum::GetValueAsString(CurrentObjective->Type) : TEXT("None");
	float Progress = CurrentObjective ? CurrentObjective->GetProgress() : 0.0f;

	FString StateText = FString::Printf(TEXT("Alive: %s\nObjective: %s\nProgress: %.1f%%"),
		bIsAlive ? TEXT("Yes") : TEXT("Dead"),
		*ObjectiveStr,
		Progress * 100.0f);

	DrawDebugString(World, FollowerPos + FVector(0, 0, 120), StateText, nullptr, FColor::Cyan, 0.1f, true);

	// Draw line to objective target (actor or location)
	if (HasActiveObjective())
	{
		// Check if target actor exists and is alive
		if (CurrentObjective->TargetActor && IsValid(CurrentObjective->TargetActor))
		{
			// Check if target has health component and is alive
			UHealthComponent* TargetHealth = CurrentObjective->TargetActor->FindComponentByClass<UHealthComponent>();
			bool bTargetAlive = !TargetHealth || TargetHealth->IsAlive();

			if (bTargetAlive)
			{
				FVector TargetPos = CurrentObjective->TargetActor->GetActorLocation();
				DrawDebugLine(World, FollowerPos, TargetPos, FColor::Red, false, 0.1f, 0, 2.0f);
				DrawDebugSphere(World, TargetPos, 50.0f, 8, FColor::Red, false, 0.1f);

				// Draw target name
				FString TargetName = FString::Printf(TEXT("Target: %s"), *CurrentObjective->TargetActor->GetName());
				DrawDebugString(World, TargetPos + FVector(0, 0, 100), TargetName, nullptr, FColor::Red, 0.1f, true);
			}
		}
		// Draw to target location if no valid actor
		else if (!CurrentObjective->TargetLocation.IsZero())
		{
			DrawDebugLine(World, FollowerPos, CurrentObjective->TargetLocation, FColor::Yellow, false, 0.1f, 0, 2.0f);
			DrawDebugSphere(World, CurrentObjective->TargetLocation, 50.0f, 8, FColor::Yellow, false, 0.1f);
		}
	}

	// Draw cover information (Sprint 6)
	if (bHasCachedCover)
	{
		// Draw line to cover
		DrawDebugLine(World, FollowerPos, CachedCoverLocation, FColor::Green, false, 0.1f, 0, 3.0f);
		DrawDebugSphere(World, CachedCoverLocation, 75.0f, 8, FColor::Green, false, 0.1f, 2.0f);

		// Draw cover info text
		FString CoverInfo = FString::Printf(TEXT("Cover: %.0fcm"), CachedCoverDistance);
		DrawDebugString(World, CachedCoverLocation + FVector(0, 0, 100), CoverInfo, nullptr, FColor::Green, 0.1f, true);
	}
	else
	{
		// Draw "No Cover" indicator
		DrawDebugString(World, FollowerPos + FVector(0, 0, 80), TEXT("No Cover"), nullptr, FColor::Red, 0.1f, true);
	}

	// Draw line to team leader
	if (TeamLeader && TeamLeader->GetOwner())
	{
		FVector LeaderPos = TeamLeader->GetOwner()->GetActorLocation();
		DrawDebugLine(World, FollowerPos, LeaderPos, TeamLeader->TeamColor.ToFColor(true), false, 0.1f, 0, 1.0f);
	}
}

//------------------------------------------------------------------------------
// COMBAT EVENT HANDLERS (RL REWARD INTEGRATION v5.0)
//------------------------------------------------------------------------------
// Architecture: HealthComponent → FollowerAgentComponent → RewardCalculator
//
// HealthComponent broadcasts combat events (damage, kills, death)
// FollowerAgentComponent subscribes and forwards to RewardCalculator
// RewardCalculator calculates strategy-specific rewards based on:
//   - Assault: High kill rewards (+15), low death penalty (-8)
//   - Defend: Position holding rewards, higher death penalty (-12)
//   - Support: Ally protection rewards, severe ally death penalty (-20)
//   - Retreat: Survival rewards, highest death penalty if failed (-15)
//
// The RewardCalculator is REQUIRED in v5.0 - fallback code removed.
//------------------------------------------------------------------------------

void UFollowerAgentComponent::OnDamageTakenEvent(const FDamageEventData& DamageEvent, float CurrentHealth)
{
	// Notify RewardCalculator (v5.0 strategy-specific rewards)
	if (RewardCalculator)
	{
		RewardCalculator->OnTakeDamage(DamageEvent.DamageAmount);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("FollowerAgent '%s': RewardCalculator missing! Cannot calculate damage reward."),
			*GetOwner()->GetName());
	}

	UE_LOG(LogTemp, Verbose, TEXT("FollowerAgent '%s': Took %.1f damage from %s"),
		*GetOwner()->GetName(),
		DamageEvent.DamageAmount,
		DamageEvent.Instigator ? *DamageEvent.Instigator->GetName() : TEXT("Unknown"));

	// Signal event to team leader if damage is significant
	if (TeamLeader && DamageEvent.DamageAmount >= 10.0f)
	{
		SignalEventToLeader(EStrategicEvent::UnderFire, DamageEvent.Instigator, GetOwner()->GetActorLocation(), 6);
	}
}

void UFollowerAgentComponent::OnDamageDealtEvent(AActor* Victim, float DamageAmount)
{
	// Notify RewardCalculator (v5.0 strategy-specific rewards)
	if (RewardCalculator)
	{
		RewardCalculator->OnDealDamage(DamageAmount, Victim);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("FollowerAgent '%s': RewardCalculator missing! Cannot calculate damage reward."),
			*GetOwner()->GetName());
	}

	UE_LOG(LogTemp, Verbose, TEXT("FollowerAgent '%s': Dealt %.1f damage to %s"),
		*GetOwner()->GetName(),
		DamageAmount,
		Victim ? *Victim->GetName() : TEXT("Unknown"));
}

void UFollowerAgentComponent::OnKillEvent(AActor* Victim, float TotalDamage)
{
	// Notify RewardCalculator (v5.0 strategy-specific rewards)
	if (RewardCalculator)
	{
		RewardCalculator->OnKillEnemy(Victim);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("FollowerAgent '%s': RewardCalculator missing! Cannot calculate kill reward."),
			*GetOwner()->GetName());
	}

	UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': KILL confirmed on %s"),
		*GetOwner()->GetName(),
		Victim ? *Victim->GetName() : TEXT("Unknown"));

	// Signal kill to team leader
	if (TeamLeader)
	{
		SignalEventToLeader(EStrategicEvent::EnemyKilled, Victim, GetOwner()->GetActorLocation(), 7);
	}
}

void UFollowerAgentComponent::OnDeathEvent(const FDeathEventData& DeathEvent)
{
	// Notify RewardCalculator (v5.0 strategy-specific rewards)
	if (RewardCalculator)
	{
		RewardCalculator->OnDeath();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("FollowerAgent '%s': RewardCalculator missing! Cannot calculate death penalty."),
			*GetOwner()->GetName());
	}

	UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': DIED (Killed by %s)"),
		*GetOwner()->GetName(),
		DeathEvent.Killer ? *DeathEvent.Killer->GetName() : TEXT("Unknown"));

	// Mark as dead
	MarkAsDead();

	// Signal death to team leader
	if (TeamLeader)
	{
		SignalEventToLeader(EStrategicEvent::AllyKilled, DeathEvent.Killer, GetOwner()->GetActorLocation(), 10);
	}
}

//------------------------------------------------------------------------------
// OBJECTIVE INTEGRATION (SPRINT 4)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::SetCurrentObjective(UObjective* Objective)
{
	// Set the current objective
	CurrentObjective = Objective;

	// Notify reward calculator
	if (RewardCalculator)
	{
		RewardCalculator->SetCurrentObjective(Objective);
	}

	UE_LOG(LogTemp, Warning, TEXT("📝 [FOLLOWER] '%s': Objective set to %s, Active=%d"),
		*GetOwner()->GetName(),
		Objective ? *UEnum::GetValueAsString(Objective->Type) : TEXT("None"),
		Objective ? Objective->IsActive() : false);

	// Broadcast delegate for StateTree event binding
	OnObjectiveReceived.Broadcast(Objective);
}

//------------------------------------------------------------------------------
// INDIVIDUAL STRATEGY (v5.0)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::SetCurrentStrategy(EStrategyType Strategy)
{
	EStrategyType PreviousStrategy = CurrentStrategy;
	CurrentStrategy = Strategy;

	// Update the last tactical action to reflect the new strategy
	LastTacticalAction.ActiveStrategy = Strategy;

	// Notify reward calculator of strategy change (for strategy-specific rewards)
	if (RewardCalculator)
	{
		RewardCalculator->SetCurrentStrategy(Strategy);
	}

	// Log strategy change if different
	if (PreviousStrategy != Strategy)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 [STRATEGY] '%s': Strategy changed %s → %s"),
			*GetOwner()->GetName(),
			*UEnum::GetValueAsString(PreviousStrategy),
			*UEnum::GetValueAsString(Strategy));
	}
}

//------------------------------------------------------------------------------
// STATE TRANSITION LOGGING (SPRINT 2 - WORLD MODEL)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::EnableStateTransitionLogging(bool bEnable)
{
	bLogStateTransitions = bEnable;

	if (bEnable)
	{
		LoggedTransitions.Empty();
		LastStateLogTime = 0.0f;
		UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': State transition logging ENABLED"), *GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': State transition logging DISABLED (%d samples collected)"),
			*GetOwner()->GetName(), LoggedTransitions.Num());
	}
}

void UFollowerAgentComponent::LogStateTransition()
{
	if (!bLogStateTransitions || !TeamLeader)
	{
		return;
	}

	// Throttle logging (only log at intervals)
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastStateLogTime < StateLogInterval)
	{
		return;
	}

	// Get current team observation from leader
	FTeamObservation CurrentTeamObs = TeamLeader->CurrentTeamObservation;

	// Only log if we have a previous observation
	if (PreviousTeamObservation.AliveFollowers > 0)
	{
		FStateTransitionSample Sample;

		// State before
		Sample.StateBefore = PreviousTeamObservation.Flatten();

		// Actions (tactical actions only in v3.0)
		Sample.TacticalActions.Add(LastTacticalAction);

		// State after
		Sample.StateAfter = CurrentTeamObs.Flatten();

		// Calculate actual delta
		FTeamStateDelta ActualDelta;
		ActualDelta.TeamHealthDelta = CurrentTeamObs.AverageTeamHealth - PreviousTeamObservation.AverageTeamHealth;
		ActualDelta.AliveCountDelta = CurrentTeamObs.AliveFollowers - PreviousTeamObservation.AliveFollowers;
		ActualDelta.TeamCohesionDelta = CurrentTeamObs.FormationCoherence - PreviousTeamObservation.FormationCoherence;
		ActualDelta.DeltaTime = StateLogInterval;

		Sample.ActualDelta = ActualDelta;
		Sample.Timestamp = CurrentTime;

		// Store sample
		LoggedTransitions.Add(Sample);

		if (LoggedTransitions.Num() % 50 == 0)
		{
			UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Logged %d state transitions"),
				*GetOwner()->GetName(), LoggedTransitions.Num());
		}
	}

	// Update previous observation
	PreviousTeamObservation = CurrentTeamObs;
	LastStateLogTime = CurrentTime;
}

bool UFollowerAgentComponent::ExportStateTransitions(const FString& FilePath)
{
	if (LoggedTransitions.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': No state transitions to export"), *GetOwner()->GetName());
		return false;
	}

	// Build JSON
	FString JsonString = TEXT("{\n  \"transitions\": [\n");

	for (int32 i = 0; i < LoggedTransitions.Num(); ++i)
	{
		const FStateTransitionSample& Sample = LoggedTransitions[i];

		JsonString += TEXT("    {\n");
		JsonString += FString::Printf(TEXT("      \"timestamp\": %.2f,\n"), Sample.Timestamp);
		JsonString += FString::Printf(TEXT("      \"game_outcome\": %.2f,\n"), Sample.GameOutcome);

		// State before
		JsonString += TEXT("      \"state_before\": [");
		for (int32 j = 0; j < Sample.StateBefore.Num(); ++j)
		{
			JsonString += FString::Printf(TEXT("%.4f"), Sample.StateBefore[j]);
			if (j < Sample.StateBefore.Num() - 1) JsonString += TEXT(", ");
		}
		JsonString += TEXT("],\n");

		// State after
		JsonString += TEXT("      \"state_after\": [");
		for (int32 j = 0; j < Sample.StateAfter.Num(); ++j)
		{
			JsonString += FString::Printf(TEXT("%.4f"), Sample.StateAfter[j]);
			if (j < Sample.StateAfter.Num() - 1) JsonString += TEXT(", ");
		}
		JsonString += TEXT("],\n");

		// Actual delta
		JsonString += TEXT("      \"actual_delta\": {\n");
		JsonString += FString::Printf(TEXT("        \"team_health_delta\": %.2f,\n"), Sample.ActualDelta.TeamHealthDelta);
		JsonString += FString::Printf(TEXT("        \"alive_count_delta\": %d,\n"), Sample.ActualDelta.AliveCountDelta);
		JsonString += FString::Printf(TEXT("        \"team_cohesion_delta\": %.4f\n"), Sample.ActualDelta.TeamCohesionDelta);
		JsonString += TEXT("      }\n");

		JsonString += TEXT("    }");
		if (i < LoggedTransitions.Num() - 1) JsonString += TEXT(",");
		JsonString += TEXT("\n");
	}

	JsonString += TEXT("  ]\n}\n");

	// Write to file
	const FString FullPath = FPaths::ProjectDir() / FilePath;
	if (FFileHelper::SaveStringToFile(JsonString, *FullPath))
	{
		UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Exported %d transitions to %s"),
			*GetOwner()->GetName(), LoggedTransitions.Num(), *FullPath);
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("FollowerAgent '%s': Failed to export transitions to %s"),
			*GetOwner()->GetName(), *FullPath);
		return false;
	}
}

//------------------------------------------------------------------------------
// PERFORMANCE PROFILING (Sprint 6)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::GetPerformanceStats(float& OutObservationTime, float& OutCoverQueryTime, int32& OutCoverQueriesThisEpisode) const
{
	OutObservationTime = TotalObservationTime;
	OutCoverQueryTime = TotalCoverQueryTime;
	OutCoverQueriesThisEpisode = CoverQueriesThisEpisode;
}
