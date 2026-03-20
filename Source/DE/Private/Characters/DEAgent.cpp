// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/DEAgent.h"
#include "Data/DETeamData.h"
#include "Data/DEStrategyData.h"
#include "GAS/DEAttributeSet.h"
#include "GAS/DEGameplayTags.h"
#include "GAS/Abilities/DEGA_Attack.h"
#include "GAS/Abilities/DEGA_Heal.h"
#include "Schola/Components/DEScholaAgent.h"
#include "Core/Subsystems/DERewardSubsystem.h"
#include "EQS/DynamicEQSExecutor.h"
#include "Data/DEAbilityData.h"

#include "Kismet/GameplayStatics.h"
#include "AbilitySystemComponent.h"
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
#include "Kismet/KismetMathLibrary.h"
#include "BrainComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Misc/Optional.h"
#include "Components/WidgetComponent.h"


ADEAgent::ADEAgent()
	: Super()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create GAS components
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AttributeSet = CreateDefaultSubobject<UDEAttributeSet>(TEXT("AttributeSet"));

	// Create other components
	ScholaAgent =	CreateDefaultSubobject<UDEScholaAgent>(TEXT("ScholaAgent"));
	StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
	EQSExecutor =	CreateDefaultSubobject<UDynamicEQSExecutor>(TEXT("EQSExecutor"));

	// Overhead strategy + health widget (screen space)
	OverheadWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("OverheadWidget"));
	OverheadWidgetComponent->SetupAttachment(GetCapsuleComponent());
	OverheadWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	OverheadWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	OverheadWidgetComponent->SetDrawSize(FVector2D(200.0f, 60.0f));
	OverheadWidgetComponent->SetPivot(FVector2D(0.5f, 1.0f));

	// Create third-person spectator camera
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


	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->MaxWalkSpeed = 600.0f;
		MovementComp->bOrientRotationToMovement = false;
		MovementComp->bUseControllerDesiredRotation = true;
		MovementComp->bUseRVOAvoidance = true;
		MovementComp->AvoidanceConsiderationRadius = 120.f;
	}

	bUseControllerRotationYaw = false;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void ADEAgent::BeginPlay()
{
	Super::BeginPlay();

	// Ensure GAS actor info is set before any ability grants.
	// PossessedBy may not fire before BeginPlay when spawned during another actor's BeginPlay.
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}

	// Subscribe to Health attribute changes for death detection
	if (AbilitySystemComponent && AttributeSet)
	{
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
			UDEAttributeSet::GetHealthAttribute()).AddUObject(this, &ADEAgent::OnHealthChanged);
	}

	// Configure EQS executor with semantic weight parameter names so the query
	// asset uses readable identifiers instead of generic Weight0..WeightN.
	if (EQSExecutor)
	{
		EQSExecutor->WeightParamNames = FDEEQSWeightParameters::GetParamNames();
	}

	// Register perception stimuli
	if (StimuliSource)
	{
		StimuliSource->RegisterWithPerceptionSystem();
		StimuliSource->RegisterForSense(TSubclassOf<UAISense_Sight>());
	}

	// RewardSubsystem is obtained lazily from the MatchManager in ComputeStepReward().

	// Initialize GAS abilities from data asset
	InitializeGASAbilities();

	const bool bIsTraining = ScholaAgent && ScholaAgent->AgentMode == EDynamicEQSAgentMode::Training;
	if (bIsTraining)
	{
		GetWorld()->GetTimerManager().SetTimer(
			TrainingAbilityTimerHandle,
			this,
			&ADEAgent::ProcessTrainingAbilities,
			0.2f,
			true
		);
	}
}

void ADEAgent::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// GAS requires ASC to be initialized after possession
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}
}

void ADEAgent::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Mana regeneration — rate is slower than heal cost, so continuous healing depletes mana
	if (AttributeSet && AbilityData && AbilityData->ManaRegenRate > 0.0f)
	{
		const float CurrentMana = AttributeSet->GetMana();
		const float Cap = AttributeSet->GetMaxMana();
		if (CurrentMana < Cap)
		{
			AttributeSet->SetMana(FMath::Min(CurrentMana + AbilityData->ManaRegenRate * DeltaTime, Cap));
		}
	}
}


