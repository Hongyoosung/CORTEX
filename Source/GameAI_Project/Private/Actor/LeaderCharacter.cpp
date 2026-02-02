// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actor/LeaderCharacter.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Team/Components/SquadManagerComponent.h"
#include "Team/Components/IntelManagerComponent.h"
#include "Team/Components/StrategicPlannerComponent.h"
#include "Util/Components/VisualLoggerComponent.h"
// v9.0 Phase 4: Additional includes for wrapper API
#include "Team/TeamTypes.h"
#include "Observation/TeamObservation.h"

ALeaderCharacter::ALeaderCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	//--------------------------------------------------------------------------
	// v9.0: Create manager components (decomposed architecture)
	//--------------------------------------------------------------------------

	// Core coordinator component
	TeamLeaderComponent = CreateDefaultSubobject<UTeamLeaderComponent>(TEXT("TeamLeaderComponent"));

	// Manager components (v9.0 Phase 3)
	IntelManagerComponent = CreateDefaultSubobject<UIntelManagerComponent>(TEXT("IntelManagerComponent"));
	StrategicPlannerComponent = CreateDefaultSubobject<UStrategicPlannerComponent>(TEXT("StrategicPlannerComponent"));

	// Debug visualization (optional - can be disabled in editor)
	VisualLoggerComponent = CreateDefaultSubobject<UVisualLoggerComponent>(TEXT("VisualLoggerComponent"));

	//--------------------------------------------------------------------------
	// Component Configuration
	//--------------------------------------------------------------------------

	// v9.0 Phase 4: Squad management is now merged into character (no separate component)
	// Configuration: MaxFollowers = 4, TeamID = 0 (set in header defaults)

	// Intel Manager: Set default objective tags
	if (IntelManagerComponent)
	{
		IntelManagerComponent->TeamID = 0;
	}

	// Strategic Planner: Initialize with 500 MCTS simulations
	if (StrategicPlannerComponent)
	{
		StrategicPlannerComponent->MCTSSimulations = 500;
		StrategicPlannerComponent->bEnableVerboseLogging = false;
	}

	// Visual Logger: Configure debug options (disabled by default)
	if (VisualLoggerComponent)
	{
		VisualLoggerComponent->bEnableDebugDrawing = false;  // Enable in editor for debugging
		VisualLoggerComponent->bDrawTeamInfo = true;
		VisualLoggerComponent->bDrawObjectives = true;
		VisualLoggerComponent->bDrawFormation = true;
	}
}

void ALeaderCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Add Team.Ally and TeamLeader tags for gameplay tag identification system
	Tags.AddUnique("Team.Ally");
	Tags.AddUnique("TeamLeader");  // v9.0: Required for TeamCommsComponent discovery

	//--------------------------------------------------------------------------
	// v9.0: Initialize manager components
	//--------------------------------------------------------------------------

	// Intel Manager: Discover objectives after 0.3s delay (allows level to initialize)
	if (IntelManagerComponent)
	{
		FTimerHandle DiscoveryTimer;
		GetWorld()->GetTimerManager().SetTimer(
			DiscoveryTimer,
			[this]()
			{
				if (IntelManagerComponent)
				{
					IntelManagerComponent->DiscoverWorldObjectives();
					UE_LOG(LogTemp, Log, TEXT("[LeaderCharacter v9.0] '%s': Objectives discovered"), *GetName());
				}
			},
			0.3f,
			false
		);
	}

	// Strategic Planner: Initialize MCTS with configured simulation count
	if (StrategicPlannerComponent)
	{
		StrategicPlannerComponent->InitializeMCTS(StrategicPlannerComponent->MCTSSimulations);
		UE_LOG(LogTemp, Log, TEXT("[LeaderCharacter v9.0] '%s': MCTS initialized with %d simulations"),
			*GetName(), StrategicPlannerComponent->MCTSSimulations);
	}

	UE_LOG(LogTemp, Log, TEXT("[LeaderCharacter v9.0] '%s': All manager components initialized"), *GetName());
}

void ALeaderCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// v9.0 Phase 4: Process deferred follower registrations (merged from SquadManager)
	if (PendingFollowerRegistration.Num() > 0)
	{
		ProcessDeferredRegistrations();
	}

	// Update combat timers
	UpdateTimers(DeltaTime);
}

