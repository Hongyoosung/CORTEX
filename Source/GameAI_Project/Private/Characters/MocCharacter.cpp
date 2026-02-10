// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/MocCharacter.h"
#include "Combat/Components/HealthComponent.h"
#include "Combat/Components/WeaponComponent.h"
#include "Schola/Components/ScholaMocAgent.h"
#include "Core/MocGameMode.h"
#include "Team/TeamManager.h"
#include "Team/SquadManager.h"
#include "Team/FogOfWarManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AISense_Sight.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"

AMocCharacter::AMocCharacter()
	: Super()
	, HealthComponent(nullptr)
	, WeaponComponent(nullptr)
	, ScholaAgent(nullptr)
	, StimuliSource(nullptr)
	, BehaviorTree(nullptr)
	, VisionRange(3000.0f)
	, AgentID(0)
	, bIsAlive(true)
	, CommandedStrategy(EStrategyType::Assault)
	, SquadCommander(nullptr)
	, GameMode(nullptr)
	, TM(nullptr)
	, FogManager(nullptr)
{
	PrimaryActorTick.bCanEverTick = true;

	// Create components
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	WeaponComponent = CreateDefaultSubobject<UWeaponComponent>(TEXT("WeaponComponent"));
	ScholaAgent = CreateDefaultSubobject<UScholaMocAgent>(TEXT("ScholaAgent"));
	StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));

	// Configure HealthComponent
	if (HealthComponent)
	{
		HealthComponent->MaxHealth = 100.0f;
		HealthComponent->StartingHealth = 100.0f;
		HealthComponent->bEnableHealthRegen = false;
	}

	// Configure WeaponComponent (Section 0.3 - Combat Parameters)
	if (WeaponComponent)
	{
		WeaponComponent->Damage = 15.0f;               // Base damage per shot
		WeaponComponent->AttackSpeed = 6.67f;          // Fire rate (shots/sec)
		WeaponComponent->bUseAmmo = true;
		WeaponComponent->MaxAmmo = 150;
		WeaponComponent->StartingAmmo = 150;
		WeaponComponent->WeaponSpread = 2.0f;          // Degrees
		WeaponComponent->bUsePredictiveAiming = false; // Let AI learn aiming
	}

	// Configure movement (Section 0.3 - Movement Speed)
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->MaxWalkSpeed = 600.0f; // 6 m/s
	}

	// Auto-possession by AI
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void AMocCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Subscribe to death event
	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AMocCharacter::OnDeath);
	}

	// Register perception stimuli
	if (StimuliSource)
	{
		StimuliSource->RegisterWithPerceptionSystem();
		StimuliSource->RegisterForSense(TSubclassOf<UAISense_Sight>());
	}

	// Start behavior tree
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (BehaviorTree)
		{
			AI->RunBehaviorTree(BehaviorTree);
		}
	}

	// v10.2: Find Squad Commander reference
	if (UWorld* World = GetWorld())
	{
		if (GameMode = Cast<AMocGameMode>(World->GetAuthGameMode()))
		{
			if (TM = GameMode->GetTeamManager())
			{
				int32 MyTeamID = GetTeamID_Implementation();
				// TODO: Get Squad Commander from TeamManager
				// SquadCommander = TM->GetSquadCommander(MyTeamID);
			}
		}
	}


	TM = GameMode->GetTeamManager();
	if (!TM)
	{
		UE_LOG(LogTemp, Error, TEXT("[MocCharacter] Failed GetTeamManager"));
		return;
	}

	FogManager = TM->GetFogOfWarManager();
	if (!FogManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[MocCharacter] Failed GetFogManager"));
		return;
	}
}

void AMocCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Update fog-of-war vision ONLY if alive (FIXED: was inverted logic)
	if (!bIsAlive)
	{
		return; // Skip if dead
	}

	int32 MyTeamID = GetTeamID_Implementation();
	if (MyTeamID >= 0)
	{
		FVector MyLocation = GetActorLocation();
		FogManager->UpdateVision(MyTeamID, MyLocation, VisionRange);
	}
}

//========================================
// Team Identification
//========================================

