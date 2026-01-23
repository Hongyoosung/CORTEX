// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Components/HealthComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"

UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	// Initialize health
	CurrentHealth = (StartingHealth > 0.0f) ? StartingHealth : MaxHealth;
	CurrentHealth = FMath::Clamp(CurrentHealth, 0.0f, MaxHealth);

	// Initialize armor
	Armor = FMath::Clamp(StartingArmor, 0.0f, MaxArmor);

	// Set invulnerability
	bIsInvulnerable = bStartInvulnerable;

	// Initialize state
	bIsAlive = true;
	bHasDied = false;
	TimeOfLastDamage = -999.0f;

	// Reset stats
	ResetCombatStats();

	UE_LOG(LogTemp, Log, TEXT("🏥 HealthComponent initialized: %s (Health: %.1f/%.1f, Armor: %.1f)"),
		*GetOwner()->GetName(), CurrentHealth, MaxHealth, Armor);
}

void UHealthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Update health regeneration
	if (bEnableHealthRegen && bIsAlive)
	{
		UpdateHealthRegen(DeltaTime);
	}

	// Debug drawing
	if (bEnableDebugDrawing)
	{
		DrawDebugInfo();
	}
}

//------------------------------------------------------------------------------
// DAMAGE HANDLING
//------------------------------------------------------------------------------

float UHealthComponent::TakeDamage(float DamageAmount, AActor* Instigator, AActor* DamageCauser,
	const FVector& HitLocation, const FVector& HitNormal)
{
	// Validation
	if (!bIsAlive || bIsInvulnerable || DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	// Apply armor mitigation
	float MitigatedDamage = ApplyArmorMitigation(DamageAmount);

	// Clamp to current health
	float ActualDamage = FMath::Min(MitigatedDamage, CurrentHealth);

	// Apply damage
	CurrentHealth -= ActualDamage;
	CurrentHealth = FMath::Max(CurrentHealth, 0.0f);

	// Update stats
	TotalDamageTaken += ActualDamage;
	TimeOfLastDamage = GetWorld()->GetTimeSeconds();

	// Create damage event
	FDamageEventData DamageEvent(Instigator, DamageCauser, ActualDamage, HitLocation, HitNormal);
	LastDamageEvent = DamageEvent;

	// Broadcast events (FollowerAgentComponent handles reward integration)
	OnDamageTaken.Broadcast(DamageEvent, CurrentHealth);
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);

	/*UE_LOG(LogTemp, Warning, TEXT("💥 %s took %.1f damage (%.1f mitigated) from %s → HP: %.1f/%.1f"),
		*GetOwner()->GetName(), ActualDamage, DamageAmount - MitigatedDamage,
		Instigator ? *Instigator->GetName() : TEXT("Unknown"),
		CurrentHealth, MaxHealth);*/

	// Check for death
	if (CurrentHealth <= 0.0f)
	{
		HandleDeath(Instigator, ActualDamage);
	}

	return ActualDamage;
}

void UHealthComponent::NotifyDamageDealt(AActor* Victim, float DamageAmount)
{
	if (!Victim || DamageAmount <= 0.0f)
	{
		return;
	}

	// Update stats
	TotalDamageDealt += DamageAmount;

	// Broadcast event (FollowerAgentComponent handles reward integration)
	OnDamageDealt.Broadcast(Victim, DamageAmount);

	UE_LOG(LogTemp, Log, TEXT("⚔️ %s dealt %.1f damage to %s"),
		*GetOwner()->GetName(), DamageAmount, *Victim->GetName());
}

void UHealthComponent::NotifyKillConfirmed(AActor* Victim, float InTotalDamageDealt)
{
	if (!Victim)
	{
		return;
	}

	// Update stats
	KillCount++;

	// Broadcast event (FollowerAgentComponent handles reward integration)
	OnKillConfirmed.Broadcast(Victim, InTotalDamageDealt);

	UE_LOG(LogTemp, Warning, TEXT("💀 %s KILLED %s! Total Kills: %d"),
		*GetOwner()->GetName(), *Victim->GetName(), KillCount);
}

//------------------------------------------------------------------------------
// HEALTH MANAGEMENT
//------------------------------------------------------------------------------

float UHealthComponent::Heal(float HealAmount)
{
	if (!bIsAlive || HealAmount <= 0.0f)
	{
		return 0.0f;
	}

	float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Min(CurrentHealth + HealAmount, MaxHealth);
	float ActualHeal = CurrentHealth - OldHealth;

	if (ActualHeal > 0.0f)
	{
		OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
		UE_LOG(LogTemp, Log, TEXT("💚 %s healed %.1f HP → %.1f/%.1f"),
			*GetOwner()->GetName(), ActualHeal, CurrentHealth, MaxHealth);
	}

	return ActualHeal;
}