//------------------------------------------------------------------------------
// COMBAT STATS (ICombatStatsInterface Implementation)
//------------------------------------------------------------------------------

float ALeaderCharacter::GetHealthPercentage_Implementation() const
{
	return (CombatStats.MaxHealth > 0.0f) ? (CombatStats.CurrentHealth / CombatStats.MaxHealth) * 100.0f : 0.0f;
}


bool ALeaderCharacter::IsAlive_Implementation() const
{
	return !bIsDead && CombatStats.CurrentHealth > 0.0f;
}

float ALeaderCharacter::GetWeaponCooldown_Implementation() const
{
	return CombatStats.CurrentWeaponCooldown;
}

bool ALeaderCharacter::CanFireWeapon_Implementation() const
{
	return IsAlive_Implementation() && CombatStats.CurrentWeaponCooldown <= 0.0f;
}

//------------------------------------------------------------------------------
// HEALTH SYSTEM
//------------------------------------------------------------------------------

void ALeaderCharacter::TakeDamage(float DamageAmount)
{
	if (bIsDead || DamageAmount <= 0.0f)
	{
		return;
	}

	// Apply remaining to health
	if (DamageAmount > 0.0f)
	{
		CombatStats.CurrentHealth = FMath::Max(0.0f, CombatStats.CurrentHealth - DamageAmount);
	}

	// Reset shield regen timer
	TimeSinceLastDamage = 0.0f;

	// Check for death
	if (CombatStats.CurrentHealth <= 0.0f)
	{
		Kill();
	}
}

void ALeaderCharacter::Heal(float HealAmount)
{
	if (bIsDead)
	{
		return;
	}

	CombatStats.CurrentHealth = FMath::Min(CombatStats.MaxHealth, CombatStats.CurrentHealth + HealAmount);
}

void ALeaderCharacter::Kill()
{
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;
	CombatStats.CurrentHealth = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("LeaderCharacter '%s' died"), *GetName());
}

void ALeaderCharacter::Respawn()
{
	bIsDead = false;
	CombatStats.CurrentHealth = CombatStats.MaxHealth;
	CombatStats.CurrentWeaponCooldown = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("LeaderCharacter '%s' respawned"), *GetName());
}

//------------------------------------------------------------------------------
// WEAPON SYSTEM
//------------------------------------------------------------------------------

bool ALeaderCharacter::FireWeapon()
{
	if (!CanFireWeapon_Implementation())
	{
		return false;
	}


	// Start cooldown
	CombatStats.CurrentWeaponCooldown = CombatStats.CooldownTime;

	return true;
}

//------------------------------------------------------------------------------
// PRIVATE
//------------------------------------------------------------------------------

void ALeaderCharacter::UpdateTimers(float DeltaTime)
{
	UpdateWeaponCooldown(DeltaTime);
}


void ALeaderCharacter::UpdateWeaponCooldown(float DeltaTime)
{
	if (CombatStats.CurrentWeaponCooldown > 0.0f)
	{
		CombatStats.CurrentWeaponCooldown = FMath::Max(0.0f, CombatStats.CurrentWeaponCooldown - DeltaTime);
	}
}

//==============================================================================
// v9.0 PHASE 4: CHARACTER WRAPPER API IMPLEMENTATIONS (Leader)
//==============================================================================

//------------------------------------------------------------------------------
// SQUAD MANAGEMENT (v9.0 Phase 4: merged from SquadManagerComponent)
//------------------------------------------------------------------------------

bool ALeaderCharacter::RegisterFollower(AActor* Follower)
{
	if (!Follower)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LeaderCharacter v9.0] RegisterFollower: Null follower provided"));
		return false;
	}

	if (RegisteredFollowers.Contains(Follower))
	{
		UE_LOG(LogTemp, Warning, TEXT("[LeaderCharacter v9.0] RegisterFollower: %s already registered"), *Follower->GetName());
		return false;
	}

	if (RegisteredFollowers.Num() >= MaxFollowers)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LeaderCharacter v9.0] RegisterFollower: Squad full (%d/%d), cannot register %s"),
			RegisteredFollowers.Num(), MaxFollowers, *Follower->GetName());
		return false;
	}

	// Add to roster
	RegisteredFollowers.Add(Follower);

	UE_LOG(LogTemp, Log, TEXT("[LeaderCharacter v9.0] Registered follower: %s (Total: %d/%d)"),
		*Follower->GetName(), RegisteredFollowers.Num(), MaxFollowers);

	return true;
}

