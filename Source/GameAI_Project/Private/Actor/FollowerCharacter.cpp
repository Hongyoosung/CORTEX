// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actor/FollowerCharacter.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/Components/ContextBridgeComponent.h"
#include "Util/Components/VisualLoggerComponent.h"
#include "Combat/Components/HealthComponent.h"
#include "Combat/Components/WeaponComponent.h"
#include "RL/Components/TacticalStateComponent.h"
#include "Observation/Components/ObservationBuilderComponent.h"
#include "RL/Components/RLAgentComponent.h"
#include "Combat/Components/CombatExecutorComponent.h"
#include "Combat/Components/AgentPerceptionComponent.h"
#include "RL/Components/RewardCalculator.h"
#include "RL/RLPolicyNetwork.h"
#include "Team/TeamTypes.h"
#include "RL/RLTypes.h"
#include "Observation/ObservationElement.h"
#include "Observation/TeamObservation.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Core/SimulationManagerGameMode.h"
#include "Kismet/GameplayStatics.h"

AFollowerCharacter::AFollowerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;


	StateTreeComponent		= CreateDefaultSubobject<UFollowerStateTreeComponent>(TEXT("StateTreeComponent"));
	ContextBridgeComponent	= CreateDefaultSubobject<UContextBridgeComponent>(TEXT("ContextBridgeComponent"));
	VisualLoggerComponent	= CreateDefaultSubobject<UVisualLoggerComponent>(TEXT("VisualLoggerComponent"));


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


	if (!CachedHealthComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter v9.0] '%s': Missing HealthComponent! Death handling disabled."),
			*GetName());
	}
	
	CachedHealthComponent->OnDeath.AddDynamic(this, &AFollowerCharacter::OnHealthComponentDeath);
	UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': Connected HealthComponent::OnDeath to FollowerAgentComponent coordination"),
		*GetName());


	// Context Bridge: Ready for FollowerAgent writes and StateTree reads
	if (ContextBridgeComponent)
	{
		UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': ContextBridge initialized with default tactical params"),
			*GetName());
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
	if (ShouldUpdateStrategy())
	{
		// Build observation
		FObservationElement Obs = BuildLocalObservation();

		// Get assigned strategy from MCTS
		EStrategyType AssignedStrategy = GetAssignedStrategy();

		// Query RL policy for tactical parameters + combat choices
		if (CachedRLAgent && IsTacticalPolicyReady())
		{
			URLPolicyNetwork* Policy = GetTacticalPolicy();
			if (Policy)
			{
				// Run RL inference (2-4ms if batched)
				FMacroAction NewAction = Policy->GetMacroAction(Obs, AssignedStrategy);

				// Update tactical state
				SetTacticalParameters(NewAction.TacticalParams);
				SetCombatParameters(NewAction.CombatParams);

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
	}

	// Always increment tick counter (for timeout fallback)
	TicksSinceLastUpdate++;

	// ========================================
	// Combat execution (every tick, 60 Hz)
	// ========================================
	ExecuteCombatInternal();

	// Draw debug info if enabled
	if (VisualLoggerComponent && VisualLoggerComponent->bEnableDebugDrawing)
	{
		// TODO: DrawDebugInfo() - implement if needed
	}
}


float AFollowerCharacter::GetHealthPercentage_Implementation() const
{
	return CachedHealthComponent ? CachedHealthComponent->GetHealthPercentage() * 100.0f : 0.0f;
}

bool AFollowerCharacter::IsAlive_Implementation() const
{
	return CachedHealthComponent ? CachedHealthComponent->IsAlive() : false;
}

float AFollowerCharacter::GetWeaponCooldown_Implementation() const
{
	return CachedWeaponComponent ? CachedWeaponComponent->GetRemainingCooldown() : 0.0f;
}

bool AFollowerCharacter::CanFireWeapon_Implementation() const
{
	return CachedWeaponComponent ? CachedWeaponComponent->CanFire() : false;
}


void AFollowerCharacter::OnHealthComponentDeath(const FDeathEventData& DeathEvent)
{
	MarkAsDead();
}


void AFollowerCharacter::SetStrategyAssignment(const FStrategyAssignment& Assignment)
{
	if (CachedTacticalState)
	{
		CachedTacticalState->SetStrategyAssignment(Assignment);
	}

	// Synchronize strategy to RewardCalculator (required for strategy-specific rewards)
	if (CachedRLAgent)
	{
		if (URewardCalculator* RewardCalc = CachedRLAgent->GetRewardCalculator())
		{
			RewardCalc->SetCurrentStrategy(Assignment.Strategy);
		}
	}

	if (ContextBridgeComponent)
	{
		ContextBridgeComponent->SetStrategy(Assignment.Strategy);
	}
}

EStrategyType AFollowerCharacter::GetAssignedStrategy() const
{
	return CachedTacticalState ? CachedTacticalState->GetAssignedStrategy() : EStrategyType::Assault;
}

FStrategyAssignment AFollowerCharacter::GetStrategyAssignment() const
{
	return CachedTacticalState ? CachedTacticalState->GetStrategyAssignment() : FStrategyAssignment();
}

void AFollowerCharacter::SetTacticalParameters(const FTacticalParameters& Params)
{
	if (CachedTacticalState)
	{
		CachedTacticalState->SetTacticalParameters(Params);
	}
}

FTacticalParameters AFollowerCharacter::GetTacticalParameters() const
{
	return CachedTacticalState ? CachedTacticalState->GetTacticalParameters() : FTacticalParameters();
}

void AFollowerCharacter::SetCombatParameters(const FCombatParameters& Params)
{
	if (CachedTacticalState)
	{
		CachedTacticalState->SetCombatParameters(Params);
	}
}

FCombatParameters AFollowerCharacter::GetCombatParameters() const
{
	return CachedTacticalState ? CachedTacticalState->GetCombatParameters() : FCombatParameters();
}

FMacroAction AFollowerCharacter::GetCurrentMacroAction() const
{
	return CachedTacticalState ? CachedTacticalState->GetCurrentMacroAction() : FMacroAction();
}

FAllyContext AFollowerCharacter::GetAllyContext() const
{
	FAllyContext Context;

	if (!CachedObservationBuilder)
	{
		return Context; // Return empty context if ObservationBuilder is missing
	}

	// Extract ally context from current observation
	const FObservationElement& Obs = CachedObservationBuilder->GetLocalObservation();
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
	return CachedObservationBuilder ? CachedObservationBuilder->BuildLocalObservation() : FObservationElement();
}

FObservationElement AFollowerCharacter::GetLocalObservation() const
{
	return CachedObservationBuilder ? CachedObservationBuilder->GetLocalObservation() : FObservationElement();
}

FObservationElement AFollowerCharacter::GetPreviousObservation() const
{
	return CachedObservationBuilder ? CachedObservationBuilder->GetPreviousObservation() : FObservationElement();
}

void AFollowerCharacter::UpdateLocalObservation(const FObservationElement& NewObservation)
{
	if (CachedObservationBuilder)
	{
		CachedObservationBuilder->UpdateLocalObservation(NewObservation);
	}
}

void AFollowerCharacter::UpdateObjectiveContext(AObjectiveActor* Friendly, AObjectiveActor* Hostile)
{
	if (CachedObservationBuilder)
	{
		CachedObservationBuilder->SetObjectives(Friendly, Hostile);
	}
}

void AFollowerCharacter::UpdateTeamIntel(const FTeamObservation& TeamObs)
{
	if (CachedObservationBuilder)
	{
		CachedObservationBuilder->UpdateTeamIntel(TeamObs);
	}
}

bool AFollowerCharacter::FindNearestCover(FVector& OutCoverLocation, float& OutDistance, const TArray<AActor*>& Enemies)
{
	if (CachedObservationBuilder)
	{
		return CachedObservationBuilder->FindNearestCover(OutCoverLocation, OutDistance, Enemies);
	}
	return false;
}

//------------------------------------------------------------------------------
// REINFORCEMENT LEARNING WRAPPERS (delegate to RLAgentComponent)
//------------------------------------------------------------------------------

void AFollowerCharacter::ProvideReward(float Reward, bool bTerminal)
{
	if (CachedRLAgent)
	{
		CachedRLAgent->ProvideReward(Reward, bTerminal);
	}
}

void AFollowerCharacter::AccumulateReward(float Reward)
{
	if (CachedRLAgent)
	{
		CachedRLAgent->AccumulateReward(Reward);
	}
}

float AFollowerCharacter::GetAccumulatedReward() const
{
	return CachedRLAgent ? CachedRLAgent->GetAccumulatedReward() : 0.0f;
}

URewardCalculator* AFollowerCharacter::GetRewardCalculator() const
{
	return CachedRLAgent ? CachedRLAgent->GetRewardCalculator() : nullptr;
}

bool AFollowerCharacter::IsUsingRLPolicy() const
{
	return CachedRLAgent ? CachedRLAgent->bUseRLPolicy : false;
}

URLPolicyNetwork* AFollowerCharacter::GetTacticalPolicy() const
{
	return CachedRLAgent ? CachedRLAgent->GetTacticalPolicy() : nullptr;
}

bool AFollowerCharacter::IsTacticalPolicyReady() const
{
	return CachedRLAgent ? CachedRLAgent->IsTacticalPolicyReady() : false;
}

//------------------------------------------------------------------------------
// COMBAT WRAPPERS (delegate to CombatExecutorComponent)
//------------------------------------------------------------------------------

void AFollowerCharacter::ExecuteCombat(const FCombatParameters& Params)
{
	if (CachedCombatExecutor)
	{
		CachedCombatExecutor->ExecuteCombat(Params);
	}
}

AActor* AFollowerCharacter::GetClosestEnemy(const TArray<AActor*>& Enemies) const
{
	return CachedCombatExecutor ? CachedCombatExecutor->GetClosestEnemy(Enemies) : nullptr;
}

AActor* AFollowerCharacter::GetLowestHPEnemy(const TArray<AActor*>& Enemies) const
{
	return CachedCombatExecutor ? CachedCombatExecutor->GetLowestHPEnemy(Enemies) : nullptr;
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
	if (CachedTacticalState)
	{
		CachedTacticalState->ResetState();
	}

	if (CachedObservationBuilder)
	{
		CachedObservationBuilder->ResetEpisode();
	}

	if (CachedRLAgent)
	{
		CachedRLAgent->ResetEpisode();
	}

	if (ContextBridgeComponent)
	{
		ContextBridgeComponent->ResetContext();
	}

	UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter] '%s': Episode reset complete"), *GetName());
}

