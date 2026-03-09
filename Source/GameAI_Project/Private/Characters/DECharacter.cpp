// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/DECharacter.h"
#include "Combat/Abilities/DEAttackAbility.h"
#include "Combat/Abilities/DEHealAbility.h"
#include "Combat/Components/DEHealthComponent.h"
#include "Schola/Components/DEScholaAgent.h"
#include "Schola/Actuators/DETacticalParameterActuator.h"
#include "Core/Subsystems/DERewardSubsystem.h"
#include "AI/EQS/DEEQSExecutor.h"


#include "Navigation/PathFollowingComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AISense_Sight.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Misc/Optional.h"


ADECharacter::ADECharacter()
	: Super()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create components
	DEHealthComponent = CreateDefaultSubobject<UDEHealthComponent>(TEXT("DEHealthComponent"));
	ScholaAgent =		CreateDefaultSubobject<UDEScholaAgent>(TEXT("ScholaAgent"));
	StimuliSource =		CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
	EQSExecutor =		CreateDefaultSubobject<UDEEQSExecutor>(TEXT("EQSExecutor"));
	AbilityComponent =	CreateDefaultSubobject<UDEAbilityComponent>(TEXT("AbilityComponent"));

	// Create third-person spectator camera (inactive during normal AI operation)
	CameraSpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraSpringArm"));
	CameraSpringArm->SetupAttachment(GetCapsuleComponent());
	CameraSpringArm->TargetArmLength = 350.0f;
	CameraSpringArm->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));
	CameraSpringArm->SetRelativeRotation(FRotator(-20.0f, 0.0f, 0.0f));
	CameraSpringArm->bUsePawnControlRotation = false;
	CameraSpringArm->bInheritYaw = true;
	CameraSpringArm->bInheritPitch = false;
	CameraSpringArm->bInheritRoll = false;
	CameraSpringArm->bDoCollisionTest = true;

	AgentCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("AgentCamera"));
	AgentCamera->SetupAttachment(CameraSpringArm, USpringArmComponent::SocketName);
	AgentCamera->bUsePawnControlRotation = false;


	// Configure DEHealthComponent
	if (DEHealthComponent)
	{
		DEHealthComponent->MaxHealth = 100.0f;
		DEHealthComponent->StartingHealth = 100.0f;
		DEHealthComponent->bEnableHealthRegen = false;
	}

	// Configure movement (Section 0.3 - Movement Speed)
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->MaxWalkSpeed = 600.0f; // 6 m/s
		MovementComp->bOrientRotationToMovement = true;

		// Prevent agents from stacking on each other
		MovementComp->bUseRVOAvoidance = true;
		MovementComp->AvoidanceConsiderationRadius = 120.f; // ~2× capsule radius
	}

	// Let movement component handle rotation, not controller
	bUseControllerRotationYaw = false;

	// Auto-possession by AI
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void ADECharacter::BeginPlay()
{
	Super::BeginPlay();

	// Subscribe to death event
	if (DEHealthComponent)
	{
		DEHealthComponent->OnDeath_Delegate.AddDynamic(this, &ADECharacter::OnDeath);
	}

	// Register perception stimuli
	if (StimuliSource)
	{
		StimuliSource->RegisterWithPerceptionSystem();
		StimuliSource->RegisterForSense(TSubclassOf<UAISense_Sight>());
	}

	RewardSubsystem = GetWorld()->GetSubsystem<UDERewardSubsystem>();



	const bool bIsTraining = ScholaAgent && ScholaAgent->CurrentMode == EDEAgentMode::Training;
	if (bIsTraining)
	{
		GetWorld()->GetTimerManager().SetTimer(
			TrainingAbilityTimerHandle,
			this,
			&ADECharacter::ProcessTrainingAbilities,
			0.2f, // 1초에 5번만 실행 (매 프레임 실행 대비 압도적인 성능 향상)
			true
		);
	}


	// Apply modifier for the initial commanded strategy
	ApplyStrategyStatModifiers(CommandedStrategy);


}

void ADECharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

//========================================
// Heal Interface pass-throughs (DERewardCalculator calls these)
//========================================

float ADECharacter::GetLastTickHealAmount() const
{
	return (AbilityComponent && AbilityComponent->GetHealAbility()) ? AbilityComponent->GetHealAbility()->GetLastTickHealAmount() : 0.0f;
}

bool ADECharacter::ConsumeHealBurst(float Threshold)
{
	return (AbilityComponent && AbilityComponent->GetHealAbility()) ? AbilityComponent->GetHealAbility()->ConsumeHealBurst(Threshold) : false;
}

//========================================
// Team Identification
//========================================

int32 ADECharacter::GetTeamID_Implementation() const
{
	return TeamID;
}

