// TacticalParameterActuator.cpp - Implementation

#include "Schola/Actuators/TacticalParameterActuator.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"
#include "Characters/MocCharacter.h"
#include "AI/AIController/MocAIController.h"
#include "GameFramework/Pawn.h"


UTacticalParameterActuator::UTacticalParameterActuator()
{
	// Default constructor
}

FBoxSpace UTacticalParameterActuator::GetActionSpace()
{
	// v10.2 Action Space: Box([-1, 1]^8)
	// 8 continuous values representing EQS weights
	//
	// Dimension mapping:
	// [0]: EnemyObjectiveProximity
	// [1]: AllyObjectiveProximity
	// [2]: CoverDensity
	// [3]: EnemyVisibility
	// [4]: AllyProximity
	// [5]: CombatRange
	// [6]: PickupProximity

	FBoxSpace Space;
	for (int32 i = 0; i < 8; ++i)
	{
		Space.Add(-1.0f, 1.0f);  // All weights in range [-1, 1]
	}

	return Space;
}

void UTacticalParameterActuator::TakeAction(const FBoxPoint& Action)
{
	// Validate action dimensions
	if (Action.Values.Num() != 8)
	{
		UE_LOG(LogTemp, Error, TEXT("[TacticalParameterActuator] Invalid action dimension: %d (expected 8)"),
			Action.Values.Num());
		return;
	}

	// Convert action to EQS weights
	FEQSWeightParameters Weights = ActionToEQSWeights(Action);

	// Clamp if enabled
	if (bClampOutputs)
	{
		Weights.Clamp();
	}

	// Validate weights
	if (!ValidateEQSWeights(Weights))
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalParameterActuator] Weights validation failed, clamping..."));
		Weights.Clamp();
	}

	// Store for debugging
	LastEQSWeights = Weights;
	ActionCount++;

	// Apply weights to AIController
	if (MocAgent)
	{
		AMocAIController* AIController = Cast<AMocAIController>(MocAgent->GetController());
		if (AIController)
		{
			AIController->UpdateBlackboardWeights(Weights);

			if (bDebugLogging)
			{
				UE_LOG(LogTemp, Log, TEXT("[TacticalParameterActuator] Agent=%s, Strategy=%d, Action=%d: %s"),
					*MocAgent->GetName(),
					static_cast<int32>(CurrentCommandedStrategy),
					ActionCount,
					*Weights.ToString());
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[TacticalParameterActuator] AIController not found for %s"),
				*MocAgent->GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalParameterActuator] MocAgent not found"));
	}
}

void UTacticalParameterActuator::InitializeActuator()
{
	// Auto-find MocCharacter owner
	if (bAutoFindMoc)
	{
		MocAgent = GetTypedOuter<AMocCharacter>();
		if (MocAgent)
		{
			UE_LOG(LogTemp, Log, TEXT("[TacticalParameterActuator] Initialized for %s"), *MocAgent->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[TacticalParameterActuator] Owner is not AMocCharacter"));
		}
	}

	// Initialize default commanded strategy
	CurrentCommandedStrategy = EStrategyType::Assault;
	ActionCount = 0;
}

void UTacticalParameterActuator::ResetActuator()
{
	// Reset to default state
	LastEQSWeights = FEQSWeightParameters();
	ActionCount = 0;

	// Keep commanded strategy (set by Squad Commander)
	// Don't reset CurrentCommandedStrategy here

	if (bDebugLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[TacticalParameterActuator] Reset for %s"),
			MocAgent ? *MocAgent->GetName() : TEXT("NULL"));
	}
}

void UTacticalParameterActuator::SetCommandedStrategy(EStrategyType CommandedStrategy)
{
	if (CurrentCommandedStrategy != CommandedStrategy)
	{
		CurrentCommandedStrategy = CommandedStrategy;

		if (bDebugLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("[TacticalParameterActuator] %s received strategy command: %d"),
				MocAgent ? *MocAgent->GetName() : TEXT("NULL"),
				static_cast<int32>(CommandedStrategy));
		}
	}
}

FEQSWeightParameters UTacticalParameterActuator::ActionToEQSWeights(const FBoxPoint& Action) const
{
	// Direct mapping from 7-dim Box action to FEQSWeightParameters
	// Box action space is [-1, 1]^7, matching the weight range

	check(Action.Values.Num() == 7);

	FEQSWeightParameters Weights;
	Weights.EnemyObjectiveProximity = Action.Values[0];
	Weights.AllyObjectiveProximity = Action.Values[1];
	Weights.CoverDensity = Action.Values[2];
	Weights.EnemyVisibility = Action.Values[3];
	Weights.AllyProximity = Action.Values[4];
	Weights.CombatRange = Action.Values[5];
	Weights.PickupProximity = Action.Values[6];

	return Weights;
}

bool UTacticalParameterActuator::ValidateEQSWeights(const FEQSWeightParameters& Weights) const
{
	// Check if any weight is outside [-1, 1] range
	TArray<float> WeightArray = Weights.ToArray();

	for (int32 i = 0; i < WeightArray.Num(); ++i)
	{
		if (WeightArray[i] < -1.0f || WeightArray[i] > 1.0f)
		{
			if (bDebugLogging)
			{
				UE_LOG(LogTemp, Warning, TEXT("[TacticalParameterActuator] Weight[%d] out of range: %.3f"),
					i, WeightArray[i]);
			}
			return false;
		}
	}

	// Check for NaN or Inf
	for (int32 i = 0; i < WeightArray.Num(); ++i)
	{
		if (!FMath::IsFinite(WeightArray[i]))
		{
			UE_LOG(LogTemp, Error, TEXT("[TacticalParameterActuator] Weight[%d] is NaN or Inf: %.3f"),
				i, WeightArray[i]);
			return false;
		}
	}

	return true;
}
