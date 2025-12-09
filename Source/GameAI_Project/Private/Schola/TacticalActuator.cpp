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
	LastAction = FTacticalAction();
}

FBoxSpace UTacticalActuator::GetActionSpace()
{
	TArray<float> LowBounds = { -1.0f, -1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f };
	TArray<float> HighBounds = { 1.0f,  1.0f, 1.0f,  1.0f,  1.0f, 1.0f, 1.0f, 1.0f };
	TArray<int> Shape = { 8 };

	FBoxSpace ActionSpace = FBoxSpace(LowBounds, HighBounds, Shape);

	return ActionSpace;
}

void UTacticalActuator::TakeAction(const FBoxPoint& Action)
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

	// Validate action dimensions
	if (Action.Values.Num() < 8)
	{
		UE_LOG(LogTemp, Error, TEXT("[TacticalActuator] %s: Invalid action dimensions (expected 8, got %d)"),
			*GetNameSafe(GetOuter()), Action.Values.Num());
		return;
	}

	// CRITICAL FIX: Ignore zero-filled dummy actions from VectorEnv batching
	// VectorEnv sends (num_envs, 8) batches where only one row is the real action
	// Schola dispatches ALL rows, so we get multiple TakeAction calls (1 real + N-1 zeros)
	// Skip if all values are near zero (tolerance for floating point errors)
	bool bIsZeroAction = true;
	const float ZeroThreshold = 0.001f;
	for (int32 i = 0; i < Action.Values.Num(); ++i)
	{
		if (FMath::Abs(Action.Values[i]) > ZeroThreshold)
		{
			bIsZeroAction = false;
			break;
		}
	}

	if (bIsZeroAction)
	{
		// Silently ignore zero actions (expected batching artifact)
		return;
	}

	// Parse 8-dimensional action vector
	FTacticalAction ParsedAction;

	// [0-1]: move_direction
	ParsedAction.MoveDirection = FVector2D(Action.Values[0], Action.Values[1]);

	// [2]: move_speed
	ParsedAction.MoveSpeed = Action.Values[2];

	// [3-4]: look_direction
	ParsedAction.LookDirection = FVector2D(Action.Values[3], Action.Values[4]);

	// [5]: fire (interpret as binary: >= 0.5 = true)
	ParsedAction.bFire = (Action.Values[5] >= 0.5f);

	// [6]: crouch
	ParsedAction.bCrouch = (Action.Values[6] >= 0.5f);

	// [7]: use_ability
	ParsedAction.bUseAbility = (Action.Values[7] >= 0.5f);

	// Store action in shared context for StateTree execution
	FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
	SharedContext.CurrentAtomicAction = ParsedAction;
	SharedContext.bScholaActionReceived = true; // Flag that action came from Schola

	// NOTE: Dummy objective is now created in FollowerAgentComponent::BeginPlay()
	// This ensures it exists BEFORE StateTree starts, allowing proper state entry

	LastAction = ParsedAction;

	// Debug logging
	AActor* Owner = GetTypedOuter<AActor>();
	UE_LOG(LogTemp, Verbose, TEXT("🎮 [SCHOLA ACTUATOR] '%s': Received action from Python → Move=(%.2f,%.2f) Speed=%.2f, Look=(%.2f,%.2f), Fire=%d"),
		*GetNameSafe(Owner),
		ParsedAction.MoveDirection.X, ParsedAction.MoveDirection.Y, ParsedAction.MoveSpeed,
		ParsedAction.LookDirection.X, ParsedAction.LookDirection.Y,
		ParsedAction.bFire ? 1 : 0);

	UE_LOG(LogTemp, Verbose, TEXT("    → SharedContext.bScholaActionReceived = %d (should be TRUE)"),
		SharedContext.bScholaActionReceived ? 1 : 0);
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

	UE_LOG(LogTemp, Log, TEXT("[TacticalActuator] %s: Initialized (Follower=%s, ActionSpace=8D Box)"),
		*GetNameSafe(GetOuter()), *GetNameSafe(FollowerAgent));
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
