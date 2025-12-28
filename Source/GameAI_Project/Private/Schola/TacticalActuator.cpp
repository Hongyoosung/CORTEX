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
	// v4.0: MultiDiscrete([4, MaxEnemies+1, 3]) - Stance removed for simpler learning
	// [0]: Position (4 options: Hold, ForwardCover, Retreat, Advance)
	// [1]: Target (MaxEnemies+1 options: None + Enemy_0...Enemy_N)
	// [2]: Fire Mode (3 options: HoldFire, Fire, Suppress)

	TArray<int32> Nvec = { 4, MaxEnemies + 1, 3 };
	FDiscreteSpace ActionSpace = FDiscreteSpace(Nvec);

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

	// v4.0: Validate action dimensions: MultiDiscrete([4, N+1, 3]) - Stance removed
	if (Action.Values.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("[TacticalActuator] %s: Invalid action dimensions (expected 3, got %d)"),
			*GetNameSafe(GetOuter()), Action.Values.Num());
		return;
	}

	// Parse MultiDiscrete action indices
	int32 PositionIdx = Action.Values[0];    // [0-3]: Position choice (4 options)
	int32 TargetIdx = Action.Values[1];      // [0-N]: Target index (0 = none)
	int32 FireModeIdx = Action.Values[2];    // [0-2]: Fire mode

	// Validate indices (v4.0: Position is 0-3, not 0-5)
	if (PositionIdx < 0 || PositionIdx > 3)
	{
		UE_LOG(LogTemp, Error, TEXT("[TacticalActuator] %s: Invalid position index %d (expected 0-3)"),
			*GetNameSafe(GetOuter()), PositionIdx);
		return;
	}

	// Build macro action from indices
	FMacroAction MacroAction;

	// Map position index to enum
	MacroAction.PositionChoice = static_cast<ETacticalPosition>(PositionIdx);

	// Map target index (0 = none, 1+ = enemy index 0, 1, 2, ...)
	// v7.3: Clamp to prevent "Target_X not found" spam when RLlib explores invalid indices
	// Action space allows Target 0-10, but actual visible enemies may be 0-4
	// Indices > 4 (enemy 3) are almost always invalid in 4v4 scenarios
	const int32 MaxValidTarget = 5;  // Support up to 4 visible enemies (action values 1-4)
	int32 ClampedTargetIdx = FMath::Clamp(TargetIdx, 0, MaxValidTarget);
	MacroAction.TargetIndex = (ClampedTargetIdx == 0) ? -1 : (ClampedTargetIdx - 1);

	// Map fire mode index to enum
	MacroAction.FireMode = static_cast<EFireMode>(FMath::Clamp(FireModeIdx, 0, 2));

	// Store in FTacticalAction (v4.0 uses MacroAction field)
	FTacticalAction ParsedAction(MacroAction);

	// Store action in shared context for StateTree execution
	FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
	SharedContext.CurrentAtomicAction = ParsedAction;
	SharedContext.bScholaActionReceived = true; // Flag that action came from Schola

	LastMacroAction = MacroAction;

	// v7.3: Debug logging reduced to Verbose to prevent log spam during training
	// Enable via console: Log LogTemp Verbose
	#if !UE_BUILD_SHIPPING
	AActor* Owner = GetTypedOuter<AActor>();
	if (bDebugLogging)
	{
		// Only log when action actually CHANGES (to reduce spam during action persistence)
		static TMap<FString, FMacroAction> LastLoggedActions;
		FString OwnerName = GetNameSafe(Owner);
		bool bActionChanged = true;

		if (LastLoggedActions.Contains(OwnerName))
		{
			FMacroAction& LastAction = LastLoggedActions[OwnerName];
			bActionChanged = (LastAction.PositionChoice != MacroAction.PositionChoice ||
			                  LastAction.TargetIndex != MacroAction.TargetIndex ||
			                  LastAction.FireMode != MacroAction.FireMode);
		}

		if (bActionChanged)
		{
			// Log only significant action changes at Verbose level
			UE_LOG(LogTemp, Verbose, TEXT("[MACRO ACTION] '%s': Position=%s, Target=%d, Fire=%s"),
				*OwnerName,
				*UEnum::GetValueAsString(MacroAction.PositionChoice),
				MacroAction.TargetIndex,
				*UEnum::GetValueAsString(MacroAction.FireMode));

			LastLoggedActions.Add(OwnerName, MacroAction);
		}
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
