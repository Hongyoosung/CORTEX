// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/MocCharacter.h"
#include "RL/Rewards/MocRewardCalculator.h"
#include "Combat/Components/HealthComponent.h"
#include "Combat/Components/WeaponComponent.h"
#include "Schola/Components/ScholaMocAgent.h"
#include "Schola/Actuators/TacticalParameterActuator.h"
#include "AI/EQS/MocEQSExecutor.h"
#include "Navigation/PathFollowingComponent.h"
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
#include "BrainComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Misc/Optional.h"


AMocCharacter::AMocCharacter()
	: Super()
	, HealthComponent(nullptr)
	, WeaponComponent(nullptr)
	, ScholaAgent(nullptr)
	, RewardCalculator(nullptr)
	, StimuliSource(nullptr)
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
	RewardCalculator = CreateDefaultSubobject<UMocRewardCalculator>(TEXT("RewardCalculator"));
	StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
	EQSExecutor = CreateDefaultSubobject<UMocEQSExecutor>(TEXT("EQSExecutor"));

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
		MovementComp->bOrientRotationToMovement = true;
	}

	// Let movement component handle rotation, not controller
	bUseControllerRotationYaw = false;

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

	// Skip tick logic if dead
	if (!bIsAlive)
	{
		return;
	}

	HandleCombat();
}

