// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/DEProjectileBase.h"
#include "Characters/DEAgent.h"
#include "GAS/DEAttributeSet.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"


ADEProjectileBase::ADEProjectileBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	// Create collision component
	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitSphereRadius(CollisionRadius);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
	CollisionComponent->SetNotifyRigidBodyCollision(true);
	RootComponent = CollisionComponent;

	// Create projectile movement
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionComponent;
	ProjectileMovement->InitialSpeed = 0.0f; // Start at zero, set in InitializeProjectile
	ProjectileMovement->MaxSpeed = ProjectileSpeed;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->ProjectileGravityScale = 0.0f; // No gravity by default
	ProjectileMovement->bInitialVelocityInLocalSpace = false;
	ProjectileMovement->bAutoActivate = true; // Ensure component is active

	// Create mesh component (optional)
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(CollisionComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Create trail VFX component (optional)
	TrailVFX = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("TrailVFX"));
	TrailVFX->SetupAttachment(CollisionComponent);
	TrailVFX->bAutoActivate = false;

	// Set lifetime
	InitialLifeSpan = 0.0f; // We handle lifetime manually for better control
}

void ADEProjectileBase::BeginPlay()
{
	Super::BeginPlay();

	// Bind hit event
	if (CollisionComponent)
	{
		CollisionComponent->OnComponentHit.AddDynamic(this, &ADEProjectileBase::OnProjectileHit);
		CollisionComponent->SetSphereRadius(CollisionRadius);
	}

	// Set projectile max speed
	if (ProjectileMovement)
	{
		ProjectileMovement->MaxSpeed = ProjectileSpeed;
	}

	// Initialize state
	SpawnLocation = GetActorLocation();
	LastFrameLocation = SpawnLocation;
	DistanceTraveled = 0.0f;
	HitCount = 0;
	bIsActive = true;
	HitActors.Empty();

	// Start lifetime timer
	if (Lifetime > 0.0f)
	{
		GetWorldTimerManager().SetTimer(LifetimeTimerHandle, this, &ADEProjectileBase::OnLifetimeExpired, Lifetime, false);
	}

	// Activate trail VFX
	if (TrailVFX && TrailVFX->Template)
	{
		TrailVFX->Activate(true);
	}
}

void ADEProjectileBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsActive)
	{
		return;
	}

	// Update distance traveled
	FVector CurrentLocation = GetActorLocation();
	float FrameDistance = FVector::Dist(LastFrameLocation, CurrentLocation);
	DistanceTraveled += FrameDistance;
	LastFrameLocation = CurrentLocation;

	// Update homing
	if (bEnableHoming && HomingTarget && ProjectileMovement)
	{
		ProjectileMovement->bIsHomingProjectile = true;
		ProjectileMovement->HomingTargetComponent = HomingTarget->GetRootComponent();
		ProjectileMovement->HomingAccelerationMagnitude = HomingAcceleration;
	}

	// Debug drawing
	if (bEnableDebugDrawing)
	{
		DrawDebugSphere(GetWorld(), CurrentLocation, CollisionRadius, 8, FColor::Yellow, false, 0.0f, 0, 1.0f);
		DrawDebugLine(GetWorld(), SpawnLocation, CurrentLocation, FColor::Cyan, false, 0.0f, 0, 1.0f);

		FString DebugText = FString::Printf(TEXT("Dist: %.0fcm\nDmg: %.1f"), DistanceTraveled, CalculateDamageWithFalloff(DistanceTraveled));
		DrawDebugString(GetWorld(), CurrentLocation + FVector(0, 0, 30), DebugText, nullptr, FColor::White, 0.0f, true);
	}
}

//------------------------------------------------------------------------------
// PUBLIC API
//------------------------------------------------------------------------------