//========================================
// IAbilitySystemInterface
//========================================

UAbilitySystemComponent* ADEAgent::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}


//========================================
// GAS Ability Initialization
//========================================

void ADEAgent::InitializeGASAbilities()
{
	if (!AbilitySystemComponent || !AbilityData) return;

	// Initialize Mana attribute from data asset
	if (AttributeSet)
	{
		AttributeSet->InitMaxMana(AbilityData->MaxMana);
		AttributeSet->InitMana(AbilityData->MaxMana);
	}

	// 1. Grant Attack ability
	// Pass the class to GiveAbility — GAS creates and owns the instance internally.
	// Retrieve the GAS-managed instance afterward so SetConfig targets the real object.
	if (AbilityData->AttackConfig.AbilityClass)
	{
		FGameplayAbilitySpec AttackSpec(AbilityData->AttackConfig.AbilityClass, 1, INDEX_NONE, this);
		FGameplayAbilitySpecHandle AttackHandle = AbilitySystemComponent->GiveAbility(AttackSpec);

		if (FGameplayAbilitySpec* GrantedSpec = AbilitySystemComponent->FindAbilitySpecFromHandle(AttackHandle))
		{
			AttackAbility = Cast<UDEGA_Attack>(GrantedSpec->GetPrimaryInstance());
			if (AttackAbility)
			{
				AttackAbility->SetConfig(AbilityData->AttackConfig);
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] AttackAbilityClass is not assigned to DA_AbilityConfig!"), *GetName());
	}

	// 2. Grant Heal ability
	if (AbilityData->HealConfig.AbilityClass)
	{
		FGameplayAbilitySpec HealSpec(AbilityData->HealConfig.AbilityClass, 1, INDEX_NONE, this);
		FGameplayAbilitySpecHandle HealHandle = AbilitySystemComponent->GiveAbility(HealSpec);

		if (FGameplayAbilitySpec* GrantedSpec = AbilitySystemComponent->FindAbilitySpecFromHandle(HealHandle))
		{
			HealAbility = Cast<UDEGA_Heal>(GrantedSpec->GetPrimaryInstance());
			if (HealAbility)
			{
				HealAbility->SetConfig(AbilityData->HealConfig);
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] HealAbilityClass is not assigned to DA_AbilityConfig!"), *GetName());
	}
}


//========================================
// Heal Interface pass-throughs (DERewardCalculator calls these)
//========================================

float ADEAgent::GetLastTickHealAmount() const
{
	return HealAbility ? HealAbility->GetLastTickHealAmount() : 0.0f;
}

bool ADEAgent::ConsumeHealBurst(float Threshold)
{
	return HealAbility ? HealAbility->ConsumeHealBurst(Threshold) : false;
}


//========================================
// Team Identification
//========================================

int32 ADEAgent::GetTeamID_Implementation() const { return TeamID; }
int32 ADEAgent::GetEnvID_Implementation() const { return EnvID; }
void ADEAgent::SetTeamID_Implementation(int32 NewTeamID) { TeamID = NewTeamID; }
void ADEAgent::SetEnvID_Implementation(int32 NewEnvID) { EnvID = NewEnvID; }


//========================================
// GAS Health Queries
//========================================

float ADEAgent::GetHealthPercentage() const
{
	if (AttributeSet && AttributeSet->GetMaxHealth() > 0.0f)
	{
		return AttributeSet->GetHealth() / AttributeSet->GetMaxHealth();
	}
	return 1.0f;
}

float ADEAgent::GetCurrentHealth() const
{
	return AttributeSet ? AttributeSet->GetHealth() : 0.0f;
}

float ADEAgent::GetMaxHealth() const
{
	return AttributeSet ? AttributeSet->GetMaxHealth() : 100.0f;
}

float ADEAgent::GetManaPercentage() const
{
	if (AttributeSet && AttributeSet->GetMaxMana() > 0.0f)
	{
		return AttributeSet->GetMana() / AttributeSet->GetMaxMana();
	}
	return 1.0f;
}

float ADEAgent::HealCharacter(float HealAmount)
{
	if (!AbilitySystemComponent || !AttributeSet || HealAmount <= 0.0f) return 0.0f;

	TSubclassOf<UGameplayEffect> HealEffectClass = AbilityData ? AbilityData->HealConfig.HealEffectClass : nullptr;
	if (!HealEffectClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("DEAgent: HealEffectClass not set on %s — assign it in DA_AbilityConfig HealConfig."), *GetName());
		return 0.0f;
	}

	float OldHealth = AttributeSet->GetHealth();

	FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
	Context.AddInstigator(this, this);
	Context.AddOrigin(GetActorLocation());
	{
		FHitResult HealHit;
		HealHit.Location = GetActorLocation() + FVector(0, 0, 90);
		HealHit.ImpactPoint = HealHit.Location;
		HealHit.ImpactNormal = FVector::UpVector;
		Context.AddHitResult(HealHit);
	}
	FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(HealEffectClass, 1.0f, Context);
	if (SpecHandle.IsValid())
	{
		SpecHandle.Data->SetSetByCallerMagnitude(DEGameplayTags::Data_Healing, HealAmount);
		AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);
	}

	return AttributeSet->GetHealth() - OldHealth;
}