void AMocCharacter::HandleCombat()
{
	if (!WeaponComponent || !WeaponComponent->CanFire()) return;
	if (!TM) return;

	AActor* ClosestEnemy = nullptr;
	float ClosestDistance = FLT_MAX;
	const FVector MyLocation = GetActorLocation();

	// Use TeamManager's cached enemy list — avoids GetAllActorsOfClass world scan
	TArray<AMocCharacter*> Enemies = TM->GetEnemyAgents(TeamID);
	for (AMocCharacter* Enemy : Enemies)
	{
		if (!Enemy || !Enemy->IsAlive_Implementation()) continue;

		const float Distance = FVector::Dist(Enemy->GetActorLocation(), MyLocation);
		if (Distance > 8000.0f) continue;

		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		const bool bVisible = !GetWorld()->LineTraceSingleByChannel(
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
		WeaponComponent->FireAtTarget(ClosestEnemy, true);
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
		UE_LOG(LogTemp, Error, TEXT("[MocCharacter DEATH] PREMATURE DEATH - Agent died less than 1s after spawn! Possible initialization bug."));
	}

	// Stop weapon immediately (prevent invisible dead agents from firing)
	if (WeaponComponent)
	{
		WeaponComponent->StopFiring();
	}

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

	// Sparse RL rewards for death/kill
	if (RewardCalculator)
	{
		RewardCalculator->CalculateDeathPenalty(CommandedStrategy);
	}
	if (AMocCharacter* Killer = Cast<AMocCharacter>(DeathEvent.Killer))
	{
		if (Killer->RewardCalculator)
		{
			Killer->RewardCalculator->CalculateKillReward(Killer->CommandedStrategy);
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

		// Register kill (TeamManager broadcasts OnAgentKilled and queues respawn)
		TM->RegisterKill(KillerTeamID, GetTeamID_Implementation(), this);
	}
}

void AMocCharacter::ResetCharacter()
{
	UE_LOG(LogTemp, Log, TEXT("[MocCharacter] %s resetting for new episode"), *GetName());

	// 0. Undo deactivation from QueueRespawn() — ensure actor is fully visible and ticking
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(true);

	// 1. Reset health, combat, and reward components
	if (HealthComponent)
	{
		HealthComponent->ResetHealth();
	}

	if (RewardCalculator)
	{
		RewardCalculator->ResetEpisodeState();
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

	// 7. Delegate to RL component
	if (ScholaAgent)
	{
		ScholaAgent->ResetAgent();
	}

	// 8. DO NOT clear EQS weights or bWeightsDirty.
	//    The RL actuator may have sent weights while the agent was dead.
	//    Those weights are RL-generated (not hardcoded), so they don't corrupt training.
	//    Leaving bWeightsDirty as-is lets the agent move immediately after respawn
	//    using the last RL output, instead of freezing until the next RL step (~0.5s).
	//    The RL policy will send updated weights within one decision interval.
	//
	//    Previously we zeroed weights + cleared bWeightsDirty here, which caused
	//    post-respawn freeze: agents waited for RL but timing issues could cause
	//    permanent stalls in the Schola action pipeline.
	UE_LOG(LogTemp, Log, TEXT("[MocCharacter] %s post-reset EQS state: bWeightsDirty=%d"),
		*GetName(), bWeightsDirty);

	UE_LOG(LogTemp, Verbose, TEXT("[MocCharacter] %s reset complete"), *GetName());
}

//========================================
// v10.2 EQS Weight Storage & Execution
//========================================

void AMocCharacter::UpdateTacticalWeights(const FEQSWeightParameters& NewWeights)
{
	CurrentEQSWeights = NewWeights;
	bWeightsDirty = true;
}

void AMocCharacter::PerformTacticalAction()
{
	// Dead agents must not act (Schola may still send actions while agent awaits group respawn)
	if (!bIsAlive)
	{
		return;
	}

	if (!EQSExecutor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MocCharacter] %s: EQSExecutor not available"), *GetName());
		return;
	}

	AAIController* AICtrl = Cast<AAIController>(GetController());
	if (!AICtrl)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MocCharacter] %s: No AIController found"), *GetName());
		return;
	}

	UBlackboardComponent* BB = AICtrl->GetBlackboardComponent();
	bool bIsTraining = ScholaAgent && ScholaAgent->CurrentMode == EAgentMode::Training;

	// DIAGNOSTIC: Log mode detection state so freeze cause is visible in output log
	UE_LOG(LogTemp, Verbose,
		TEXT("[MocCharacter] %s PerformTacticalAction: bIsTraining=%s (ScholaAgent=%s, Mode=%s) BB=%s"),
		*GetName(),
		bIsTraining ? TEXT("true") : TEXT("false"),
		ScholaAgent ? TEXT("Valid") : TEXT("NULL"),
		ScholaAgent ? *UEnum::GetValueAsString(ScholaAgent->CurrentMode) : TEXT("N/A"),
		BB ? TEXT("Valid") : TEXT("NULL"));

	if (BB && !bIsTraining)
	{
		// DIAGNOSTIC: This branch skips MoveTo — if hit during training, agent will freeze
		UE_LOG(LogTemp, Warning,
			TEXT("[MocCharacter] %s: Entering INFERENCE branch during PerformTacticalAction — no MoveTo issued! ScholaAgent=%s, Mode=%s"),
			*GetName(),
			ScholaAgent ? TEXT("Valid") : TEXT("NULL"),
			ScholaAgent ? *UEnum::GetValueAsString(ScholaAgent->CurrentMode) : TEXT("N/A"));

		// Inference mode: sync weights to Blackboard, let BT handle EQS
		BB->SetValueAsFloat(TEXT("Weight_EnemyObj"), CurrentEQSWeights.EnemyObjectiveProximity);
		BB->SetValueAsFloat(TEXT("Weight_AllyObj"), CurrentEQSWeights.AllyObjectiveProximity);
		BB->SetValueAsFloat(TEXT("Weight_Cover"), CurrentEQSWeights.CoverDensity);
		BB->SetValueAsFloat(TEXT("Weight_EnemyVis"), CurrentEQSWeights.EnemyVisibility);
		BB->SetValueAsFloat(TEXT("Weight_AllyProx"), CurrentEQSWeights.AllyProximity);
		BB->SetValueAsFloat(TEXT("Weight_Range"), CurrentEQSWeights.CombatRange);
		BB->SetValueAsFloat(TEXT("Weight_Pickup"), CurrentEQSWeights.PickupProximity);
		return;
	}

	// Training mode: run synchronous EQS via executor, then navigate
	// Clear stale pathfinding state from death/respawn cycle.
	// OnDeath calls StopMovementImmediately + Brain->StopLogic, which can leave the
	// PathFollowingComponent in a state that silently rejects new MoveTo requests.
	AICtrl->StopMovement();

	TOptional<FVector> Result = EQSExecutor->ExecuteSynchronousQuery(CurrentEQSWeights);
	if (!Result.IsSet())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MocCharacter] %s: EQS query returned no result - using fallback movement"), *GetName());

		// Fallback: move to a random nearby point to unstick the agent
		const FVector CurrentLocation = GetActorLocation();
		const float FallbackRadius = 300.0f;
		const float RandomAngle = FMath::FRandRange(0.0f, 2.0f * PI);
		const FVector FallbackTarget = CurrentLocation + FVector(
			FMath::Cos(RandomAngle) * FallbackRadius,
			FMath::Sin(RandomAngle) * FallbackRadius,
			0.0f
		);

		LastEQSTargetLocation = FallbackTarget;

		FAIMoveRequest FallbackReq(FallbackTarget);
		FallbackReq.SetAcceptanceRadius(EQSAcceptanceRadius);
		FallbackReq.SetUsePathfinding(true);
		AICtrl->MoveTo(FallbackReq);
		return;
	}

	LastEQSTargetLocation = Result.GetValue();

	FAIMoveRequest MoveReq(LastEQSTargetLocation);
	MoveReq.SetAcceptanceRadius(EQSAcceptanceRadius);
	MoveReq.SetUsePathfinding(true);
	AICtrl->MoveTo(MoveReq);
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
	FLinearColor TeamColor = TeamConfig.GetTeamColor();

	// Update Niagara color parameter
	TeamColorVFX->SetVariableLinearColor(VFXColorParameterName, TeamColor);

	UE_LOG(LogTemp, Verbose, TEXT("[MocCharacter] Agent %s (Team %d) VFX color updated to: R=%.2f, G=%.2f, B=%.2f"),
		*GetName(), TeamID, TeamColor.R, TeamColor.G, TeamColor.B);
}

//========================================
// Reward Interface (forwarding to UMocRewardCalculator)
//========================================

float AMocCharacter::ComputeStepReward(
	EStrategyType Strategy,
	const FObservation& Prev,
	const FObservation& Current,
	const FEQSWeightParameters& Action)
{
	if (!RewardCalculator) return 0.0f;
	return RewardCalculator->ComputeStepReward(Strategy, Prev, Current, Action);
}

FRewardBreakdown AMocCharacter::ComputeRewardBreakdown(
	EStrategyType Strategy,
	const FObservation& Prev,
	const FObservation& Current) const
{
	if (!RewardCalculator) return FRewardBreakdown{};
	return RewardCalculator->ComputeRewardBreakdown(Strategy, Prev, Current);
}

void AMocCharacter::ResetRewardState()
{
	if (RewardCalculator)
	{
		RewardCalculator->ResetEpisodeState();
	}
}
