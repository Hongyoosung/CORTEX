// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actor/LeaderCharacter.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Team/Components/SquadManagerComponent.h"
#include "Team/Components/IntelManagerComponent.h"
#include "Team/Components/StrategicPlannerComponent.h"
#include "Util/Components/VisualLoggerComponent.h"

ALeaderCharacter::ALeaderCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	//--------------------------------------------------------------------------
	// v9.0: Create manager components (decomposed architecture)
	//--------------------------------------------------------------------------

	// Core coordinator component
	TeamLeaderComponent = CreateDefaultSubobject<UTeamLeaderComponent>(TEXT("TeamLeaderComponent"));

	// Manager components (v9.0 Phase 3)
	SquadManagerComponent = CreateDefaultSubobject<USquadManagerComponent>(TEXT("SquadManagerComponent"));
	IntelManagerComponent = CreateDefaultSubobject<UIntelManagerComponent>(TEXT("IntelManagerComponent"));
	StrategicPlannerComponent = CreateDefaultSubobject<UStrategicPlannerComponent>(TEXT("StrategicPlannerComponent"));

	// Debug visualization (optional - can be disabled in editor)
	VisualLoggerComponent = CreateDefaultSubobject<UVisualLoggerComponent>(TEXT("VisualLoggerComponent"));

	//--------------------------------------------------------------------------
	// Component Configuration
	//--------------------------------------------------------------------------

	// Squad Manager: Max followers = 4 (default)
	if (SquadManagerComponent)
	{
		SquadManagerComponent->MaxFollowers = 4;
	}

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
