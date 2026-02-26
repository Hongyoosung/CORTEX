// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/HealAbility.h"
#include "Characters/MocCharacter.h"
#include "Combat/CombatStatsInterface.h"
#include "Team/TeamManager.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UHealAbility::UHealAbility()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHealAbility::BeginPlay()
{
	Super::BeginPlay();

	CachedOwner = GetOwnerCharacter();
	if (CachedOwner)
	{
		CachedTM = CachedOwner->GetTeamManager();
	}

	if (HealBeamSystem && CachedOwner)
	{
		HealBeamComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			HealBeamSystem,
			CachedOwner->GetRootComponent(),
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			false,
			false);
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

	if (!CachedOwner) return;

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

	if (!CachedOwner) return;

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

	if (!CachedOwner) return;

	HealBeamComponent->SetVariableVec3(BeamStartParamName, CachedOwner->GetActorLocation());
	HealBeamComponent->SetVariableVec3(BeamEndParamName, Target->GetActorLocation());

	if (!HealBeamComponent->IsActive())
	{
		HealBeamComponent->Activate(true);
	}
}

AMocCharacter* UHealAbility::FindNearestInjuredAlly() const
{
	if (!CachedOwner) return nullptr;

	TArray<AMocCharacter*> Allies;
	if (CachedTM)
	{
		Allies = CachedTM->GetTeamAgents(CachedOwner->TeamID);
	}
	else
	{
		// Fallback: world scan when no TeamManager (e.g. test maps)
		for (TActorIterator<AMocCharacter> It(GetWorld()); It; ++It)
		{
			if (*It != CachedOwner && (*It)->TeamID == CachedOwner->TeamID)
				Allies.Add(*It);
		}
	}
	AMocCharacter* NearestAlly = nullptr;

	const float HealRangeSq = FMath::Square(HealRange);
	float NearestDistSq = HealRangeSq;
	const FVector MyLoc = CachedOwner->GetActorLocation();

	for (AMocCharacter* Ally : Allies)
	{
		if (!Ally || Ally == CachedOwner || !Ally->IsAlive_Implementation()) continue;

		const float AllyHP = Ally->Execute_GetHealthPercentage(Ally);
		if (AllyHP >= 1.0f) continue;

		const float DistSq = FVector::DistSquared(Ally->GetActorLocation(), MyLoc);
		if (DistSq < NearestDistSq)
		{
			// bRequireLineOfSight가 true이면 시야가 확보되는지 검사
			if (bRequireLineOfSight)
			{
				FCollisionQueryParams QueryParams;
				QueryParams.AddIgnoredActor(CachedOwner);
				QueryParams.AddIgnoredActor(Ally);

				FHitResult HitResult;
				bool bBlocked = GetWorld()->LineTraceSingleByChannel(
					HitResult,
					MyLoc + FVector(0, 0, 90),
					Ally->GetActorLocation() + FVector(0, 0, 90),
					ECC_Visibility,
					QueryParams
				);

				if (bBlocked) continue; // 벽에 막혔으므로 제외
			}

			NearestDistSq = DistSq;
			NearestAlly = Ally;
		}
	}

	return NearestAlly;
}