void AFollowerCharacter::OnEpisodeEnded(float EpisodeReward)
{
	if (CachedRLAgent)
	{
		CachedRLAgent->OnEpisodeEnded(EpisodeReward);
	}
}

void AFollowerCharacter::MarkAsDead()
{
	if (CachedTacticalState)
	{
		CachedTacticalState->MarkAsDead();
	}

	// TODO: remove CombatExecutor -> move to state tree task
	if (CachedCombatExecutor)
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
	if (CachedTacticalState)
	{
		CachedTacticalState->MarkAsAlive();
	}

	if (CachedCombatExecutor)
	{
		ContextBridgeComponent->SetIsAlive(true);
	}

	if (ContextBridgeComponent)
	{
		ContextBridgeComponent->SetIsAlive(true);
	}

	if (CachedHealthComponent)
	{
		CachedHealthComponent->ResetHealth();
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
	return CachedTacticalState ? CachedTacticalState->IsAlive() : false;
}


void AFollowerCharacter::InitializeComponents()
{
	// Find all sub-components (ONLY place we use FindComponentByClass, called once in BeginPlay)
	// v9.0: Cached forever, zero searches at runtime

	CachedTacticalState = FindComponentByClass<UTacticalStateComponent>();
	CachedObservationBuilder = FindComponentByClass<UObservationBuilderComponent>();
	CachedRLAgent = FindComponentByClass<URLAgentComponent>();
	CachedCombatExecutor = FindComponentByClass<UCombatExecutorComponent>();
	CachedHealthComponent = FindComponentByClass<UHealthComponent>();
	CachedWeaponComponent = FindComponentByClass<UWeaponComponent>();
	CachedPerceptionComponent = FindComponentByClass<UAgentPerceptionComponent>();

	// Validate critical sub-components
	bool bAllComponentsValid = true;

	if (!CachedTacticalState)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter] '%s': Missing TacticalStateComponent!"), *GetName());
		bAllComponentsValid = false;
	}

	if (!CachedObservationBuilder)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter] '%s': Missing ObservationBuilderComponent!"), *GetName());
		bAllComponentsValid = false;
	}

	if (!CachedRLAgent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter] '%s': Missing RLAgentComponent!"), *GetName());
		bAllComponentsValid = false;
	}

	if (!CachedCombatExecutor)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter] '%s': Missing CombatExecutorComponent!"), *GetName());
		bAllComponentsValid = false;
	}

	if (!CachedHealthComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter] '%s': Missing HealthComponent!"), *GetName());
	}

	if (!CachedWeaponComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter] '%s': Missing WeaponComponent!"), *GetName());
	}

	if (!CachedPerceptionComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter] '%s': Missing AgentPerceptionComponent!"), *GetName());
	}

	if (bAllComponentsValid)
	{
		UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0 Phase4] '%s': All sub-components cached successfully"),
			*GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter v9.0 Phase4] '%s': Missing critical sub-components! System may malfunction."),
			*GetName());
	}
}