void UHealthComponent::SetHealth(float NewHealth)
{
	CurrentHealth = FMath::Clamp(NewHealth, 0.0f, MaxHealth);
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);

	if (CurrentHealth <= 0.0f && bIsAlive)
	{
		HandleDeath(nullptr, 0.0f);
	}
}

void UHealthComponent::ResetHealth()
{
	CurrentHealth = MaxHealth;
	Armor = StartingArmor;
	bIsAlive = true;
	bHasDied = false;
	TimeOfLastDamage = -999.0f;

	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}

void UHealthComponent::Kill(AActor* Killer)
{
	if (!bIsAlive)
	{
		return;
	}

	CurrentHealth = 0.0f;
	HandleDeath(Killer, CurrentHealth);
}

//------------------------------------------------------------------------------
// QUERIES
//------------------------------------------------------------------------------

float UHealthComponent::GetTimeSinceLastDamage() const
{
	if (TimeOfLastDamage < 0.0f)
	{
		return 999999.0f;
	}
	return GetWorld()->GetTimeSeconds() - TimeOfLastDamage;
}

//------------------------------------------------------------------------------
// STATS
//------------------------------------------------------------------------------

void UHealthComponent::ResetCombatStats()
{
	TotalDamageTaken = 0.0f;
	TotalDamageDealt = 0.0f;
	KillCount = 0;
}

//------------------------------------------------------------------------------
// PRIVATE METHODS
//------------------------------------------------------------------------------

void UHealthComponent::HandleDeath(AActor* Killer, float FinalDamage)
{
	if (bHasDied)
	{
		return; // Already handled
	}

	bIsAlive = false;
	bHasDied = true;
	CurrentHealth = 0.0f;

	// Create death event
	FDeathEventData DeathEvent(GetOwner(), Killer, FinalDamage, GetWorld()->GetTimeSeconds());
	LastDeathEvent = DeathEvent;

	// Broadcast death event (FollowerAgentComponent handles reward integration)
	OnDeath.Broadcast(DeathEvent);

	// Notify killer's HealthComponent
	if (Killer && Killer != GetOwner())
	{
		if (UHealthComponent* KillerHealth = Killer->FindComponentByClass<UHealthComponent>())
		{
			KillerHealth->NotifyKillConfirmed(GetOwner(), TotalDamageTaken);
		}
	}

	UE_LOG(LogTemp, Error, TEXT("💀💀💀 %s DIED! Killer: %s, Final Damage: %.1f"),
		*GetOwner()->GetName(),
		Killer ? *Killer->GetName() : TEXT("Unknown"),
		FinalDamage);
}

float UHealthComponent::ApplyArmorMitigation(float IncomingDamage)
{
	if (Armor <= 0.0f || ArmorEffectiveness <= 0.0f)
	{
		return IncomingDamage;
	}

	// Calculate damage reduction: DamageReduction = Armor * Effectiveness
	float DamageReduction = Armor * ArmorEffectiveness;
	float MitigatedDamage = FMath::Max(IncomingDamage - DamageReduction, 0.0f);

	return MitigatedDamage;
}

void UHealthComponent::UpdateHealthRegen(float DeltaTime)
{
	// Check if we can regenerate
	if (CurrentHealth >= MaxHealth)
	{
		return;
	}

	// Check if enough time has passed since last damage
	float TimeSinceDamage = GetTimeSinceLastDamage();
	if (TimeSinceDamage < HealthRegenDelay)
	{
		return;
	}

	// Regenerate health
	float RegenAmount = HealthRegenRate * DeltaTime;
	Heal(RegenAmount);
}

void UHealthComponent::DrawDebugInfo()
{
	if (!GetOwner())
	{
		return;
	}

	FVector Location = GetOwner()->GetActorLocation() + FVector(0, 0, 100);
	FColor HealthColor = FColor::Green;

	float HealthPct = GetHealthPercentage();
	if (HealthPct < 0.3f)
	{
		HealthColor = FColor::Red;
	}
	else if (HealthPct < 0.6f)
	{
		HealthColor = FColor::Yellow;
	}

	// Draw health bar text
	FString HealthText = FString::Printf(TEXT("HP: %.0f/%.0f (%.0f%%)"),
		CurrentHealth, MaxHealth, HealthPct * 100.0f);

	DrawDebugString(GetWorld(), Location, HealthText, nullptr, HealthColor, 0.0f, true);

	// Draw armor if present
	if (Armor > 0.0f)
	{
		FString ArmorText = FString::Printf(TEXT("Armor: %.0f"), Armor);
		DrawDebugString(GetWorld(), Location - FVector(0, 0, 20), ArmorText, nullptr, FColor::Cyan, 0.0f, true);
	}

	// Draw kill count if any
	if (KillCount > 0)
	{
		FString KillText = FString::Printf(TEXT("Kills: %d"), KillCount);
		DrawDebugString(GetWorld(), Location + FVector(0, 0, 20), KillText, nullptr, FColor::Orange, 0.0f, true);
	}
}
