

#include "Schola/Actuators/TacticalParameterActuator.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"
#include "GameFramework/Pawn.h"
#include "Characters/MocCharacter.h"


UTacticalParameterActuator::UTacticalParameterActuator()
{

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

}

void UTacticalParameterActuator::InitializeActuator()
{

}

void UTacticalParameterActuator::ResetActuator()
{

}