bool ALeaderCharacter::UnregisterFollower(AActor* Follower)
{
	if (!Follower)
	{
		return false;
	}

	int32 RemovedCount = RegisteredFollowers.Remove(Follower);

	if (RemovedCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[LeaderCharacter v9.0] Unregistered follower: %s (Remaining: %d)"),
			*Follower->GetName(), RegisteredFollowers.Num());

		return true;
	}

	return false;
}

TArray<AActor*> ALeaderCharacter::GetFollowers() const
{
	return RegisteredFollowers;
}

TArray<AActor*> ALeaderCharacter::GetAliveFollowers() const
{
	TArray<AActor*> AliveFollowers;

	for (AActor* Follower : RegisteredFollowers)
	{
		if (!Follower)
		{
			continue;
		}

		// Assume all registered followers are alive (TODO: integrate with health system)
		AliveFollowers.Add(Follower);
	}

	return AliveFollowers;
}

int32 ALeaderCharacter::GetFollowerCount() const
{
	return RegisteredFollowers.Num();
}

bool ALeaderCharacter::IsSquadFull() const
{
	return RegisteredFollowers.Num() >= MaxFollowers;
}

//------------------------------------------------------------------------------
// INTELLIGENCE WRAPPERS (delegate to IntelManagerComponent)
//------------------------------------------------------------------------------

void ALeaderCharacter::RegisterEnemy(AActor* Enemy)
{
	if (IntelManagerComponent)
	{
		IntelManagerComponent->RegisterEnemy(Enemy);
	}
}

void ALeaderCharacter::UnregisterEnemy(AActor* Enemy)
{
	if (IntelManagerComponent)
	{
		IntelManagerComponent->UnregisterEnemy(Enemy);
	}
}

AObjectiveActor* ALeaderCharacter::GetFriendlyObjective() const
{
	return IntelManagerComponent ? IntelManagerComponent->GetFriendlyObjective() : nullptr;
}

AObjectiveActor* ALeaderCharacter::GetHostileObjective() const
{
	return IntelManagerComponent ? IntelManagerComponent->GetHostileObjective() : nullptr;
}

FTeamObservation ALeaderCharacter::BuildTeamObservation(const TArray<AActor*>& Followers)
{
	return IntelManagerComponent ? IntelManagerComponent->BuildTeamObservation(Followers) : FTeamObservation();
}

bool ALeaderCharacter::AreObjectivesDiscovered() const
{
	return IntelManagerComponent ? IntelManagerComponent->AreObjectivesDiscovered() : false;
}

//------------------------------------------------------------------------------
// STRATEGIC PLANNING WRAPPERS (delegate to StrategicPlannerComponent)
//------------------------------------------------------------------------------

void ALeaderCharacter::RunStrategyAssignmentAsync(const TArray<AActor*>& Agents, const TArray<AObjectiveActor*>& Objectives)
{
	if (StrategicPlannerComponent)
	{
		StrategicPlannerComponent->RunStrategyAssignmentAsync(Agents, Objectives);
	}
}

bool ALeaderCharacter::IsRunningMCTS() const
{
	return StrategicPlannerComponent ? StrategicPlannerComponent->IsRunningMCTS() : false;
}

void ALeaderCharacter::ApplyStrategyAssignment(const TArray<FStrategyAssignment>& Assignments)
{
	// This is typically handled by TeamLeaderComponent, delegate to it
	if (TeamLeaderComponent)
	{
		TeamLeaderComponent->ApplyStrategyAssignment(Assignments);
	}
}

//------------------------------------------------------------------------------
// v9.0 PHASE 4: SQUAD MANAGEMENT HELPERS (merged from SquadManagerComponent)
//------------------------------------------------------------------------------

void ALeaderCharacter::ProcessDeferredRegistrations()
{
	if (PendingFollowerRegistration.Num() == 0)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[LeaderCharacter v9.0] Processing %d pending registrations"), PendingFollowerRegistration.Num());

	TArray<AActor*> ToProcess = PendingFollowerRegistration;
	PendingFollowerRegistration.Empty();

	for (AActor* Follower : ToProcess)
	{
		RegisterFollower(Follower);
	}
}
