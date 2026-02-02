// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actor/FollowerCharacter.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/Components/TeamCommsComponent.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/Components/ContextBridgeComponent.h"
#include "Util/Components/VisualLoggerComponent.h"
#include "Combat/Components/HealthComponent.h"
#include "Combat/Components/WeaponComponent.h"
// v9.0 Phase 4: Sub-component includes for wrapper API
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
// v9.0 Phase 4: Team communication includes
#include "EngineUtils.h"

AFollowerCharacter::AFollowerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	//--------------------------------------------------------------------------
	// v9.0: Create components (decomposed architecture)
	//--------------------------------------------------------------------------

	// Core components (v8.0)
	FollowerAgentComponent = CreateDefaultSubobject<UFollowerAgentComponent>(TEXT("FollowerAgentComponent"));
	StateTreeComponent = CreateDefaultSubobject<UFollowerStateTreeComponent>(TEXT("StateTreeComponent"));

	// Context (v9.0)
	ContextBridgeComponent = CreateDefaultSubobject<UContextBridgeComponent>(TEXT("ContextBridgeComponent"));

	// Debug visualization (optional - can be disabled in editor)
	VisualLoggerComponent = CreateDefaultSubobject<UVisualLoggerComponent>(TEXT("VisualLoggerComponent"));

	//--------------------------------------------------------------------------
	// Component Configuration
	//--------------------------------------------------------------------------

	// v9.0 Phase 4: Team communication is now merged into character (no separate component)
	// Configuration: bAutoRegisterWithLeader = true, TeamID = 0 (set in header defaults)

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

	//--------------------------------------------------------------------------
	// v9.0 Phase 4: Initialize all component references (ONCE in BeginPlay)
	//--------------------------------------------------------------------------
	InitializeComponents();

	//--------------------------------------------------------------------------
	// v9.0 Phase 4: Inject dependencies (NO cross-component FindComponentByClass)
	//--------------------------------------------------------------------------
	InjectDependencies();

	//--------------------------------------------------------------------------
	// v8.0: Component Coordination - Subscribe HealthComponent death events
	//--------------------------------------------------------------------------
	if (CachedHealthComponent && FollowerAgentComponent)
	{
		CachedHealthComponent->OnDeath.AddDynamic(this, &AFollowerCharacter::OnHealthComponentDeath);
		UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': Connected HealthComponent::OnDeath to FollowerAgentComponent coordination"),
			*GetName());
	}
	else
	{
		if (!CachedHealthComponent)
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
	// v9.0 Phase 4: Team Communication (merged from TeamCommsComponent)
	//--------------------------------------------------------------------------

	// Auto-register with team leader (merged logic)
	if (bAutoRegisterWithLeader)
	{
		if (RegisterWithLeader())
		{
			UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': Successfully registered with team leader"),
				*GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter v9.0] '%s': Failed to register with team leader - check TeamLeader tag or TeamID"),
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

void AFollowerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// v9.0 Phase 4: Unregister from team leader (merged from TeamCommsComponent)
	UnregisterFromLeader();

	Super::EndPlay(EndPlayReason);
}

void AFollowerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

//------------------------------------------------------------------------------
// COMBAT STATS (ICombatStatsInterface Implementation)
// Delegates to HealthComponent and WeaponComponent
// v9.0 Phase 4: Now uses cached components (no FindComponentByClass)
//------------------------------------------------------------------------------

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

//==============================================================================
// v9.0 PHASE 4: CHARACTER WRAPPER API IMPLEMENTATIONS
// Simple delegation to cached sub-components (zero FindComponentByClass at runtime)
//==============================================================================

//------------------------------------------------------------------------------
// TACTICAL STATE WRAPPERS (delegate to TacticalStateComponent)
//------------------------------------------------------------------------------

void AFollowerCharacter::SetStrategyAssignment(const FStrategyAssignment& Assignment)
{
	if (CachedTacticalState)
	{
		CachedTacticalState->SetStrategyAssignment(Assignment);
	}
}

EStrategyType AFollowerCharacter::GetAssignedStrategy() const
{
	return CachedTacticalState ? CachedTacticalState->GetAssignedStrategy() : EStrategyType::None;
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

//------------------------------------------------------------------------------
// TEAM COMMUNICATION WRAPPERS (v9.0 Phase 4: merged from TeamCommsComponent)
//------------------------------------------------------------------------------

void AFollowerCharacter::SignalEventToLeader(EStrategicEvent Event, AActor* Instigator, FVector Location, int32 Priority)
{
	if (!CachedTeamLeader)
	{
		if (bEnableTeamCommsLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter v9.0] '%s': Cannot signal event - no team leader"),
				*GetName());
		}
		return;
	}

	// Forward event to team leader
	CachedTeamLeader->ProcessStrategicEvent(Event, Instigator, Location, Priority);

	if (bEnableTeamCommsLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': Signaled event %d (priority %d) to leader"),
			*GetName(), static_cast<int32>(Event), Priority);
	}
}

UTeamLeaderComponent* AFollowerCharacter::GetTeamLeader() const
{
	if (!CachedTeamLeader)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter v9.0] '%s': GetTeamLeader called but CachedTeamLeader is null"),
			*GetName());
		return nullptr;
	}

	return CachedTeamLeader;
}

int32 AFollowerCharacter::GetTeamID() const
{
	return TeamID;
}

bool AFollowerCharacter::IsRegisteredWithLeader() const
{
	return bIsRegisteredWithLeader;
}

