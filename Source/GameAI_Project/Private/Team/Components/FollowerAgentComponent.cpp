#include "Team/Components/FollowerAgentComponent.h"
#include "Team/ObjectiveActor.h"
// v8.0 Refactored: New component includes
#include "RL/Components/TacticalStateComponent.h"
#include "Observation/Components/ObservationBuilderComponent.h"
#include "RL/Components/RLAgentComponent.h"
#include "Combat/Components/CombatExecutorComponent.h"
// v9.0 Phase 3: Manager components
#include "Team/Components/TeamCommsComponent.h"
#include "Team/Components/IntelManagerComponent.h"
#include "StateTree/Components/ContextBridgeComponent.h"
#include "Util/Components/VisualLoggerComponent.h"
// Other includes
#include "RL/RLPolicyNetwork.h"
#include "RL/Components/RewardCalculator.h"
#include "Combat/Components/AgentPerceptionComponent.h"
#include "Combat/Components/HealthComponent.h"
#include "Core/SimulationManagerGameMode.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "StateTree/FollowerStateTreeComponent.h"

UFollowerAgentComponent::UFollowerAgentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.0f;  // Every frame (event-driven work is cheap)
}

void UFollowerAgentComponent::BeginPlay()
{
	Super::BeginPlay();

	// ========================================
	// v8.0 REFACTORED: Find sub-components
	// ========================================
	TacticalState = GetOwner()->FindComponentByClass<UTacticalStateComponent>();
	ObservationBuilder = GetOwner()->FindComponentByClass<UObservationBuilderComponent>();
	RLAgent = GetOwner()->FindComponentByClass<URLAgentComponent>();
	CombatExecutor = GetOwner()->FindComponentByClass<UCombatExecutorComponent>();

	// ========================================
	// v9.0 PHASE 3: Find manager components
	// ========================================
	TeamComms = GetOwner()->FindComponentByClass<UTeamCommsComponent>();
	ContextBridge = GetOwner()->FindComponentByClass<UContextBridgeComponent>();
	VisualLogger = GetOwner()->FindComponentByClass<UVisualLoggerComponent>();

	// Validate required components
	if (!TacticalState)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent v9.0] '%s': Missing TacticalStateComponent! Agent will not function correctly."),
			*GetOwner()->GetName());
	}

	if (!ObservationBuilder)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent v9.0] '%s': Missing ObservationBuilderComponent! Observations will fail."),
			*GetOwner()->GetName());
	}

	if (!RLAgent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent v9.0] '%s': Missing RLAgentComponent! RL functionality disabled."),
			*GetOwner()->GetName());
	}

	if (!CombatExecutor)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent v9.0] '%s': Missing CombatExecutorComponent! Combat will fail."),
			*GetOwner()->GetName());
	}

	if (!TeamComms)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent v9.0] '%s': Missing TeamCommsComponent! Team communication disabled."),
			*GetOwner()->GetName());
	}

	if (!ContextBridge)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerAgent v9.0] '%s': Missing ContextBridgeComponent! StateTree context updates disabled."),
			*GetOwner()->GetName());
	}

	if (!VisualLogger)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerAgent v9.0] '%s': Missing VisualLoggerComponent! Debug visualization disabled."),
			*GetOwner()->GetName());
	}

	// ========================================
	// v8.0: Connect RLAgent RewardCalculator to CombatExecutor
	// ========================================
	if (RLAgent && CombatExecutor)
	{
		CombatExecutor->RewardCalculator = RLAgent->GetRewardCalculator();
	}

	// NOTE: Death event subscription is handled by FollowerCharacter (owner coordinates components)

	// v9.0 PHASE 3: Registration is now handled by TeamCommsComponent
	// TeamComms has its own BeginPlay that will auto-register if enabled

	// Initialize strategy update timestamp
	LastStrategyUpdateTime = FPlatformTime::Seconds();

	UE_LOG(LogTemp, Log, TEXT("[FollowerAgent v9.0 REFACTORED] Initialized on %s (TeamComms: %s, ContextBridge: %s, VisualLogger: %s)"),
		*GetOwner()->GetName(),
		TeamComms ? TEXT("OK") : TEXT("MISSING"),
		ContextBridge ? TEXT("OK") : TEXT("MISSING"),
		VisualLogger ? TEXT("OK") : TEXT("MISSING"));
}



void UFollowerAgentComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Check if simulation is running
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
	if (SimManager && !SimManager->IsSimulationRunning())
	{
		return;
	}

	// Skip if not alive
	if (!GetIsAlive())
	{
		return;
	}

	// ========================================
	// v8.0 HIERARCHICAL DECISION MAKING
	//
	// Layer 1 (Strategic - MCTS): Team Leader assigns STRATEGIES to agents
	// Layer 2 (Tactical - RL): Follower outputs TACTICAL PARAMETERS + COMBAT CHOICES
	// Layer 3 (Execution - EQS): EQS uses tactical parameters as query weights
	// Layer 4 (Execution - Rules): Combat execution with learned target priority
	// ========================================
	if (ShouldUpdateStrategy())
	{
		// Build observation (delegates to ObservationBuilderComponent)
		FObservationElement Obs = BuildLocalObservation();

		// Get assigned strategy from MCTS (delegates to TacticalStateComponent)
		EStrategyType AssignedStrategy = GetAssignedStrategy();

		// Query RL policy for tactical parameters + combat choices
		if (RLAgent && RLAgent->IsTacticalPolicyReady() && RLAgent->bUseRLPolicy)
		{
			// Run RL inference (2-4ms if batched)
			URLPolicyNetwork* Policy = RLAgent->GetTacticalPolicy();
			if (Policy)
			{
				FMacroAction NewAction = Policy->GetMacroAction(Obs, AssignedStrategy);

				// Update tactical state (delegates to TacticalStateComponent)
				if (TacticalState)
				{
					TacticalState->SetTacticalParameters(NewAction.TacticalParams);
					TacticalState->SetCombatParameters(NewAction.CombatParams);
				}

				// v9.0 PHASE 3: Update ContextBridge for StateTree
				if (ContextBridge)
				{
					ContextBridge->SetStrategy(AssignedStrategy);
					ContextBridge->SetTacticalParameters(NewAction.TacticalParams);
					ContextBridge->SetCombatParameters(NewAction.CombatParams);
					ContextBridge->SetIsAlive(GetIsAlive());
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
	// v8.0: Combat execution (every tick, 60 Hz)
	// Delegates to CombatExecutorComponent
	// ========================================
	ExecuteCombat();

	// Draw debug info if enabled - Phase 3: Check VisualLogger setting
	if (VisualLogger && VisualLogger->bEnableDebugDrawing)
	{
		DrawDebugInfo();
	}
}

void UFollowerAgentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{

	// NOTE: Death event unsubscription is handled by FollowerCharacter (owner coordinates components)

	Super::EndPlay(EndPlayReason);
}


void UFollowerAgentComponent::UpdateTacticalContext(AObjectiveActor* Friendly, AObjectiveActor* Hostile, const FTeamObservation& TeamObs)
{
	// 2. 하위 컴포넌트로 데이터 전파 (Push)
	if (ObservationBuilder)
	{
		ObservationBuilder->SetObjectives(Friendly, Hostile);
		ObservationBuilder->UpdateTeamIntel(TeamObs);
	}

	// 3. (옵션) TacticalState 등 다른 컴포넌트에도 필요하다면 전파

	UE_LOG(LogTemp, Verbose, TEXT("[%s] Tactical Context Updated via Push"), *GetOwner()->GetName());
}

//------------------------------------------------------------------------------
// TEAM LEADER COMMUNICATION (v9.0 PHASE 3: Delegates to TeamCommsComponent)
//------------------------------------------------------------------------------
void UFollowerAgentComponent::SignalEventToLeader(
	EStrategicEvent Event,
	AActor* Instigator,
	FVector Location,
	int32 Priority)
{
	if (!TeamComms)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent v9.0] '%s': Cannot signal event, missing TeamCommsComponent"),
			*GetOwner()->GetName());
		return;
	}

	TeamComms->SignalEventToLeader(Event, Instigator, Location, Priority);
}