int32 ADECharacter::GetEnvID_Implementation() const
{
	return EnvID;
}

void ADECharacter::SetTeamID_Implementation(int32 NewTeamID)
{
	TeamID = NewTeamID;
}

void ADECharacter::SetEnvID_Implementation(int32 NewEnvID)
{
	EnvID = NewEnvID;
}

bool ADECharacter::IsAlive_Implementation() const
{
	return bIsAlive;
}

ADEMatchManager* ADECharacter::GetMatchManager() const
{
	// Look up the DEMatchManager for this environment
	TArray<AActor*> MatchManagers;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADEMatchManager::StaticClass(), MatchManagers);

	for (AActor* Actor : MatchManagers)
	{
		if (ADEMatchManager* MM = Cast<ADEMatchManager>(Actor))
		{
			if (MM->GetEnvID() == EnvID)
			{
				return MM;
			}
		}
	}
	return nullptr;
}



//========================================
// Death & Respawn
//========================================

void ADECharacter::OnDeath(const FDEDeathEventData& DeathEvent)
{
	bIsAlive = false;

	// v10.2 FIX: Enhanced death logging to diagnose immediate death after spawn
	float TimeSinceSpawn = GetWorld() ? (GetWorld()->GetTimeSeconds() - SpawnTime) : -1.0f;
	float CurrentHealth = DEHealthComponent ? DEHealthComponent->GetHealthPercentage() : 0.0f;

	UE_LOG(LogTemp, Error, TEXT("[DECharacter DEATH] %s died %.2fs after spawn - Health=%.1f%%, Killer=%s, Location=%s"),
		*GetName(),
		TimeSinceSpawn,
		CurrentHealth * 100.0f,
		DeathEvent.Killer ? *DeathEvent.Killer->GetName() : TEXT("None"),
		*GetActorLocation().ToString());

	if (TimeSinceSpawn < 1.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("[DECharacter DEATH] PREMATURE DEATH - Agent died less than 1s after spawn! Possible initialization bug."));
	}

	// Stop weapon immediately (prevent invisible dead agents from firing)
	if (AbilityComponent && AbilityComponent->GetAttackAbility())
	{
		// Note: Use a StopFiring method if implemented, or just let it be.
		// For now we assume CanFire check in TickAbility/Execute handles this.
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

	OnAgentDied_Delegate.Broadcast(this, Cast<ADECharacter>(DeathEvent.Killer));


	// broadcast death event for any other listeners (e.g. UI, audio)
	OnAgentDeathEvent_Delegate.Broadcast(DeathEvent);
}

void ADECharacter::ResetCharacter()
{
	UE_LOG(LogTemp, Log, TEXT("[DECharacter] %s resetting for new episode"), *GetName());

	// 0. Undo deactivation from QueueRespawn() — ensure actor is fully visible and ticking
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(true);

	// 1. Reset health, combat, and reward components
	if (DEHealthComponent)
	{
		DEHealthComponent->ResetHealth();
	}

	RewardState.Reset();

	if (AbilityComponent)
	{
		AbilityComponent->ResetAbilities();
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
	float CurrentHealth = DEHealthComponent ? DEHealthComponent->GetHealthPercentage() : 0.0f;
	UE_LOG(LogTemp, Log, TEXT("[DECharacter] %s reset complete - bIsAlive=%d, Health=%.1f%%, Location=%s"),
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
	UE_LOG(LogTemp, Log, TEXT("[DECharacter] %s post-reset EQS state: bWeightsDirty=%d"),
		*GetName(), bWeightsDirty);

	UE_LOG(LogTemp, Verbose, TEXT("[DECharacter] %s reset complete"), *GetName());
}


void ADECharacter::Activate()
{
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(true);
	ResetCharacter();
}

void ADECharacter::Deactivate()
{
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);
}


//========================================
// QS Weight Storage & Execution
//========================================

void ADECharacter::UpdateTacticalWeights(const FDEEQSWeightParameters& NewWeights)
{
	CurrentEQSWeights = NewWeights;
	bWeightsDirty = true;
}

void ADECharacter::PerformTacticalAction()
{
	// Dead agents must not act (Schola may still send actions while agent awaits group respawn)
	if (!bIsAlive)
	{
		return;
	}

	if (!EQSExecutor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DECharacter] %s: EQSExecutor not available"), *GetName());
		return;
	}

	AAIController* AICtrl = Cast<AAIController>(GetController());
	if (!AICtrl)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DECharacter] %s: No AIController found"), *GetName());
		return;
	}

	UBlackboardComponent* BB = AICtrl->GetBlackboardComponent();
	bool bIsTraining = ScholaAgent && ScholaAgent->CurrentMode == EDEAgentMode::Training;

	// DIAGNOSTIC: Log mode detection state — promoted to Warning so it's visible without log filters
	static int32 PerfTactDiagCount = 0;
	if (++PerfTactDiagCount <= 5)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DECharacter] %s PerformTacticalAction#%d: bIsTraining=%s (ScholaAgent=%s, Mode=%s) BB=%s → Branch=%s"),
			*GetName(), PerfTactDiagCount,
			bIsTraining ? TEXT("true") : TEXT("false"),
			ScholaAgent ? TEXT("Valid") : TEXT("NULL"),
			ScholaAgent ? *UEnum::GetValueAsString(ScholaAgent->CurrentMode) : TEXT("N/A"),
			BB ? TEXT("Valid") : TEXT("NULL"),
			(BB && !bIsTraining) ? TEXT("INFERENCE (EQS skipped!)") : TEXT("TRAINING (EQS will run)"));
	}

	if (BB && !bIsTraining)
	{
		// DIAGNOSTIC: This branch skips MoveTo — if hit during training, agent will freeze
		UE_LOG(LogTemp, Warning,
			TEXT("[DECharacter] %s: Entering INFERENCE branch during PerformTacticalAction — no MoveTo issued! ScholaAgent=%s, Mode=%s"),
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
		UE_LOG(LogTemp, Warning, TEXT("[DECharacter] %s: EQS query returned no result - using fallback movement"), *GetName());

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

void ADECharacter::SetCommandedStrategy(EDEStrategyType NewStrategy)
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

		ApplyStrategyStatModifiers(NewStrategy);

		UE_LOG(LogTemp, Log, TEXT("[DECharacter] Agent %d received strategy command: %s"),
			AgentID, *UEnum::GetValueAsString(NewStrategy));
	}
}

