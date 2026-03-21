// Copyright Epic Games, Inc. All Rights Reserved.

#include "GAS/Abilities/DEGA_Heal.h"
#include "GAS/DEGameplayTags.h"
#include "GAS/DEAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Characters/DEAgent.h"
#include "Team/DEMatchManager.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/DESupportAnimInstance.h"
#include "Types/DEClassTypes.h"
#include "Data/DETeamData.h"


void UDEGA_Heal::SetConfig(const FDEHealAbilityConfig& InConfig)
{
	// Preserve the existing HealBeamSystem if the incoming config leaves it unset.
	// This prevents AbilityDataOverrides that omit the beam from clearing a previously
	// configured beam (e.g. when set on the base UDEAbilityData in Test Mode).
	UNiagaraSystem* ExistingBeam = Config.HealBeamSystem.Get();
	Config = InConfig;
	if (!Config.HealBeamSystem && ExistingBeam)
	{
		Config.HealBeamSystem = ExistingBeam;
	}
}

UDEGA_Heal::UDEGA_Heal()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(DEGameplayTags::Ability_Heal);
	SetAssetTags(AssetTags);

	ActivationBlockedTags.AddTag(DEGameplayTags::State_Dead);
	// CooldownGameplayEffectClass left unset — heal has no cooldown by default

	// One persistent instance per actor so the channel can be held across frames.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
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

	const ADEAgent* OwnerChar = Cast<ADEAgent>(ActorInfo->AvatarActor.Get());
	if (!OwnerChar || OwnerChar->GetCommandedClass() != EDEClassType::Support)
	{
		return false;
	}

	// Enforce minimum interval between heal ticks
	if (Config.HealTickInterval > 0.0f)
	{
		const UWorld* World = ActorInfo->AvatarActor.IsValid()
			? ActorInfo->AvatarActor->GetWorld() : nullptr;
		if (World && LastHealTime >= 0.0f &&
			(World->GetTimeSeconds() - LastHealTime) < Config.HealTickInterval)
		{
			return false;
		}
	}

	// Block if mana is fully depleted (continuous drain — just need > 0 to begin the channel)
	if (Config.ManaCost > 0.0f)
	{
		const UDEAttributeSet* AS = ActorInfo->AbilitySystemComponent->GetSet<UDEAttributeSet>();
		if (AS && AS->GetMana() <= 0.0f)
		{
			return false;
		}
	}

	// Block if no injured ally is in range — prevents wasting mana/montage on full-health allies
	if (!HasInjuredAllyInRange())
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

	ADEAgent* OwnerChar = Cast<ADEAgent>(ActorInfo->AvatarActor.Get());
	if (!OwnerChar)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UWorld* World = OwnerChar->GetWorld();
	if (!World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Resolve initial target
	if (TriggerEventData && TriggerEventData->Target)
	{
		CurrentTarget = Cast<ADEAgent>(const_cast<AActor*>(TriggerEventData->Target.Get()));
	}
	else
	{
		CurrentTarget = FindNearestInjuredAlly();
	}

	if (!CurrentTarget)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Spawn the beam immediately so it appears on the first frame.
	// Heal and mana drain begin on the first OnBeamUpdateTick (≤ 16 ms away).
	UpdateHealBeam(CurrentTarget);

	if (USkeletalMeshComponent* Mesh = OwnerChar->GetMesh())
	{
		UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
		if (AnimInstance && Config.HealMontage)
		{
			AnimInstance->Montage_Play(Config.HealMontage);
			CachedAnimInstance = AnimInstance;

			CachedSupportAnimInstance = Cast<UDESupportAnimInstance>(AnimInstance);
			if (CachedSupportAnimInstance)
			{
				CachedSupportAnimInstance->BeginHealLoop(Config.HealMontage);
			}
		}
	}

	// Heal tick — re-validates target and applies heal at the configured interval.
	const float TickInterval = FMath::Max(Config.HealTickInterval, 0.1f);
	World->GetTimerManager().SetTimer(
		HealTickTimerHandle,
		this, &UDEGA_Heal::OnHealTick,
		TickInterval,
		/*bLoop=*/true);

	// Beam position update — fires every frame (~0.016 s) so the line tracks movement smoothly.
	World->GetTimerManager().SetTimer(
		BeamUpdateTimerHandle,
		this, &UDEGA_Heal::OnBeamUpdateTick,
		0.016f,
		/*bLoop=*/true);

	LastHealTime = World->GetTimeSeconds();
}