void ADEProjectileBase::InitializeProjectile(AActor* InOwner, AActor* InInstigator, float InBaseDamage, const FVector& InDirection)
{
	OwnerActor = InOwner;
	InstigatorActor = InInstigator;
	BaseDamage = InBaseDamage;

	// Copy EnvID from owner for parallel environment isolation
	if (ADEAgent* OwnerChar = Cast<ADEAgent>(InOwner))
	{
		EnvID = OwnerChar->GetEnvID_Implementation();
	}

	if (CollisionComponent)
	{
		// 투사체가 이동할 때 Owner(캐릭터)를 무시하도록 설정
		if (InOwner)
		{
			CollisionComponent->IgnoreActorWhenMoving(InOwner, true);

			// (선택 사항) 만약 Owner 측에서도 투사체를 확실히 무시하게 하려면:
			// Owner의 루트 컴포넌트(주로 캡슐)를 가져와서 설정해야 합니다.
			if (UPrimitiveComponent* OwnerRoot = Cast<UPrimitiveComponent>(InOwner->GetRootComponent()))
			{
				OwnerRoot->IgnoreActorWhenMoving(this, true);
			}
		}

		// Instigator(가해자)도 무시
		/*if (InInstigator && InInstigator != InOwner)
		{
			CollisionComponent->IgnoreActorWhenMoving(InInstigator, true);

			if (UPrimitiveComponent* InstigatorRoot = Cast<UPrimitiveComponent>(InInstigator->GetRootComponent()))
			{
				InstigatorRoot->IgnoreActorWhenMoving(this, true);
			}
		}*/
	}

	// 2. 속도 및 이동 설정
	if (ProjectileMovement)
	{
		FVector NormalizedDir = InDirection.GetSafeNormal();

		// 속도 값 강제 설정
		ProjectileMovement->InitialSpeed = ProjectileSpeed;
		ProjectileMovement->MaxSpeed = ProjectileSpeed;

		// 속도 벡터 직접 할당 (중요)
		ProjectileMovement->Velocity = NormalizedDir * ProjectileSpeed;

		// 컴포넌트 활성화 및 업데이트 대상 설정
		ProjectileMovement->SetUpdatedComponent(CollisionComponent);
		ProjectileMovement->UpdateComponentVelocity();
	}

	SetActorRotation(InDirection.Rotation());
}

void ADEProjectileBase::SetVelocity(const FVector& NewVelocity)
{
	if (ProjectileMovement)
	{
		ProjectileMovement->Velocity = NewVelocity;
		SetActorRotation(NewVelocity.Rotation());
	}
}

float ADEProjectileBase::GetRemainingLifetime() const
{
	if (Lifetime <= 0.0f)
	{
		return 999999.0f;
	}

	float Elapsed = GetWorldTimerManager().GetTimerElapsed(LifetimeTimerHandle);
	return FMath::Max(Lifetime - Elapsed, 0.0f);
}


//------------------------------------------------------------------------------
// COLLISION & DAMAGE
//------------------------------------------------------------------------------

void ADEProjectileBase::OnProjectileHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bIsActive || !OtherActor)
	{
		return;
	}

	if (OtherActor == this || OtherActor == OwnerActor || OtherActor == InstigatorActor)
	{
		return;
	}


	// Check if the hit actor is a damageable character
	ADEAgent* TargetChar = Cast<ADEAgent>(OtherActor);
	if (TargetChar)
	{
		if (!ValidateHit(OtherActor))
		{
			return;
		}
	}

	SpawnImpactEffects(Hit.ImpactPoint, Hit.ImpactNormal);

	if (TargetChar)
	{

		if (HitActors.Contains(OtherActor))
		{
			return;
		}

		ApplyDamageToActor(OtherActor, Hit.ImpactPoint, Hit.ImpactNormal);

		HitActors.Add(OtherActor);
		HitCount++;

		float FinalDamage = CalculateDamageWithFalloff(DistanceTraveled);
		FDEProjectileHitData HitData(OtherActor, Hit.ImpactPoint, Hit.ImpactNormal, DistanceTraveled, FinalDamage, true);
		OnProjectileHit_Delegate.Broadcast(HitData);

		if (bPenetrateActors && (MaxPenetrations == 0 || HitCount < MaxPenetrations))
		{
			UE_LOG(LogTemp, Log, TEXT("Projectile penetrated %s"), *OtherActor->GetName());
			return;
		}
	}

	if (bDestroyOnHit)
	{
		DeactivateProjectile();
	}
}

