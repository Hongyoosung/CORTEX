// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/MocCharacter.h"
#include "Combat/Abilities/AttackAbility.h"
#include "Combat/Abilities/HealAbility.h"
#include "RL/Rewards/MocRewardCalculator.h"
#include "Combat/Components/HealthComponent.h"
#include "Schola/Components/ScholaMocAgent.h"
#include "Schola/Actuators/TacticalParameterActuator.h"
#include "AI/EQS/MocEQSExecutor.h"
#include "Navigation/PathFollowingComponent.h"
#include "Team/TeamManager.h"
#include "Schola/ScholaEnvironment.h"
#include "EngineUtils.h"
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
	, ScholaAgent(nullptr)
	, StimuliSource(nullptr)
	, TeamColorVFX(nullptr)
	, VisionRange(3000.0f)
	, AttackAbility(nullptr)
	, HealAbility(nullptr)
	, EQSExecutor(nullptr)
	, EQSAcceptanceRadius(50.0f)
	, TeamColorVFXAsset(nullptr)
	, VFXColorParameterName(FName("TeamColor"))
	, AgentID(0)
	, TeamID(-1)
	, bIsAlive(true)
	, CommandedStrategy(EStrategyType::Support)
	, bWeightsDirty(false)
	, LastEQSTargetLocation(FVector::ZeroVector)
	, SquadCommander(nullptr)
	, SpawnTime(0.0f)
	, TM(nullptr)
	, BaseAttackDamage(0.0f)
	, BaseAttackRange(0.0f)
	, BaseMaxHealth(0.0f)
{
	PrimaryActorTick.bCanEverTick = true;

	// Create components
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	ScholaAgent = CreateDefaultSubobject<UScholaMocAgent>(TEXT("ScholaAgent"));
	StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
	EQSExecutor = CreateDefaultSubobject<UMocEQSExecutor>(TEXT("EQSExecutor"));
	AttackAbility = CreateDefaultSubobject<UAttackAbility>(TEXT("AttackAbility"));
	HealAbility = CreateDefaultSubobject<UHealAbility>(TEXT("HealAbility"));

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


	// Get TeamManager: prefer direct reference (set by TeamManager::SpawnAgent),
	// fall back to ScholaEnvironment lookup for multi-env parallel architecture
	if (!TM)
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AScholaEnvironment> It(World); It; ++It)
			{
				if ((*It)->GetEnvId() == EnvID)
				{
					TM = (*It)->GetTeamManager();
					break;
				}
			}
		}
	}

	// Cache SquadCommander (may be null if TeamID not yet assigned)
	if (TM)
	{
		int32 MyTeamID = GetTeamID_Implementation();
		if (MyTeamID >= 0)
		{
			SquadCommander = TM->GetSquadCommander(MyTeamID);
		}
	}

	// Setup Niagara VFX
	if (TeamColorVFX && TeamColorVFXAsset)
	{
		TeamColorVFX->SetAsset(TeamColorVFXAsset);
		TeamColorVFX->Activate();
	}

	const bool bIsTraining = ScholaAgent && ScholaAgent->CurrentMode == EAgentMode::Training;
	if (bIsTraining)
	{
		GetWorld()->GetTimerManager().SetTimer(
			TrainingAbilityTimerHandle,
			this,
			&AMocCharacter::ProcessTrainingAbilities,
			0.2f, // 1초에 5번만 실행 (매 프레임 실행 대비 압도적인 성능 향상)
			true
		);
	}

	// Cache base stats for strategy-based modifiers
	if (AttackAbility)
	{
		BaseAttackDamage = AttackAbility->Damage;
		BaseAttackRange  = AttackAbility->AttackRange;
	}
	if (HealthComponent)
	{
		BaseMaxHealth = HealthComponent->MaxHealth;
	}

	// Apply modifier for the initial commanded strategy
	ApplyStrategyStatModifiers(CommandedStrategy);

	// Update team color VFX
	UpdateTeamColorVFX();
}

void AMocCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

//========================================
// Heal Interface pass-throughs (MocRewardCalculator calls these)
//========================================

float AMocCharacter::GetLastTickHealAmount() const
{
	return HealAbility ? HealAbility->GetLastTickHealAmount() : 0.0f;
}

bool AMocCharacter::ConsumeHealBurst(float Threshold)
{
	return HealAbility ? HealAbility->ConsumeHealBurst(Threshold) : false;
}

//========================================
// Team Identification
//========================================

int32 AMocCharacter::GetTeamID_Implementation() const
{
	return TeamID;
}

