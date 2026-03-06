// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/MocAttackAbility.h"
#include "Combat/ProjectileBase.h"
#include "Characters/MocCharacter.h"
#include "Team/MatchManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Kismet/KismetMathLibrary.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"

UMocAttackAbility::UMocAttackAbility()
{
}

void UMocAttackAbility::Initialize(AMocCharacter* InOwner)
{
	Super::Initialize(InOwner);

	if (OwnerCharacter)
	{
		CachedMeshComponent = OwnerCharacter->GetMesh();
	}
}

void UMocAttackAbility::SetConfig(const FAttackAbilityConfig& InConfig)
{
	Config = InConfig;
	CurrentAmmo = Config.bUseAmmo ? Config.MaxAmmo : 999;
}

void UMocAttackAbility::Execute(float DeltaTime)
{
	if (!CachedMatchManager)
	{
		CachedMatchManager = GetMatchManager();
	}

	if (!OwnerCharacter || !CanFire() || !CachedMatchManager) return;

	const FVector MyLocation = OwnerCharacter->GetActorLocation();
	const FVector EyeLocation = MyLocation + FVector(0, 0, 90);
	const float AttackRangeSq = FMath::Square(Config.Range);

	struct FEnemyData {
		AMocCharacter* Enemy;
		float DistSq;
	};
	TArray<FEnemyData> NearbyEnemies;

	TArray<AMocCharacter*> Enemies = CachedMatchManager->GetEnemyAgents(OwnerCharacter->GetTeamID_Implementation());

	for (AMocCharacter* Enemy : Enemies)
	{
		if (!Enemy || !Enemy->IsAlive_Implementation()) continue;

		float DistSq = FVector::DistSquared(Enemy->GetActorLocation(), MyLocation);
		if (DistSq <= AttackRangeSq)
		{
			NearbyEnemies.Add({ Enemy, DistSq });
		}
	}

	// Sort by closeness
	NearbyEnemies.Sort([](const FEnemyData& A, const FEnemyData& B) {
		return A.DistSq < B.DistSq;
	});

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerCharacter);

	for (const FEnemyData& Data : NearbyEnemies)
	{
		QueryParams.ClearIgnoredActors();
		QueryParams.AddIgnoredActor(OwnerCharacter);
		QueryParams.AddIgnoredActor(Data.Enemy);

		FHitResult HitResult;
		const bool bBlocked = OwnerCharacter->GetWorld()->LineTraceSingleByChannel(
			HitResult,
			EyeLocation,
			Data.Enemy->GetActorLocation() + FVector(0, 0, 90),
			ECC_Visibility,
			QueryParams
		);

		if (!bBlocked)
		{
			FireAtTarget(Data.Enemy);
			break;
		}
	}
}

void UMocAttackAbility::ExecuteWithTarget(float DeltaTime, AActor* Target)
{
	if (!Target || !CanFire()) return;
	FireAtTarget(Target);
}

bool UMocAttackAbility::CanFire() const
{
	if (!OwnerCharacter || !OwnerCharacter->IsAlive_Implementation()) return false;
	if (bIsReloading) return false;
	if (Config.bUseAmmo && CurrentAmmo <= 0) return false;

	if (OwnerCharacter->GetWorld())
	{
		float GameTime = OwnerCharacter->GetWorld()->GetTimeSeconds();
		if (GameTime < (LastFireTime + CurrentRequiredCooldown)) return false;
	}

	return true;
}

bool UMocAttackAbility::FireAtTarget(AActor* Target, bool bUsePredictiveAiming)
{
	if (!Target || !CanFire()) return false;

	FVector AimLocation = Target->GetActorLocation();
	if (bUsePredictiveAiming)
	{
		AimLocation = CalculatePredictedAimLocation(Target);
	}

	FVector MuzzleLocation = GetMuzzleLocation();
	FVector FireDirection = (AimLocation - MuzzleLocation).GetSafeNormal();

	return FireInternal(FireDirection, Target);
}

