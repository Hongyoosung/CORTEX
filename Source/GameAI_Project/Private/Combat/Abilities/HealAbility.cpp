// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/HealAbility.h"
#include "Characters/MocCharacter.h"
#include "Combat/CombatStatsInterface.h"
#include "Team/TeamManager.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

UHealAbility::UHealAbility()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHealAbility::BeginPlay()
{
	Super::BeginPlay();
	if (AMocCharacter* Owner = GetOwnerCharacter())
	{
		CachedTM = Owner->GetTeamManager();
	}

	// Spawn the beam component once; kept inactive until healing begins
	if (HealBeamSystem)
	{
		if (AMocCharacter* Owner = GetOwnerCharacter())
		{
			HealBeamComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
				HealBeamSystem,
				Owner->GetRootComponent(),
				NAME_None,
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset,
				false,  // bAutoDestroy
				false); // bAutoActivate
		}
	}
}

void UHealAbility::ExecuteAbility(float DeltaTime)
{
	LastTickHealAmount = 0.0f;

	AMocCharacter* Target = FindNearestInjuredAlly();
	if (!Target)
	{
		UpdateHealBeam(nullptr);
		return;
	}

	AMocCharacter* Owner = GetOwnerCharacter();
	if (!Owner) return;

	// Detect target change — reset cumulative tracker
	if (Target->AgentID != CurrentHealTargetIdx)
	{
		CumulativeHealAmount = 0.0f;
		CurrentHealTargetIdx = Target->AgentID;
	}

	const float HealThisTick = HealRate * DeltaTime;
	const float ActualHealed = Target->Execute_Heal(Target, HealThisTick);
	LastTickHealAmount = ActualHealed;
	CumulativeHealAmount += ActualHealed;

	UpdateHealBeam(Target);
}

void UHealAbility::ExecuteAbilityWithTarget(float DeltaTime, AActor* Target)
{
	LastTickHealAmount = 0.0f;

	AMocCharacter* Ally = Cast<AMocCharacter>(Target);
	if (!Ally || !Ally->IsAlive_Implementation())
	{
		UpdateHealBeam(nullptr);
		return;
	}

	AMocCharacter* Owner = GetOwnerCharacter();
	if (!Owner) return;

	if (Ally->AgentID != CurrentHealTargetIdx)
	{
		CumulativeHealAmount = 0.0f;
		CurrentHealTargetIdx = Ally->AgentID;
	}

	const float HealThisTick = HealRate * DeltaTime;
	const float ActualHealed = Ally->Execute_Heal(Ally, HealThisTick);
	LastTickHealAmount = ActualHealed;
	CumulativeHealAmount += ActualHealed;

	UpdateHealBeam(Ally);
}

void UHealAbility::ResetHealState()
{
	LastTickHealAmount = 0.0f;
	CumulativeHealAmount = 0.0f;
	CurrentHealTargetIdx = -1;
	UpdateHealBeam(nullptr);
}

bool UHealAbility::ConsumeHealBurst(float Threshold)
{
	if (CumulativeHealAmount >= Threshold)
	{
		CumulativeHealAmount = 0.0f;
		return true;
	}
	return false;
}

void UHealAbility::UpdateHealBeam(const AActor* Target)
{
	if (!HealBeamComponent) return;

	if (!Target)
	{
		if (HealBeamComponent->IsActive())
		{
			HealBeamComponent->Deactivate();
		}
		return;
	}

	const AMocCharacter* Owner = GetOwnerCharacter();
	if (!Owner) return;

	HealBeamComponent->SetVariableVec3(BeamStartParamName, Owner->GetActorLocation());
	HealBeamComponent->SetVariableVec3(BeamEndParamName, Target->GetActorLocation());

	if (!HealBeamComponent->IsActive())
	{
		HealBeamComponent->Activate(true);
	}
}

AMocCharacter* UHealAbility::FindNearestInjuredAlly() const
{
	AMocCharacter* Owner = GetOwnerCharacter();
	if (!Owner || !CachedTM) return nullptr;

	TArray<AMocCharacter*> Allies = CachedTM->GetTeamAgents(Owner->TeamID);
	AMocCharacter* Nearest = nullptr;
	float NearestDist = HealRange;

	const FVector MyLoc = Owner->GetActorLocation();

	for (AMocCharacter* Ally : Allies)
	{
		if (!Ally || Ally == Owner || !Ally->IsAlive_Implementation()) continue;

		const float AllyHP = Ally->Execute_GetHealthPercentage(Ally);
		if (AllyHP >= 1.0f) continue;

		const float Dist = FVector::Dist(Ally->GetActorLocation(), MyLoc);
		if (Dist < NearestDist)
		{
			NearestDist = Dist;
			Nearest = Ally;
		}
	}

	return Nearest;
}
