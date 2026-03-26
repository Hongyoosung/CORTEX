// Copyright Epic Games, Inc. All Rights Reserved.

#include "GAS/Abilities/DEGA_Attack.h"
#include "GAS/DEGameplayTags.h"
#include "GAS/DEAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Combat/DEProjectileBase.h"
#include "Characters/DEAgent.h"
#include "Team/DEMatchManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Types/DEClassTypes.h"
#include "Data/DETeamData.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "AIController.h"

UDEGA_Attack::UDEGA_Attack()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(DEGameplayTags::Ability_Attack);
	SetAssetTags(AssetTags);

	ActivationBlockedTags.AddTag(DEGameplayTags::State_Dead);
	// CooldownGameplayEffectClass must be assigned via Blueprint (GE_Cooldown_Attack)

	// Allow activation via SendGameplayEventToActor (passes BB target through TriggerEventData)
	FAbilityTriggerData TriggerData;
	TriggerData.TriggerTag    = DEGameplayTags::Ability_Attack;
	TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(TriggerData);
}

bool UDEGA_Attack::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
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
	if (OwnerChar && OwnerChar->GetCommandedClass() == EDEClassType::Support)
	{
		return false;
	}

	return CanFire();
}

void UDEGA_Attack::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AActor* Target = nullptr;

	// If triggered via event, use the target from event data
	if (TriggerEventData && TriggerEventData->Target)
	{
		Target = const_cast<AActor*>(TriggerEventData->Target.Get());
	}
	else
	{
		// Training mode: auto-select best enemy via priority scoring
		Target = FindBestEnemy();
	}

	if (Target)
	{
		ADEAgent* OwnerChar = Cast<ADEAgent>(ActorInfo->AvatarActor.Get());
		if (OwnerChar)
		{
			if (AAIController* AIC = Cast<AAIController>(OwnerChar->GetController()))
			{
				AIC->SetFocus(Target);
			}
		}

		FireAtTarget(Target, ActorInfo);
	}
	else
	{
		ADEAgent* OwnerChar = Cast<ADEAgent>(ActorInfo->AvatarActor.Get());
		if (OwnerChar)
		{
			if (AAIController* AIC = Cast<AAIController>(OwnerChar->GetController()))
			{
				AIC->ClearFocus(EAIFocusPriority::Gameplay);
			}
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

bool UDEGA_Attack::CanFire() const
{
	if (Config.bUseAmmo && CurrentAmmo <= 0)
	{
		if (!bIsReloading)
		{
			// Ammo empty but reload not started — start it now (handles cases where
			// StartReload couldn't schedule a timer, e.g. during training).
			const_cast<UDEGA_Attack*>(this)->StartReload();
		}
		else if (Config.ReloadTime > 0.0f)
		{
			// Time-based fallback: complete reload if enough time has elapsed,
			// in case the timer callback never fires (common during training).
			const AActor* AvatarActor = GetAvatarActorFromActorInfo();
			if (AvatarActor && AvatarActor->GetWorld())
			{
				float GameTime = AvatarActor->GetWorld()->GetTimeSeconds();
				if (GameTime >= ReloadStartTime + Config.ReloadTime)
				{
					const_cast<UDEGA_Attack*>(this)->CompleteReload();
					return true;
				}
			}
		}
		return false;
	}

	if (bIsReloading) return false;

	// Manual cooldown check (complements GAS tag-based cooldown).
	// GetAvatarActorFromActorInfo() returns null when called outside an active
	// ActivateAbility context (e.g. BTTask_DEAttackAbility::TickTask calling
	// CanFire() directly). Fall back to GetWorld() via the UObject outer so the
	// cooldown is never skipped for Script AI agents.
	const UWorld* World = GetWorld();
	if (World)
	{
		float GameTime = World->GetTimeSeconds();
		float BaseCooldown = Config.Speed > 0.0f ? 1.0f / Config.Speed : 1.0f;
		if (GameTime < (LastFireTime + BaseCooldown)) return false;
	}

	return true;
}


bool UDEGA_Attack::IsTargetValid(AActor* Target) const
{
	const APawn* OwnerPawn = Cast<APawn>(GetAvatarActorFromActorInfo());
	if (!OwnerPawn || !Target) return false;

	// Alive check
	if (const ADEAgent* TargetChar = Cast<ADEAgent>(Target))
	{
		if (!TargetChar->IsAlive()) return false;
	}

	// Range check
	const float DistSq = FVector::DistSquared(OwnerPawn->GetActorLocation(), Target->GetActorLocation());
	if (Config.Range > 0.0f && DistSq > FMath::Square(Config.Range)) return false;
	if (Config.MinRange > 0.0f && DistSq < FMath::Square(Config.MinRange)) return false;

	// Line-of-sight check
	if (Config.bRequireLineOfSight)
	{
		const ADEAgent* SelfChar = Cast<ADEAgent>(OwnerPawn);
		const int32 MyEnvID = SelfChar ? SelfChar->GetEnvID_Implementation() : -1;

		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(OwnerPawn);
		QueryParams.AddIgnoredActor(Target);

		// Exclude characters from other training environments
		if (SelfChar)
		{
			for (TActorIterator<ADEAgent> It(OwnerPawn->GetWorld()); It; ++It)
			{
				if ((*It)->GetEnvID_Implementation() != MyEnvID)
				{
					QueryParams.AddIgnoredActor(*It);
				}
			}
		}

		FHitResult Hit;
		const FVector Start = OwnerPawn->GetPawnViewLocation();
		const FVector End = Target->GetActorLocation() + FVector(0.f, 0.f, 50.f);
		if (OwnerPawn->GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams))
		{
			return false;
		}
	}

	return true;
}

bool UDEGA_Attack::FireAtTarget(AActor* Target, const FGameplayAbilityActorInfo* ActorInfo)
{
	if (!Target) return false;

	ADEAgent* OwnerChar = Cast<ADEAgent>(ActorInfo->AvatarActor.Get());
	if (!OwnerChar) return false;

	FVector AimLocation = Target->GetActorLocation();
	FVector MuzzleLoc = GetMuzzleLocation();
	FVector FireDirection = (AimLocation - MuzzleLoc).GetSafeNormal();

	// Apply spread
	if (Config.Spread > 0.0f)
	{
		bool bIsMoving = OwnerChar->GetVelocity().Size() > 10.0f;
		float AccuracyMod = bIsMoving ? 1.2f : 1.0f;
		float SpreadRadians = FMath::DegreesToRadians(Config.Spread * AccuracyMod);
		FireDirection = UKismetMathLibrary::RandomUnitVectorInConeInRadians(FireDirection, SpreadRadians);
	}

	float RandomizedDamage = CalculateRandomizedDamage();
	ADEProjectileBase* Projectile = SpawnProjectile(MuzzleLoc, FireDirection, RandomizedDamage);
	if (!Projectile) return false;

	// Ammo management
	if (Config.bUseAmmo)
	{
		CurrentAmmo--;
		if (CurrentAmmo <= 0 && Config.ReloadTime > 0.0f)
		{
			StartReload();
		}
	}

	LastFireTime = OwnerChar->GetWorld()->GetTimeSeconds();

	// Fire animation — montage is stored in the ability config so it works in
	// both Test Mode (set on UDEAbilityData) and Training Mode (set via AbilityDataOverride).
	if (Config.AttackMontage)
	{
		if (USkeletalMeshComponent* Mesh = OwnerChar->GetMesh())
		{
			if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				AnimInstance->Montage_Play(Config.AttackMontage);
			}
		}
	}

	return true;
}