bool UMocAttackAbility::FireInternal(const FVector& FireDirection, AActor* Target)
{
	if (!CanFire()) return false;

	float RandomizedDamage = CalculateRandomizedDamage();
	FVector SpreadDirection = FireDirection;

	if (Config.Spread > 0.0f)
	{
		float AccuracyModifier = CalculateAccuracyModifier();
		float SpreadRadians = FMath::DegreesToRadians(Config.Spread * AccuracyModifier);
		SpreadDirection = UKismetMathLibrary::RandomUnitVectorInConeInRadians(FireDirection, SpreadRadians);
	}

	FVector MuzzleLocation = GetMuzzleLocation();
	AProjectileBase* Projectile = SpawnProjectile(MuzzleLocation, SpreadDirection, RandomizedDamage);

	if (!Projectile) return false;

	if (Config.bUseAmmo)
	{
		CurrentAmmo--;
		if (CurrentAmmo <= 0 && Config.ReloadTime > 0.0f)
		{
			bIsReloading = true;
			OwnerCharacter->GetWorldTimerManager().SetTimer(ReloadTimerHandle, this, &UMocAttackAbility::CompleteReload, Config.ReloadTime, false);
		}
	}

	CurrentRequiredCooldown = CalculateRandomizedCooldown();
	LastFireTime = OwnerCharacter->GetWorld()->GetTimeSeconds();

	if (Config.FireMontage && CachedMeshComponent)
	{
		if (UAnimInstance* AnimInstance = CachedMeshComponent->GetAnimInstance())
		{
			AnimInstance->Montage_Play(Config.FireMontage);
		}
	}

	return true;
}

AProjectileBase* UMocAttackAbility::SpawnProjectile(const FVector& FireLocation, const FVector& FireDirection, float ProjectileDamage)
{
	if (!Config.ProjectileClass || !OwnerCharacter->GetWorld()) return nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerCharacter;
	SpawnParams.Instigator = Cast<APawn>(OwnerCharacter);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AProjectileBase* Projectile = OwnerCharacter->GetWorld()->SpawnActor<AProjectileBase>(
		Config.ProjectileClass,
		FireLocation,
		FireDirection.Rotation(),
		SpawnParams
	);

	if (Projectile)
	{
		Projectile->InitializeProjectile(OwnerCharacter, OwnerCharacter, ProjectileDamage, FireDirection);
	}

	return Projectile;
}

void UMocAttackAbility::RefillAmmo()
{
	CurrentAmmo = Config.MaxAmmo;
	bIsReloading = false;
	OwnerCharacter->GetWorldTimerManager().ClearTimer(ReloadTimerHandle);
}

void UMocAttackAbility::AddAmmo(int32 Amount)
{
	CurrentAmmo = FMath::Clamp(CurrentAmmo + Amount, 0, Config.MaxAmmo);
}

float UMocAttackAbility::GetAmmoPercentage() const
{
	return Config.MaxAmmo > 0 ? static_cast<float>(CurrentAmmo) / static_cast<float>(Config.MaxAmmo) : 1.0f;
}

float UMocAttackAbility::GetCooldownProgress() const
{
	if (CurrentRequiredCooldown <= 0.0f) return 1.0f;
	float Remaining = GetRemainingCooldown();
	return FMath::Clamp(1.0f - (Remaining / CurrentRequiredCooldown), 0.0f, 1.0f);
}

float UMocAttackAbility::GetRemainingCooldown() const
{
	if (!OwnerCharacter || !OwnerCharacter->GetWorld()) return 0.0f;
	float GameTime = OwnerCharacter->GetWorld()->GetTimeSeconds();
	return FMath::Max(0.0f, (LastFireTime + CurrentRequiredCooldown) - GameTime);
}

float UMocAttackAbility::CalculateRandomizedDamage() const
{
	// Simple randomization for now
	return Config.Damage * FMath::RandRange(0.9f, 1.1f);
}

float UMocAttackAbility::CalculateRandomizedCooldown() const
{
	float BaseCooldown = Config.Speed > 0.0f ? 1.0f / Config.Speed : 1.0f;
	if (Config.RandomCycle <= 0.0f) return BaseCooldown;
	float Variance = BaseCooldown * Config.RandomCycle;
	return FMath::Max(FMath::RandRange(BaseCooldown - Variance, BaseCooldown + Variance), 0.01f);
}

float UMocAttackAbility::CalculateAccuracyModifier() const
{
	if (!OwnerCharacter) return 1.0f;
	UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement();
	if (!MoveComp) return 1.0f;

	bool bIsMoving = OwnerCharacter->GetVelocity().Size() > 10.0f;
	return bIsMoving ? 1.2f : 1.0f;
}

FVector UMocAttackAbility::GetMuzzleLocation() const
{
	if (CachedMeshComponent && CachedMeshComponent->DoesSocketExist(Config.MuzzleSocketName))
	{
		return CachedMeshComponent->GetSocketLocation(Config.MuzzleSocketName);
	}
	return OwnerCharacter ? OwnerCharacter->GetActorLocation() : FVector::ZeroVector;
}

FVector UMocAttackAbility::CalculatePredictedAimLocation(AActor* Target) const
{
	if (!Target) return FVector::ZeroVector;
	FVector TargetVelocity = Target->GetVelocity();
	return Target->GetActorLocation() + (TargetVelocity * 0.5f); // Constant lookahead
}

void UMocAttackAbility::CompleteReload()
{
	bIsReloading = false;
	CurrentAmmo = Config.MaxAmmo;
}
