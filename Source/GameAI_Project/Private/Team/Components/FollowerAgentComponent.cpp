#include "Team/Components/FollowerAgentComponent.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Team/ObjectiveActor.h"
// v8.0 Refactored: New component includes
#include "RL/Components/TacticalStateComponent.h"
#include "Observation/Components/ObservationBuilderComponent.h"
#include "RL/Components/RLAgentComponent.h"
#include "Combat/Components/CombatExecutorComponent.h"
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

	// Validate required components
	if (!TacticalState)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent v8.0] '%s': Missing TacticalStateComponent! Agent will not function correctly."),
			*GetOwner()->GetName());
	}

	if (!ObservationBuilder)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent v8.0] '%s': Missing ObservationBuilderComponent! Observations will fail."),
			*GetOwner()->GetName());
	}

	if (!RLAgent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent v8.0] '%s': Missing RLAgentComponent! RL functionality disabled."),
			*GetOwner()->GetName());
	}

	if (!CombatExecutor)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent v8.0] '%s': Missing CombatExecutorComponent! Combat will fail."),
			*GetOwner()->GetName());
	}

	// ========================================
	// Find team leader component
	// ========================================
	if (!TeamLeader)
	{
		// Option 1: Get from TeamLeaderActor if specified
		if (TeamLeaderActor)
		{
			TeamLeader = TeamLeaderActor->FindComponentByClass<UTeamLeaderComponent>();
			if (TeamLeader)
			{
				UE_LOG(LogTemp, Log, TEXT("[FollowerAgent] '%s': Found TeamLeader on specified actor '%s'"),
					*GetOwner()->GetName(), *TeamLeaderActor->GetName());
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[FollowerAgent] '%s': TeamLeaderActor '%s' has no TeamLeaderComponent!"),
					*GetOwner()->GetName(), *TeamLeaderActor->GetName());
			}
		}
		// Option 2: Auto-find by tag
		else if (TeamLeaderTag != NAME_None)
		{
			TArray<AActor*> FoundActors;
			UGameplayStatics::GetAllActorsWithTag(GetWorld(), TeamLeaderTag, FoundActors);

			if (FoundActors.Num() > 0)
			{
				TeamLeaderActor = FoundActors[0];
				TeamLeader = TeamLeaderActor->FindComponentByClass<UTeamLeaderComponent>();

				if (TeamLeader)
				{
					UE_LOG(LogTemp, Log, TEXT("[FollowerAgent] '%s': Auto-found TeamLeader on actor '%s' by tag '%s'"),
						*GetOwner()->GetName(), *TeamLeaderActor->GetName(), *TeamLeaderTag.ToString());
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[FollowerAgent] '%s': Found actor with tag '%s' but no TeamLeaderComponent"),
						*GetOwner()->GetName(), *TeamLeaderTag.ToString());
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[FollowerAgent] '%s': No actor found with tag '%s'"),
					*GetOwner()->GetName(), *TeamLeaderTag.ToString());
			}
		}
	}

	// ========================================
	// v8.0: Set TeamLeader reference in sub-components
	// ========================================
	if (TeamLeader)
	{
		if (ObservationBuilder)
		{
			ObservationBuilder->TeamLeader = TeamLeader;
		}

		if (CombatExecutor)
		{
			CombatExecutor->TeamLeader = TeamLeader;
		}
	}

	// ========================================
	// v8.0: Connect RLAgent RewardCalculator to CombatExecutor
	// ========================================
	if (RLAgent && CombatExecutor)
	{
		CombatExecutor->RewardCalculator = RLAgent->GetRewardCalculator();
	}

	// Auto-register with team leader
	if (bAutoRegisterWithLeader && TeamLeader)
	{
		RegisterWithTeamLeader();
	}

	// Initialize strategy update timestamp
	LastStrategyUpdateTime = FPlatformTime::Seconds();

	UE_LOG(LogTemp, Log, TEXT("[FollowerAgent v8.0 REFACTORED] Initialized on %s"), *GetOwner()->GetName());
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

				// Update timestamp
				LastStrategyUpdateTime = FPlatformTime::Seconds();
			}
		}

		// Cache state for next event check
		UAgentPerceptionComponent* PerceptionComp = GetOwner()->FindComponentByClass<UAgentPerceptionComponent>();
		if (PerceptionComp)
		{
			LastEnemyCount = PerceptionComp->GetDetectedEnemies().Num();
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

	// Draw debug info if enabled
	if (bEnableDebugDrawing)
	{
		DrawDebugInfo();
	}
}

void UFollowerAgentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregister from team leader
	UnregisterFromTeamLeader();

	Super::EndPlay(EndPlayReason);
}

//------------------------------------------------------------------------------
// TEAM LEADER COMMUNICATION
//------------------------------------------------------------------------------