float ADEAgent::ApplyDamageToSelf(float DamageAmount, AActor* DamageInstigator, AActor* DamageCauser,
	const FVector& HitLocation, const FVector& HitNormal)
{
	if (!AbilitySystemComponent || !AttributeSet || DamageAmount <= 0.0f) return 0.0f;
	if (!bIsAlive) return 0.0f;

	float OldHealth = AttributeSet->GetHealth();

	// Track damage instigator for death attribution
	LastDamageInstigator = DamageInstigator;
	LastDamageAmount = DamageAmount;

	// Track damage contributors for assist rewards
	if (DamageInstigator)
	{
		float& ContributorDamage = DamageContributors.FindOrAdd(DamageInstigator);
		ContributorDamage += DamageAmount;
	}

	// Get DamageEffectClass from the instigator's AbilityData (the shooter owns the attack config)
	TSubclassOf<UGameplayEffect> DamageEffectClass = nullptr;
	if (const ADEAgent* InstigatorChar = Cast<ADEAgent>(DamageInstigator))
	{
		if (InstigatorChar->AbilityData)
		{
			DamageEffectClass = InstigatorChar->AbilityData->AttackConfig.DamageEffectClass;
		}
	}
	if (!DamageEffectClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("DEAgent: DamageEffectClass not set — assign it in DA_AbilityConfig AttackConfig on %s."),
			DamageInstigator ? *DamageInstigator->GetName() : TEXT("Unknown"));
		return 0.0f;
	}

	FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
	Context.AddInstigator(DamageInstigator, DamageCauser);

	FHitResult HitResult;
	HitResult.bBlockingHit = true;          
	HitResult.Location = HitLocation;       
	HitResult.ImpactPoint = HitLocation;    
	HitResult.Normal = HitNormal;          
	HitResult.ImpactNormal = HitNormal;
	HitResult.HitObjectHandle = FActorInstanceHandle(this); 

	Context.AddHitResult(HitResult);

	FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(DamageEffectClass, 1.0f, Context);
	if (SpecHandle.IsValid())
	{
		SpecHandle.Data->SetSetByCallerMagnitude(DEGameplayTags::Data_Damage, DamageAmount);
		AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);
	}

	float ActualDamage = OldHealth - AttributeSet->GetHealth();

	// Accumulate stats
	TotalDamageTaken += ActualDamage;

	// Broadcast damage event
	FDEDamageEventData DamageEvent(DamageInstigator, DamageCauser, ActualDamage, HitLocation, HitNormal);
	OnDamageTaken_Delegate.Broadcast(DamageEvent, AttributeSet->GetHealth());
	OnHealthChanged_Delegate.Broadcast(AttributeSet->GetHealth(), AttributeSet->GetMaxHealth());

	return ActualDamage;
}