void AFollowerCharacter::InjectDependencies()
{
	// CombatExecutor needs: RewardCalculator, HealthComponent, PerceptionComponent
	if (CachedCombatExecutor)
	{
		if (CachedRLAgent)
		{
			URewardCalculator* RewardCalc = CachedRLAgent->GetRewardCalculator();
			if (RewardCalc)
			{
				CachedCombatExecutor->RewardCalculator = RewardCalc;
				UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0 Phase3] '%s': Injected RewardCalculator into CombatExecutor"),
					*GetName());
			}
		}

		if (CachedHealthComponent)
		{
			CachedCombatExecutor->SetHealthComponent(CachedHealthComponent);
		}

		if (CachedPerceptionComponent)
		{
			CachedCombatExecutor->SetPerceptionComponent(CachedPerceptionComponent);
		}
	}

	// ObservationBuilder needs: HealthComponent, PerceptionComponent
	if (CachedObservationBuilder)
	{
		if (CachedHealthComponent)
		{
			CachedObservationBuilder->SetHealthComponent(CachedHealthComponent);
		}

		if (CachedPerceptionComponent)
		{
			CachedObservationBuilder->SetPerceptionComponent(CachedPerceptionComponent);
		}

		UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0 Phase3] '%s': Injected dependencies into ObservationBuilder"),
			*GetName());
	}

	UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0 Phase3] '%s': Dependency injection complete (all components)"),
		*GetName());
}