void ADECharacter::ApplyStrategyStatModifiers(EDEStrategyType Strategy)
{
	if (AbilityComponent && AbilityComponent->GetAttackAbility())
	{
		// Stat modifiers would ideally be handled via a unified system, but porting current logic:
		// However, the new system uses DataAssets. Modification of stats might need a different approach.
		// For now, we'll keep it as is if AttackAbility pointer is available.
	}
}

//========================================
// Combat Stats Interface
//========================================

float ADECharacter::GetHealthPercentage_Implementation() const
{
	if (DEHealthComponent)
	{
		return DEHealthComponent->GetHealthPercentage();
	}
	return 1.0f;
}

float ADECharacter::Heal_Implementation(float HealAmount)
{
	if (DEHealthComponent)
	{
		return DEHealthComponent->Heal(HealAmount);
	}

	return 0.0f;
}

float ADECharacter::GetWeaponCooldown_Implementation() const
{
	return (AbilityComponent && AbilityComponent->GetAttackAbility()) ? AbilityComponent->GetAttackAbility()->GetCooldownProgress() : 0.0f;
}

int32 ADECharacter::AddAmmo_Implementation(int32 AmmoAmount)
{
	if (AbilityComponent && AbilityComponent->GetAttackAbility())
	{
		AbilityComponent->GetAttackAbility()->AddAmmo(AmmoAmount);
		return AbilityComponent->GetAttackAbility()->GetCurrentAmmo();
	}
	return 0;
}

float ADECharacter::GetAmmoPercentage_Implementation() const
{
	return (AbilityComponent && AbilityComponent->GetAttackAbility()) ? AbilityComponent->GetAttackAbility()->GetAmmoPercentage() : 0.0f;
}

bool ADECharacter::CanFireWeapon_Implementation() const
{
	return (AbilityComponent && AbilityComponent->GetAttackAbility()) ? AbilityComponent->GetAttackAbility()->CanFire() : false;
}


//========================================
// Reward Interface (forwarding to UDERewardCalculator)
//========================================

float ADECharacter::ComputeStepReward(
	EDEStrategyType Strategy,
	const FDEObservation& Prev,
	const FDEObservation& Current,
	const FDEEQSWeightParameters& Action)
{
	if (RewardSubsystem = GetWorld()->GetSubsystem<UDERewardSubsystem>())
	{
		return RewardSubsystem->ComputeStepReward(this, RewardState, Strategy, Prev, Current, Action);
	}

	return 0.0f;
}



void ADECharacter::ProcessTrainingAbilities()
{
	if (!bIsAlive || !AbilityComponent) return;


	if (AbilityComponent->GetAttackAbility())
	{
		AbilityComponent->GetAttackAbility()->Execute(0.2f);
	}

	if (AbilityComponent->GetHealAbility())
	{
		if (CommandedStrategy == EDEStrategyType::Support)
		{
			AbilityComponent->GetHealAbility()->Execute(0.2f);
		}
		else
		{
			AbilityComponent->GetHealAbility()->ResetTickHeal();
		}
	}
}