//------------------------------------------------------------------------------
// v8.0: STRATEGY ASSIGNMENT (Delegates to TacticalStateComponent)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::SetStrategyAssignment(const FStrategyAssignment& Assignment)
{
	if (!TacticalState)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent] '%s': Cannot set strategy assignment, missing TacticalStateComponent"),
			*GetOwner()->GetName());
		return;
	}

	TacticalState->SetStrategyAssignment(Assignment);

	// Synchronize strategy to RewardCalculator (required for strategy-specific rewards)
	if (RLAgent)
	{
		if (URewardCalculator* RewardCalc = RLAgent->GetRewardCalculator())
		{
			RewardCalc->SetCurrentStrategy(Assignment.Strategy);
		}
	}

	// Update ContextBridge for StateTree
	if (ContextBridge)
	{
		ContextBridge->SetStrategy(Assignment.Strategy);
	}

	// Broadcast event and force update
	OnStrategyAssignmentReceived.Broadcast(Assignment);
	TicksSinceLastUpdate = 999;
}


//------------------------------------------------------------------------------
// TACTICAL & COMBAT PARAMETERS (Delegates to TacticalStateComponent)
//------------------------------------------------------------------------------

EStrategyType UFollowerAgentComponent::GetAssignedStrategy() const
{
	if (!TacticalState) return EStrategyType::Assault;
	return TacticalState->GetAssignedStrategy();
}

EStrategyType UFollowerAgentComponent::GetCurrentStrategy() const
{
	return GetAssignedStrategy();
}

FMacroAction UFollowerAgentComponent::GetCurrentMacroAction() const
{
	if (!TacticalState) return FMacroAction();
	return TacticalState->GetCurrentMacroAction();
}

FAllyContext UFollowerAgentComponent::GetAllyContext() const
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

void UFollowerAgentComponent::SetTacticalParameters(const FTacticalParameters& Params)
{
	if (!TacticalState)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent] '%s': Cannot set tactical parameters, missing TacticalStateComponent"),
			*GetOwner()->GetName());
		return;
	}

	TacticalState->SetTacticalParameters(Params);
}

void UFollowerAgentComponent::SetCombatParameters(const FCombatParameters& Params)
{
	if (!TacticalState)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent] '%s': Cannot set combat parameters, missing TacticalStateComponent"),
			*GetOwner()->GetName());
		return;
	}

	TacticalState->SetCombatParameters(Params);
}

FTacticalParameters UFollowerAgentComponent::GetTacticalParameters() const
{
	if (!TacticalState) return FTacticalParameters();
	return TacticalState->GetTacticalParameters();
}

FCombatParameters UFollowerAgentComponent::GetCombatParameters() const
{
	if (!TacticalState) return FCombatParameters();
	return TacticalState->GetCombatParameters();
}



//------------------------------------------------------------------------------
// STATE MANAGEMENT (Delegates to TacticalStateComponent & CombatExecutorComponent)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::MarkAsDead()
{
	if (TacticalState)
	{
		TacticalState->MarkAsDead();
	}

	if (CombatExecutor)
	{
		CombatExecutor->bIsAlive = false;
	}

	// v9.0 PHASE 3: Update ContextBridge
	if (ContextBridge)
	{
		ContextBridge->SetIsAlive(false);
	}

	// Deactivate the actor: disable collision and make invisible
	AActor* Owner = GetOwner();
	if (Owner)
	{
		Owner->SetActorEnableCollision(false);
		Owner->SetActorHiddenInGame(true);
	}

	// Signal death to team leader
	SignalEventToLeader(EStrategicEvent::AllyKilled, Owner, FVector::ZeroVector, 10);

	// Notify StateTree of death
	UFollowerStateTreeComponent* StateTreeComp = Owner ? Owner->FindComponentByClass<UFollowerStateTreeComponent>() : nullptr;
	if (StateTreeComp)
	{
		StateTreeComp->OnFollowerDied();
	}

	UE_LOG(LogTemp, Warning, TEXT("[FollowerAgent] '%s': Marked as dead (deactivated: collision OFF, visibility OFF)"),
		Owner ? *Owner->GetName() : TEXT("NULL"));
}

