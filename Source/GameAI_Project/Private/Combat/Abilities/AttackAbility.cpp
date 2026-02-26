// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/AttackAbility.h"
#include "Characters/MocCharacter.h"
#include "Combat/Components/WeaponComponent.h"
#include "Team/TeamManager.h"

UAttackAbility::UAttackAbility()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UAttackAbility::BeginPlay()
{
	Super::BeginPlay();
	if (AMocCharacter* Owner = GetOwnerCharacter())
	{
		CachedTM = Owner->GetTeamManager();
	}
}

void UAttackAbility::ExecuteAbility(float DeltaTime)
{
	AMocCharacter* Owner = GetOwnerCharacter();
	if (!Owner) return;

	UWeaponComponent* Weapon = Owner->GetWeaponComponent();
	if (!Weapon || !Weapon->CanFire()) return;
	if (!CachedTM) return;

	AActor* ClosestEnemy = nullptr;
	float ClosestDistance = FLT_MAX;
	const FVector MyLocation = Owner->GetActorLocation();

	// Use TeamManager's cached enemy list — avoids GetAllActorsOfClass world scan
	TArray<AMocCharacter*> Enemies = CachedTM->GetEnemyAgents(Owner->TeamID);
	for (AMocCharacter* Enemy : Enemies)
	{
		if (!Enemy || !Enemy->IsAlive_Implementation()) continue;

		const float Distance = FVector::Dist(Enemy->GetActorLocation(), MyLocation);
		if (Distance > AttackRange) continue;

		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Owner);

		const bool bVisible = !Owner->GetWorld()->LineTraceSingleByChannel(
			HitResult,
			MyLocation + FVector(0, 0, 90),
			Enemy->GetActorLocation() + FVector(0, 0, 90),
			ECC_Visibility,
			QueryParams
		);

		if (bVisible && Distance < ClosestDistance)
		{
			ClosestEnemy = Enemy;
			ClosestDistance = Distance;
		}
	}

	if (ClosestEnemy)
	{
		Weapon->FireAtTarget(ClosestEnemy, true);
	}
}

void UAttackAbility::ExecuteAbilityWithTarget(float DeltaTime, AActor* Target)
{
	if (!Target) return;

	AMocCharacter* Owner = GetOwnerCharacter();
	if (!Owner) return;

	UWeaponComponent* Weapon = Owner->GetWeaponComponent();
	if (!Weapon || !Weapon->CanFire()) return;

	Weapon->FireAtTarget(Target, true);
}