float ADEAgent::GetWeaponCooldown() const
{
	return AttackAbility ? AttackAbility->GetCooldownProgress() : 0.0f;
}

bool ADEAgent::CanFireWeapon() const
{
	return AttackAbility ? AttackAbility->CanFire() : false;
}

int32 ADEAgent::AddAmmo(int32 AmmoAmount)
{
	if (AttackAbility)
	{
		AttackAbility->AddAmmo(AmmoAmount);
		return AttackAbility->GetCurrentAmmo();
	}
	return 0;
}

float ADEAgent::GetAmmoPercentage() const
{
	return AttackAbility ? AttackAbility->GetAmmoPercentage() : 0.0f;
}


//========================================
// Damage Event System
//========================================

void ADEAgent::NotifyDamageDealt(AActor* Victim, float DamageAmount)
{
	TotalDamageDealt += DamageAmount;
	OnDamageDealt_Delegate.Broadcast(Victim, DamageAmount);
}

void ADEAgent::NotifyKillConfirmed(AActor* Victim, float TotalDamageDealtToVictim)
{
	KillCount++;
	OnKillConfirmed_Delegate.Broadcast(Victim, TotalDamageDealtToVictim);
}


//========================================
// Health Change & Death
//========================================

void ADEAgent::OnHealthChanged(const FOnAttributeChangeData& Data)
{
	if (Data.NewValue <= 0.0f && bIsAlive && !bHasDied)
	{
		HandleDeath(LastDamageInstigator.Get(), LastDamageAmount);
	}

	OnHealthChanged_Delegate.Broadcast(Data.NewValue,
		AttributeSet ? AttributeSet->GetMaxHealth() : 100.0f);
}

void ADEAgent::HandleDeath(AActor* Killer, float FinalDamage)
{
	bIsAlive = false;
	bHasDied = true;

	float TimeSinceSpawn = GetWorld() ? (GetWorld()->GetTimeSeconds() - SpawnTime) : -1.0f;
	UE_LOG(LogTemp, Error, TEXT("[DEAgent DEATH] %s died %.2fs after spawn - Health=%.1f%%, Killer=%s, Location=%s"),
		*GetName(), TimeSinceSpawn,
		GetHealthPercentage() * 100.0f,
		Killer ? *Killer->GetName() : TEXT("None"),
		*GetActorLocation().ToString());

	if (TimeSinceSpawn < 1.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("[DEAgent DEATH] PREMATURE DEATH - Agent died less than 1s after spawn!"));
	}

	// Notify killer's stats
	if (ADEAgent* KillerChar = Cast<ADEAgent>(Killer))
	{
		KillerChar->NotifyKillConfirmed(this, DamageContributors.Contains(Killer) ? DamageContributors[Killer] : 0.0f);
	}

	// Disable movement
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->DisableMovement();
		MovementComp->StopMovementImmediately();
	}

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

	// Broadcast death events
	FDEDeathEventData DeathEvent(this, Killer, FinalDamage,
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);

	OnAgentDied_Delegate.Broadcast(this, Cast<ADEAgent>(Killer));
	OnAgentDeathEvent_Delegate.Broadcast(DeathEvent);
}