void ADEProjectileBase::OnLifetimeExpired()
{
	if (!bIsActive)
	{
		return;
	}

	/*UE_LOG(LogTemp, Verbose, TEXT("Projectile lifetime expired (%.1fs, %.0fcm traveled)"),
		Lifetime, DistanceTraveled);*/

	OnProjectileExpired_Delegate.Broadcast();
	DeactivateProjectile();
}

//------------------------------------------------------------------------------
// DAMAGE CALCULATION
//------------------------------------------------------------------------------

float ADEProjectileBase::CalculateDamageWithFalloff(float Distance) const
{
	if (!bEnableDamageFalloff || Distance <= FalloffStartDistance)
	{
		return BaseDamage;
	}

	if (Distance >= FalloffEndDistance)
	{
		return BaseDamage * MinDamageMultiplier;
	}

	// Linear interpolation between start and end
	float FalloffRange = FalloffEndDistance - FalloffStartDistance;
	float DistanceIntoFalloff = Distance - FalloffStartDistance;
	float FalloffAlpha = DistanceIntoFalloff / FalloffRange;

	float DamageMultiplier = FMath::Lerp(1.0f, MinDamageMultiplier, FalloffAlpha);
	return BaseDamage * DamageMultiplier;
}

bool ADEProjectileBase::ValidateHit(AActor* HitActor) const
{
	if (!HitActor)
	{
		return false;
	}

	// EnvID isolation: reject hits on agents from different environments
	if (const ADEAgent* HitChar = Cast<ADEAgent>(HitActor))
	{
		if (HitChar->GetEnvID_Implementation() != EnvID)
		{
			return false;
		}

		// Friendly fire check
		if (!bAllowFriendlyFire)
		{
			const ADEAgent* OwnerChar = Cast<ADEAgent>(OwnerActor);
			if (OwnerChar && HitChar->GetTeamID_Implementation() == OwnerChar->GetTeamID_Implementation())
			{
				return false;
			}
		}
	}
	else if (!bAllowFriendlyFire)
	{
		// Non-DEAgent target: legacy friendly fire check
		const ADEAgent* OwnerChar = Cast<ADEAgent>(OwnerActor);
		// No team comparison possible — allow hit
	}

	return true;
}

void ADEProjectileBase::ApplyDamageToActor(AActor* HitActor, const FVector& HitLocation, const FVector& HitNormal)
{
	if (!HitActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ ApplyDamageToActor called with null HitActor"));
		
		return;
	}

	// Calculate final damage
	float FinalDamage = CalculateDamageWithFalloff(DistanceTraveled);

	// Apply damage via GAS on the target character
	ADEAgent* TargetChar = Cast<ADEAgent>(HitActor);
	if (TargetChar)
	{
		float ActualDamage = TargetChar->ApplyDamageToSelf(FinalDamage, InstigatorActor, OwnerActor, HitLocation, HitNormal);

		// Notify owner that we dealt damage (for stats/rewards)
		if (ADEAgent* OwnerChar = Cast<ADEAgent>(OwnerActor))
		{
			OwnerChar->NotifyDamageDealt(HitActor, ActualDamage);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Projectile hit %s (not a DEAgent)"), *HitActor->GetName());
	}
}

//------------------------------------------------------------------------------
// VFX/SFX
//------------------------------------------------------------------------------

void ADEProjectileBase::SpawnImpactEffects(const FVector& ImpactLocation, const FVector& ImpactNormal)
{
	// Spawn impact VFX
	if (ImpactVFX)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ImpactVFX, ImpactLocation, ImpactNormal.Rotation());
	}

	// Play impact sound
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), ImpactSound, ImpactLocation);
	}
}

//------------------------------------------------------------------------------
// LIFECYCLE
//------------------------------------------------------------------------------

void ADEProjectileBase::DeactivateProjectile()
{
	if (!bIsActive)
	{
		return;
	}

	bIsActive = false;

	// Stop movement
	if (ProjectileMovement)
	{
		ProjectileMovement->Velocity = FVector::ZeroVector;
		ProjectileMovement->Deactivate();
	}

	// Deactivate trail VFX
	if (TrailVFX)
	{
		TrailVFX->Deactivate();
	}

	// Clear lifetime timer
	GetWorldTimerManager().ClearTimer(LifetimeTimerHandle);

	// Destroy actor (or return to pool if pooling is implemented)
	Destroy();
}
