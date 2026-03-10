// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/DEHealAbility.h"
#include "Characters/DECharacter.h"
#include "Team/DEMatchManager.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

void UDEHealAbility::Initialize(ADECharacter* InOwner)
{
	Super::Initialize(InOwner);
}

void UDEHealAbility::SetConfig(const FDEHealAbilityConfig& InConfig)
{
	Config = InConfig;
}

void UDEHealAbility::Execute(float DeltaTime)
{
	if (!CachedMatchManager)
	{
		CachedMatchManager = GetMatchManager();
	}

	if (!OwnerCharacter || !OwnerCharacter->IsAlive() || !CachedMatchManager)
	{
		UpdateHealBeam(nullptr);
		return;
	}

	ADECharacter* Target = FindNearestInjuredAlly();
	if (Target)
	{
		float HealAmount = Config.Rate * DeltaTime;
		float ActualHeal = Target->HealCharacter(HealAmount);
		
		LastTickHealAmount = ActualHeal;
		CumulativeHealAmount += ActualHeal;
		
		UpdateHealBeam(Target);
	}
	else
	{
		LastTickHealAmount = 0.0f;
		UpdateHealBeam(nullptr);
	}
}

void UDEHealAbility::ExecuteWithTarget(float DeltaTime, AActor* Target)
{
	ADECharacter* DETarget = Cast<ADECharacter>(Target);
	if (DETarget && DETarget->IsAlive())
	{
		float HealAmount = Config.Rate * DeltaTime;
		float ActualHeal = DETarget->HealCharacter(HealAmount);
		
		LastTickHealAmount = ActualHeal;
		CumulativeHealAmount += ActualHeal;
		
		UpdateHealBeam(DETarget);
	}
}

void UDEHealAbility::ResetHealState()
{
	CumulativeHealAmount = 0.0f;
	LastTickHealAmount = 0.0f;
	UpdateHealBeam(nullptr);
}

bool UDEHealAbility::ConsumeHealBurst(float Threshold)
{
	if (CumulativeHealAmount >= Threshold)
	{
		CumulativeHealAmount = 0.0f;
		return true;
	}
	return false;
}

ADECharacter* UDEHealAbility::FindNearestInjuredAlly() const
{
	if (!OwnerCharacter || !CachedMatchManager) return nullptr;

	TArray<ADECharacter*> Allies = CachedMatchManager->GetTeamAgents(OwnerCharacter->GetTeamID_Implementation());
	ADECharacter* BestTarget = nullptr;
	float MinDistSq = FMath::Square(Config.Range);

	FVector MyLoc = OwnerCharacter->GetActorLocation();

	for (ADECharacter* Ally : Allies)
	{
		if (!Ally || Ally == OwnerCharacter || !Ally->IsAlive()) continue;

		// Only heal if injured
		if (Ally->GetHealthPercentage() >= 1.0f) continue;

		float DistSq = FVector::DistSquared(Ally->GetActorLocation(), MyLoc);
		if (DistSq < MinDistSq)
		{
			if (Config.bRequireLineOfSight)
			{
				FHitResult Hit;
				FCollisionQueryParams Params;
				Params.AddIgnoredActor(OwnerCharacter);
				Params.AddIgnoredActor(Ally);

				bool bBlocked = OwnerCharacter->GetWorld()->LineTraceSingleByChannel(
					Hit, MyLoc + FVector(0,0,90), Ally->GetActorLocation() + FVector(0,0,90),
					ECC_Visibility, Params);
				
				if (bBlocked) continue;
			}

			MinDistSq = DistSq;
			BestTarget = Ally;
		}
	}

	return BestTarget;
}

void UDEHealAbility::UpdateHealBeam(const AActor* Target)
{
	if (!Target)
	{
		if (HealBeamComponent)
		{
			HealBeamComponent->Deactivate();
		}
		return;
	}

	if (!HealBeamComponent && Config.HealBeamSystem)
	{
		HealBeamComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			Config.HealBeamSystem, OwnerCharacter->GetRootComponent(), NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
	}

	if (HealBeamComponent)
	{
		HealBeamComponent->Activate();
		HealBeamComponent->SetVariableVec3(TEXT("BeamEnd"), Target->GetActorLocation() + FVector(0,0,90));
	}
}