void UFollowerAgentComponent::MarkAsAlive()
{
	if (TacticalState)
	{
		TacticalState->MarkAsAlive();
	}

	if (CombatExecutor)
	{
		CombatExecutor->bIsAlive = true;
	}

	// v9.0 PHASE 3: Update ContextBridge
	if (ContextBridge)
	{
		ContextBridge->SetIsAlive(true);
	}

	// Reactivate the actor: enable collision and make visible
	AActor* Owner = GetOwner();
	if (Owner)
	{
		Owner->SetActorEnableCollision(true);
		Owner->SetActorHiddenInGame(false);
	}

	// Reset health to full
	UHealthComponent* HealthComp = Owner ? Owner->FindComponentByClass<UHealthComponent>() : nullptr;
	if (HealthComp)
	{
		HealthComp->ResetHealth();
	}

	// Reset episode
	ResetEpisode();

	// Notify StateTree of respawn
	UFollowerStateTreeComponent* StateTreeComp = Owner ? Owner->FindComponentByClass<UFollowerStateTreeComponent>() : nullptr;
	if (StateTreeComp && StateTreeComp->IsValidLowLevel() && StateTreeComp->GetWorld())
	{
		StateTreeComp->OnFollowerRespawned();
	}

	UE_LOG(LogTemp, Warning, TEXT("[FollowerAgent] '%s': Marked as alive (reactivated: collision ON, visibility ON)"),
		Owner ? *Owner->GetName() : TEXT("NULL"));
}

bool UFollowerAgentComponent::GetIsAlive() const
{
	if (!TacticalState) return true; // Default to alive if no component
	return TacticalState->IsAlive();
}

//------------------------------------------------------------------------------
// OBSERVATION (Delegates to ObservationBuilderComponent)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::UpdateLocalObservation(const FObservationElement& NewObservation)
{
	if (!ObservationBuilder)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent] '%s': Cannot update observation, missing ObservationBuilderComponent"),
			*GetOwner()->GetName());
		return;
	}

	ObservationBuilder->UpdateLocalObservation(NewObservation);
}

FObservationElement UFollowerAgentComponent::GetLocalObservation() const
{
	if (!ObservationBuilder) return FObservationElement();
	return ObservationBuilder->GetLocalObservation();
}

FObservationElement UFollowerAgentComponent::BuildLocalObservation()
{
	if (!ObservationBuilder)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent] '%s': Cannot build observation, missing ObservationBuilderComponent"),
			*GetOwner()->GetName());
		return FObservationElement();
	}

	FObservationElement Obs = ObservationBuilder->BuildLocalObservation();

	// 2. [v9.0 수정] 현재 할당된 전략 정보 주입
	// 이 정보가 있어야 ToFeatureVector()에서 One-Hot을 만들 수 있습니다.
	CurrentStrategy = GetAssignedStrategy();
	Obs.AssignedStrategyIndex = (int32)CurrentStrategy;

	// 3. Update stored observation
	ObservationBuilder->UpdateLocalObservation(Obs);

	return Obs;
}


//------------------------------------------------------------------------------
// REINFORCEMENT LEARNING (Delegates to RLAgentComponent)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::ProvideReward(float Reward, bool bTerminal)
{
	if (!RLAgent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent] '%s': Cannot provide reward, missing RLAgentComponent"),
			*GetOwner()->GetName());
		return;
	}

	RLAgent->ProvideReward(Reward, bTerminal);
}

void UFollowerAgentComponent::AccumulateReward(float Reward)
{
	if (!RLAgent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent] '%s': Cannot accumulate reward, missing RLAgentComponent"),
			*GetOwner()->GetName());
		return;
	}

	RLAgent->AccumulateReward(Reward);
}

float UFollowerAgentComponent::GetAccumulatedReward() const
{
	if (!RLAgent) return 0.0f;
	return RLAgent->GetAccumulatedReward();
}

void UFollowerAgentComponent::ResetEpisode()
{
	// Reset all sub-components
	if (TacticalState)
	{
		TacticalState->ResetState();
	}

	if (ObservationBuilder)
	{
		ObservationBuilder->ResetEpisode();
	}

	if (RLAgent)
	{
		RLAgent->ResetEpisode();
	}

	// v9.0 PHASE 3: Reset ContextBridge instead of direct StateTree manipulation
	if (ContextBridge)
	{
		ContextBridge->ResetContext();
	}

	// Reset event-driven tracking
	TicksSinceLastUpdate = 0;

	// v9.0 REFACTOR: ClearKnownEnemies() now called by TeamLeader::OnEpisodeStart()
	// No need to call it from follower - the leader owns this state and manages its lifecycle
}

