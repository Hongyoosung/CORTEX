// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actor/LeaderCharacter.h"
#include "Team/TeamLeaderComponent.h"
#include "Team/ObjectiveManager.h"

ALeaderCharacter::ALeaderCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create team leader component
	TeamLeaderComponent = CreateDefaultSubobject<UTeamLeaderComponent>(TEXT("TeamLeaderComponent"));

	// Create objective manager component
	ObjectiveManager = CreateDefaultSubobject<UObjectiveManager>(TEXT("ObjectiveManagerComponent"));
}

void ALeaderCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Add Team.Ally tag for gameplay tag identification system
	Tags.AddUnique("Team.Ally");
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