AActor* AFollowerCharacter::FindTeamLeaderByTeamID()
{
	
	return nullptr;
}


bool AFollowerCharacter::ShouldUpdateStrategy() const
{
	// RATE LIMIT: Prevent oscillation - minimum 50ms between updates
	double CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - LastStrategyUpdateTime < MinStrategyUpdateInterval)
	{
		return false;
	}

	// Check if strategy assignment changed
	bool bAssignmentChanged = false;
	if (CachedTacticalState)
	{
		FStrategyAssignment Current = CachedTacticalState->GetStrategyAssignment();
		FStrategyAssignment Last = CachedTacticalState->GetLastAssignment();
		// v9.0: Only check strategy change (objectives are implicit in rewards)
		bAssignmentChanged = (Current.Strategy != Last.Strategy);
	}

	// Fallback: Force update every 30 ticks (~0.5s at 60 FPS)
	bool bTimeout = TicksSinceLastUpdate > 30;

	return bAssignmentChanged || bTimeout;
}

void AFollowerCharacter::ExecuteCombatInternal()
{
	if (!CachedCombatExecutor || !CachedTacticalState)
	{
		return;
	}

	FCombatParameters CombatParams = CachedTacticalState->GetCombatParameters();
	CachedCombatExecutor->ExecuteCombat(CombatParams);
}