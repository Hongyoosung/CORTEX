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
	// v6.0: Discrete(4) - Strategy only (Assault, Defend, Support, Retreat)
	// Position and targeting are now handled by rule-based StateTree execution
	// This simplifies learning and aligns with MCTS-RL coordination architecture

	TArray<int32> Nvec = { 4 };  // 4 strategies
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

	// v6.0: Validate action dimensions: Discrete(4) - Strategy only
	if (Action.Values.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[TacticalActuator] %s: Invalid action dimensions (expected 1, got %d)"),
			*GetNameSafe(GetOuter()), Action.Values.Num());
		return;
	}

	// v6.0: Parse strategy index
	int32 StrategyIdx = Action.Values[0];  // [0-3]: Strategy (Assault, Defend, Support, Retreat)

	// Validate index
	if (StrategyIdx < 0 || StrategyIdx > 3)
	{
		UE_LOG(LogTemp, Error, TEXT("[TacticalActuator] %s: Invalid strategy index %d (expected 0-3)"),
			*GetNameSafe(GetOuter()), StrategyIdx);
		return;
	}

	// Build macro action (v6.0 - strategy only)
	FMacroAction MacroAction;
	MacroAction.Strategy = static_cast<EStrategyType>(StrategyIdx);

	// Store action in shared context for StateTree execution
	FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
	SharedContext.CurrentAction = MacroAction;
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
			                  LastAction.TargetIndex != MacroAction.TargetIndex);
		}

		if (bActionChanged)
		{
			// v5.0: Log only significant action changes at Verbose level
			UE_LOG(LogTemp, Verbose, TEXT("[MACRO ACTION v5.0] '%s': Position=%s, Target=%d"),
				*OwnerName,
				*UEnum::GetValueAsString(MacroAction.PositionChoice),
				MacroAction.TargetIndex);

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