ADEProjectileBase* UDEGA_Attack::SpawnProjectile(const FVector& FireLocation, const FVector& FireDirection, float ProjectileDamage)
{
	if (!Config.ProjectileClass) return nullptr;

	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!AvatarActor || !AvatarActor->GetWorld()) return nullptr;

	ADEAgent* OwnerChar = Cast<ADEAgent>(const_cast<AActor*>(AvatarActor));

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerChar;
	SpawnParams.Instigator = OwnerChar;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ADEProjectileBase* Projectile = AvatarActor->GetWorld()->SpawnActor<ADEProjectileBase>(
		Config.ProjectileClass,
		FireLocation,
		FireDirection.Rotation(),
		SpawnParams
	);

	if (Projectile)
	{
		Projectile->InitializeProjectile(OwnerChar, OwnerChar, ProjectileDamage, FireDirection);
	}

	return Projectile;
}

float UDEGA_Attack::GetClassTargetPriority(const ADEAgent* Enemy) const
{
	if (!Enemy) return 0.0f;
	switch (Enemy->GetCommandedClass())
	{
	case EDEClassType::Support:  return 1.0f;  // Kill the healer first
	case EDEClassType::Strike:   return 0.6f;  // Squishy DPS second
	case EDEClassType::Vanguard: return 0.2f;  // Tanky, low priority
	default:                     return 0.5f;
	}
}

