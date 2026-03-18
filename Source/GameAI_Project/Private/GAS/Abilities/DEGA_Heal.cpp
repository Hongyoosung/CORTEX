// Copyright Epic Games, Inc. All Rights Reserved.

#include "GAS/Abilities/DEGA_Heal.h"
#include "GAS/DEGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Characters/DECharacter.h"
#include "Team/DEMatchManager.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Data/DEStrategyData.h"
#include "Types/DEStrategyTypes.h"
#include "Data/DETeamData.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"

UDEGA_Heal::UDEGA_Heal()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(DEGameplayTags::Ability_Heal);
	SetAssetTags(AssetTags);

	ActivationBlockedTags.AddTag(DEGameplayTags::State_Dead);
	// CooldownGameplayEffectClass left unset — heal has no cooldown by default
}

bool UDEGA_Heal::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const ADECharacter* OwnerChar = Cast<ADECharacter>(ActorInfo->AvatarActor.Get());
	if (!OwnerChar || OwnerChar->GetCommandedStrategy() != EDEStrategyType::Support)
	{
		return false;
	}

	return true;
}

void UDEGA_Heal::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ADECharacter* OwnerChar = Cast<ADECharacter>(ActorInfo->AvatarActor.Get());
	if (!OwnerChar)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ADECharacter* Target = nullptr;

	// If triggered via event with explicit target
	if (TriggerEventData && TriggerEventData->Target)
	{
		Target = Cast<ADECharacter>(const_cast<AActor*>(TriggerEventData->Target.Get()));
	}
	else
	{
		Target = FindNearestInjuredAlly();
	}

	if (Target && Target->IsAlive())
	{
		UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent();
		if (TargetASC && Config.HealEffectClass)
		{
			// DeltaTime for this tick is approximated at 0.2s (training timer interval)
			const float HealAmount = Config.Rate * 0.2f;

			FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo, Config.HealEffectClass);
			if (SpecHandle.IsValid())
			{
				// Build context with target location so GameplayCue VFX spawns on the healed character
				FGameplayEffectContextHandle ContextHandle = SpecHandle.Data->GetContext();
				ContextHandle.AddInstigator(OwnerChar, OwnerChar);
				ContextHandle.AddOrigin(Target->GetActorLocation());
				FHitResult HealHit;
				HealHit.Location = Target->GetActorLocation() + FVector(0, 0, 90);
				HealHit.ImpactPoint = HealHit.Location;
				HealHit.ImpactNormal = FVector::UpVector;
				ContextHandle.AddHitResult(HealHit);
				SpecHandle.Data->SetContext(ContextHandle);
				SpecHandle.Data->SetSetByCallerMagnitude(DEGameplayTags::Data_Healing, HealAmount);
				TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);
			}

			LastTickHealAmount = HealAmount;
			CumulativeHealAmount += HealAmount;

			UpdateHealBeam(Target);

			// Heal animation — montage is per-strategy
			if (const UDEStrategyData* SD = OwnerChar->GetCurrentStrategyData())
			{
				if (SD->HealMontage)
				{
					if (USkeletalMeshComponent* Mesh = OwnerChar->GetMesh())
					{
						if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
						{
							AnimInstance->Montage_Play(SD->HealMontage);
						}
					}
				}
			}
		}
		else if (!Config.HealEffectClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("DEGA_Heal: HealEffectClass not set — assign GE_HealTick in DA_AbilityConfig."));
		}
	}
	else
	{
		LastTickHealAmount = 0.0f;
		UpdateHealBeam(nullptr);
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

bool UDEGA_Heal::ConsumeHealBurst(float Threshold)
{
	if (CumulativeHealAmount >= Threshold)
	{
		CumulativeHealAmount = 0.0f;
		return true;
	}
	return false;
}

void UDEGA_Heal::ResetHealState()
{
	CumulativeHealAmount = 0.0f;
	LastTickHealAmount = 0.0f;
	UpdateHealBeam(nullptr);
}

ADECharacter* UDEGA_Heal::FindNearestInjuredAlly() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	const ADECharacter* OwnerChar = Cast<ADECharacter>(AvatarActor);
	if (!OwnerChar) return nullptr;

	// --- Inference mode: use AIPerception to find allies by TeamID ---
	// Falls back to MatchManager if no AIController / perception available.
	AAIController* AIC = Cast<AAIController>(OwnerChar->GetController());
	const UAIPerceptionComponent* Perception = AIC ? AIC->GetPerceptionComponent() : nullptr;

	TArray<ADECharacter*> Candidates;

	if (Perception)
	{
		TArray<AActor*> PerceivedActors;
		Perception->GetCurrentlyPerceivedActors(nullptr, PerceivedActors);

		for (AActor* Actor : PerceivedActors)
		{
			ADECharacter* Other = Cast<ADECharacter>(Actor);
			if (!Other || Other == OwnerChar || !Other->IsAlive()) continue;
			// Same team, same environment
			if (Other->GetTeamID_Implementation() != OwnerChar->GetTeamID_Implementation()) continue;
			if (Other->GetEnvID_Implementation()  != OwnerChar->GetEnvID_Implementation())  continue;
			Candidates.Add(Other);
		}
	}
	else
	{
		// Fallback: MatchManager (training mode)
		if (!CachedMatchManager)
		{
			CachedMatchManager = const_cast<UDEGA_Heal*>(this)->GetMatchManager();
		}
		if (CachedMatchManager)
		{
			TArray<ADECharacter*> TeamAgents = CachedMatchManager->GetTeamAgents(OwnerChar->GetTeamID_Implementation());
			for (ADECharacter* A : TeamAgents)
			{
				if (A && A != OwnerChar && A->IsAlive()) Candidates.Add(A);
			}
		}
	}

	ADECharacter* BestTarget = nullptr;
	float MinDistSq = FMath::Square(Config.Range);
	FVector MyLoc = OwnerChar->GetActorLocation();

	for (ADECharacter* Ally : Candidates)
	{
		if (Ally->GetHealthPercentage() >= 1.0f) continue;

		float DistSq = FVector::DistSquared(Ally->GetActorLocation(), MyLoc);
		if (DistSq < MinDistSq)
		{
			if (Config.bRequireLineOfSight)
			{
				FHitResult Hit;
				FCollisionQueryParams Params;
				Params.AddIgnoredActor(OwnerChar);
				Params.AddIgnoredActor(Ally);

				bool bBlocked = OwnerChar->GetWorld()->LineTraceSingleByChannel(
					Hit, MyLoc + FVector(0, 0, 90), Ally->GetActorLocation() + FVector(0, 0, 90),
					ECC_Visibility, Params);

				if (bBlocked) continue;
			}

			MinDistSq = DistSq;
			BestTarget = Ally;
		}
	}

	return BestTarget;
}

void UDEGA_Heal::UpdateHealBeam(const AActor* Target)
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	ADECharacter* OwnerChar = Cast<ADECharacter>(const_cast<AActor*>(AvatarActor));

	if (!Target)
	{
		if (HealBeamComponent) HealBeamComponent->Deactivate();
		return;
	}

	if (!HealBeamComponent && Config.HealBeamSystem && OwnerChar)
	{
		HealBeamComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			Config.HealBeamSystem, OwnerChar->GetRootComponent(), NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
	}

	if (HealBeamComponent)
	{
		HealBeamComponent->Activate();
		HealBeamComponent->SetVariableVec3(TEXT("BeamStart"), OwnerChar->GetActorLocation() + FVector(0, 0, 90));
		HealBeamComponent->SetVariableVec3(TEXT("BeamEnd"), Target->GetActorLocation() + FVector(0, 0, 90));
	}
}

ADEMatchManager* UDEGA_Heal::GetMatchManager() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	const ADECharacter* OwnerChar = Cast<ADECharacter>(AvatarActor);
	if (!OwnerChar) return nullptr;
	return OwnerChar->GetMatchManager();
}
