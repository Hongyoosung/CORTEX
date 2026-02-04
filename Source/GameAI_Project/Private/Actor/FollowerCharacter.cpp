// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actor/FollowerCharacter.h"
#include "Actor/LeaderCharacter.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/Components/ContextBridgeComponent.h"
#include "Util/Components/VisualLoggerComponent.h"
#include "Combat/Components/HealthComponent.h"
#include "Combat/Components/WeaponComponent.h"
#include "Observation/Components/ObservationBuilderComponent.h"
#include "Schola/ScholaAgentComponent.h"
#include "Combat/Components/CombatExecutorComponent.h"
#include "Combat/Components/AgentPerceptionComponent.h"
#include "RL/Components/RewardCalculator.h"
#include "RL/RLPolicyNetwork.h"
#include "Team/TeamTypes.h"
#include "RL/RLTypes.h"
#include "Observation/ObservationElement.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Core/SimulationManagerGameMode.h"
#include "Kismet/GameplayStatics.h"

AFollowerCharacter::AFollowerCharacter()
	: Super()
	, TeamLeader(nullptr)
	, StateTreeComponent(nullptr)
	, ContextBridgeComponent(nullptr)
	, VisualLoggerComponent(nullptr)
	, ScholaAgentComponent(nullptr)
	, ObservationBuilder(nullptr)
	, CombatExecutor(nullptr)
	, HealthComponent(nullptr)
	, WeaponComponent(nullptr)
	, PerceptionComponent(nullptr)
	, bIsRegisteredWithLeader(false)
	, TeamID(0)
	, bAutoRegisterWithLeader(true)
	, bEnableTeamCommsLogging(false)
	, LastStrategyUpdateTime(0.0f)
	, TicksSinceLastUpdate(0)
	, MinStrategyUpdateInterval(0.05f)
{
	PrimaryActorTick.bCanEverTick = true;


	StateTreeComponent			= CreateDefaultSubobject<UFollowerStateTreeComponent>(TEXT("StateTreeComponent"));
	ContextBridgeComponent		= CreateDefaultSubobject<UContextBridgeComponent>(TEXT("ContextBridgeComponent"));
	VisualLoggerComponent		= CreateDefaultSubobject<UVisualLoggerComponent>(TEXT("VisualLoggerComponent"));
	ObservationBuilder			= CreateDefaultSubobject<UObservationBuilderComponent>(TEXT("ObservationBuilderComponent"));
	ScholaAgentComponent		= CreateDefaultSubobject<UScholaAgentComponent>(TEXT("ScholaAgentComponent"));
	CombatExecutor				= CreateDefaultSubobject<UCombatExecutorComponent>(TEXT("CombatExecutorComponent"));
	HealthComponent				= CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	WeaponComponent				= CreateDefaultSubobject<UWeaponComponent>(TEXT("WeaponComponent"));
	PerceptionComponent			= CreateDefaultSubobject<UAgentPerceptionComponent>(TEXT("PerceptionComponent"));
	RewardCalculatorComponent	= CreateDefaultSubobject<URewardCalculator>(TEXT("RewardCalculatorComponent"));


	// Context Bridge: Initialize with default tactical parameters [0.5, 0.5, 0.5, 0.5]
	if (ContextBridgeComponent)
	{
		ContextBridgeComponent->bEnableVerboseLogging = false;
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

	InitializeComponents();

	InjectDependencies();


	if (!HealthComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter v9.0] '%s': Missing HealthComponent! Death handling disabled."),
			*GetName());
	}
	
	HealthComponent->OnDeath.AddDynamic(this, &AFollowerCharacter::OnHealthComponentDeath);
	UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': Connected HealthComponent::OnDeath to FollowerAgentComponent coordination"),
		*GetName());


	// Context Bridge: Ready for FollowerAgent writes and StateTree reads
	if (ContextBridgeComponent)
	{
		UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': ContextBridge initialized with default tactical params"),
			*GetName());
	}

	if (bAutoRegisterWithLeader)
	{
		FindTeamByTeamID();
	}


	UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': All v9.0 components initialized"), *GetName());
}

void AFollowerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{

	Super::EndPlay(EndPlayReason);
}

void AFollowerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//==========================================================================
	// v9.0 PHASE 5: DECISION LOOP (merged from FollowerAgentComponent)
	//==========================================================================

	// Check if simulation is running
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
	if (SimManager && !SimManager->IsSimulationRunning())
	{
		return;
	}

	// Skip if not alive
	if (!IsAliveState())
	{
		return;
	}

	// ========================================
	// HIERARCHICAL DECISION MAKING (v9.0)
	//
	// Layer 1 (Strategic - MCTS): Team Leader assigns STRATEGIES to agents
	// Layer 2 (Tactical - RL): Follower outputs TACTICAL PARAMETERS + COMBAT CHOICES
	// Layer 3 (Execution - EQS): EQS uses tactical parameters as query weights
	// Layer 4 (Execution - Rules): Combat execution with learned target priority
	// ========================================

	// Build observation
	FObservationElement Obs = BuildLocalObservation();

	// Get assigned strategy from MCTS
	EStrategyType AssignedStrategy = CurrentAssigned.Strategy;

	// Query RL policy for tactical parameters + combat choices
	if (ScholaAgentComponent && IsTacticalPolicyReady())
	{
		URLPolicyNetwork* Policy = GetTacticalPolicy();
		if (Policy)
		{
			// Run RL inference (2-4ms if batched)
			FMacroAction NewAction = Policy->GetMacroAction(Obs, AssignedStrategy);

			SetMacroAction(NewAction);

			// Update ContextBridge for StateTree
			if (ContextBridgeComponent)
			{
				ContextBridgeComponent->SetStrategy(AssignedStrategy);
				ContextBridgeComponent->SetTacticalParameters(NewAction.TacticalParams);
				ContextBridgeComponent->SetCombatParameters(NewAction.CombatParams);
				ContextBridgeComponent->SetIsAlive(IsAliveState());
			}

			// Update timestamp
			LastStrategyUpdateTime = FPlatformTime::Seconds();
		}
	}

	TicksSinceLastUpdate = 0;
	

	// Always increment tick counter (for timeout fallback)
	TicksSinceLastUpdate++;

	// Draw debug info if enabled
	if (VisualLoggerComponent && VisualLoggerComponent->bEnableDebugDrawing)
	{
		// TODO: DrawDebugInfo() - implement if needed
	}
}


float AFollowerCharacter::GetHealthPercentage_Implementation() const
{
	return HealthComponent ? HealthComponent->GetHealthPercentage() * 100.0f : 0.0f;
}

bool AFollowerCharacter::IsAlive_Implementation() const
{
	return HealthComponent ? HealthComponent->IsAlive() : false;
}

float AFollowerCharacter::GetWeaponCooldown_Implementation() const
{
	return WeaponComponent ? WeaponComponent->GetRemainingCooldown() : 0.0f;
}

bool AFollowerCharacter::CanFireWeapon_Implementation() const
{
	return WeaponComponent ? WeaponComponent->CanFire() : false;
}


void AFollowerCharacter::OnHealthComponentDeath(const FDeathEventData& DeathEvent)
{
	MarkAsDead();
}


void AFollowerCharacter::SetStrategyAssignment(const FStrategyAssignment& Assignment)
{
	CurrentAssigned = Assignment;

	if (ContextBridgeComponent)
	{
		ContextBridgeComponent->SetStrategy(Assignment.Strategy);
	}
}

void AFollowerCharacter::SetMacroAction(const FMacroAction& MacroAction)
{
	CurrentMacroAction = MacroAction;
}


FStrategyAssignment AFollowerCharacter::GetStrategyAssignment() const
{
	return CurrentAssigned;
}


FMacroAction AFollowerCharacter::GetCurrentMacroAction() const
{
	return CurrentMacroAction;
}

FAllyContext AFollowerCharacter::GetAllyContext() const
{
	FAllyContext Context;

	if (!ObservationBuilder)
	{
		return Context; // Return empty context if ObservationBuilder is missing
	}

	// Extract ally context from current observation
	const FObservationElement& Obs = ObservationBuilder->GetLocalObservation();
	Context.bAllyNeedsHelp = Obs.bAllyNeedsHelp;
	Context.AllyHealth = Obs.AllyHealth;
	Context.AllyDistance = Obs.AllyDistance;
	Context.AllyDirection = Obs.AllyDirection;

	return Context;
}

