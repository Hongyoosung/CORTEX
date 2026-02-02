// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actor/FollowerCharacter.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/Components/TeamCommsComponent.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/Components/ContextBridgeComponent.h"
#include "Util/Components/VisualLoggerComponent.h"
#include "Combat/Components/HealthComponent.h"
#include "Combat/Components/WeaponComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"

AFollowerCharacter::AFollowerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	//--------------------------------------------------------------------------
	// v9.0: Create components (decomposed architecture)
	//--------------------------------------------------------------------------

	// Core components (v8.0)
	FollowerAgentComponent = CreateDefaultSubobject<UFollowerAgentComponent>(TEXT("FollowerAgentComponent"));
	StateTreeComponent = CreateDefaultSubobject<UFollowerStateTreeComponent>(TEXT("StateTreeComponent"));

	// Communication & Context (v9.0 Phase 3)
	TeamCommsComponent = CreateDefaultSubobject<UTeamCommsComponent>(TEXT("TeamCommsComponent"));
	ContextBridgeComponent = CreateDefaultSubobject<UContextBridgeComponent>(TEXT("ContextBridgeComponent"));

	// Debug visualization (optional - can be disabled in editor)
	VisualLoggerComponent = CreateDefaultSubobject<UVisualLoggerComponent>(TEXT("VisualLoggerComponent"));

	//--------------------------------------------------------------------------
	// Component Configuration
	//--------------------------------------------------------------------------

	// Team Comms: Auto-register with leader by tag
	if (TeamCommsComponent)
	{
		TeamCommsComponent->bAutoRegisterWithLeader = true;
		TeamCommsComponent->bEnableVerboseLogging = false;
	}

	// Context Bridge: Initialize with default tactical parameters [0.5, 0.5, 0.5, 0.5]
	if (ContextBridgeComponent)
	{
		ContextBridgeComponent->bEnableVerboseLogging = false;
		// Default tactical params are set in component constructor
	}

	// Visual Logger: Configure debug options (disabled by default)
	if (VisualLoggerComponent)
	{
		VisualLoggerComponent->bEnableDebugDrawing = false;  // Enable in editor for debugging
		VisualLoggerComponent->bDrawAgentInfo = true;
		VisualLoggerComponent->bDrawTacticalParams = true;
		VisualLoggerComponent->bDrawCombatInfo = true;
		VisualLoggerComponent->bDrawObjectives = true;
	}

	//--------------------------------------------------------------------------
	// Character Movement Configuration
	//--------------------------------------------------------------------------
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

	//--------------------------------------------------------------------------
	// v8.0: Component Coordination - Subscribe HealthComponent death events
	//--------------------------------------------------------------------------
	UHealthComponent* HealthComp = FindComponentByClass<UHealthComponent>();
	if (HealthComp && FollowerAgentComponent)
	{
		HealthComp->OnDeath.AddDynamic(this, &AFollowerCharacter::OnHealthComponentDeath);
		UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': Connected HealthComponent::OnDeath to FollowerAgentComponent coordination"),
			*GetName());
	}
	else
	{
		if (!HealthComp)
		{
			UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter v9.0] '%s': Missing HealthComponent! Death handling disabled."),
				*GetName());
		}
		if (!FollowerAgentComponent)
		{
			UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter v9.0] '%s': Missing FollowerAgentComponent! Death coordination disabled."),
				*GetName());
		}
	}

	//--------------------------------------------------------------------------
	// v9.0: Initialize communication and context components
	//--------------------------------------------------------------------------

	// Team Comms: Auto-registration handled by component's BeginPlay (bAutoRegisterWithLeader=true)
	if (TeamCommsComponent)
	{
		if (TeamCommsComponent->IsRegisteredWithLeader())
		{
			UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': Successfully registered with team leader"),
				*GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter v9.0] '%s': Failed to register with team leader - check TeamLeader tag"),
				*GetName());
		}
	}

	// Context Bridge: Ready for FollowerAgent writes and StateTree reads
	if (ContextBridgeComponent)
	{
		UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': ContextBridge initialized with default tactical params"),
			*GetName());
	}

	UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': All v9.0 components initialized"), *GetName());
}

void AFollowerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
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

//------------------------------------------------------------------------------
// COMPONENT COORDINATION (v8.0)
//------------------------------------------------------------------------------

void AFollowerCharacter::OnHealthComponentDeath(const FDeathEventData& DeathEvent)
{
	// Coordinate death event: HealthComponent → FollowerAgentComponent
	// This maintains loose coupling - components don't directly reference each other
	if (FollowerAgentComponent)
	{
		FollowerAgentComponent->MarkAsDead();
		UE_LOG(LogTemp, Display, TEXT("[FollowerCharacter] '%s': Coordinated death event to FollowerAgentComponent"),
			*GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter] '%s': Cannot coordinate death - FollowerAgentComponent missing!"),
			*GetName());
	}
}