ADEMatchManager* ADEAgent::GetMatchManager() const
{
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

void ADEAgent::ResetCharacter()
{
	UE_LOG(LogTemp, Log, TEXT("[DEAgent] %s resetting for new episode"), *GetName());

	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(true);

	// Reset health via GAS
	if (AttributeSet && AbilitySystemComponent)
	{
		AttributeSet->SetHealth(AttributeSet->GetMaxHealth());

		// Remove State.Dead tag
		FGameplayTagContainer DeadTag;
		DeadTag.AddTag(DEGameplayTags::State_Dead);
		AbilitySystemComponent->RemoveLooseGameplayTags(DeadTag);
	}

	RewardState.Reset();

	// Reset abilities
	if (AttackAbility) AttackAbility->ResetState();
	if (HealAbility) HealAbility->ResetHealState();

	// Reset damage stats
	TotalDamageTaken = 0.0f;
	TotalDamageDealt = 0.0f;
	KillCount = 0;
	DamageContributors.Empty();
	LastDamageInstigator = nullptr;
	bHasDied = false;

	// Re-enable movement
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->SetMovementMode(MOVE_Walking);
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	if (USkeletalMeshComponent* InMesh = GetMesh())
	{
		InMesh->SetSimulatePhysics(false);
		InMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		InMesh->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		InMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
		InMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	}

	bIsAlive = true;
	SpawnTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	float CurrentHealth = GetHealthPercentage();
	UE_LOG(LogTemp, Log, TEXT("[DEAgent] %s reset complete - bIsAlive=%d, Health=%.1f%%, Location=%s"),
		*GetName(), bIsAlive, CurrentHealth * 100.0f, *GetActorLocation().ToString());

	if (ScholaAgent)
	{
		ScholaAgent->ResetAgent();
	}

	UE_LOG(LogTemp, Log, TEXT("[DEAgent] %s post-reset EQS state: bWeightsDirty=%d"),
		*GetName(), bWeightsDirty);

	UE_LOG(LogTemp, Verbose, TEXT("[DEAgent] %s reset complete"), *GetName());
}

void ADEAgent::Activate()
{
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(true);
	ResetCharacter();
}

void ADEAgent::Deactivate()
{
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);
}


//========================================
// EQS Weight Storage & Execution
//========================================

void ADEAgent::UpdateTacticalWeights(const FDEEQSWeightParameters& NewWeights)
{
	CurrentEQSWeights = NewWeights;
	bWeightsDirty = true;
}

void ADEAgent::PerformTacticalAction()
{
	if (!bIsAlive) return;

	if (!EQSExecutor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEAgent] %s: EQSExecutor not available"), *GetName());
		return;
	}

	AAIController* AICtrl = Cast<AAIController>(GetController());
	if (!AICtrl)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEAgent] %s: No AIController found"), *GetName());
		return;
	}

	TacticalActionCallCount++;
	if (TacticalActionCallCount <= 10 || TacticalActionCallCount % 60 == 0)
	{
		/*UE_LOG(LogTemp, Warning,
			TEXT("[DEAgent] %s PerformTacticalAction#%d: EQS direct path"),
			*GetName(), TacticalActionCallCount);*/
	}

	AICtrl->StopMovement();

	EQSExecutor->SetWeights(FDynamicEQSWeightParameters(CurrentEQSWeights.ToArray()));
	TOptional<FVector> Result = EQSExecutor->ExecuteQuerySynchronous();
	if (!Result.IsSet())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEAgent] %s: EQS query returned no result - using fallback movement"), *GetName());

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
// Command Interface
//========================================

void ADEAgent::SetCommandedStrategy(EDEStrategyType NewStrategy)
{
	if (ScholaAgent && ScholaAgent->GetCommandedStrategy() != NewStrategy)
	{
		ScholaAgent->UpdateCommandedStrategy(NewStrategy);
	}

	ApplyStrategyAppearance(NewStrategy);
}