void UDEGA_Heal::OnHealTick()
{
	// Re-find the nearest injured ally every tick so the healer naturally switches
	// to a more injured teammate who moves into range.
	ADEAgent* NewTarget = FindNearestInjuredAlly();
	if (!NewTarget)
	{
		// No valid target — end the channel.
		const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
		EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo,
			GetCurrentActivationInfo(), /*bReplicate=*/true, /*bWasCancelled=*/false);
		return;
	}

	CurrentTarget = NewTarget;
}

// Timer interval used for both beam position updates and per-frame heal / mana drain.
static constexpr float BEAM_DT = 0.016f;

void UDEGA_Heal::OnBeamUpdateTick()
{
	// --- Section management ---
	if (CachedAnimInstance && Config.HealMontage)
	{
		const FName CurSection = CachedAnimInstance->Montage_GetCurrentSection(Config.HealMontage);
		if (CurSection == UDESupportAnimInstance::SectionLoop)
		{
			// Keep redirecting Loop→Loop while the channel is active.
			// EndAbility switches this to Loop→End so the outro plays naturally.
			CachedAnimInstance->Montage_SetNextSection(
				UDESupportAnimInstance::SectionLoop,
				UDESupportAnimInstance::SectionLoop,
				Config.HealMontage);
		}
	}

	if (!CurrentTarget || !CurrentTarget->IsAlive())
	{
		UpdateHealBeam(nullptr);
		return;
	}

	// --- Character faces target ---
	const FGameplayAbilityActorInfo* FaceActorInfo = GetCurrentActorInfo();
	if (ADEAgent* OwnerChar = FaceActorInfo ? Cast<ADEAgent>(FaceActorInfo->AvatarActor.Get()) : nullptr)
	{
		const FVector ToTarget = (CurrentTarget->GetActorLocation() - OwnerChar->GetActorLocation()).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			const FRotator CurRot = OwnerChar->GetActorRotation();
			OwnerChar->SetActorRotation(FRotator(CurRot.Pitch, ToTarget.Rotation().Yaw, CurRot.Roll));
		}
	}

	// --- Continuous mana drain ---
	if (Config.ManaCost > 0.0f)
	{
		const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
		if (ActorInfo)
		{
			if (UDEAttributeSet* AS = const_cast<UDEAttributeSet*>(
				ActorInfo->AbilitySystemComponent->GetSet<UDEAttributeSet>()))
			{
				const float NewMana = AS->GetMana() - Config.ManaCost * BEAM_DT;
				if (NewMana <= 0.0f)
				{
					AS->SetMana(0.0f);
					EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo,
						GetCurrentActivationInfo(), /*bReplicate=*/true, /*bWasCancelled=*/false);
					return;
				}
				AS->SetMana(NewMana);
			}
		}
	}

	// --- Continuous heal ---
	ApplyHealToTarget(CurrentTarget, Config.Rate * BEAM_DT);

	// --- Beam position ---
	UpdateHealBeam(CurrentTarget);
}

