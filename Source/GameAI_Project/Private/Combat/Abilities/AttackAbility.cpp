// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/AttackAbility.h"
#include "Combat/ProjectileBase.h"
#include "Characters/MocCharacter.h"
#include "Team/TeamManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

UAttackAbility::UAttackAbility()
{
	// 틱(Tick)은 디버그 드로잉용으로만 사용되므로 기본적으로 비활성화해도 무방합니다.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UAttackAbility::BeginPlay()
{
	Super::BeginPlay();

	CachedOwner = GetOwnerCharacter();
	if (CachedOwner)
	{
		CachedTM = CachedOwner->GetTeamManager();
	}

	CurrentAmmo = bUseAmmo ? StartingAmmo : MaxAmmo;
	bIsReloading = false;
	bIsFiring = false;
	CurrentFireTarget = nullptr;
	ShotsFired = 0;
	LastFireTime = -9999.0f;

	CachedMeshComponent = FindOwnerMesh();

	if (!ProjectileClass)
	{
		UE_LOG(LogTemp, Error, TEXT("AttackAbility on %s has no ProjectileClass set!"), *GetOwner()->GetName());
	}
}

void UAttackAbility::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// UpdateCooldown(DeltaTime) 호출 제거 - 성능 최적화

	if (bEnableDebugDrawing)
	{
		DrawDebugInfo();
	}
}

void UAttackAbility::ExecuteAbility(float DeltaTime)
{
	// CachedTM is set in BeginPlay, but component BeginPlay runs before AMocCharacter::BeginPlay
	// which is when TM is assigned. Lazy-init here to catch the first valid frame.
	if (!CachedTM && CachedOwner)
	{
		CachedTM = CachedOwner->GetTeamManager();
	}

	if (!CachedOwner || !CanFire() || !CachedTM) return;

	const FVector MyLocation = CachedOwner->GetActorLocation();
	const FVector EyeLocation = MyLocation + FVector(0, 0, 90);
	const float AttackRangeSq = FMath::Square(AttackRange);

	// 구조체를 활용하여 적과의 거리를 캐싱
	struct FEnemyData {
		AMocCharacter* Enemy;
		float DistSq;
	};
	TArray<FEnemyData> NearbyEnemies;

	TArray<AMocCharacter*> Enemies = CachedTM->GetEnemyAgents(CachedOwner->TeamID);

	// 1. 사거리 내의 적만 필터링 및 거리 제곱 계산
	for (AMocCharacter* Enemy : Enemies)
	{
		if (!Enemy || !Enemy->IsAlive_Implementation()) continue;

		float DistSq = FVector::DistSquared(Enemy->GetActorLocation(), MyLocation);
		if (DistSq <= AttackRangeSq)
		{
			NearbyEnemies.Add({ Enemy, DistSq });
		}
	}

	// 2. 거리가 가까운 순으로 정렬
	NearbyEnemies.Sort([](const FEnemyData& A, const FEnemyData& B) {
		return A.DistSq < B.DistSq;
		});

	// 3. 가장 가까운 적부터 순서대로 시야(LOS) 체크 후 발사
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(CachedOwner);

	for (const FEnemyData& Data : NearbyEnemies)
	{
		QueryParams.ClearIgnoredActors();
		QueryParams.AddIgnoredActor(CachedOwner);
		QueryParams.AddIgnoredActor(Data.Enemy);

		FHitResult HitResult;
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			HitResult,
			EyeLocation,
			Data.Enemy->GetActorLocation() + FVector(0, 0, 90),
			ECC_Visibility,
			QueryParams
		);

		// 시야가 확보되었다면 해당 적을 타겟으로 발사 후 즉시 종료 (가장 비싼 라인트레이스 최소화)
		if (!bBlocked)
		{
			FireAtTarget(Data.Enemy, bUsePredictiveAiming);
			break;
		}
	}
}

void UAttackAbility::ExecuteAbilityWithTarget(float DeltaTime, AActor* Target)
{
	if (!Target || !CachedOwner || !CanFire()) return;
	FireAtTarget(Target, bUsePredictiveAiming);
}

bool UAttackAbility::FireAtTarget(AActor* Target, bool bUsePrediction)
{
	if (!Target || !CanFire()) return false;

	FVector AimLocation = Target->GetActorLocation();
	if (bUsePrediction && bUsePredictiveAiming)
	{
		AimLocation = CalculatePredictedAimLocation(Target);
	}

	FVector MuzzleLocation = GetMuzzleLocation();
	FVector FireDirection = (AimLocation - MuzzleLocation).GetSafeNormal();

	return FireInternal(FireDirection, Target);
}

bool UAttackAbility::FireInDirection(const FVector& Direction)
{
	if (!CanFire()) return false;
	return FireInternal(Direction.GetSafeNormal(), nullptr);
}

bool UAttackAbility::FireAtLocation(const FVector& Location)
{
	if (!CanFire()) return false;
	FVector MuzzleLocation = GetMuzzleLocation();
	FVector FireDirection = (Location - MuzzleLocation).GetSafeNormal();
	return FireInternal(FireDirection, nullptr);
}

