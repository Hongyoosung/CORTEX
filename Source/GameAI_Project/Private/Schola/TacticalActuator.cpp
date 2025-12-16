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

FMultiDiscreteSpace UTacticalActuator::GetActionSpace()
{
	// MultiDiscrete([6, MaxEnemies+1, 3, 3])
	// [0]: Position (6 options: Hold, ForwardCover, Retreat, FlankL, FlankR, Advance)
	// [1]: Target (MaxEnemies+1 options: None + Enemy_0...Enemy_N)
	// [2]: Fire Mode (3 options: HoldFire, Fire, Suppress)
	// [3]: Stance (3 options: Stand, Crouch, Prone)

	TArray<int32> Nvec = { 6, MaxEnemies + 1, 3, 3 };
	FMultiDiscreteSpace ActionSpace = FMultiDiscreteSpace(Nvec);

	return ActionSpace;
}

void UTacticalActuator::TakeAction(const FMultiDiscretePoint& Action)
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

	// Validate action dimensions: MultiDiscrete([6, N+1, 3, 3])
	if (Action.Values.Num() < 4)
	{
		UE_LOG(LogTemp, Error, TEXT("[TacticalActuator] %s: Invalid action dimensions (expected 4, got %d)"),
			*GetNameSafe(GetOuter()), Action.Values.Num());
		return;
	}

	// Parse MultiDiscrete action indices
	int32 PositionIdx = Action.Values[0];    // [0-5]: Position choice
	int32 TargetIdx = Action.Values[1];      // [0-N]: Target index (0 = none)
	int32 FireModeIdx = Action.Values[2];    // [0-2]: Fire mode
	int32 StanceIdx = Action.Values[3];      // [0-2]: Stance

	// Validate indices
	if (PositionIdx < 0 || PositionIdx > 5)
	{
		UE_LOG(LogTemp, Error, TEXT("[TacticalActuator] %s: Invalid position index %d (expected 0-5)"),
			*GetNameSafe(GetOuter()), PositionIdx);
		return;
	}

	// Build macro action from indices
	FMacroAction MacroAction;

	// Map position index to enum
	MacroAction.PositionChoice = static_cast<ETacticalPosition>(PositionIdx);

	// Map target index (0 = none, 1+ = enemy index 0, 1, 2, ...)
	MacroAction.TargetIndex = (TargetIdx == 0) ? -1 : (TargetIdx - 1);

	// Map fire mode index to enum
	MacroAction.FireMode = static_cast<EFireMode>(FMath::Clamp(FireModeIdx, 0, 2));

	// Map stance index to enum
	MacroAction.Stance = static_cast<EStance>(FMath::Clamp(StanceIdx, 0, 2));

	// Store in FTacticalAction (v4.0 uses MacroAction field)
	FTacticalAction ParsedAction(MacroAction);

	// Store action in shared context for StateTree execution
	FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
	SharedContext.CurrentAtomicAction = ParsedAction;
	SharedContext.bScholaActionReceived = true; // Flag that action came from Schola

	LastMacroAction = MacroAction;

	// Debug logging
	#if !UE_BUILD_SHIPPING
	AActor* Owner = GetTypedOuter<AActor>();
	if (bDebugLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("🎮 [MACRO ACTION] '%s': Position=%s, Target=%d, Fire=%s, Stance=%s"),
			*GetNameSafe(Owner),
			*UEnum::GetValueAsString(MacroAction.PositionChoice),
			MacroAction.TargetIndex,
			*UEnum::GetValueAsString(MacroAction.FireMode),
			*UEnum::GetValueAsString(MacroAction.Stance));
	}
	#endif
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

	UE_LOG(LogTemp, Log, TEXT("[TacticalActuator] %s: Initialized (Follower=%s, ActionSpace=MultiDiscrete([6,%d,3,3]))"),
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