bool UFollowerAgentComponent::RegisterWithTeamLeader()
{
	if (!TeamLeader)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerAgent] '%s': No TeamLeader set, cannot register"),
			*GetOwner()->GetName());
		return false;
	}

	bool bSuccess = TeamLeader->RegisterFollower(GetOwner());

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("[FollowerAgent] '%s': Registered with TeamLeader '%s'"),
			*GetOwner()->GetName(), *TeamLeader->TeamName);

		// Also register with SimulationManager
		ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
		if (SimManager)
		{
			bool bSimRegistered = SimManager->RegisterTeamMember(TeamLeader->TeamID, GetOwner());
			if (bSimRegistered)
			{
				UE_LOG(LogTemp, Warning, TEXT("✅ Follower '%s': Registered with SimulationManager (TeamID: %d)"),
					*GetOwner()->GetName(), TeamLeader->TeamID);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("❌ Follower '%s': Failed to register with SimulationManager (TeamID: %d)"),
					*GetOwner()->GetName(), TeamLeader->TeamID);
			}
		}
	}

	return bSuccess;
}

void UFollowerAgentComponent::UnregisterFromTeamLeader()
{
	if (!TeamLeader) return;

	if (IsRegisteredWithLeader())
	{
		TeamLeader->UnregisterFollower(GetOwner());
		UE_LOG(LogTemp, Log, TEXT("[FollowerAgent] '%s': Unregistered from TeamLeader"),
			*GetOwner()->GetName());
	}
}

void UFollowerAgentComponent::SignalEventToLeader(
	EStrategicEvent Event,
	AActor* Instigator,
	FVector Location,
	int32 Priority)
{
	if (!TeamLeader)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerAgent] '%s': ❌ Cannot signal event, no TeamLeader!"),
			*GetOwner()->GetName());
		return;
	}

	// Use owner's location if not specified
	if (Location.IsZero() && GetOwner())
	{
		Location = GetOwner()->GetActorLocation();
	}

	FString EventName = UEnum::GetValueAsString(Event);
	FString InstigatorName = Instigator ? Instigator->GetName() : TEXT("None");

	UE_LOG(LogTemp, Warning, TEXT("[FollowerAgent] '%s': 📡 Signaling event '%s' to Team Leader '%s' (Instigator: %s, Priority: %d)"),
		*GetOwner()->GetName(),
		*EventName,
		*TeamLeader->TeamName,
		*InstigatorName,
		Priority);

	TeamLeader->ProcessStrategicEvent(Event, Instigator, Location, Priority);

	// Broadcast event
	OnEventSignaled.Broadcast(Event, Instigator, Priority);
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

	UE_LOG(LogTemp, Warning, TEXT("📝 [FOLLOWER v8.0] '%s': Strategy assignment received - Strategy=%s, Objective=%s"),
		*GetOwner()->GetName(),
		*UEnum::GetValueAsString(Assignment.Strategy),
		Assignment.TargetObjective ? *Assignment.TargetObjective->GetName() : TEXT("None"));

	// Broadcast event for StateTree or other systems
	OnStrategyAssignmentReceived.Broadcast(Assignment);

	// Force strategy update on next tick
	TicksSinceLastUpdate = 999;
}

AObjectiveActor* UFollowerAgentComponent::GetTargetObjective() const
{
	if (!TacticalState) return nullptr;
	return TacticalState->GetTargetObjective();
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

	// Signal death to team leader
	SignalEventToLeader(EStrategicEvent::AllyKilled, GetOwner(), FVector::ZeroVector, 10);

	// Notify StateTree of death
	UFollowerStateTreeComponent* StateTreeComp = GetOwner()->FindComponentByClass<UFollowerStateTreeComponent>();
	if (StateTreeComp)
	{
		StateTreeComp->OnFollowerDied();
	}

	UE_LOG(LogTemp, Warning, TEXT("[FollowerAgent] '%s': Marked as dead"), *GetOwner()->GetName());
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

	// Reset health to full
	UHealthComponent* HealthComp = GetOwner()->FindComponentByClass<UHealthComponent>();
	if (HealthComp)
	{
		HealthComp->ResetHealth();
	}

	// Reset episode
	ResetEpisode();

	// Notify StateTree of respawn
	UFollowerStateTreeComponent* StateTreeComp = GetOwner()->FindComponentByClass<UFollowerStateTreeComponent>();
	if (StateTreeComp && StateTreeComp->IsValidLowLevel() && StateTreeComp->GetWorld())
	{
		StateTreeComp->OnFollowerRespawned();
	}

	UE_LOG(LogTemp, Warning, TEXT("[FollowerAgent] '%s': Respawn complete"), *GetOwner()->GetName());
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

	return ObservationBuilder->BuildLocalObservation();
}