//------------------------------------------------------------------------------
// OBSERVATION WRAPPERS (delegate to ObservationBuilderComponent)
//------------------------------------------------------------------------------

FObservationElement AFollowerCharacter::BuildLocalObservation()
{
	return ObservationBuilder ? ObservationBuilder->BuildLocalObservation() : FObservationElement();
}

FObservationElement AFollowerCharacter::GetLocalObservation() const
{
	return ObservationBuilder ? ObservationBuilder->GetLocalObservation() : FObservationElement();
}

FObservationElement AFollowerCharacter::GetPreviousObservation() const
{
	return ObservationBuilder ? ObservationBuilder->GetPreviousObservation() : FObservationElement();
}

void AFollowerCharacter::UpdateLocalObservation(const FObservationElement& NewObservation)
{
	if (ObservationBuilder)
	{
		ObservationBuilder->UpdateLocalObservation(NewObservation);
	}
}

void AFollowerCharacter::UpdateObjectiveContext(AObjectiveActor* Friendly, AObjectiveActor* Hostile)
{
	if (ObservationBuilder)
	{
		ObservationBuilder->SetObjectives(Friendly, Hostile);
	}
}


bool AFollowerCharacter::FindNearestCover(FVector& OutCoverLocation, float& OutDistance, const TArray<AActor*>& Enemies)
{
	if (ObservationBuilder)
	{
		return ObservationBuilder->FindNearestCover(OutCoverLocation, OutDistance, Enemies);
	}
	return false;
}

void AFollowerCharacter::RegisterVisibleEnemy(AActor* Enemy)
{
	if (!TeamLeader || !ObservationBuilder)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter v9.0] '%s': Cannot register visible enemy - missing TeamLeader or ObservationBuilder"),
			*GetName());

		return;
	}

	TeamLeader->RegisterEnemy(Enemy);
	ObservationBuilder->EnemySpotted();
}

AObjectiveActor* AFollowerCharacter::GetFriendlyObjective() const
{
	return ObservationBuilder ? ObservationBuilder->GetFriendlyObjective() : nullptr;
}

AObjectiveActor* AFollowerCharacter::GetHostileObjective() const
{
	return ObservationBuilder ? ObservationBuilder->GetHostileObjective() : nullptr;
}



void AFollowerCharacter::ProvideReward(float Reward)
{
	if (ScholaAgentComponent)
	{
		ScholaAgentComponent->ProviderReward(Reward);
	}
}


float AFollowerCharacter::GetCurrentReward() const
{
	if (!ScholaAgentComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter] ScholaAgentComponent is null, cannot get current reward"));
		return 0.0f;
	}

	return ScholaAgentComponent->GetCurrentReward();
}

URewardCalculator* AFollowerCharacter::GetRewardCalculator() const
{
	return RewardCalculatorComponent ? RewardCalculatorComponent : nullptr;
}

UObservationBuilderComponent* AFollowerCharacter::GetObservationBuilder() const
{
	return ObservationBuilder;
}



//------------------------------------------------------------------------------
// COMBAT WRAPPERS (delegate to CombatExecutorComponent)
//------------------------------------------------------------------------------

void AFollowerCharacter::ExecuteCombat(const FCombatParameters& Params)
{
	if (CombatExecutor)
	{
		CombatExecutor->ExecuteCombat(Params);
	}
}

AActor* AFollowerCharacter::GetClosestEnemy(const TArray<AActor*>& Enemies) const
{
	return CombatExecutor ? CombatExecutor->GetClosestEnemy(Enemies) : nullptr;
}

AActor* AFollowerCharacter::GetLowestHPEnemy(const TArray<AActor*>& Enemies) const
{
	return CombatExecutor ? CombatExecutor->GetLowestHPEnemy(Enemies) : nullptr;
}


ALeaderCharacter* AFollowerCharacter::GetTeamLeader() const
{
	if (!TeamLeader)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter v9.0] '%s': GetTeamLeader called but CachedTeamLeader is null"),
			*GetName());
		return nullptr;
	}

	return TeamLeader;
}

