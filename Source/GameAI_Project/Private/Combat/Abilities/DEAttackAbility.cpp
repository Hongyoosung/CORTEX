// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/DEAttackAbility.h"
#include "EngineUtils.h"
#include "Combat/DEProjectileBase.h"
#include "Characters/DECharacter.h"
#include "Team/DEMatchManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Kismet/KismetMathLibrary.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"

UDEAttackAbility::UDEAttackAbility()
{
}

void UDEAttackAbility::Initialize(ADECharacter* InOwner)
{
	Super::Initialize(InOwner);

	if (OwnerCharacter)
	{
		CachedMeshComponent = OwnerCharacter->GetMesh();
	}
}

void UDEAttackAbility::SetConfig(const FDEAttackAbilityConfig& InConfig)
{
	Config = InConfig;
	CurrentAmmo = Config.bUseAmmo ? Config.MaxAmmo : 999;
}

void UDEAttackAbility::Execute(float DeltaTime)
{
	if (!CachedMatchManager)
	{
		CachedMatchManager = GetMatchManager();
	}

	if (!OwnerCharacter || !CanFire() || !CachedMatchManager) return;

	const FVector MyLocation = OwnerCharacter->GetActorLocation();
	const FVector EyeLocation = MyLocation + FVector(0, 0, 90);
	const float AttackRangeSq = FMath::Square(Config.Range);
	const float MinRangeSq    = FMath::Square(Config.MinRange);

	struct FEnemyData {
		ADECharacter* Enemy;
		float DistSq;
	};
	TArray<FEnemyData> NearbyEnemies;

	TArray<ADECharacter*> Enemies = CachedMatchManager->GetEnemyAgents(OwnerCharacter->GetTeamID_Implementation());

	for (ADECharacter* Enemy : Enemies)
	{
		if (!Enemy || !Enemy->IsAlive()) continue;

		float DistSq = FVector::DistSquared(Enemy->GetActorLocation(), MyLocation);
		if (DistSq <= AttackRangeSq && DistSq >= MinRangeSq)
		{
			NearbyEnemies.Add({ Enemy, DistSq });
		}
	}

	// Sort by closeness
	NearbyEnemies.Sort([](const FEnemyData& A, const FEnemyData& B) {
		return A.DistSq < B.DistSq;
	});

	// Collect cross-environment characters once to exclude from LOS checks.
	// In multi-env training, their capsules block ECC_Visibility traces and prevent firing.
	const int32 MyEnvID = OwnerCharacter->GetEnvID_Implementation();
	TArray<AActor*> CrossEnvActors;
	for (TActorIterator<ADECharacter> It(OwnerCharacter->GetWorld()); It; ++It)
	{
		if ((*It)->GetEnvID_Implementation() != MyEnvID)
		{
			CrossEnvActors.Add(*It);
		}
	}

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerCharacter);
	QueryParams.AddIgnoredActors(CrossEnvActors);

	for (const FEnemyData& Data : NearbyEnemies)
	{
		QueryParams.ClearIgnoredActors();
		QueryParams.AddIgnoredActor(OwnerCharacter);
		QueryParams.AddIgnoredActor(Data.Enemy);
		QueryParams.AddIgnoredActors(CrossEnvActors);

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

void UDEAttackAbility::ExecuteWithTarget(float DeltaTime, AActor* Target)
{
	if (!Target || !CanFire()) return;
	FireAtTarget(Target);
}

bool UDEAttackAbility::CanFire() const
{
	if (!OwnerCharacter || !OwnerCharacter->IsAlive()) return false;
	if (bIsReloading) return false;
	if (Config.bUseAmmo && CurrentAmmo <= 0) return false;

	if (OwnerCharacter->GetWorld())
	{
		float GameTime = OwnerCharacter->GetWorld()->GetTimeSeconds();
		if (GameTime < (LastFireTime + CurrentRequiredCooldown)) return false;
	}

	return true;
}

bool UDEAttackAbility::FireAtTarget(AActor* Target, bool bUsePredictiveAiming)
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

bool UDEAttackAbility::FireInternal(const FVector& FireDirection, AActor* Target)
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
	ADEProjectileBase* Projectile = SpawnProjectile(MuzzleLocation, SpreadDirection, RandomizedDamage);

	if (!Projectile) return false;

	if (Config.bUseAmmo)
	{
		CurrentAmmo--;
		if (CurrentAmmo <= 0 && Config.ReloadTime > 0.0f)
		{
			bIsReloading = true;
			OwnerCharacter->GetWorldTimerManager().SetTimer(ReloadTimerHandle, this, &UDEAttackAbility::CompleteReload, Config.ReloadTime, false);
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

ADEProjectileBase* UDEAttackAbility::SpawnProjectile(const FVector& FireLocation, const FVector& FireDirection, float ProjectileDamage)
{
	if (!Config.ProjectileClass || !OwnerCharacter->GetWorld()) return nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerCharacter;
	SpawnParams.Instigator = Cast<APawn>(OwnerCharacter);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ADEProjectileBase* Projectile = OwnerCharacter->GetWorld()->SpawnActor<ADEProjectileBase>(
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

void UDEAttackAbility::RefillAmmo()
{
	CurrentAmmo = Config.MaxAmmo;
	bIsReloading = false;
	OwnerCharacter->GetWorldTimerManager().ClearTimer(ReloadTimerHandle);
}

void UDEAttackAbility::AddAmmo(int32 Amount)
{
	CurrentAmmo = FMath::Clamp(CurrentAmmo + Amount, 0, Config.MaxAmmo);
}

float UDEAttackAbility::GetAmmoPercentage() const
{
	return Config.MaxAmmo > 0 ? static_cast<float>(CurrentAmmo) / static_cast<float>(Config.MaxAmmo) : 1.0f;
}

float UDEAttackAbility::GetCooldownProgress() const
{
	if (CurrentRequiredCooldown <= 0.0f) return 1.0f;
	float Remaining = GetRemainingCooldown();
	return FMath::Clamp(1.0f - (Remaining / CurrentRequiredCooldown), 0.0f, 1.0f);
}

float UDEAttackAbility::GetRemainingCooldown() const
{
	if (!OwnerCharacter || !OwnerCharacter->GetWorld()) return 0.0f;
	float GameTime = OwnerCharacter->GetWorld()->GetTimeSeconds();
	return FMath::Max(0.0f, (LastFireTime + CurrentRequiredCooldown) - GameTime);
}

float UDEAttackAbility::CalculateRandomizedDamage() const
{
	// Simple randomization for now
	return Config.Damage * FMath::RandRange(0.9f, 1.1f);
}

float UDEAttackAbility::CalculateRandomizedCooldown() const
{
	float BaseCooldown = Config.Speed > 0.0f ? 1.0f / Config.Speed : 1.0f;
	if (Config.RandomCycle <= 0.0f) return BaseCooldown;
	float Variance = BaseCooldown * Config.RandomCycle;
	return FMath::Max(FMath::RandRange(BaseCooldown - Variance, BaseCooldown + Variance), 0.01f);
}

float UDEAttackAbility::CalculateAccuracyModifier() const
{
	if (!OwnerCharacter) return 1.0f;
	UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement();
	if (!MoveComp) return 1.0f;

	bool bIsMoving = OwnerCharacter->GetVelocity().Size() > 10.0f;
	return bIsMoving ? 1.2f : 1.0f;
}

FVector UDEAttackAbility::GetMuzzleLocation() const
{
	if (CachedMeshComponent && CachedMeshComponent->DoesSocketExist(Config.MuzzleSocketName))
	{
		return CachedMeshComponent->GetSocketLocation(Config.MuzzleSocketName);
	}
	return OwnerCharacter ? OwnerCharacter->GetActorLocation() : FVector::ZeroVector;
}

FVector UDEAttackAbility::CalculatePredictedAimLocation(AActor* Target) const
{
	if (!Target) return FVector::ZeroVector;
	FVector TargetVelocity = Target->GetVelocity();
	return Target->GetActorLocation() + (TargetVelocity * 0.5f); // Constant lookahead
}

void UDEAttackAbility::CompleteReload()
{
	bIsReloading = false;
	CurrentAmmo = Config.MaxAmmo;
}
