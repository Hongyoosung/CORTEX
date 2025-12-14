// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/WeaponComponent.h"
#include "Combat/ProjectileBase.h"
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

UWeaponComponent::UWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UWeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	// Initialize ammo
	CurrentAmmo = bUseAmmo ? StartingAmmo : MaxAmmo;

	// Initialize state
	CurrentCooldown = 0.0f;
	bIsReloading = false;
	bIsFiring = false;
	CurrentFireTarget = nullptr;
	ShotsFired = 0;

	// Cache mesh component
	CachedMeshComponent = FindOwnerMesh();

	// Validate configuration
	if (!ProjectileClass)
	{
		UE_LOG(LogTemp, Error, TEXT("⚠️ WeaponComponent on %s has no ProjectileClass set!"), *GetOwner()->GetName());
	}

	if (!CachedMeshComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ WeaponComponent on %s could not find SkeletalMeshComponent!"), *GetOwner()->GetName());
	}
	else if (!CachedMeshComponent->DoesSocketExist(MuzzleSocketName))
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ WeaponComponent on %s: Socket '%s' not found on mesh!"),
			*GetOwner()->GetName(), *MuzzleSocketName.ToString());
	}

	UE_LOG(LogTemp, Log, TEXT("🔫 WeaponComponent initialized: %s (Damage: %.1f±%.0f%%, FireRate: %.1f±%.0f%%, Ammo: %d/%d)"),
		*GetOwner()->GetName(), Damage, AttackPowerRandomWeight * 100.0f,
		AttackSpeed, AttackRandomCycle * 100.0f,
		CurrentAmmo, MaxAmmo);
}

void UWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Update cooldown
	UpdateCooldown(DeltaTime);

	// Update auto fire
	if (bIsFiring && FireMode == EWeaponFireMode::FullAuto)
	{
		UpdateAutoFire(DeltaTime);
	}

	// Debug drawing
	if (bEnableDebugDrawing)
	{
		DrawDebugInfo();
	}
}

//------------------------------------------------------------------------------
// FIRING
//------------------------------------------------------------------------------

bool UWeaponComponent::FireAtTarget(AActor* Target, bool bUsePrediction)
{
	if (!Target || !CanFire())
	{
		return false;
	}

	// Calculate aim location
	FVector AimLocation = Target->GetActorLocation();
	if (bUsePrediction && bUsePredictiveAiming)
	{
		AimLocation = CalculatePredictedAimLocation(Target);
	}

	// Calculate fire direction
	FVector MuzzleLocation = GetMuzzleLocation();
	FVector FireDirection = (AimLocation - MuzzleLocation).GetSafeNormal();

	return FireInternal(FireDirection, Target);
}

bool UWeaponComponent::FireInDirection(const FVector& Direction)
{
	if (!CanFire())
	{
		return false;
	}

	return FireInternal(Direction.GetSafeNormal(), nullptr);
}

bool UWeaponComponent::FireAtLocation(const FVector& Location)
{
	if (!CanFire())
	{
		return false;
	}

	FVector MuzzleLocation = GetMuzzleLocation();
	FVector FireDirection = (Location - MuzzleLocation).GetSafeNormal();

	return FireInternal(FireDirection, nullptr);
}

void UWeaponComponent::StartFiring(AActor* Target)
{
	bIsFiring = true;
	CurrentFireTarget = Target;

	UE_LOG(LogTemp, Log, TEXT("🔫 %s started firing at %s"),
		*GetOwner()->GetName(), Target ? *Target->GetName() : TEXT("Direction"));
}

void UWeaponComponent::StopFiring()
{
	bIsFiring = false;
	CurrentFireTarget = nullptr;

	UE_LOG(LogTemp, Log, TEXT("🔫 %s stopped firing"), *GetOwner()->GetName());
}

//------------------------------------------------------------------------------
// QUERIES
//------------------------------------------------------------------------------