int32 AFollowerCharacter::GetTeamID() const
{
	return TeamID;
}


void AFollowerCharacter::ResetEpisode()
{
	// Reset all components that maintain episode state
	if (ScholaAgentComponent)
	{
		ScholaAgentComponent->ResetEpisode();
	}

	if (ObservationBuilder)
	{
		ObservationBuilder->ResetEpisode();
	}


	if (ContextBridgeComponent)
	{
		ContextBridgeComponent->ResetContext();
	}

	UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter] '%s': Episode reset complete"), *GetName());
}

void AFollowerCharacter::OnEpisodeEnded(float EpisodeReward)
{

}

void AFollowerCharacter::MarkAsDead()
{
	// TODO: remove CombatExecutor -> move to state tree task
	if (CombatExecutor)
	{
		ContextBridgeComponent->SetIsAlive(false);
	}	

	if (ContextBridgeComponent)
	{
		ContextBridgeComponent->SetIsAlive(false);
	}

	if (StateTreeComponent)
	{
		StateTreeComponent->OnFollowerDied();
	}

	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
}

void AFollowerCharacter::MarkAsAlive()
{
	if (ContextBridgeComponent)
	{
		ContextBridgeComponent->SetIsAlive(true);
	}

	if (HealthComponent)
	{
		HealthComponent->ResetHealth();
	}

	if (StateTreeComponent)
	{
		StateTreeComponent->OnFollowerRespawned();
	}

	SetActorEnableCollision(true);
	SetActorHiddenInGame(false);
}

bool AFollowerCharacter::IsAliveState() const
{
	if (!HealthComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter] HealthComponent has null. can not check is Alive"))
	}

	return HealthComponent->IsAlive();
}


void AFollowerCharacter::InitializeComponents()
{
	// Validate critical sub-components
	bool bAllComponentsValid = true;

	if (ScholaAgentComponent)
	{
		ScholaAgentComponent->InitializeScholaComponents();
	}

}

void AFollowerCharacter::InjectDependencies()
{
	// CombatExecutor needs: RewardCalculator, HealthComponent, PerceptionComponent
	if (CombatExecutor)
	{
		if (HealthComponent)
		{
			CombatExecutor->SetHealthComponent(HealthComponent);
		}

		if (PerceptionComponent)
		{
			CombatExecutor->SetPerceptionComponent(PerceptionComponent);
		}
	}

	// ObservationBuilder needs: HealthComponent, PerceptionComponent
	if (ObservationBuilder)
	{
		if (HealthComponent)
		{
			ObservationBuilder->SetHealthComponent(HealthComponent);
		}

		if (PerceptionComponent)
		{
			ObservationBuilder->SetPerceptionComponent(PerceptionComponent);
		}

		UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0 Phase3] '%s': Injected dependencies into ObservationBuilder"),
			*GetName());
	}

	UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0 Phase3] '%s': Dependency injection complete (all components)"),
		*GetName());
}


void AFollowerCharacter::FindTeamByTeamID()
{
	
	if (!GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter] '%s': FindTeamByTeamID() - Invalid World"), *GetName());
		return;
	}

	// Iterate through all ALeaderCharacter actors in the world
	for (TActorIterator<ALeaderCharacter> It(GetWorld()); It; ++It)
	{
		ALeaderCharacter* Leader = *It;
		if (!Leader || !Leader->TeamManagerComponent)
		{
			continue;
		}

		// Check if this leader's TeamID matches our TeamID
		if (Leader->GetTeamID() == TeamID)
		{
			// Found matching leader - register this follower
			bool bSuccess = Leader->RegisterFollower(this);

			if (bSuccess)
			{
				UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter] '%s': Successfully registered with Leader '%s' (TeamID: %d)"),
					*GetName(), *Leader->GetName(), TeamID);

				// Cache the team leader reference
				TeamLeader = Leader;
				bIsRegisteredWithLeader = true;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter] '%s': Failed to register with Leader '%s' (TeamID: %d)"),
					*GetName(), *Leader->GetName(), TeamID);
			}
		}
	}

	// No matching leader found
	UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter] '%s': No leader found with matching TeamID: %d"),
		*GetName(), TeamID);
}