void UAttackAbility::StartFiring(AActor* Target)
{
	bIsFiring = true;
	CurrentFireTarget = Target;

	// 타이머가 이미 실행 중이 아니라면 시작 (예: 0.1초 간격으로 타겟 확인 및 사격 시도)
	if (GetWorld() && !GetWorld()->GetTimerManager().IsTimerActive(AutoFireTimerHandle))
	{
		GetWorld()->GetTimerManager().SetTimer(AutoFireTimerHandle, this, &UAttackAbility::ProcessAutoFire, 0.1f, true, 0.0f);
	}
}

void UAttackAbility::StopFiring()
{
	bIsFiring = false;
	CurrentFireTarget = nullptr;

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(AutoFireTimerHandle);
	}
}

// 타임스탬프 기반 쿨다운 체크로 변경
bool UAttackAbility::IsOnCooldown() const
{
	if (!GetWorld()) return false;
	return GetWorld()->GetTimeSeconds() < (LastFireTime + CurrentRequiredCooldown);
}

float UAttackAbility::GetRemainingCooldown() const
{
	if (!GetWorld()) return 0.0f;
	float Remaining = (LastFireTime + CurrentRequiredCooldown) - GetWorld()->GetTimeSeconds();
	return FMath::Max(Remaining, 0.0f);
}

float UAttackAbility::GetCooldownProgress() const
{
	if (CurrentRequiredCooldown <= 0.0f) return 1.0f;
	float Remaining = (LastFireTime + CurrentRequiredCooldown) - GetWorld()->GetTimeSeconds();
	float Progress = 1.0f - (FMath::Max(Remaining, 0.0f) / CurrentRequiredCooldown);
	return FMath::Clamp(Progress, 0.0f, 1.0f);
}

bool UAttackAbility::CanFire() const
{
	if (CachedOwner && !CachedOwner->IsAlive_Implementation()) return false;
	if (IsOnCooldown() || IsReloading()) return false;
	if (bUseAmmo && !HasAmmo()) return false;
	if (!ProjectileClass) return false;

	return true;
}

void UAttackAbility::Reload()
{
	if (!bUseAmmo || bIsReloading || CurrentAmmo >= MaxAmmo) return;

	bIsReloading = true;
	OnWeaponReloadStarted.Broadcast();
	GetWorld()->GetTimerManager().SetTimer(ReloadTimerHandle, this, &UAttackAbility::CompleteReload, ReloadTime, false);
}

void UAttackAbility::AddAmmo(int32 Amount)
{
	if (!bUseAmmo) return;
	CurrentAmmo = FMath::Clamp(CurrentAmmo + Amount, 0, MaxAmmo);
}

void UAttackAbility::RefillAmmo()
{
	CurrentAmmo = MaxAmmo;
}

void UAttackAbility::CompleteReload()
{
	bIsReloading = false;
	CurrentAmmo = MaxAmmo;
	OnWeaponReloadCompleted.Broadcast();
}