void UDEGA_Heal::ApplyHealToTarget(ADEAgent* Target, float HealAmount)
{
	if (!Target || !Target->IsAlive() || HealAmount <= 0.0f) return;

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	ADEAgent* OwnerChar = ActorInfo ? Cast<ADEAgent>(ActorInfo->AvatarActor.Get()) : nullptr;
	UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent();

	if (!TargetASC || !Config.HealEffectClass)
	{
		if (!Config.HealEffectClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("DEGA_Heal: HealEffectClass not set — assign GE_HealTick in DA_AbilityConfig."));
		}
		return;
	}
	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(
		GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), Config.HealEffectClass);

	if (SpecHandle.IsValid())
	{
		FGameplayEffectContextHandle ContextHandle = SpecHandle.Data->GetContext();
		if (OwnerChar)
		{
			ContextHandle.AddInstigator(OwnerChar, OwnerChar);
		}
		ContextHandle.AddOrigin(Target->GetActorLocation());
		FHitResult HealHit;
		HealHit.Location     = Target->GetActorLocation() + FVector(0, 0, 90);
		HealHit.ImpactPoint  = HealHit.Location;
		HealHit.ImpactNormal = FVector::UpVector;
		ContextHandle.AddHitResult(HealHit);
		SpecHandle.Data->SetContext(ContextHandle);
		SpecHandle.Data->SetSetByCallerMagnitude(DEGameplayTags::Data_Healing, HealAmount);
		TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);
	}

	LastTickHealAmount    = HealAmount;
	CumulativeHealAmount += HealAmount;

	if (UWorld* World = Target->GetWorld())
	{
		LastHealTime = World->GetTimeSeconds();
	}
}

void UDEGA_Heal::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// Stop both channeling timers before tearing down the beam.
	if (const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : GetAvatarActorFromActorInfo())
	{
		if (UWorld* World = Avatar->GetWorld())
		{
			World->GetTimerManager().ClearTimer(HealTickTimerHandle);
			World->GetTimerManager().ClearTimer(BeamUpdateTimerHandle);
		}
	}

	CurrentTarget = nullptr;

	// Restore play rate then jump directly to the End section.
	// Loop was frozen at play rate 0 — using SetNextSection would require Loop to play
	// to completion before End begins, during which the ABP can cut the slot.
	// Jumping directly means End starts on the very next frame regardless of ABP state.
	if (CachedAnimInstance && Config.HealMontage)
	{
		// Switch Loop→End so the montage exits the loop and plays the outro at normal speed.
		CachedAnimInstance->Montage_SetNextSection(
			UDESupportAnimInstance::SectionLoop,
			UDESupportAnimInstance::SectionEnd,
			Config.HealMontage);

		// Clear ABP channeling state only after the End section finishes.
		if (CachedSupportAnimInstance)
		{
			UDESupportAnimInstance* SupportAnim = CachedSupportAnimInstance;
			FOnMontageEnded EndDelegate;
			EndDelegate.BindLambda([SupportAnim](UAnimMontage* /*Montage*/, bool /*bInterrupted*/)
			{
				SupportAnim->StopHealLoop();
			});
			CachedAnimInstance->Montage_SetEndDelegate(EndDelegate, Config.HealMontage);
		}
	}
	CachedAnimInstance = nullptr;
	CachedSupportAnimInstance = nullptr;

	if (HealBeamComponent)
	{
		HealBeamComponent->Deactivate();
		HealBeamComponent->DestroyComponent();
		HealBeamComponent = nullptr;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
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
	LastHealTime = -1.0f;
	UpdateHealBeam(nullptr);
}

bool UDEGA_Heal::HasInjuredAllyInRange(float HealthThreshold) const
{
	return FindNearestInjuredAlly(HealthThreshold) != nullptr;
}

ADEAgent* UDEGA_Heal::FindNearestInjuredAlly(float HealthThreshold) const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	const ADEAgent* OwnerChar = Cast<ADEAgent>(AvatarActor);
	if (!OwnerChar) return nullptr;

