#include "Team/FollowerAgentComponent.h"
#include "Team/TeamLeaderComponent.h"
#include "Team/ObjectiveActor.h"
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
#include "Schola/ScholaAgentComponent.h"
#include "Simulation/StateTransition.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UFollowerAgentComponent::UFollowerAgentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// v6.0 Phase 11: Tick every frame for fast event detection
	// Expensive work (RL inference) only happens on significant events via ShouldUpdateStrategy()
	// This allows ~0.16s reaction time (10 ticks at 60 FPS) instead of 1s (10 ticks at 10 FPS)
	PrimaryComponentTick.TickInterval = 0.0f;  // Every frame (event-driven work is cheap)
}

void UFollowerAgentComponent::BeginPlay()
{
	Super::BeginPlay();

	// Initialize RL policy if not set (v6.0: Single-head network)
	if (!TacticalPolicy && bUseRLPolicy)
	{
		TacticalPolicy = NewObject<URLPolicyNetwork>(this);
		FRLPolicyConfig Config;  // v6.0: Single-head policy config

		UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Created RL policy (v6.0 single-head)"), *GetOwner()->GetName());
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

	// v6.0 Phase 11: Cache components to avoid repeated FindComponentByClass calls
	CachedHealthComponent = GetOwner()->FindComponentByClass<UHealthComponent>();
	CachedPerceptionComponent = GetOwner()->FindComponentByClass<UAgentPerceptionComponent>();

	// Initialize strategy update timestamp
	LastStrategyUpdateTime = FPlatformTime::Seconds();

	// Bind to HealthComponent events for RL reward integration
	if (CachedHealthComponent)
	{
		CachedHealthComponent->OnDamageTaken.AddDynamic(this, &UFollowerAgentComponent::OnDamageTakenEvent);
		CachedHealthComponent->OnDamageDealt.AddDynamic(this, &UFollowerAgentComponent::OnDamageDealtEvent);
		CachedHealthComponent->OnKillConfirmed.AddDynamic(this, &UFollowerAgentComponent::OnKillEvent);
		CachedHealthComponent->OnDeath.AddDynamic(this, &UFollowerAgentComponent::OnDeathEvent);

		UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Bound to HealthComponent events for RL rewards"),
			*GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': No HealthComponent found, RL combat rewards disabled"),
			*GetOwner()->GetName());
	}

	// Log perception component cache status
	if (CachedPerceptionComponent)
	{
		UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Cached PerceptionComponent for event-driven updates"),
			*GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FollowerAgent '%s': No PerceptionComponent found, enemy detection disabled"),
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

	// v8.0: Initialize with default strategy assignment
	// Schola agents and regular agents both start with Assault strategy
	CurrentAssignment.Agent = GetOwner();
	CurrentAssignment.Strategy = EStrategyType::Assault;
	CurrentAssignment.TargetObjective = nullptr;  // Will be assigned by MCTS
	CurrentAssignment.Priority = 5;
	CurrentAssignment.ExpectedValue = 0.0f;
	CurrentAssignment.VisitCount = 0;
	CurrentAssignment.Timestamp = GetWorld()->GetTimeSeconds();

	UE_LOG(LogTemp, Log, TEXT("FollowerAgentComponent v8.0: Initialized on %s (Default Strategy: Assault)"), *GetOwner()->GetName());
}

// ========================================
// v8.0: Strategy Assignment (from MCTS)
// ========================================

void UFollowerAgentComponent::SetStrategyAssignment(const FStrategyAssignment& Assignment)
{
	// Store previous assignment for change detection
	LastAssignment = CurrentAssignment;

	// Set new assignment
	CurrentAssignment = Assignment;

	// Update timestamp
	CurrentAssignment.Timestamp = GetWorld()->GetTimeSeconds();

	// Notify RewardCalculator of strategy change
	if (RewardCalculator)
	{
		// RewardCalculator may need to know about objective changes
		// For now, we'll handle this through the existing reward system
	}

	UE_LOG(LogTemp, Warning, TEXT("📝 [FOLLOWER v8.0] '%s': Strategy assignment received - Strategy=%s, Objective=%s, Priority=%d, Value=%.2f"),
		*GetOwner()->GetName(),
		*UEnum::GetValueAsString(Assignment.Strategy),
		Assignment.TargetObjective ? *Assignment.TargetObjective->GetName() : TEXT("None"),
		Assignment.Priority,
		Assignment.ExpectedValue);

	// Broadcast event for StateTree or other systems
	OnStrategyAssignmentReceived.Broadcast(Assignment);

	// Force strategy update on next tick
	TicksSinceLastUpdate = 999;
}

AObjectiveActor* UFollowerAgentComponent::GetTargetObjective() const
{
	return CurrentAssignment.TargetObjective;
}

//------------------------------------------------------------------------------
// v8.0: TACTICAL & COMBAT PARAMETER SETTERS (for Schola actuators)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::SetTacticalParameters(const FTacticalParameters& Params)
{
	CurrentMacroAction.TacticalParams = Params;

	// Notify StateTree of parameter change (if using StateTree-based execution)
	// The StateTree task STTask_ExecuteTacticalMovement_v8 will read these params
	// and apply them to EQS query weights

	UE_LOG(LogTemp, Verbose, TEXT("[FollowerAgent v8.0] '%s': Tactical params set [Agg=%.2f, Cover=%.2f, Spread=%.2f, Risk=%.2f]"),
		*GetOwner()->GetName(),
		Params.Aggression, Params.CoverPreference, Params.SpreadDistance, Params.RiskTolerance);
}

void UFollowerAgentComponent::SetCombatParameters(const FCombatParameters& Params)
{
	CurrentMacroAction.CombatParams = Params;

	// Combat parameters affect target selection in ExecuteCombat()
	UE_LOG(LogTemp, Verbose, TEXT("[FollowerAgent v8.0] '%s': Combat params set [Priority=%s]"),
		*GetOwner()->GetName(),
		Params.Priority == ETargetPriority::Closest ? TEXT("Closest") : TEXT("LowestHP"));
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

	// Skip if not alive
	if (!bIsAlive)
	{
		return;
	}

	// ========================================
	// v8.0 HIERARCHICAL DECISION MAKING
	//
	// Layer 1 (Strategic - MCTS): Team Leader assigns STRATEGIES to agents
	//   - "Agent1 → Assault strategy targeting Point A"
	//   - "Agent2 → Defend strategy at Point B"
	//   - Runs async every 1.5s, doesn't block real-time execution
	//
	// Layer 2 (Tactical - RL): Follower outputs TACTICAL PARAMETERS + COMBAT CHOICES
	//   - Strategy: Assault → Tactical Params: [Aggression=0.8, CoverPref=0.3, ...]
	//   - Strategy: Defend → Tactical Params: [Aggression=0.2, CoverPref=0.9, ...]
	//   - Combat: Target priority (Closest vs LowestHP)
	//   - Event-driven updates (reduces inference cost by 75-83%)
	//   - Separate strategy heads GUARANTEE behavioral differentiation
	//
	// Layer 3 (Execution - EQS): EQS uses tactical parameters as query weights
	//   - Tactical Params → EQS weights → Position selection
	//   - Example: High Aggression → MinDistance=200cm, Low CoverWeight
	//
	// Layer 4 (Execution - Rules): Combat execution with learned target priority
	//   - Combat Params → Target selection (Closest vs LowestHP)
	//   - Auto-aim and firing (no learned aiming in v8.0)
	// ========================================
	if (ShouldUpdateStrategy())
	{
		// Build observation
		FObservationElement Obs = BuildLocalObservation();

		// Get assigned strategy from MCTS (via Mission type)
		EStrategyType AssignedStrategy = GetAssignedStrategy();

		// Query RL policy for tactical parameters + combat choices (v8.0 multi-head network)
		if (TacticalPolicy && bUseRLPolicy)
		{
			// Run RL inference (2-4ms if batched, otherwise 1-3ms)
			// v8.0: RL outputs tactical parameters, NOT strategy selection
			// Strategy is determined by MCTS assignment, RL controls HOW to execute it
			FMacroAction NewAction = TacticalPolicy->GetMacroAction(Obs, AssignedStrategy);

			// Log if tactical parameters changed significantly
			bool bParamsChanged = FMath::Abs(NewAction.TacticalParams.Aggression - CurrentMacroAction.TacticalParams.Aggression) > 0.1f ||
			                      FMath::Abs(NewAction.TacticalParams.CoverPreference - CurrentMacroAction.TacticalParams.CoverPreference) > 0.1f;

			if (bParamsChanged || NewAction.CombatParams.Priority != CurrentMacroAction.CombatParams.Priority)
			{
				UE_LOG(LogTemp, Display, TEXT("✅ [RL v8.0] '%s': Strategy=%s, Aggression=%.2f, Cover=%.2f, Spread=%.2f, Risk=%.2f, Combat=%s (Health: %.0f%%, Enemies: %d)"),
					*GetOwner()->GetName(),
					*UEnum::GetValueAsString(AssignedStrategy),
					NewAction.TacticalParams.Aggression,
					NewAction.TacticalParams.CoverPreference,
					NewAction.TacticalParams.SpreadDistance,
					NewAction.TacticalParams.RiskTolerance,
					*UEnum::GetValueAsString(NewAction.CombatParams.Priority),
					Obs.AgentHealth * 100.0f,
					Obs.VisibleEnemyCount);
			}

			CurrentMacroAction = NewAction;

			// v8.0 DEPRECATED: Update CurrentStrategy for backwards compatibility with v7.0 code
			CurrentStrategy = AssignedStrategy;

			// Update timestamp
			const_cast<UFollowerAgentComponent*>(this)->LastStrategyUpdateTime = FPlatformTime::Seconds();
		}
		else
		{
			// v8.0 BOOTSTRAP: Fallback heuristic if no RL policy (uses strategy-specific defaults)
			// The RLPolicyNetwork fallback (RLPolicyNetwork.cpp:GetMacroAction) provides strategy-specific params
			CurrentMacroAction = TacticalPolicy ? TacticalPolicy->GetMacroAction(Obs, AssignedStrategy)
			                                     : FMacroAction();  // Default constructor if no policy

			// v8.0 DEPRECATED: Update CurrentStrategy for backwards compatibility
			CurrentStrategy = AssignedStrategy;

			// Update timestamp
			const_cast<UFollowerAgentComponent*>(this)->LastStrategyUpdateTime = FPlatformTime::Seconds();
		}

		// Cache state for next event check (using cached components - v6.0 Phase 11 optimization)
		// v7.0 FIX: Removed health caching (now handled by damage events)
		if (CachedPerceptionComponent)
		{
			LastEnemyCount = CachedPerceptionComponent->GetDetectedEnemies().Num();
		}

		// v8.0: Cache assignment for change detection
		LastAssignment = CurrentAssignment;
		TicksSinceLastUpdate = 0;
	}

	// Always increment tick counter (for timeout fallback)
	TicksSinceLastUpdate++;

	// ========================================
	// v8.0: Combat execution (every tick, 60 Hz)
	// Tactical parameter updates run at 2-5 Hz
	// Combat runs every tick for responsive targeting
	// ========================================
	ExecuteCombat();

	// ========================================
	// Strategy execution happens in StateTree (cheap, <0.5ms)
	// FollowerAgentComponent only decides WHICH strategy, not HOW to execute
	// ========================================

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


//------------------------------------------------------------------------------
// v8.0: Strategy Assignment Helpers
//------------------------------------------------------------------------------

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

	// Reset episode (v3.0: Missions are cleared via MissionManager)
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

		}
		else
		{
			Observation.AllyDistance = 0.0f;
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

	// v6.0 Phase 11: Reset event-driven strategy tracking
	// v7.0 FIX: Removed LastStrategyHealth (now handled by damage events)
	LastEnemyCount = 0;
	TicksSinceLastUpdate = 0;

	// v8.0: Reset assignment to default
	CurrentAssignment.Strategy = EStrategyType::Assault;
	CurrentAssignment.TargetObjective = nullptr;
	CurrentAssignment.Priority = 5;
	LastAssignment = FStrategyAssignment();

	CurrentStrategy = EStrategyType::Assault; // v8.0 DEPRECATED: Reset to default (kept for v7.0 compatibility)
	CurrentMacroAction = FMacroAction();  // v8.0: Reset macro action to defaults

	// CRITICAL FIX (Issue #2): Clear visible enemies from StateTree context
	// When episode resets, agents must forget previously detected enemies
	UFollowerStateTreeComponent* StateTreeComp = GetOwner()->FindComponentByClass<UFollowerStateTreeComponent>();
	if (StateTreeComp)
	{
		FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
		SharedContext.VisibleEnemies.Empty();
		SharedContext.PrimaryTarget = nullptr;
		SharedContext.DistanceToPrimaryTarget = 99999.0f;
	}

	// Also clear perception component's tracked enemies
	UAgentPerceptionComponent* PerceptionComp = GetOwner()->FindComponentByClass<UAgentPerceptionComponent>();

	// FIX (Issue #4): Clear team leader's known enemies (team-based enemy sharing)
	// Only one follower needs to do this (avoid duplicate clears), so check if we're the first follower
	if (TeamLeader)
	{
		TArray<AActor*> AllFollowers = TeamLeader->GetFollowers();
		if (AllFollowers.Num() > 0 && AllFollowers[0] == GetOwner())
		{
			// First follower clears team knowledge
			TeamLeader->ClearKnownEnemies();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("FollowerAgent '%s': Episode reset (v6.0 event-driven state cleared)"), *GetOwner()->GetName());
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

int32 UFollowerAgentComponent::GetTeamID() const
{
	if (TeamLeader)
	{
		return TeamLeader->TeamID;
	}
	else
	{
		return -1; // Invalid team
	}
}

bool UFollowerAgentComponent::IsRegisteredWithLeader() const
{
	if (!TeamLeader) return false;

	return TeamLeader->IsFollowerRegistered(GetOwner());
}


//------------------------------------------------------------------------------
// DEBUG VISUALIZATION (v6.0 Phase 13)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::DrawDebugInfo()
{
	if (!GetOwner()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FVector FollowerPos = GetOwner()->GetActorLocation();

	// ============================================
	// v6.0 Phase 13: RL Strategy State Visualization
	// ============================================
	if (bEnableDebugDrawing)
	{
		// Strategy color coding
		FColor StrategyColor;
		switch (CurrentStrategy)
		{
			case EStrategyType::Assault:
				StrategyColor = FColor::Red;
				break;
			case EStrategyType::Defend:
				StrategyColor = FColor::Blue;
				break;
			case EStrategyType::Support:
				StrategyColor = FColor::Green;
				break;
			case EStrategyType::Retreat:
				StrategyColor = FColor::Yellow;
				break;
			default:
				StrategyColor = FColor::White;
				break;
		}

		// Draw strategy sphere around agent
		DrawDebugSphere(
			World,
			FollowerPos,
			100.0f,  // Radius
			12,      // Segments
			StrategyColor,
			false, 0.0f, 0, 2.0f  // Thickness
		);

		// Draw strategy text above agent
		FString StrategyStr = UEnum::GetValueAsString(CurrentStrategy);
		DrawDebugString(
			World,
			FollowerPos + FVector(0, 0, 200),
			StrategyStr,
			nullptr,
			StrategyColor,
			0.0f,
			true  // Draw shadow
		);

		// Draw health bar
		if (CachedHealthComponent)
		{
			float HealthPct = CachedHealthComponent->GetCurrentHealth() / CachedHealthComponent->GetMaxHealth();

			// Interpolate color from Red to Green based on health percentage
			FLinearColor HealthLinearColor = FLinearColor::LerpUsingHSV(
				FLinearColor(FColor::Red),
				FLinearColor(FColor::Green),
				HealthPct
			);
			FColor HealthColor = HealthLinearColor.ToFColor(true);

			FVector BarStart = FollowerPos + FVector(-50, 0, 250);
			FVector BarEnd = FollowerPos + FVector(-50 + HealthPct * 100, 0, 250);

			// Draw health bar background
			DrawDebugLine(
				World,
				BarStart,
				BarStart + FVector(100, 0, 0),
				FColor::Black,
				false, 0.0f, 0, 7.0f
			);

			// Draw health bar foreground
			DrawDebugLine(
				World,
				BarStart,
				BarEnd,
				HealthColor,
				false, 0.0f, 0, 5.0f
			);

			// Draw health percentage
			FString HealthText = FString::Printf(TEXT("%.0f%%"), HealthPct * 100.0f);
			DrawDebugString(
				World,
				FollowerPos + FVector(60, 0, 245),
				HealthText,
				nullptr,
				HealthColor,
				0.0f,
				true
			);
		}
	}
	// ============================================
	// End v6.0 RL Strategy Visualization
	// ============================================

	// v8.0: Draw assignment info above follower
	if (!bEnableDebugDrawing)
	{
		FString StrategyStr = UEnum::GetValueAsString(CurrentAssignment.Strategy);
		FString ObjectiveStr = CurrentAssignment.TargetObjective ? CurrentAssignment.TargetObjective->GetName() : TEXT("None");

		FString StateText = FString::Printf(TEXT("Alive: %s\nStrategy: %s\nObjective: %s\nPriority: %d"),
			bIsAlive ? TEXT("Yes") : TEXT("Dead"),
			*StrategyStr,
			*ObjectiveStr,
			CurrentAssignment.Priority);

		DrawDebugString(World, FollowerPos + FVector(0, 0, 120), StateText, nullptr, FColor::Cyan, 0.1f, true);
	}

	// v8.0: Draw line to target objective
	if (CurrentAssignment.TargetObjective && IsValid(CurrentAssignment.TargetObjective))
	{
		FVector TargetPos = CurrentAssignment.TargetObjective->GetActorLocation();
		DrawDebugLine(World, FollowerPos, TargetPos, FColor::Red, false, 0.1f, 0, 2.0f);
		DrawDebugSphere(World, TargetPos, 50.0f, 8, FColor::Red, false, 0.1f);

		// Draw objective name
		FString ObjectiveName = FString::Printf(TEXT("Objective: %s"), *CurrentAssignment.TargetObjective->GetName());
		DrawDebugString(World, TargetPos + FVector(0, 0, 100), ObjectiveName, nullptr, FColor::Red, 0.1f, true);
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

	// v7.0 FIX: Force strategy re-evaluation on significant damage
	// Damage is a critical event - agent should immediately reconsider its strategy
	// Threshold: 5.0 damage avoids re-evaluation spam from minor hits
	if (DamageEvent.DamageAmount >= 5.0f)
	{
		TicksSinceLastUpdate = 999;  // Exceed timeout threshold to force update on next tick

		UE_LOG(LogTemp, Verbose, TEXT("🔄 [DAMAGE EVENT] '%s': Significant damage (%.1f) - forcing strategy re-evaluation"),
			*GetOwner()->GetName(), DamageEvent.DamageAmount);
	}

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
// EVENT-DRIVEN STRATEGY UPDATES (v6.0 Phase 11 - Performance Optimization)
//------------------------------------------------------------------------------

bool UFollowerAgentComponent::ShouldUpdateStrategy() const
{
	// RATE LIMIT: Prevent oscillation by enforcing minimum update interval
	double CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - LastStrategyUpdateTime < MinStrategyUpdateInterval)
	{
		return false;  // Too soon since last update
	}

	// v7.0 FIX: Health change check REMOVED
	// Rationale:
	// - No health regeneration → health only decreases
	// - Damage events (OnDamageTakenEvent) now force strategy updates
	// - Arbitrary 20% threshold was redundant and could miss critical moments
	// - Damage-based updates provide more precise control

	// Get current enemy count (using cached component - v6.0 Phase 11 optimization)
	int32 CurrentEnemyCount = 0;
	if (CachedPerceptionComponent)
	{
		CurrentEnemyCount = CachedPerceptionComponent->GetDetectedEnemies().Num();
	}
	bool bNewEnemyDetected = CurrentEnemyCount > LastEnemyCount;

	// v8.0: Check assignment change (strategy or objective changed)
	bool bAssignmentChanged = (CurrentAssignment.Strategy != LastAssignment.Strategy) ||
	                           (CurrentAssignment.TargetObjective != LastAssignment.TargetObjective);

	// Fallback: Force update every 30 ticks (~0.5s at 60 FPS)
	// v7.0 FIX: Increased from 10 ticks to reduce oscillation
	bool bTimeout = TicksSinceLastUpdate > 30;

	// Debug log significant events
	if (bNewEnemyDetected || bAssignmentChanged || bTimeout)
	{
		FString Reason = TEXT("");
		if (bNewEnemyDetected) Reason += TEXT("NewEnemy ");
		if (bAssignmentChanged) Reason += TEXT("AssignmentChange ");
		if (bTimeout) Reason += TEXT("Timeout");

		UE_LOG(LogTemp, Verbose, TEXT("🔄 [EVENT-DRIVEN v8.0] '%s': Strategy update triggered - %s (Enemies: %d→%d)"),
			*GetOwner()->GetName(),
			*Reason,
			LastEnemyCount,
			CurrentEnemyCount);
	}

	return bNewEnemyDetected || bAssignmentChanged || bTimeout;
}

//------------------------------------------------------------------------------
// COMBAT EXECUTION (v8.0)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::ExecuteCombat()
{
	// ========================================
	// v8.0 COMBAT SYSTEM (Layer 4 of Hierarchy)
	//
	// ARCHITECTURE:
	// - RL selects target priority (Closest vs LowestHP)
	// - Auto-aim handles targeting (no learned aiming)
	// - Auto-fire when has LOS
	//
	// DESIGN DECISION:
	// v8.0 focuses on tactical positioning + target selection
	// Learned aiming deferred to v8.5 (reduces complexity)
	// ========================================

	if (!GetOwner() || !bIsAlive)
	{
		return;
	}

	// Get detected enemies from perception
	if (!CachedPerceptionComponent)
	{
		return;
	}

	TArray<AActor*> Enemies = CachedPerceptionComponent->GetDetectedEnemies();
	if (Enemies.Num() == 0)
	{
		// No enemies detected, clear focus
		AAIController* AIController = Cast<AAIController>(GetOwner()->GetInstigatorController());
		if (AIController)
		{
			AIController->ClearFocus(EAIFocusPriority::Gameplay);
		}
		return;
	}

	// ========================================
	// v8.0: RL-controlled target selection
	// Combat parameters determine priority
	// ========================================

	AActor* Target = nullptr;
	FCombatParameters CombatParams = CurrentMacroAction.CombatParams;

	switch (CombatParams.Priority)
	{
		case ETargetPriority::Closest:
			Target = GetClosestEnemy(Enemies);
			break;

		case ETargetPriority::LowestHP:
			Target = GetLowestHPEnemy(Enemies);
			break;

		default:
			Target = GetClosestEnemy(Enemies); // Fallback
			break;
	}

	if (!Target)
	{
		return;
	}

	// ========================================
	// Auto-aim and auto-fire (v8.0)
	// No learned aiming - engine handles targeting
	// ========================================

	AAIController* AIController = Cast<AAIController>(GetOwner()->GetInstigatorController());
	if (AIController)
	{
		// Set focus for auto-aim
		AIController->SetFocus(Target, EAIFocusPriority::Gameplay);

		// Auto-fire is handled by STTask_ExecuteFire in StateTree
		// This function only handles target selection
		UE_LOG(LogTemp, Verbose, TEXT("[COMBAT v8.0] '%s': Targeting %s (Priority: %s)"),
			*GetOwner()->GetName(),
			*Target->GetName(),
			*UEnum::GetValueAsString(CombatParams.Priority));
	}
}

AActor* UFollowerAgentComponent::GetClosestEnemy(const TArray<AActor*>& Enemies) const
{
	if (Enemies.Num() == 0 || !GetOwner())
	{
		return nullptr;
	}

	FVector AgentLocation = GetOwner()->GetActorLocation();
	AActor* ClosestEnemy = nullptr;
	float ClosestDistance = MAX_FLT;

	for (AActor* Enemy : Enemies)
	{
		if (!Enemy)
		{
			continue;
		}

		float Distance = FVector::Dist(AgentLocation, Enemy->GetActorLocation());
		if (Distance < ClosestDistance)
		{
			ClosestDistance = Distance;
			ClosestEnemy = Enemy;
		}
	}

	return ClosestEnemy;
}

AActor* UFollowerAgentComponent::GetLowestHPEnemy(const TArray<AActor*>& Enemies) const
{
	if (Enemies.Num() == 0)
	{
		return nullptr;
	}

	AActor* LowestHPEnemy = nullptr;
	float LowestHP = MAX_FLT;

	for (AActor* Enemy : Enemies)
	{
		if (!Enemy)
		{
			continue;
		}

		// Get enemy health component
		UHealthComponent* EnemyHealthComp = Enemy->FindComponentByClass<UHealthComponent>();
		if (!EnemyHealthComp || !EnemyHealthComp->IsAlive())
		{
			continue;
		}

		float EnemyHP = EnemyHealthComp->GetCurrentHealth();
		if (EnemyHP < LowestHP)
		{
			LowestHP = EnemyHP;
			LowestHPEnemy = Enemy;
		}
	}

	// Fallback: If no valid health components found, return closest enemy
	if (!LowestHPEnemy)
	{
		return GetClosestEnemy(Enemies);
	}

	return LowestHPEnemy;
}