AActor* UDEGA_Attack::FindBestEnemy() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	const ADEAgent* OwnerChar = Cast<ADEAgent>(AvatarActor);
	if (!OwnerChar) return nullptr;

	if (!CachedMatchManager)
	{
		CachedMatchManager = const_cast<UDEGA_Attack*>(this)->GetMatchManager();
	}

	const int32 MyEnvID = OwnerChar->GetEnvID_Implementation();
	const bool bLogDiag = (MyEnvID == 0);


	if (!CachedMatchManager) return nullptr;

	const FVector MyLocation = OwnerChar->GetActorLocation();
	const FVector EyeLocation = MyLocation + FVector(0, 0, 90);
	const float AttackRange = Config.Range;
	const float AttackRangeSq = FMath::Square(AttackRange);
	const float MinRangeSq = FMath::Square(Config.MinRange);

	// Scoring weights
	constexpr float W_DIST  = 0.3f;  // Closer is better (but not dominant)
	constexpr float W_HP    = 0.4f;  // Lower HP = higher priority (finish kills)
	constexpr float W_CLASS = 0.3f;  // Support > Strike > Vanguard

	struct FEnemyCandidate { ADEAgent* Enemy; float Score; };
	TArray<FEnemyCandidate> Candidates;

	TArray<ADEAgent*> Enemies = CachedMatchManager->GetEnemyAgents(OwnerChar->GetTeamID_Implementation());

	for (ADEAgent* Enemy : Enemies)
	{
		if (!Enemy || !Enemy->IsAlive())
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(Enemy->GetActorLocation(), MyLocation);
		const float Dist = FMath::Sqrt(DistSq);

		if (DistSq > AttackRangeSq || DistSq < MinRangeSq)
		{
			continue;
		}

		const float InDist = FMath::Sqrt(DistSq);
		const float DistScore  = 1.0f - FMath::Clamp(InDist / AttackRange, 0.0f, 1.0f);
		const float HPScore    = 1.0f - FMath::Clamp(Enemy->GetHealthPercentage(), 0.0f, 1.0f);
		const float ClassScore = GetClassTargetPriority(Enemy);

		const float TotalScore = W_DIST * DistScore + W_HP * HPScore + W_CLASS * ClassScore;
		Candidates.Add({ Enemy, TotalScore });
	}

	// Sort by score descending (highest priority first)
	Candidates.Sort([](const FEnemyCandidate& A, const FEnemyCandidate& B) { return A.Score > B.Score; });

	// Collect cross-environment actors for LOS exclusion
	TArray<AActor*> CrossEnvActors;
	for (TActorIterator<ADEAgent> It(OwnerChar->GetWorld()); It; ++It)
	{
		if ((*It)->GetEnvID_Implementation() != MyEnvID)
		{
			CrossEnvActors.Add(*It);
		}
	}

	// Return first candidate with clear LOS
	for (const FEnemyCandidate& C : Candidates)
	{
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(OwnerChar);
		QueryParams.AddIgnoredActor(C.Enemy);
		QueryParams.AddIgnoredActors(CrossEnvActors);

		FHitResult HitResult;
		bool bBlocked = OwnerChar->GetWorld()->LineTraceSingleByChannel(
			HitResult, EyeLocation, C.Enemy->GetActorLocation() + FVector(0, 0, 90),
			ECC_Visibility, QueryParams);

		if (!bBlocked)
		{
			return C.Enemy;
		}
	}

	return nullptr;
}