bool UWeaponComponent::CanFire() const
{
	// Check basic conditions
	if (IsOnCooldown() || IsReloading())
	{
		return false;
	}

	// Check ammo
	if (bUseAmmo && !HasAmmo())
	{
		return false;
	}

	// Check projectile class
	if (!ProjectileClass)
	{
		return false;
	}

	return true;
}

//------------------------------------------------------------------------------
// AMMO & RELOAD
//------------------------------------------------------------------------------

void UWeaponComponent::Reload()
{
	if (!bUseAmmo || bIsReloading || CurrentAmmo >= MaxAmmo)
	{
		return;
	}

	bIsReloading = true;

	// Broadcast event
	OnWeaponReloadStarted.Broadcast();

	// Start reload timer
	GetWorld()->GetTimerManager().SetTimer(ReloadTimerHandle, this, &UWeaponComponent::CompleteReload, ReloadTime, false);

	UE_LOG(LogTemp, Log, TEXT("🔄 %s reloading... (%.1fs)"), *GetOwner()->GetName(), ReloadTime);
}

void UWeaponComponent::AddAmmo(int32 Amount)
{
	if (!bUseAmmo)
	{
		return;
	}

	CurrentAmmo = FMath::Clamp(CurrentAmmo + Amount, 0, MaxAmmo);
}

void UWeaponComponent::RefillAmmo()
{
	CurrentAmmo = MaxAmmo;
}

void UWeaponComponent::CompleteReload()
{
	bIsReloading = false;
	CurrentAmmo = MaxAmmo;

	// Broadcast event
	OnWeaponReloadCompleted.Broadcast();

	UE_LOG(LogTemp, Log, TEXT("✅ %s reload complete → %d/%d ammo"), *GetOwner()->GetName(), CurrentAmmo, MaxAmmo);
}

//------------------------------------------------------------------------------
// UTILITY
//------------------------------------------------------------------------------

