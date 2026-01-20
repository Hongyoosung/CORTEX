// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actor/FollowerCharacter.h"
#include "Team/FollowerAgentComponent.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/WeaponComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"

AFollowerCharacter::AFollowerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create follower agent component
	FollowerAgentComponent = CreateDefaultSubobject<UFollowerAgentComponent>(TEXT("FollowerAgentComponent"));

	// Create state tree component
	StateTreeComponent = CreateDefaultSubobject<UFollowerStateTreeComponent>(TEXT("StateTreeComponent"));

	// Configure character movement for AI pathfinding
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
		MoveComp->MaxWalkSpeed = 600.0f;
		MoveComp->bUseControllerDesiredRotation = false;
	}

	// Don't rotate character based on controller - let movement component handle it
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}

void AFollowerCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Add Team.Ally tag for gameplay tag identification system
	Tags.AddUnique("Team.Ally");

	AAIController* AICtrl = Cast<AAIController>(GetController());
	UPathFollowingComponent* PathComp = AICtrl ? AICtrl->GetPathFollowingComponent() : nullptr;
}

void AFollowerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// DIAGNOSTIC: Track if controller gets lost during gameplay (only check every 60 frames to reduce spam)
	/*static int32 TickCounter = 0;
	if (++TickCounter % 60 == 0)
	{
		static TMap<AFollowerCharacter*, AController*> LastKnownController;
		AController* CurrentController = GetController();
		AController* InPreviousController = LastKnownController.FindRef(this);

		if (InPreviousController != CurrentController)
		{
			UE_LOG(LogTemp, Error, TEXT("[FollowerChar] ❌ CONTROLLER CHANGED on '%s'! Was='%s', Now='%s'"),
				*GetName(),
				*GetNameSafe(InPreviousController),
				*GetNameSafe(CurrentController));

			LastKnownController.Add(this, CurrentController);
		}
	}*/
}

//------------------------------------------------------------------------------
// COMBAT STATS (ICombatStatsInterface Implementation)
// Delegates to HealthComponent and WeaponComponent
//------------------------------------------------------------------------------

float AFollowerCharacter::GetHealthPercentage_Implementation() const
{
	UHealthComponent* HealthComp = FindComponentByClass<UHealthComponent>();
	return HealthComp ? HealthComp->GetHealthPercentage() * 100.0f : 0.0f;
}

bool AFollowerCharacter::IsAlive_Implementation() const
{
	UHealthComponent* HealthComp = FindComponentByClass<UHealthComponent>();
	return HealthComp ? HealthComp->IsAlive() : false;
}

float AFollowerCharacter::GetWeaponCooldown_Implementation() const
{
	UWeaponComponent* WeaponComp = FindComponentByClass<UWeaponComponent>();
	return WeaponComp ? WeaponComp->GetRemainingCooldown() : 0.0f;
}

bool AFollowerCharacter::CanFireWeapon_Implementation() const
{
	UWeaponComponent* WeaponComp = FindComponentByClass<UWeaponComponent>();
	return WeaponComp ? WeaponComp->CanFire() : false;
}