TArray<ADEAgent*> Candidates;

	// Use MatchManager to enumerate allies directly — perception only tracks enemies (sight sense)
	// and would miss same-team members in both training and inference mode.
	if (!CachedMatchManager)
	{
		CachedMatchManager = const_cast<UDEGA_Heal*>(this)->GetMatchManager();
	}
	if (CachedMatchManager)
	{
		TArray<ADEAgent*> TeamAgents = CachedMatchManager->GetTeamAgents(OwnerChar->GetTeamID_Implementation());
		for (ADEAgent* A : TeamAgents)
		{
			if (A && A != OwnerChar && A->IsAlive()) Candidates.Add(A);
		}
	}
	else
	{
		// Fallback for test mode (no MatchManager): scan all DECharacters with matching TeamID
		TArray<AActor*> AllChars;
		UGameplayStatics::GetAllActorsOfClass(OwnerChar->GetWorld(), ADEAgent::StaticClass(), AllChars);
		for (AActor* Actor : AllChars)
		{
			ADEAgent* A = Cast<ADEAgent>(Actor);
			if (A && A != OwnerChar && A->IsAlive() && A->GetTeamID_Implementation() == OwnerChar->GetTeamID_Implementation())
			{
				Candidates.Add(A);
			}
		}
	}

	ADEAgent* BestTarget = nullptr;
	float MinDistSq = FMath::Square(Config.Range);
	FVector MyLoc = OwnerChar->GetActorLocation();

	for (ADEAgent* Ally : Candidates)
	{
		// Use KINDA_SMALL_NUMBER epsilon to avoid treating an ally as injured when GAS
		// health is at e.g. 0.9999 due to float precision after healing to near-maximum.
		if (Ally->GetHealthPercentage() >= (HealthThreshold - KINDA_SMALL_NUMBER)) continue;

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
	ADEAgent* OwnerChar = Cast<ADEAgent>(const_cast<AActor*>(AvatarActor));

	if (!Target)
	{
		if (HealBeamComponent)
		{
			HealBeamComponent->Deactivate();
		}
		return;
	}

	// Spawn the beam component unattached so BeamStart/BeamEnd are unambiguously world-space.
	// Attaching to the character root caused the Niagara simulation to apply the character's
	// transform on top of the already-world-space position parameters, making all beams converge.
	if (!HealBeamComponent && !Config.HealBeamSystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("DEGA_Heal: HealBeamSystem is null on %s — assign a Niagara beam system in DA_AbilityConfig HealConfig."),
			OwnerChar ? *OwnerChar->GetName() : TEXT("Unknown"));
	}
	const bool bJustSpawned = !HealBeamComponent;
	if (bJustSpawned && Config.HealBeamSystem && OwnerChar)
	{
		HealBeamComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			OwnerChar->GetWorld(),
			Config.HealBeamSystem,
			OwnerChar->GetActorLocation(),
			FRotator::ZeroRotator,
			FVector::OneVector,
			/*bAutoDestroy=*/false,
			/*bAutoActivate=*/false,
			ENCPoolMethod::None);
	}

	if (HealBeamComponent && OwnerChar)
	{
		// Use mesh socket for beam origin if available, otherwise fall back to actor location offset
		FVector BeamStart = OwnerChar->GetActorLocation() + FVector(0.f, 0.f, 90.f);
		if (!Config.HealBeamSocketName.IsNone())
		{
			if (USkeletalMeshComponent* Mesh = OwnerChar->GetMesh())
			{
				if (Mesh->DoesSocketExist(Config.HealBeamSocketName))
				{
					BeamStart = Mesh->GetSocketLocation(Config.HealBeamSocketName);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("DEGA_Heal: Socket '%s' not found on mesh — using fallback offset."),
						*Config.HealBeamSocketName.ToString());
				}
			}
		}

		const FVector BeamEnd = Target->GetActorLocation() + FVector(0.f, 0.f, 60.f);
		HealBeamComponent->SetVariableVec3(TEXT("User.BeamStart"), BeamStart);
		HealBeamComponent->SetVariableVec3(TEXT("User.BeamEnd"),   BeamEnd);

		// Only activate on first spawn — re-calling Activate() each frame re-triggers
		// emitters and causes visual overlap/stacking.
		if (bJustSpawned)
		{
			HealBeamComponent->Activate();
		}
	}
}

UDESupportAnimInstance* UDEGA_Heal::GetSupportAnimInstance() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	const ADEAgent* OwnerChar = Cast<ADEAgent>(AvatarActor);
	if (!OwnerChar) return nullptr;

	if (USkeletalMeshComponent* Mesh = OwnerChar->GetMesh())
	{
		return Cast<UDESupportAnimInstance>(Mesh->GetAnimInstance());
	}
	return nullptr;
}

ADEMatchManager* UDEGA_Heal::GetMatchManager() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	const ADEAgent* OwnerChar = Cast<ADEAgent>(AvatarActor);
	if (!OwnerChar) return nullptr;
	return OwnerChar->GetMatchManager();
}
