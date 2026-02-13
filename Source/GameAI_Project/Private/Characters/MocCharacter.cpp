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
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"


AMocCharacter::AMocCharacter()
	: Super()
	, HealthComponent(nullptr)
	, WeaponComponent(nullptr)
	, ScholaAgent(nullptr)
	, StimuliSource(nullptr)
	, BehaviorTree(nullptr)
	, VisionRange(3000.0f)
	, AgentID(0)
	, TeamID(-1)
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

	// Create Niagara VFX component
	TeamColorVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TeamColorVFX"));
	TeamColorVFX->SetupAttachment(RootComponent);
	TeamColorVFX->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));
	TeamColorVFX->bAutoActivate = true;

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


	if (UWorld* World = GetWorld())
	{
		if (GameMode = Cast<AMocGameMode>(World->GetAuthGameMode()))
		{
			if (TM = GameMode->GetTeamManager())
			{
				int32 MyTeamID = GetTeamID_Implementation();
				SquadCommander = TM->GetSquadCommander(MyTeamID);
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
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MocCharacter] Failed GetWorld"));
	}

	// Setup Niagara VFX
	if (TeamColorVFX && TeamColorVFXAsset)
	{
		TeamColorVFX->SetAsset(TeamColorVFXAsset);
		TeamColorVFX->Activate();
	}

	// Update team color VFX
	UpdateTeamColorVFX();
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
	return TeamID;
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

	// v10.2 FIX: Enhanced death logging to diagnose immediate death after spawn
	float TimeSinceSpawn = GetWorld() ? (GetWorld()->GetTimeSeconds() - SpawnTime) : -1.0f;
	float CurrentHealth = HealthComponent ? HealthComponent->GetHealthPercentage() : 0.0f;

	UE_LOG(LogTemp, Error, TEXT("[MocCharacter DEATH] %s died %.2fs after spawn - Health=%.1f%%, Killer=%s, Location=%s"),
		*GetName(),
		TimeSinceSpawn,
		CurrentHealth * 100.0f,
		DeathEvent.Killer ? *DeathEvent.Killer->GetName() : TEXT("None"),
		*GetActorLocation().ToString());

	if (TimeSinceSpawn < 1.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("[MocCharacter DEATH] ⚠️ PREMATURE DEATH - Agent died less than 1s after spawn! Possible initialization bug."));
	}
	float InTimeSinceSpawn = GetWorld() ? GetWorld()->GetTimeSeconds() - SpawnTime : -1.0f;
	UE_LOG(LogTemp, Error, TEXT("[MocCharacter DEATH] %s died %.2fs after spawn - Killer: %s, Health: %.1f, Location: %s"),
		*GetName(),
		InTimeSinceSpawn,
		DeathEvent.Killer ? *DeathEvent.Killer->GetName() : TEXT("UNKNOWN"),
		GetHealthPercentage_Implementation(),
		*GetActorLocation().ToString());

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
			KillerTeamID = Killer->GetTeamID_Implementation();
		}

		// Register kill (TeamManager broadcasts OnAgentKilled)
		TM->RegisterKill(KillerTeamID, GetTeamID_Implementation(), this);

		// Queue respawn
		TM->QueueRespawn(this, GetTeamID_Implementation());
	}
}

void AMocCharacter::ResetCharacter()
{
	UE_LOG(LogTemp, Log, TEXT("[MocCharacter] %s resetting for new episode"), *GetName());

	// 1. Reset health and combat components
	if (HealthComponent)
	{
		HealthComponent->ResetHealth();
	}

	if (WeaponComponent)
	{
		WeaponComponent->RefillAmmo();
	}

	// 2. Re-enable movement
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->SetMovementMode(MOVE_Walking);
	}

	// 3. Re-enable collision
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// 4. Disable ragdoll physics
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

	// 5. Clear dead flag BEFORE any AI/Schola callbacks
	// v10.2 FIX: Must be set before ResetAgent() and RunBehaviorTree()
	// so that ComputeStatus() sees an alive character if evaluated during reset chain
	bIsAlive = true;

	// v10.2 FIX: Track spawn time for death diagnostics
	SpawnTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	// v10.2 DEBUG: Log character state after reset
	float CurrentHealth = HealthComponent ? HealthComponent->GetHealthPercentage() : 0.0f;
	UE_LOG(LogTemp, Log, TEXT("[MocCharacter] %s reset complete - bIsAlive=%d, Health=%.1f%%, Location=%s"),
		*GetName(), bIsAlive, CurrentHealth * 100.0f, *GetActorLocation().ToString());

	// 6. Restart AI
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (BehaviorTree)
		{
			AI->RunBehaviorTree(BehaviorTree);
		}
	}

	// 7. Delegate to RL component
	if (ScholaAgent)
	{
		ScholaAgent->ResetAgent();
	}

	UE_LOG(LogTemp, Verbose, TEXT("[MocCharacter] %s reset complete"), *GetName());
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

float AMocCharacter::Heal_Implementation(float HealAmount)
{
	if (HealthComponent)
	{
		return HealthComponent->Heal(HealAmount);
	}

	return 0.0f;
}

float AMocCharacter::GetWeaponCooldown_Implementation() const
{
	if (WeaponComponent)
	{
		return WeaponComponent->GetCooldownProgress();
	}
	return 0.0f;
}

int32 AMocCharacter::AddAmmo_Implementation(int32 AmmoAmount)
{
	if (WeaponComponent)
	{
		WeaponComponent->AddAmmo(AmmoAmount);
	}
	
	return WeaponComponent->GetCurrentAmmo();;
}

float AMocCharacter::GetAmmoPercentage_Implementation() const
{
	if (WeaponComponent)
	{
		return WeaponComponent->GetAmmoPercentage();
	}

	return 0;
}

bool AMocCharacter::CanFireWeapon_Implementation() const
{
	if (WeaponComponent)
	{
		return WeaponComponent->CanFire();
	}
	return false;
}

//========================================
// Team Color VFX
//========================================

void AMocCharacter::UpdateTeamColorVFX()
{
	if (!TeamColorVFX || !TM)
	{
		return;
	}

	// Get team color from TeamManager configuration
	FTeamConfiguration TeamConfig = TM->GetTeamConfiguration(TeamID);
	FLinearColor TeamColor = TeamConfig.TeamColor;

	// Update Niagara color parameter
	TeamColorVFX->SetVariableLinearColor(VFXColorParameterName, TeamColor);

	UE_LOG(LogTemp, Verbose, TEXT("[MocCharacter] Agent %s (Team %d) VFX color updated to: R=%.2f, G=%.2f, B=%.2f"),
		*GetName(), TeamID, TeamColor.R, TeamColor.G, TeamColor.B);
}