void UFollowerAgentComponent::OnEpisodeEnded(float EpisodeReward)
{
	if (!RLAgent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent] '%s': Cannot end episode, missing RLAgentComponent"),
			*GetOwner()->GetName());
		return;
	}

	RLAgent->OnEpisodeEnded(EpisodeReward);
}

bool UFollowerAgentComponent::IsTacticalPolicyReady() const
{
	if (!RLAgent) return false;
	return RLAgent->IsTacticalPolicyReady();
}

bool UFollowerAgentComponent::IsUsingRLPolicy() const
{
	if (!RLAgent) return false;
	return RLAgent->bUseRLPolicy;
}

URLPolicyNetwork* UFollowerAgentComponent::GetTacticalPolicy() const
{
	if (!RLAgent) return nullptr;
	return RLAgent->GetTacticalPolicy();
}

//------------------------------------------------------------------------------
// UTILITY (v9.0 PHASE 3: Delegates to TeamCommsComponent)
//------------------------------------------------------------------------------

int32 UFollowerAgentComponent::GetTeamID() const
{
	if (!TeamComms) return -1;
	return TeamComms->GetTeamID();
}


//------------------------------------------------------------------------------
// DEBUG VISUALIZATION (v9.0 PHASE 3: Delegates to VisualLoggerComponent)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::DrawDebugInfo()
{
	if (!VisualLogger || !VisualLogger->bEnableDebugDrawing)
	{
		return; // Visual logger not available or disabled
	}

	if (!GetOwner() || !TacticalState) return;

	// Get current state
	FVector Location = GetOwner()->GetActorLocation();
	EStrategyType Strategy = GetAssignedStrategy();

	// Get health from HealthComponent if available
	float Health = 1.0f;
	if (UHealthComponent* HealthComp = GetOwner()->FindComponentByClass<UHealthComponent>())
	{
		Health = HealthComp->GetHealthPercentage();
	}

	FTacticalParameters TacticalParams = GetTacticalParameters();
	AObjectiveActor* TargetObjective = nullptr; // v9.0: Objectives implicit, but can show for debugging

	// Delegate to VisualLogger
	VisualLogger->DrawFollowerState(Location, Strategy, Health, TacticalParams, TargetObjective);
}

//------------------------------------------------------------------------------
// COMBAT EXECUTION (Delegates to CombatExecutorComponent)
//------------------------------------------------------------------------------

void UFollowerAgentComponent::ExecuteCombat()
{
	if (!CombatExecutor || !TacticalState)
	{
		return;
	}

	FCombatParameters CombatParams = TacticalState->GetCombatParameters();
	CombatExecutor->ExecuteCombat(CombatParams);
}


//------------------------------------------------------------------------------
// EVENT-DRIVEN STRATEGY UPDATES
//------------------------------------------------------------------------------

bool UFollowerAgentComponent::ShouldUpdateStrategy() const
{
	// RATE LIMIT: Prevent oscillation
	double CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - LastStrategyUpdateTime < MinStrategyUpdateInterval)
	{
		return false;
	}

	// Get current enemy count
	int32 CurrentEnemyCount = 0;
	UAgentPerceptionComponent* PerceptionComp = GetOwner()->FindComponentByClass<UAgentPerceptionComponent>();
	if (PerceptionComp)
	{
		CurrentEnemyCount = PerceptionComp->GetDetectedEnemies().Num();
	}

	// Check assignment change (delegates to TacticalStateComponent)
	bool bAssignmentChanged = false;
	if (TacticalState)
	{
		FStrategyAssignment Current = TacticalState->GetStrategyAssignment();
		FStrategyAssignment Last = TacticalState->GetLastAssignment();
		// v9.0: Only check strategy change (objectives implicit)
		bAssignmentChanged = (Current.Strategy != Last.Strategy);
	}

	// Fallback: Force update every 30 ticks (~0.5s at 60 FPS)
	bool bTimeout = TicksSinceLastUpdate > 30;

	return bAssignmentChanged || bTimeout;
}