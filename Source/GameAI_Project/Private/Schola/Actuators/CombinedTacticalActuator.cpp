// CombinedTacticalActuator.cpp - v8.0 combined tactical + combat actuator implementation

#include "Schola/Actuators/CombinedTacticalActuator.h"
#include "Actor/FollowerCharacter.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"
#include "GameFramework/Pawn.h"


UCombinedTacticalActuator::UCombinedTacticalActuator()
	: Super()
	, bDebugLogging(false)
	, CombatPriorityThreshold(0.5f)
	, FollowerAgent(nullptr)
{
	LastMacroAction = FMacroAction();
}

FBoxSpace UCombinedTacticalActuator::GetActionSpace()
{
	// Combined Action space: Box([0,1]^5)
	// - [0]: Aggression
	// - [1]: CoverPreference
	// - [2]: SpreadDistance
	// - [3]: RiskTolerance
	// - [4]: CombatPriority (continuous, thresholded to discrete)

	FBoxSpace Space;
	Space.Add(0.0f, 1.0f);  // Aggression [0,1]
	Space.Add(0.0f, 1.0f);  // CoverPreference [0,1]
	Space.Add(0.0f, 1.0f);  // SpreadDistance [0,1]
	Space.Add(0.0f, 1.0f);  // RiskTolerance [0,1]
	Space.Add(0.0f, 1.0f);  // CombatPriority [0,1] -> thresholded to Closest/LowestHP

	return Space;
}

void UCombinedTacticalActuator::TakeAction(const FBoxPoint& Action)
{
	if (!FollowerAgent || !FollowerAgent->IsValidLowLevel() || !FollowerAgent->GetOwner())
	{
		UE_LOG(LogTemp, Warning, TEXT("[CombinedTacticalActuator] %s: FollowerAgent not found or invalid!"),
			*GetNameSafe(GetOuter()));
		return;
	}

	// Expected: 5 values (4 tactical + 1 combat)
	constexpr int32 ExpectedSize = 5;
	if (Action.Values.Num() < ExpectedSize)
	{
		UE_LOG(LogTemp, Error, TEXT("[CombinedTacticalActuator] %s: Invalid action size %d, expected at least %d"),
			*GetNameSafe(GetOuter()), Action.Values.Num(), ExpectedSize);
		return;
	}

	// Parse first 4 values as tactical parameters
	FTacticalParameters TacticalParams;
	TacticalParams.Aggression = Action.Values[0];
	TacticalParams.CoverPreference = Action.Values[1];
	TacticalParams.SpreadDistance = Action.Values[2];
	TacticalParams.RiskTolerance = Action.Values[3];

	// Clamp tactical params to valid range
	TacticalParams.Clamp();

	// Parse 5th value as combat priority (threshold at 0.5)
	float CombatPriorityValue = Action.Values[4];
	FCombatParameters CombatParams;
	CombatParams.Priority = (CombatPriorityValue >= CombatPriorityThreshold)
		? ETargetPriority::LowestHP
		: ETargetPriority::Closest;

	FMacroAction NewMacroAction;

	NewMacroAction.TacticalParams = TacticalParams;
	NewMacroAction.CombatParams = CombatParams;

	// Apply to follower agent
	FollowerAgent->SetMacroAction(NewMacroAction);

	LastMacroAction = NewMacroAction;

	if (bDebugLogging)
	{
		FString PriorityName = (CombatParams.Priority == ETargetPriority::Closest) ? TEXT("Closest") : TEXT("LowestHP");
		UE_LOG(LogTemp, Log, TEXT("[CombinedTacticalActuator] %s: Applied [Agg=%.2f, Cover=%.2f, Spread=%.2f, Risk=%.2f, Combat=%s (%.2f)]"),
			*GetNameSafe(GetOuter()),
			TacticalParams.Aggression, TacticalParams.CoverPreference,
			TacticalParams.SpreadDistance, TacticalParams.RiskTolerance,
			*PriorityName, CombatPriorityValue);
	}
}



void UCombinedTacticalActuator::ResetActuator()
{
	LastMacroAction.TacticalParams = FTacticalParameters(0.5f, 0.5f, 0.5f, 0.5f);
	LastMacroAction.CombatParams = FCombatParameters(ETargetPriority::Closest);

	if (FollowerAgent && FollowerAgent->IsValidLowLevel())
	{
		FollowerAgent->SetMacroAction(LastMacroAction);
	}
}

void UCombinedTacticalActuator::SetFollowerAgent(AActor* OwnerAgent)
{
	if (!OwnerAgent)
	{
		return;
	}

	AFollowerCharacter* Follower = Cast<AFollowerCharacter>(OwnerAgent);

	FollowerAgent = Follower;
}

