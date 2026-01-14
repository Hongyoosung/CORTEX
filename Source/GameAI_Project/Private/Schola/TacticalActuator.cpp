// TacticalActuator.cpp - Schola actuator implementation

#include "Schola/TacticalActuator.h"
#include "Team/FollowerAgentComponent.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "GameFramework/Pawn.h"
#include "Inference/InferenceComponent.h"
#include "StateTreeExecutionTypes.h"

UTacticalActuator::UTacticalActuator()
{
	LastMacroAction = FMacroAction();
	MaxEnemies = 10; // Default max enemies for dynamic action space
}

FDiscreteSpace UTacticalActuator::GetActionSpace()
{
	// v8.0: DEPRECATED - This actuator is for v7.0 architecture
	// v8.0 uses tactical parameters (continuous) + combat parameters (discrete)
	// TODO: Implement BoxSpace for continuous tactical params + MultiDiscrete for combat

	// Temporary: Return empty action space to prevent usage
	// This file should be replaced with v8.0 TacticalActuator implementation
	TArray<int32> Nvec = { 1 };  // Dummy placeholder
	FDiscreteSpace ActionSpace = FDiscreteSpace(Nvec);

	UE_LOG(LogTemp, Error, TEXT("[TacticalActuator v7.0 DEPRECATED] GetActionSpace() called - v8.0 requires new actuator implementation!"));
	return ActionSpace;
}

void UTacticalActuator::TakeAction(const FDiscretePoint& Action)
{
	if (!FollowerAgent || !FollowerAgent->IsValidLowLevel() || !FollowerAgent->GetOwner())
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalActuator] %s: FollowerAgent not found or invalid!"),
			*GetNameSafe(GetOuter()));
		return;
	}

	// Find state tree component
	UFollowerStateTreeComponent* StateTreeComp = FindStateTreeComponent();
	if (!StateTreeComp || !StateTreeComp->IsValidLowLevel())
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalActuator] %s: StateTreeComponent not found or invalid!"),
			*GetNameSafe(GetOuter()));
		return;
	}

	// CRITICAL: Check if StateTree is in a valid state before accessing context
	EStateTreeRunStatus StateTreeStatus = StateTreeComp->GetStateTreeRunStatus();
	if (StateTreeStatus == EStateTreeRunStatus::Failed || StateTreeStatus == EStateTreeRunStatus::Unset)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalActuator] %s: StateTree not ready (Status=%s), ignoring action"),
			*GetNameSafe(GetOuter()), *UEnum::GetValueAsString(StateTreeStatus));
		return;
	}

	// v8.0: This v7.0 actuator is DEPRECATED
	// v8.0 architecture: MCTS assigns strategies, RL outputs tactical parameters
	// FMacroAction no longer has Strategy field - it only contains TacticalParams and CombatParams

	UE_LOG(LogTemp, Error, TEXT("[TacticalActuator v7.0 DEPRECATED] TakeAction() called - this file should not be used in v8.0!"));
	UE_LOG(LogTemp, Error, TEXT("[TacticalActuator v7.0 DEPRECATED] v8.0 requires: Continuous tactical params + Discrete combat params"));
	UE_LOG(LogTemp, Error, TEXT("[TacticalActuator v7.0 DEPRECATED] Strategies are now assigned by MCTS, not RL"));

	// Do nothing - prevent crashes but don't execute legacy logic
	return;
}

void UTacticalActuator::InitializeActuator()
{
	// Auto-find follower agent if enabled
	if (bAutoFindFollower && !FollowerAgent)
	{
		FollowerAgent = FindFollowerAgent();
	}

	if (!FollowerAgent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalActuator] %s: Failed to find FollowerAgentComponent!"),
			*GetNameSafe(GetOuter()));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[TacticalActuator] %s: Initialized (Follower=%s, ActionSpace=MultiDiscrete([4,%d,3]))"),
		*GetNameSafe(GetOuter()), *GetNameSafe(FollowerAgent), MaxEnemies + 1);
}

UFollowerAgentComponent* UTacticalActuator::FindFollowerAgent() const
{
	AActor* Owner = GetTypedOuter<AActor>();
	if (!Owner)
	{
		return nullptr;
	}

	return Owner->FindComponentByClass<UFollowerAgentComponent>();
}

UFollowerStateTreeComponent* UTacticalActuator::FindStateTreeComponent() const
{
	AActor* Owner = GetTypedOuter<AActor>();
	if (!Owner)
	{
		return nullptr;
	}

	return Owner->FindComponentByClass<UFollowerStateTreeComponent>();
}
