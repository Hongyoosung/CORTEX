// Copyright Epic Games, Inc. All Rights Reserved.

#include "GAS/Abilities/DEGA_Attack.h"
#include "GAS/DEGameplayTags.h"
#include "GAS/DEAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Combat/DEProjectileBase.h"
#include "Characters/DECharacter.h"
#include "Team/DEMatchManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "AIController.h"

UDEGA_Attack::UDEGA_Attack()
{
	AbilityTags.AddTag(DEGameplayTags::Ability_Attack);
	ActivationBlockedTags.AddTag(DEGameplayTags::State_Dead);
	// CooldownGameplayEffectClass must be assigned via Blueprint (GE_Cooldown_Attack)
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
		// Training mode: auto-select nearest enemy
		Target = FindNearestEnemy();
	}

	if (Target)
	{
		// Set AI focus so ControlRotation tracks the target (drives Aim Offset in AnimBP)
		if (APawn* OwnerPawn = Cast<APawn>(GetAvatarActorFromActorInfo()))
		{
			if (AAIController* AICtrl = Cast<AAIController>(OwnerPawn->GetController()))
			{
				AICtrl->SetFocus(Target);
			}
		}
		FireAtTarget(Target, ActorInfo);
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

bool UDEGA_Attack::CanFire() const
{
	if (bIsReloading) return false;
	if (Config.bUseAmmo && CurrentAmmo <= 0) return false;

	// Manual cooldown check (complements GAS tag-based cooldown)
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (AvatarActor && AvatarActor->GetWorld())
	{
		float GameTime = AvatarActor->GetWorld()->GetTimeSeconds();
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
	if (const ADECharacter* TargetChar = Cast<ADECharacter>(Target))
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
		const ADECharacter* SelfChar = Cast<ADECharacter>(OwnerPawn);
		const int32 MyEnvID = SelfChar ? SelfChar->GetEnvID_Implementation() : -1;

		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(OwnerPawn);
		QueryParams.AddIgnoredActor(Target);

		// Exclude characters from other training environments
		if (SelfChar)
		{
			for (TActorIterator<ADECharacter> It(OwnerPawn->GetWorld()); It; ++It)
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

	ADECharacter* OwnerChar = Cast<ADECharacter>(ActorInfo->AvatarActor.Get());
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

	// Fire animation
	if (Config.FireMontage)
	{
		if (USkeletalMeshComponent* Mesh = OwnerChar->GetMesh())
		{
			if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				AnimInstance->Montage_Play(Config.FireMontage);
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

	ADECharacter* OwnerChar = Cast<ADECharacter>(const_cast<AActor*>(AvatarActor));

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

AActor* UDEGA_Attack::FindNearestEnemy() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	const ADECharacter* OwnerChar = Cast<ADECharacter>(AvatarActor);
	if (!OwnerChar) return nullptr;

	if (!CachedMatchManager)
	{
		CachedMatchManager = const_cast<UDEGA_Attack*>(this)->GetMatchManager();
	}
	if (!CachedMatchManager) return nullptr;

	const FVector MyLocation = OwnerChar->GetActorLocation();
	const FVector EyeLocation = MyLocation + FVector(0, 0, 90);
	const float AttackRangeSq = FMath::Square(Config.Range);
	const float MinRangeSq = FMath::Square(Config.MinRange);
	const int32 MyEnvID = OwnerChar->GetEnvID_Implementation();

	struct FEnemyData { ADECharacter* Enemy; float DistSq; };
	TArray<FEnemyData> NearbyEnemies;

	TArray<ADECharacter*> Enemies = CachedMatchManager->GetEnemyAgents(OwnerChar->GetTeamID_Implementation());
	for (ADECharacter* Enemy : Enemies)
	{
		if (!Enemy || !Enemy->IsAlive()) continue;
		float DistSq = FVector::DistSquared(Enemy->GetActorLocation(), MyLocation);
		if (DistSq <= AttackRangeSq && DistSq >= MinRangeSq)
		{
			NearbyEnemies.Add({ Enemy, DistSq });
		}
	}

	NearbyEnemies.Sort([](const FEnemyData& A, const FEnemyData& B) { return A.DistSq < B.DistSq; });

	// Collect cross-environment actors for LOS exclusion
	TArray<AActor*> CrossEnvActors;
	for (TActorIterator<ADECharacter> It(OwnerChar->GetWorld()); It; ++It)
	{
		if ((*It)->GetEnvID_Implementation() != MyEnvID)
		{
			CrossEnvActors.Add(*It);
		}
	}

	for (const FEnemyData& Data : NearbyEnemies)
	{
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(OwnerChar);
		QueryParams.AddIgnoredActor(Data.Enemy);
		QueryParams.AddIgnoredActors(CrossEnvActors);

		FHitResult HitResult;
		bool bBlocked = OwnerChar->GetWorld()->LineTraceSingleByChannel(
			HitResult, EyeLocation, Data.Enemy->GetActorLocation() + FVector(0, 0, 90),
			ECC_Visibility, QueryParams);

		if (!bBlocked)
		{
			return Data.Enemy;
		}
	}

	return nullptr;
}

FVector UDEGA_Attack::GetMuzzleLocation() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	const ADECharacter* OwnerChar = Cast<ADECharacter>(AvatarActor);
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
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!AvatarActor || !AvatarActor->GetWorld()) return 0.0f;
	float GameTime = AvatarActor->GetWorld()->GetTimeSeconds();
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
}

void UDEGA_Attack::StartReload()
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!AvatarActor)
	{
		// Can't start timer without a valid actor; skip reload to avoid permanently blocking fire.
		return;
	}

	bIsReloading = true;
	const_cast<AActor*>(AvatarActor)->GetWorldTimerManager().SetTimer(
		ReloadTimerHandle, this, &UDEGA_Attack::CompleteReload, Config.ReloadTime, false);
}

void UDEGA_Attack::CompleteReload()
{
	bIsReloading = false;
	CurrentAmmo = Config.MaxAmmo;
}

ADEMatchManager* UDEGA_Attack::GetMatchManager() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	const ADECharacter* OwnerChar = Cast<ADECharacter>(AvatarActor);
	if (!OwnerChar) return nullptr;
	return OwnerChar->GetMatchManager();
}
