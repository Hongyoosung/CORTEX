// CombatChoiceActuator.cpp - v8.0 combat choice actuator implementation

#include "Schola/CombatChoiceActuator.h"
#include "Team/FollowerAgentComponent.h"
#include "Common/Spaces/DiscreteSpace.h"
#include "Common/Points/DiscretePoint.h"
#include "GameFramework/Pawn.h"

UCombatChoiceActuator::UCombatChoiceActuator()
{
	LastCombatParams = FCombatParameters();
}

FDiscreteSpace UCombatChoiceActuator::GetActionSpace()
{
	// v8.0 Action space: Discrete(2)
	// - [0]: Target Priority (2 choices: Closest, LowestHP)

	FDiscreteSpace Space;
	Space.Add(RLConfig::NUM_COMBAT_CHOICES);  // 2 choices

	return Space;
}

void UCombatChoiceActuator::TakeAction(const FDiscretePoint& Action)
{
	if (!FollowerAgent || !FollowerAgent->IsValidLowLevel() || !FollowerAgent->GetOwner())
	{
		UE_LOG(LogTemp, Warning, TEXT("[CombatChoiceActuator] %s: FollowerAgent not found or invalid!"),
			*GetNameSafe(GetOuter()));
		return;
	}

	// Validate action size
	if (Action.Values.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[CombatChoiceActuator] %s: Invalid action size %d, expected 1"),
			*GetNameSafe(GetOuter()), Action.Values.Num());
		return;
	}

	// Parse discrete value into FCombatParameters
	int32 PriorityIndex = Action.Values[0];
	if (PriorityIndex < 0 || PriorityIndex >= static_cast<int32>(ETargetPriority::COUNT))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CombatChoiceActuator] %s: Invalid priority index %d, clamping"),
			*GetNameSafe(GetOuter()), PriorityIndex);
		PriorityIndex = FMath::Clamp(PriorityIndex, 0, static_cast<int32>(ETargetPriority::COUNT) - 1);
	}

	FCombatParameters Params;
	Params.Priority = static_cast<ETargetPriority>(PriorityIndex);

	// Cache for debugging
	LastCombatParams = Params;

	// Apply to follower agent
	FollowerAgent->SetCombatParameters(Params);

	if (bDebugLogging)
	{
		FString PriorityName = (Params.Priority == ETargetPriority::Closest) ? TEXT("Closest") : TEXT("LowestHP");
		UE_LOG(LogTemp, Log, TEXT("[CombatChoiceActuator] %s: Applied combat [Priority=%s]"),
			*GetNameSafe(GetOuter()), *PriorityName);
	}
}

void UCombatChoiceActuator::InitializeActuator()
{
	// Auto-find follower agent if enabled
	if (bAutoFindFollower && !FollowerAgent)
	{
		FollowerAgent = FindFollowerAgent();
	}

	if (!FollowerAgent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CombatChoiceActuator] %s: Failed to find FollowerAgentComponent!"),
			*GetNameSafe(GetOuter()));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[CombatChoiceActuator] %s: Initialized (Follower=%s, ActionSpace=Discrete(2))"),
		*GetNameSafe(GetOuter()), *GetNameSafe(FollowerAgent));
}

void UCombatChoiceActuator::ResetActuator()
{
	// Reset to default (Closest priority)
	LastCombatParams = FCombatParameters(ETargetPriority::Closest);

	if (FollowerAgent && FollowerAgent->IsValidLowLevel())
	{
		FollowerAgent->SetCombatParameters(LastCombatParams);
	}
}

UFollowerAgentComponent* UCombatChoiceActuator::FindFollowerAgent() const
{
	AActor* Owner = GetTypedOuter<AActor>();
	if (!Owner)
	{
		return nullptr;
	}

	return Owner->FindComponentByClass<UFollowerAgentComponent>();
}
