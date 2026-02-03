// TacticalParameterActuator.cpp - v8.0 tactical parameter actuator implementation

#include "Schola/Actuators/TacticalParameterActuator.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"
#include "GameFramework/Pawn.h"
#include "Actor/FollowerCharacter.h"

UTacticalParameterActuator::UTacticalParameterActuator()
{
	LastTacticalParams = FTacticalParameters();
}

FBoxSpace UTacticalParameterActuator::GetActionSpace()
{
	// v8.0 Action space: Box([0,1]^4)
	// - [0]: Aggression
	// - [1]: CoverPreference
	// - [2]: SpreadDistance
	// - [3]: RiskTolerance

	FBoxSpace Space;
	Space.Add(0.0f, 1.0f);  // Aggression [0,1]
	Space.Add(0.0f, 1.0f);  // CoverPreference [0,1]
	Space.Add(0.0f, 1.0f);  // SpreadDistance [0,1]
	Space.Add(0.0f, 1.0f);  // RiskTolerance [0,1]

	return Space;
}

void UTacticalParameterActuator::TakeAction(const FBoxPoint& Action)
{
	if (!Follower)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalParamActuator] %s: OwnerActor not found or invalid!"),
			*GetNameSafe(GetOuter()));
		return;
	}

	// Validate action size
	if (Action.Values.Num() != RLConfig::NUM_TACTICAL_PARAMS)
	{
		UE_LOG(LogTemp, Error, TEXT("[TacticalParamActuator] %s: Invalid action size %d, expected %d"),
			*GetNameSafe(GetOuter()), Action.Values.Num(), RLConfig::NUM_TACTICAL_PARAMS);
		return;
	}

	// Parse continuous values into FTacticalParameters
	FTacticalParameters Params;
	Params.Aggression = Action.Values[0];
	Params.CoverPreference = Action.Values[1];
	Params.SpreadDistance = Action.Values[2];
	Params.RiskTolerance = Action.Values[3];

	// Clamp to valid range
	Params.Clamp();

	// Cache for debugging
	LastTacticalParams = Params;

	// Apply to follower agent
	Follower->SetTacticalParameters(Params);

	if (bDebugLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[TacticalParamActuator] %s: Applied params [Agg=%.2f, Cover=%.2f, Spread=%.2f, Risk=%.2f]"),
			*GetNameSafe(GetOuter()),
			Params.Aggression, Params.CoverPreference, Params.SpreadDistance, Params.RiskTolerance);
	}
}

void UTacticalParameterActuator::InitializeActuator()
{
	AActor* Owner = GetTypedOuter<AActor>();
	Follower = Cast<AFollowerCharacter>(Owner);

	if (!Follower)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalParamActuator] %s: Failed to find FollowerActor!"),
			*GetNameSafe(GetOuter()));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[TacticalParamActuator] %s: Initialized (Follower=%s, ActionSpace=Box([0,1]^4))"),
		*GetNameSafe(GetOuter()), *GetNameSafe(Follower));
}

void UTacticalParameterActuator::ResetActuator()
{
	// Reset to neutral parameters
	LastTacticalParams = FTacticalParameters(0.5f, 0.5f, 0.5f, 0.5f);

	if (Follower->IsValidLowLevel())
	{
		Follower->SetTacticalParameters(LastTacticalParams);
	}
}