FVector UDEGA_Attack::GetMuzzleLocation() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	const ADEAgent* OwnerChar = Cast<ADEAgent>(AvatarActor);
	if (!OwnerChar) return FVector::ZeroVector;

	if (USkeletalMeshComponent* Mesh = OwnerChar->GetMesh())
	{
		if (Mesh->DoesSocketExist(Config.MuzzleSocketName))
		{
			return Mesh->GetSocketLocation(Config.MuzzleSocketName);
		}
	}
	return OwnerChar->GetActorLocation();
}

float UDEGA_Attack::CalculateRandomizedDamage() const
{
	return Config.Damage * FMath::RandRange(0.9f, 1.1f);
}

float UDEGA_Attack::GetAmmoPercentage() const
{
	return Config.MaxAmmo > 0 ? static_cast<float>(CurrentAmmo) / static_cast<float>(Config.MaxAmmo) : 1.0f;
}

float UDEGA_Attack::GetCooldownProgress() const
{
	float BaseCooldown = Config.Speed > 0.0f ? 1.0f / Config.Speed : 1.0f;
	if (BaseCooldown <= 0.0f) return 1.0f;
	float Remaining = GetRemainingCooldown();
	return FMath::Clamp(1.0f - (Remaining / BaseCooldown), 0.0f, 1.0f);
}

float UDEGA_Attack::GetRemainingCooldown() const
{
	const UWorld* World = GetWorld();
	if (!World) return 0.0f;
	float GameTime = World->GetTimeSeconds();
	float BaseCooldown = Config.Speed > 0.0f ? 1.0f / Config.Speed : 1.0f;
	return FMath::Max(0.0f, (LastFireTime + BaseCooldown) - GameTime);
}

void UDEGA_Attack::RefillAmmo()
{
	CurrentAmmo = Config.MaxAmmo;
	bIsReloading = false;
	if (const AActor* AvatarActor = GetAvatarActorFromActorInfo())
	{
		const_cast<AActor*>(AvatarActor)->GetWorldTimerManager().ClearTimer(ReloadTimerHandle);
	}
}

void UDEGA_Attack::AddAmmo(int32 Amount)
{
	CurrentAmmo = FMath::Clamp(CurrentAmmo + Amount, 0, Config.MaxAmmo);
}

void UDEGA_Attack::ResetState()
{
	RefillAmmo();
	LastFireTime = -9999.0f;
	ReloadStartTime = -9999.0f;
}

void UDEGA_Attack::StartReload()
{
	if (bIsReloading) return;

	bIsReloading = true;

	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (AvatarActor && AvatarActor->GetWorld())
	{
		ReloadStartTime = AvatarActor->GetWorld()->GetTimeSeconds();
		const_cast<AActor*>(AvatarActor)->GetWorldTimerManager().SetTimer(
			ReloadTimerHandle, this, &UDEGA_Attack::CompleteReload, Config.ReloadTime, false);
	}
	else
	{
		// No valid actor — time-based fallback in CanFire() will complete the reload.
		// Use a sentinel so the reload completes after Config.ReloadTime from first CanFire check.
		ReloadStartTime = -9999.0f;
	}
}

void UDEGA_Attack::CompleteReload()
{
	bIsReloading = false;
	CurrentAmmo = Config.MaxAmmo;
}

ADEMatchManager* UDEGA_Attack::GetMatchManager() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	const ADEAgent* OwnerChar = Cast<ADEAgent>(AvatarActor);
	if (!OwnerChar) return nullptr;
	return OwnerChar->GetMatchManager();
}