FVector UWeaponComponent::GetMuzzleLocation() const
{
	if (CachedMeshComponent && CachedMeshComponent->DoesSocketExist(MuzzleSocketName))
	{
		return CachedMeshComponent->GetSocketLocation(MuzzleSocketName);
	}

	// Fallback to owner location
	return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

FRotator UWeaponComponent::GetMuzzleRotation() const
{
	if (CachedMeshComponent && CachedMeshComponent->DoesSocketExist(MuzzleSocketName))
	{
		return CachedMeshComponent->GetSocketRotation(MuzzleSocketName);
	}

	// Fallback to owner rotation
	return GetOwner() ? GetOwner()->GetActorRotation() : FRotator::ZeroRotator;
}

FVector UWeaponComponent::CalculatePredictedAimLocation(AActor* Target) const
{
	if (!Target)
	{
		return FVector::ZeroVector;
	}

	// Get target velocity
	FVector TargetVelocity = Target->GetVelocity();
	if (TargetVelocity.IsNearlyZero())
	{
		return Target->GetActorLocation(); // Target not moving
	}

	// Predict future position
	FVector PredictedLocation = Target->GetActorLocation() + (TargetVelocity * PredictionLookahead);

	return PredictedLocation;
}

void UWeaponComponent::DrawDebugInfo()
{
	if (!GetOwner())
	{
		return;
	}

	FVector MuzzleLocation = GetMuzzleLocation();
	FVector OwnerLocation = GetOwner()->GetActorLocation();

	// Draw muzzle location
	DrawDebugSphere(GetWorld(), MuzzleLocation, 10.0f, 8, FColor::Orange, false, 0.0f, 0, 2.0f);

	// Draw weapon info
	FString WeaponInfo = FString::Printf(TEXT("Weapon: %s\nCooldown: %.2fs\nAmmo: %d/%d"),
		IsOnCooldown() ? TEXT("Cooling") : (CanFire() ? TEXT("Ready") : TEXT("Not Ready")),
		GetRemainingCooldown(),
		CurrentAmmo, MaxAmmo);

	DrawDebugString(GetWorld(), OwnerLocation + FVector(0, 0, 120), WeaponInfo, nullptr, FColor::Cyan, 0.0f, true);

	// Draw fire direction if firing
	if (bIsFiring && CurrentFireTarget)
	{
		FVector TargetLocation = CurrentFireTarget->GetActorLocation();
		DrawDebugLine(GetWorld(), MuzzleLocation, TargetLocation, FColor::Red, false, 0.0f, 0, 2.0f);
	}
}

//------------------------------------------------------------------------------
// INTERNAL METHODS
//------------------------------------------------------------------------------

bool UWeaponComponent::FireInternal(const FVector& FireDirection, AActor* Target)
{
	if (!CanFire())
	{
		return false;
	}

	// Calculate randomized damage
	float RandomizedDamage = CalculateRandomizedDamage();

	// Apply weapon spread with movement/stance accuracy modifiers
	FVector SpreadDirection = FireDirection;
	if (WeaponSpread > 0.0f)
	{
		// Calculate accuracy modifier (lower = better accuracy = less spread)
		float AccuracyModifier = CalculateAccuracyModifier();
		float FinalSpread = WeaponSpread * AccuracyModifier;

		// Add random spread
		float SpreadRadians = FMath::DegreesToRadians(FinalSpread);
		FVector RandomCone = UKismetMathLibrary::RandomUnitVectorInConeInRadians(FireDirection, SpreadRadians);
		SpreadDirection = RandomCone;
	}

	// Get muzzle location
	FVector MuzzleLocation = GetMuzzleLocation();

	// Spawn projectile
	AProjectileBase* Projectile = SpawnProjectile(MuzzleLocation, SpreadDirection, RandomizedDamage);

	if (!Projectile)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ Failed to spawn projectile for %s"), *GetOwner()->GetName());
		return false;
	}

	// Update state
	if (bUseAmmo)
	{
		CurrentAmmo--;
		if (CurrentAmmo <= 0)
		{
			OnWeaponOutOfAmmo.Broadcast();
			if (bAutoReload)
			{
				Reload();
			}
		}
	}

	// Set cooldown
	CurrentCooldown = CalculateRandomizedCooldown();

	// Update stats
	ShotsFired++;

	// Create fire data
	FWeaponFireData FireData(Target, MuzzleLocation, SpreadDirection, RandomizedDamage, Projectile);

	// Broadcast event
	OnWeaponFired.Broadcast(FireData);

	// Play fire animation montage
	if (FireMontage && CachedMeshComponent)
	{
		if (UAnimInstance* AnimInstance = CachedMeshComponent->GetAnimInstance())
		{
			AnimInstance->Montage_Play(FireMontage, FireMontagePlayRate);

			// Jump to section if specified
			if (FireMontageSection != NAME_None)
			{
				AnimInstance->Montage_JumpToSection(FireMontageSection, FireMontage);
			}
		}
	}

	/*UE_LOG(LogTemp, Log, TEXT("🔫 %s fired at %s → Damage: %.1f, Spread: %.1f°, Cooldown: %.2fs"),
		*GetOwner()->GetName(),
		Target ? *Target->GetName() : TEXT("Direction"),
		RandomizedDamage, WeaponSpread, CurrentCooldown);*/

	return true;
}

AProjectileBase* UWeaponComponent::SpawnProjectile(const FVector& FireLocation, const FVector& FireDirection, float ProjectileDamage)
{
	if (!ProjectileClass || !GetWorld())
	{
		return nullptr;
	}

	// Setup spawn parameters
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = Cast<APawn>(GetOwner());
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Spawn projectile
	AProjectileBase* Projectile = GetWorld()->SpawnActor<AProjectileBase>(
		ProjectileClass,
		FireLocation,
		FireDirection.Rotation(),
		SpawnParams
	);

	if (Projectile)
	{
		// Initialize projectile
		Projectile->InitializeProjectile(GetOwner(), GetOwner(), ProjectileDamage, FireDirection);
	}

	return Projectile;
}