int32 AMocCharacter::GetTeamID_Implementation() const
{
	// Check tags set by TeamManager
	if (Tags.Contains(FName("Team_0")))
	{
		return 0; // Red Team
	}

	if (Tags.Contains(FName("Team_1")))
	{
		return 1; // Blue Team
	}

	return -1; // No team
}

bool AMocCharacter::IsAlive_Implementation() const
{
	return bIsAlive;
}



//========================================
// Death & Respawn
//========================================

void AMocCharacter::OnDeath(const FDeathEventData& DeathEvent)
{
	bIsAlive = false;

	UE_LOG(LogTemp, Log, TEXT("[MocCharacter] %s died - Killer: %s"),
		*GetName(), DeathEvent.Killer ? *DeathEvent.Killer->GetName() : TEXT("None"));

	// Disable character movement
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->DisableMovement();
		MovementComp->StopMovementImmediately();
	}

	// Disable collision
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	
	// Stop AI
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (UBrainComponent* Brain = AI->BrainComponent)
		{
			Brain->StopLogic(TEXT("Death"));
		}
	}

	// Notify GameMode → TeamManager
	if (TM)
	{
		int32 KillerTeamID = -1;
		if (AMocCharacter* Killer = Cast<AMocCharacter>(DeathEvent.Killer))
		{
			KillerTeamID = Killer->GetTeamID();
		}

		// Register kill (TeamManager broadcasts OnAgentKilled)
		TM->RegisterKill(KillerTeamID, GetTeamID(), this);

		// Queue respawn
		TM->QueueRespawn(this, GetTeamID());
	}
}

void AMocCharacter::ResetCharacter()
{
	UE_LOG(LogTemp, Log, TEXT("[MocCharacter] %s respawning"), *GetName());

	// Reset health and ammo
	if (HealthComponent)
	{
		HealthComponent->ResetHealth();
	}


	// Re-enable movement
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->SetMovementMode(MOVE_Walking);
	}

	// Re-enable collision
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// Disable ragdoll physics
	if (USkeletalMeshComponent* InMesh = GetMesh())
	{
		InMesh->SetSimulatePhysics(false);
		InMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

		// Re-attach mesh to capsule
		InMesh->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);

		// Reset mesh transform
		InMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
		InMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	}

	// Restart AI
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (BehaviorTree)
		{
			AI->RunBehaviorTree(BehaviorTree);
		}
	}

	if (WeaponComponent)
	{
		WeaponComponent->RefillAmmo();
	}

	// Clear dead flag
	bIsAlive = true;
}

//========================================
// v10.2 Command Interface
//========================================

void AMocCharacter::SetCommandedStrategy(EStrategyType NewStrategy)
{
	if (CommandedStrategy != NewStrategy)
	{
		CommandedStrategy = NewStrategy;

		// 1. Update Schola Agent (for RL observation)
		if (ScholaAgent)
		{
			ScholaAgent->UpdateCommandedStrategy(NewStrategy);
		}

		// 2. Update Blackboard for Behavior Tree (for BT tasks)
		if (AAIController* AICtrl = Cast<AAIController>(GetController()))
		{
			if (UBlackboardComponent* BB = AICtrl->GetBlackboardComponent())
			{
				BB->SetValueAsEnum("CurrentStrategy", static_cast<uint8>(NewStrategy));
			}
		}

		UE_LOG(LogTemp, Log, TEXT("[MocCharacter] Agent %d received strategy command: %s"),
			AgentID, *UEnum::GetValueAsString(NewStrategy));
	}
}

//========================================
// Combat Stats Interface
//========================================

float AMocCharacter::GetHealthPercentage_Implementation() const
{
	if (HealthComponent)
	{
		return HealthComponent->GetHealthPercentage();
	}
	return 1.0f;
}

float AMocCharacter::GetWeaponCooldown_Implementation() const
{
	if (WeaponComponent)
	{
		return WeaponComponent->GetCooldownProgress();
	}
	return 0.0f;
}

bool AMocCharacter::CanFireWeapon_Implementation() const
{
	if (WeaponComponent)
	{
		return WeaponComponent->CanFire();
	}
	return false;
}