void ADEAgent::ApplyStrategyAppearance(EDEStrategyType Strategy)
{
	if (!TeamData) { return; }

	const UDEStrategyData* SD = TeamData->GetStrategyData(Strategy);

	// ── Mesh & Animation Blueprint ──────────────────────────────────────────
	if (USkeletalMeshComponent* InMesh = GetMesh())
	{
		// Strategy asset takes priority; fall back to team-level default.
		USkeletalMesh* NewMesh = (SD && SD->SkeletalMesh)
			? SD->SkeletalMesh.Get()
			: TeamData->SkeletalMesh.Get();

		if (NewMesh)
		{
			InMesh->SetSkeletalMesh(NewMesh);
		}

		TSubclassOf<UAnimInstance> NewAnimBP = (SD && SD->AnimationBlueprint)
			? SD->AnimationBlueprint
			: TeamData->AnimationBlueprint;

		if (NewAnimBP)
		{
			InMesh->SetAnimInstanceClass(NewAnimBP);
		}
	}

	if (!SD) { return; }

	// ── Movement Speed ───────────────────────────────────────────────────────
	if (SD->MaxWalkSpeed > 0.0f)
	{
		if (UCharacterMovementComponent* CMC = GetCharacterMovement())
		{
			CMC->MaxWalkSpeed = SD->MaxWalkSpeed;
		}
	}

	// ── Max Health ───────────────────────────────────────────────────────────
	if (SD->MaxHealth > 0.0f)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		{
			ASC->SetNumericAttributeBase(UDEAttributeSet::GetMaxHealthAttribute(), SD->MaxHealth);
			// Set current health to the new max (strategy appearance is applied at spawn/assignment time).
			ASC->SetNumericAttributeBase(UDEAttributeSet::GetHealthAttribute(), SD->MaxHealth);
		}
	}

	// ── Ability Config Override ──────────────────────────────────────────────
	// If the strategy has its own AbilityData, use it in place of the character's base data.
	const UDEAbilityData* EffectiveAbilityData = SD->AbilityDataOverride
		? SD->AbilityDataOverride.Get()
		: AbilityData.Get();

	if (EffectiveAbilityData)
	{
		if (AttackAbility)
		{
			AttackAbility->SetConfig(EffectiveAbilityData->AttackConfig);
		}
		if (HealAbility)
		{
			HealAbility->SetConfig(EffectiveAbilityData->HealConfig);
		}
	}
}

EDEStrategyType ADEAgent::GetCommandedStrategy() const
{
	if (!ScholaAgent)
	{
		UE_LOG(LogTemp, Error, TEXT("[DEAgent] Schola Agent has null"));

		return EDEStrategyType::Assault;
	}

	return ScholaAgent->GetCommandedStrategy();
}

UDEStrategyData* ADEAgent::GetCurrentStrategyData() const
{
	if (!TeamData) return nullptr;
	return TeamData->GetStrategyData(GetCommandedStrategy());
}

FString ADEAgent::GetTeamName() const
{
	if (TeamData) return TeamData->TeamName;
	return FString();
}

FLinearColor ADEAgent::GetTeamColor() const
{
	if (TeamData) return TeamData->TeamColor;
	return TeamColor;
}


//========================================
// Reward Interface
//========================================

float ADEAgent::ComputeStepReward(
	EDEStrategyType Strategy,
	const FDEAgentSnapshot& Prev,
	const FDEAgentSnapshot& Current,
	const FDEEQSWeightParameters& Action)
{
	if (ADEMatchManager* MM = GetMatchManager())
	{
		if ((RewardSubsystem = MM->GetRewardCalculator()))
		{
			return RewardSubsystem->ComputeStepReward(this, RewardState, Strategy, Prev, Current, Action);
		}
	}

	return 0.0f;
}


void ADEAgent::ProcessTrainingAbilities()
{
	if (!bIsAlive || !AbilitySystemComponent) return;

	// Activate attack ability via GAS tag
	FGameplayTagContainer AttackTag;
	AttackTag.AddTag(DEGameplayTags::Ability_Attack);
	AbilitySystemComponent->TryActivateAbilitiesByTag(AttackTag);

	// Activate heal ability only for Support strategy
	if (ScholaAgent->GetCommandedStrategy() == EDEStrategyType::Support)
	{
		FGameplayTagContainer HealTag;
		HealTag.AddTag(DEGameplayTags::Ability_Heal);
		AbilitySystemComponent->TryActivateAbilitiesByTag(HealTag);
	}
	else if (HealAbility)
	{
		HealAbility->ResetTickHeal();
	}
}