float UWeaponComponent::CalculateRandomizedDamage() const
{
	if (AttackPowerRandomWeight <= 0.0f)
	{
		return Damage;
	}

	// Calculate variance range
	float VarianceRange = Damage * AttackPowerRandomWeight;
	float MinDamage = FMath::Max(Damage - VarianceRange, Damage * MinDamageMultiplier);
	float MaxDamage = Damage + VarianceRange;

	// Random damage within range
	float RandomDamage = FMath::RandRange(MinDamage, MaxDamage);

	return RandomDamage;
}

float UWeaponComponent::CalculateRandomizedCooldown() const
{
	// Base cooldown from fire rate
	float BaseCooldown = GetTimeBetweenShots();

	if (AttackRandomCycle <= 0.0f)
	{
		return BaseCooldown;
	}

	// Calculate variance range
	float VarianceRange = BaseCooldown * AttackRandomCycle;
	float MinCooldown = BaseCooldown - VarianceRange;
	float MaxCooldown = BaseCooldown + VarianceRange;

	// Random cooldown within range
	float RandomCooldown = FMath::RandRange(MinCooldown, MaxCooldown);

	return FMath::Max(RandomCooldown, 0.01f); // Ensure positive cooldown
}

float UWeaponComponent::CalculateAccuracyModifier() const
{
	// Default modifier (baseline accuracy)
	float Modifier = 1.0f;

	// Get owner character
	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!OwnerChar)
	{
		return Modifier; // Not a character, use baseline
	}

	// Get movement component
	UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement();
	if (!MoveComp)
	{
		return Modifier;
	}

	// Check movement state
	bool bIsCrouching = MoveComp->IsCrouching();
	bool bIsMoving = OwnerChar->GetVelocity().Size() > 10.0f; // Moving if velocity > 10 cm/s

	// Apply accuracy modifiers based on stance and movement
	// Lower modifier = better accuracy = less spread
	if (bIsCrouching && !bIsMoving)
	{
		// Crouching stationary: +20% accuracy (0.8x spread)
		Modifier = 0.8f;
	}
	else if (bIsCrouching && bIsMoving)
	{
		// Crouching while moving: -10% accuracy (1.1x spread)
		Modifier = 1.1f;
	}
	else if (!bIsCrouching && !bIsMoving)
	{
		// Standing still: Baseline (1.0x spread)
		Modifier = 1.0f;
	}
	else // !bIsCrouching && bIsMoving
	{
		// Moving while standing: -20% accuracy (1.2x spread)
		Modifier = 1.2f;
	}

	return Modifier;
}

void UWeaponComponent::UpdateCooldown(float DeltaTime)
{
	if (CurrentCooldown > 0.0f)
	{
		CurrentCooldown -= DeltaTime;
		if (CurrentCooldown < 0.0f)
		{
			CurrentCooldown = 0.0f;
		}
	}
}

void UWeaponComponent::UpdateAutoFire(float DeltaTime)
{
	if (!CanFire())
	{
		return;
	}

	// Fire at target or in forward direction
	if (CurrentFireTarget)
	{
		FireAtTarget(CurrentFireTarget, bUsePredictiveAiming);
	}
	else
	{
		// Fire in owner's forward direction
		if (GetOwner())
		{
			FVector FireDirection = GetOwner()->GetActorForwardVector();
			FireInDirection(FireDirection);
		}
	}
}

USkeletalMeshComponent* UWeaponComponent::FindOwnerMesh() const
{
	if (!GetOwner())
	{
		return nullptr;
	}

	// Try to find skeletal mesh component
	USkeletalMeshComponent* MeshComp = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
	if (MeshComp)
	{
		return MeshComp;
	}

	// Try character mesh
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		return Character->GetMesh();
	}

	return nullptr;
}