FVector UAttackAbility::GetMuzzleLocation() const
{
	if (CachedMeshComponent && CachedMeshComponent->DoesSocketExist(MuzzleSocketName))
	{
		return CachedMeshComponent->GetSocketLocation(MuzzleSocketName);
	}
	return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

FRotator UAttackAbility::GetMuzzleRotation() const
{
	if (CachedMeshComponent && CachedMeshComponent->DoesSocketExist(MuzzleSocketName))
	{
		return CachedMeshComponent->GetSocketRotation(MuzzleSocketName);
	}
	return GetOwner() ? GetOwner()->GetActorRotation() : FRotator::ZeroRotator;
}

FVector UAttackAbility::CalculatePredictedAimLocation(AActor* Target) const
{
	if (!Target) return FVector::ZeroVector;

	FVector TargetVelocity = Target->GetVelocity();
	if (TargetVelocity.IsNearlyZero()) return Target->GetActorLocation();

	return Target->GetActorLocation() + (TargetVelocity * PredictionLookahead);
}

void UAttackAbility::DrawDebugInfo()
{
	if (!GetOwner()) return;

	FVector MuzzleLocation = GetMuzzleLocation();
	FVector OwnerLocation = GetOwner()->GetActorLocation();

	DrawDebugSphere(GetWorld(), MuzzleLocation, 10.0f, 8, FColor::Orange, false, 0.0f, 0, 2.0f);

	FString Info = FString::Printf(TEXT("Cooldown: %.2fs\nAmmo: %d/%d\n%s"),
		GetRemainingCooldown(), CurrentAmmo, MaxAmmo,
		IsOnCooldown() ? TEXT("Cooling") : (CanFire() ? TEXT("Ready") : TEXT("Not Ready")));

	DrawDebugString(GetWorld(), OwnerLocation + FVector(0, 0, 120), Info, nullptr, FColor::Cyan, 0.0f, true);

	if (bIsFiring && CurrentFireTarget)
	{
		DrawDebugLine(GetWorld(), MuzzleLocation, CurrentFireTarget->GetActorLocation(), FColor::Red, false, 0.0f, 0, 2.0f);
	}
}

bool UAttackAbility::FireInternal(const FVector& FireDirection, AActor* Target)
{
	if (!CanFire()) return false;

	float RandomizedDamage = CalculateRandomizedDamage();

	FVector SpreadDirection = FireDirection;
	if (WeaponSpread > 0.0f)
	{
		float AccuracyModifier = CalculateAccuracyModifier();
		float SpreadRadians = FMath::DegreesToRadians(WeaponSpread * AccuracyModifier);
		SpreadDirection = UKismetMathLibrary::RandomUnitVectorInConeInRadians(FireDirection, SpreadRadians);
	}

	FVector MuzzleLocation = GetMuzzleLocation();
	AProjectileBase* Projectile = SpawnProjectile(MuzzleLocation, SpreadDirection, RandomizedDamage);

	if (!Projectile) return false;

	if (bUseAmmo)
	{
		CurrentAmmo--;
		if (CurrentAmmo <= 0)
		{
			OnWeaponOutOfAmmo.Broadcast();
			if (bAutoReload) Reload();
		}
	}

	// 틱(Tick) 오버헤드 없이 쿨다운 갱신
	CurrentRequiredCooldown = CalculateRandomizedCooldown();
	LastFireTime = GetWorld()->GetTimeSeconds();
	ShotsFired++;

	OnWeaponFired.Broadcast(FWeaponFireData(Target, MuzzleLocation, SpreadDirection, RandomizedDamage, Projectile));

	if (FireMontage && CachedMeshComponent)
	{
		if (UAnimInstance* AnimInstance = CachedMeshComponent->GetAnimInstance())
		{
			AnimInstance->Montage_Play(FireMontage, FireMontagePlayRate);
			if (FireMontageSection != NAME_None)
			{
				AnimInstance->Montage_JumpToSection(FireMontageSection, FireMontage);
			}
		}
	}

	return true;
}

AProjectileBase* UAttackAbility::SpawnProjectile(const FVector& FireLocation, const FVector& FireDirection, float ProjectileDamage)
{
	if (!ProjectileClass || !GetWorld()) return nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = Cast<APawn>(GetOwner());
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AProjectileBase* Projectile = GetWorld()->SpawnActor<AProjectileBase>(
		ProjectileClass,
		FireLocation,
		FireDirection.Rotation(),
		SpawnParams
	);

	if (Projectile)
	{
		Projectile->InitializeProjectile(GetOwner(), GetOwner(), ProjectileDamage, FireDirection);
	}

	return Projectile;
}

float UAttackAbility::CalculateRandomizedDamage() const
{
	if (AttackPowerRandomWeight <= 0.0f) return Damage;

	float VarianceRange = Damage * AttackPowerRandomWeight;
	float MinDamage = FMath::Max(Damage - VarianceRange, Damage * MinDamageMultiplier);
	float MaxDamage = Damage + VarianceRange;

	return FMath::RandRange(MinDamage, MaxDamage);
}

float UAttackAbility::CalculateRandomizedCooldown() const
{
	float BaseCooldown = GetTimeBetweenShots();
	if (AttackRandomCycle <= 0.0f) return BaseCooldown;

	float VarianceRange = BaseCooldown * AttackRandomCycle;
	return FMath::Max(FMath::RandRange(BaseCooldown - VarianceRange, BaseCooldown + VarianceRange), 0.01f);
}

float UAttackAbility::CalculateAccuracyModifier() const
{
	if (!CachedOwner) return 1.0f;

	UCharacterMovementComponent* MoveComp = CachedOwner->GetCharacterMovement();
	if (!MoveComp) return 1.0f;

	bool bIsCrouching = MoveComp->IsCrouching();
	bool bIsMoving = CachedOwner->GetVelocity().Size() > 10.0f;

	if (bIsCrouching && !bIsMoving) return 0.8f;
	if (bIsCrouching && bIsMoving)  return 1.1f;
	if (!bIsCrouching && bIsMoving) return 1.2f;
	return 1.0f;
}

USkeletalMeshComponent* UAttackAbility::FindOwnerMesh() const
{
	if (!GetOwner()) return nullptr;

	USkeletalMeshComponent* MeshComp = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
	if (MeshComp) return MeshComp;

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		return Character->GetMesh();
	}

	return nullptr;
}

void UAttackAbility::ProcessAutoFire()
{
	if (!bIsFiring) return;

	// 쿨다운, 장전 상태, 탄약 유무 체크
	if (!CanFire()) return;

	if (CurrentFireTarget)
	{
		// 명시된 타겟이 있으면 해당 타겟을 향해 발사 (Inference / BT 모드)
		FireAtTarget(CurrentFireTarget, bUsePredictiveAiming);
	}
	else
	{
		// 타겟이 없으면 자동 탐색 후 발사 (기존 ExecuteAbility 로직 재활용)
		ExecuteAbility(0.0f);
	}
}