bool UFollowerAgentComponent::FindNearestCover(FVector& OutCoverLocation, float& OutDistance, const TArray<AActor*>& Enemies)
{
	if (!ObservationBuilder) return false;
	return ObservationBuilder->FindNearestCover(OutCoverLocation, OutDistance, Enemies);
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

	// Reset event-driven tracking
	LastEnemyCount = 0;
	TicksSinceLastUpdate = 0;

	// Clear StateTree context
	UFollowerStateTreeComponent* StateTreeComp = GetOwner()->FindComponentByClass<UFollowerStateTreeComponent>();
	if (StateTreeComp)
	{
		FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
		SharedContext.VisibleEnemies.Empty();
		SharedContext.PrimaryTarget = nullptr;
		SharedContext.DistanceToPrimaryTarget = 99999.0f;
	}

	// Clear team leader's known enemies (first follower only)
	if (TeamLeader)
	{
		TArray<AActor*> AllFollowers = TeamLeader->GetFollowers();
		if (AllFollowers.Num() > 0 && AllFollowers[0] == GetOwner())
		{
			TeamLeader->ClearKnownEnemies();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[FollowerAgent v8.0] '%s': Episode reset"), *GetOwner()->GetName());
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
// UTILITY
//------------------------------------------------------------------------------

int32 UFollowerAgentComponent::GetTeamID() const
{
	if (TeamLeader)
	{
		return TeamLeader->TeamID;
	}
	else
	{
		return -1; // Invalid team
	}
}

bool UFollowerAgentComponent::IsRegisteredWithLeader() const
{
	if (!TeamLeader) return false;
	return TeamLeader->IsFollowerRegistered(GetOwner());
}

//------------------------------------------------------------------------------
// DEBUG VISUALIZATION
//------------------------------------------------------------------------------

void UFollowerAgentComponent::DrawDebugInfo()
{
	if (!GetOwner() || !TacticalState) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FVector FollowerPos = GetOwner()->GetActorLocation();

	// Draw strategy assignment info
	FStrategyAssignment Assignment = TacticalState->GetStrategyAssignment();
	FString StrategyStr = UEnum::GetValueAsString(Assignment.Strategy);
	FString ObjectiveStr = Assignment.TargetObjective ? Assignment.TargetObjective->GetName() : TEXT("None");

	FString StateText = FString::Printf(TEXT("Alive: %s\nStrategy: %s\nObjective: %s"),
		GetIsAlive() ? TEXT("Yes") : TEXT("Dead"),
		*StrategyStr,
		*ObjectiveStr);

	DrawDebugString(World, FollowerPos + FVector(0, 0, 120), StateText, nullptr, FColor::Cyan, 0.1f, true);

	// Draw line to target objective
	if (Assignment.TargetObjective && IsValid(Assignment.TargetObjective))
	{
		FVector TargetPos = Assignment.TargetObjective->GetActorLocation();
		DrawDebugLine(World, FollowerPos, TargetPos, FColor::Red, false, 0.1f, 0, 2.0f);
		DrawDebugSphere(World, TargetPos, 50.0f, 8, FColor::Red, false, 0.1f);
	}

	// Draw line to team leader
	if (TeamLeader && TeamLeader->GetOwner())
	{
		FVector LeaderPos = TeamLeader->GetOwner()->GetActorLocation();
		DrawDebugLine(World, FollowerPos, LeaderPos, TeamLeader->TeamColor.ToFColor(true), false, 0.1f, 0, 1.0f);
	}
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

AActor* UFollowerAgentComponent::GetClosestEnemy(const TArray<AActor*>& Enemies) const
{
	if (!CombatExecutor) return nullptr;
	return CombatExecutor->GetClosestEnemy(Enemies);
}

AActor* UFollowerAgentComponent::GetLowestHPEnemy(const TArray<AActor*>& Enemies) const
{
	if (!CombatExecutor) return nullptr;
	return CombatExecutor->GetLowestHPEnemy(Enemies);
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
	bool bNewEnemyDetected = CurrentEnemyCount > LastEnemyCount;

	// Check assignment change (delegates to TacticalStateComponent)
	bool bAssignmentChanged = false;
	if (TacticalState)
	{
		FStrategyAssignment Current = TacticalState->GetStrategyAssignment();
		FStrategyAssignment Last = TacticalState->GetLastAssignment();
		bAssignmentChanged = (Current.Strategy != Last.Strategy) ||
		                     (Current.TargetObjective != Last.TargetObjective);
	}

	// Fallback: Force update every 30 ticks (~0.5s at 60 FPS)
	bool bTimeout = TicksSinceLastUpdate > 30;

	return bNewEnemyDetected || bAssignmentChanged || bTimeout;
}