int32 AMocCharacter::GetEnvID_Implementation() const
{
	return EnvID;
}

void AMocCharacter::SetTeamID_Implementation(int32 NewTeamID)
{
	TeamID = NewTeamID;
	UpdateTeamColorVFX();
}

void AMocCharacter::SetEnvID_Implementation(int32 NewEnvID)
{
	EnvID = NewEnvID;
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
	if (AttackAbility)
	{
		AttackAbility->StopFiring();
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

	if (UMocRewardSubsystem* RewardSubsystem = GetWorld()->GetSubsystem<UMocRewardSubsystem>())
	{
		if (RewardSettings)
		{
			RewardSubsystem->CalculateDeathPenalty(RewardSettings, RewardState, CommandedStrategy);
		}

		if (AMocCharacter* Killer = Cast<AMocCharacter>(DeathEvent.Killer))
		{
			if (Killer->RewardSettings)
			{
				RewardSubsystem->CalculateKillReward(Killer->RewardSettings, Killer->RewardState, Killer->CommandedStrategy);
			}
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

	RewardState.Reset();

	if (AttackAbility)
	{
		AttackAbility->RefillAmmo();
	}

	// Reset heal state
	if (HealAbility)
	{
		HealAbility->ResetHealState();
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
// QS Weight Storage & Execution
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

		ApplyStrategyStatModifiers(NewStrategy);

		UE_LOG(LogTemp, Log, TEXT("[MocCharacter] Agent %d received strategy command: %s"),
			AgentID, *UEnum::GetValueAsString(NewStrategy));
	}
}

void AMocCharacter::ApplyStrategyStatModifiers(EStrategyType Strategy)
{
	const bool bIsSupport = (Strategy == EStrategyType::Support);

	if (AttackAbility && BaseAttackDamage > 0.0f)
	{
		AttackAbility->Damage      = BaseAttackDamage * (bIsSupport ? 0.5f : 1.0f);
		AttackAbility->AttackRange = BaseAttackRange  * (bIsSupport ? 0.5f : 1.0f);
	}

	if (HealthComponent && BaseMaxHealth > 0.0f)
	{
		const float NewMaxHealth = BaseMaxHealth * (bIsSupport ? 0.7f : 1.0f);
		HealthComponent->MaxHealth = NewMaxHealth;

		// Clamp current HP so agent doesn't exceed the new lower cap
		if (HealthComponent->CurrentHealth > NewMaxHealth)
		{
			HealthComponent->SetHealth(NewMaxHealth);
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[MocCharacter] Agent %d stat modifiers applied: Strategy=%s  Damage=%.1f  Range=%.0f  MaxHP=%.0f"),
		AgentID,
		*UEnum::GetValueAsString(Strategy),
		AttackAbility ? AttackAbility->Damage      : 0.0f,
		AttackAbility ? AttackAbility->AttackRange : 0.0f,
		HealthComponent ? HealthComponent->MaxHealth : 0.0f);
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
	return AttackAbility ? AttackAbility->GetCooldownProgress() : 0.0f;
}

int32 AMocCharacter::AddAmmo_Implementation(int32 AmmoAmount)
{
	if (AttackAbility)
	{
		AttackAbility->AddAmmo(AmmoAmount);
		return AttackAbility->GetCurrentAmmo();
	}
	return 0;
}

float AMocCharacter::GetAmmoPercentage_Implementation() const
{
	return AttackAbility ? AttackAbility->GetAmmoPercentage() : 0.0f;
}

bool AMocCharacter::CanFireWeapon_Implementation() const
{
	return AttackAbility ? AttackAbility->CanFire() : false;
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
	if (UMocRewardSubsystem* RewardSubsystem = GetWorld()->GetSubsystem<UMocRewardSubsystem>())
	{
		// DataAsset과 내 상태를 넘겨서 보상을 계산 (Stateless 호출)
		return RewardSubsystem->ComputeStepReward(this, RewardSettings, RewardState, Strategy, Prev, Current, Action);
	}
	return 0.0f;
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

void AMocCharacter::ProcessTrainingAbilities()
{
	if (!bIsAlive) return;


	if (AttackAbility)
	{
		AttackAbility->ExecuteAbility(0.2f);
	}

	if (HealAbility)
	{
		if (CommandedStrategy == EStrategyType::Support)
		{
			HealAbility->ExecuteAbility(0.2f);
		}
		else
		{
			HealAbility->ResetTickHeal();
		}
	}
}