//------------------------------------------------------------------------------
// CONTEXT BRIDGE WRAPPERS (delegate to ContextBridgeComponent)
//------------------------------------------------------------------------------

void AFollowerCharacter::UpdateContextBridge()
{
	// Context bridge is updated by FollowerAgentComponent, this is a pass-through
	if (FollowerAgentComponent)
	{
		// FollowerAgent handles context bridge updates internally
	}
}

UContextBridgeComponent* AFollowerCharacter::GetContextBridge() const
{
	return ContextBridgeComponent;
}

//------------------------------------------------------------------------------
// LIFECYCLE WRAPPERS (coordinate all components)
//------------------------------------------------------------------------------

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

	if (FollowerAgentComponent)
	{
		FollowerAgentComponent->ResetEpisode();
	}

	UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter] '%s': Episode reset complete"), *GetName());
}

void AFollowerCharacter::MarkAsDead()
{
	if (CachedTacticalState)
	{
		CachedTacticalState->MarkAsDead();
	}

	if (FollowerAgentComponent)
	{
		FollowerAgentComponent->MarkAsDead();
	}
}

void AFollowerCharacter::MarkAsAlive()
{
	if (CachedTacticalState)
	{
		CachedTacticalState->MarkAsAlive();
	}

	if (FollowerAgentComponent)
	{
		FollowerAgentComponent->MarkAsAlive();
	}
}

bool AFollowerCharacter::IsAliveState() const
{
	return CachedTacticalState ? CachedTacticalState->IsAlive() : false;
}

//==============================================================================
// v9.0 PHASE 4: INITIALIZATION AND DEPENDENCY INJECTION
//==============================================================================

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
	// v9.0 Phase 3: Dependency Injection Pattern (COMPLETE)
	// Components get dependencies from character instead of searching themselves
	// This eliminates ALL cross-component FindComponentByClass calls at runtime

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

//==============================================================================
// v9.0 PHASE 4: TEAM COMMUNICATION (merged from TeamCommsComponent)
//==============================================================================

AActor* AFollowerCharacter::FindTeamLeaderByTeamID()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Search for actor with TeamLeaderComponent matching our TeamID
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		UTeamLeaderComponent* LeaderComp = Actor->FindComponentByClass<UTeamLeaderComponent>();
		if (LeaderComp && LeaderComp->TeamID == this->TeamID)
		{
			if (bEnableTeamCommsLogging)
			{
				UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': Found team leader by TeamID %d: %s"),
					*GetName(), TeamID, *Actor->GetName());
			}
			return Actor;
		}
	}

	if (bEnableTeamCommsLogging)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter v9.0] '%s': No team leader found with TeamID %d"),
			*GetName(), TeamID);
	}

	return nullptr;
}

bool AFollowerCharacter::ResolveTeamLeaderComponent()
{
	// Step 1: Find TeamLeaderActor by TeamID if not cached
	if (!CachedTeamLeaderActor)
	{
		CachedTeamLeaderActor = FindTeamLeaderByTeamID();
		if (!CachedTeamLeaderActor)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter v9.0] '%s': No team leader actor found with TeamID %d"),
				*GetName(), TeamID);
			return false;
		}
	}

	// Step 2: Get TeamLeaderComponent from actor
	CachedTeamLeader = CachedTeamLeaderActor->FindComponentByClass<UTeamLeaderComponent>();
	if (!CachedTeamLeader)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter v9.0] '%s': TeamLeaderActor %s has no TeamLeaderComponent"),
			*GetName(), *CachedTeamLeaderActor->GetName());
		return false;
	}

	// Step 3: Verify TeamID matches
	if (CachedTeamLeader->TeamID != this->TeamID)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter v9.0] '%s': TeamID mismatch - Expected %d, Leader has %d"),
			*GetName(), this->TeamID, CachedTeamLeader->TeamID);
		return false;
	}

	if (bEnableTeamCommsLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': Resolved TeamLeaderComponent from %s (TeamID: %d)"),
			*GetName(), *CachedTeamLeaderActor->GetName(), TeamID);
	}

	return true;
}

bool AFollowerCharacter::RegisterWithLeader()
{
	// Step 1: Resolve TeamLeaderComponent if not already set
	if (!CachedTeamLeader)
	{
		if (!ResolveTeamLeaderComponent())
		{
			if (bEnableTeamCommsLogging)
			{
				UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter v9.0] '%s': Failed to resolve TeamLeaderComponent"),
					*GetName());
			}
			return false;
		}
	}

	// Step 2: Attempt registration
	if (!CachedTeamLeader)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerCharacter v9.0] '%s': TeamLeader is null after resolution"), *GetName());
		return false;
	}

	bool bSuccess = CachedTeamLeader->RegisterFollower(this);

	if (bSuccess)
	{
		bIsRegisteredWithLeader = true;

		if (bEnableTeamCommsLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': Successfully registered with leader %s"),
				*GetName(), *CachedTeamLeaderActor->GetName());
		}
	}
	else
	{
		if (bEnableTeamCommsLogging)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FollowerCharacter v9.0] '%s': Registration failed (squad may be full)"),
				*GetName());
		}
	}

	return bSuccess;
}

void AFollowerCharacter::UnregisterFromLeader()
{
	if (CachedTeamLeader && bIsRegisteredWithLeader)
	{
		CachedTeamLeader->UnregisterFollower(this);
		bIsRegisteredWithLeader = false;

		if (bEnableTeamCommsLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] '%s': Unregistered from leader"), *GetName());
		}
	}
}
