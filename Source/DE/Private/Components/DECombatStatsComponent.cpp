// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DECombatStatsComponent.h"

UDECombatStatsComponent::UDECombatStatsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDECombatStatsComponent::NotifyDamageDealt(AActor* Victim, float DamageAmount)
{
	TotalDamageDealt += DamageAmount;
	StepDamageDealt += DamageAmount;
	OnDamageDealt_Delegate.Broadcast(Victim, DamageAmount);
}

void UDECombatStatsComponent::NotifyKillConfirmed(AActor* Victim, float TotalDamageDealtToVictim)
{
	KillCount++;
	OnKillConfirmed_Delegate.Broadcast(Victim, TotalDamageDealtToVictim);
}

void UDECombatStatsComponent::RecordDamageTaken(float ActualDamage, AActor* DamageInstigator)
{
	TotalDamageTaken += ActualDamage;
	LastDamageInstigator = DamageInstigator;
	LastDamageAmount = ActualDamage;

	if (DamageInstigator)
	{
		float& ContributorDamage = DamageContributors.FindOrAdd(DamageInstigator);
		ContributorDamage += ActualDamage;
	}
}

void UDECombatStatsComponent::ResetStats()
{
	TotalDamageTaken = 0.0f;
	TotalDamageDealt = 0.0f;
	KillCount = 0;
	DamageContributors.Empty();
	LastDamageInstigator = nullptr;
	LastDamageAmount = 0.0f;
	StepDamageDealt = 0.0f;
}
