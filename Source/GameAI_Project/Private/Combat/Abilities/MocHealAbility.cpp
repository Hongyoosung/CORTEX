// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/MocHealAbility.h"
#include "Characters/MocCharacter.h"
#include "Team/MatchManager.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

void UMocHealAbility::Initialize(AMocCharacter* InOwner)
{
	Super::Initialize(InOwner);
}

void UMocHealAbility::SetConfig(const FHealAbilityConfig& InConfig)
{
	Config = InConfig;
}

void UMocHealAbility::Execute(float DeltaTime)
{
	if (!CachedMatchManager)
	{
		CachedMatchManager = GetMatchManager();
	}

	if (!OwnerCharacter || !OwnerCharacter->IsAlive_Implementation() || !CachedMatchManager)
	{
		UpdateHealBeam(nullptr);
		return;
	}

	AMocCharacter* Target = FindNearestInjuredAlly();
	if (Target)
	{
		float HealAmount = Config.Rate * DeltaTime;
		float ActualHeal = Target->Heal_Implementation(HealAmount);
		
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

void UMocHealAbility::ExecuteWithTarget(float DeltaTime, AActor* Target)
{
	AMocCharacter* MocTarget = Cast<AMocCharacter>(Target);
	if (MocTarget && MocTarget->IsAlive_Implementation())
	{
		float HealAmount = Config.Rate * DeltaTime;
		float ActualHeal = MocTarget->Heal_Implementation(HealAmount);
		
		LastTickHealAmount = ActualHeal;
		CumulativeHealAmount += ActualHeal;
		
		UpdateHealBeam(MocTarget);
	}
}

void UMocHealAbility::ResetHealState()
{
	CumulativeHealAmount = 0.0f;
	LastTickHealAmount = 0.0f;
	UpdateHealBeam(nullptr);
}

bool UMocHealAbility::ConsumeHealBurst(float Threshold)
{
	if (CumulativeHealAmount >= Threshold)
	{
		CumulativeHealAmount = 0.0f;
		return true;
	}
	return false;
}

AMocCharacter* UMocHealAbility::FindNearestInjuredAlly() const
{
	if (!OwnerCharacter || !CachedMatchManager) return nullptr;

	TArray<AMocCharacter*> Allies = CachedMatchManager->GetTeamAgents(OwnerCharacter->GetTeamID_Implementation());
	AMocCharacter* BestTarget = nullptr;
	float MinDistSq = FMath::Square(Config.Range);

	FVector MyLoc = OwnerCharacter->GetActorLocation();

	for (AMocCharacter* Ally : Allies)
	{
		if (!Ally || Ally == OwnerCharacter || !Ally->IsAlive_Implementation()) continue;

		// Only heal if injured
		if (Ally->GetHealthPercentage_Implementation() >= 1.0f) continue;

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

void UMocHealAbility::UpdateHealBeam(const AActor* Target)
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
