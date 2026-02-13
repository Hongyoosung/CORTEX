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
	// v10.2 Action Space: Box([-1, 1]^7)
	// 7 continuous values representing EQS weights
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
	for (int32 i = 0; i < 7; ++i)  // v10.2: 7 dimensions (was 8 in error)
	{
		Space.Add(-1.0f, 1.0f);  // All weights in range [-1, 1]
	}

	return Space;
}

void UTacticalParameterActuator::TakeAction(const FBoxPoint& Action)
{
	// v10.2: TacticalParameterActuator is for RUNTIME GAMEPLAY only, not training
	// During training, MocTrainer::ApplyAction() handles action application directly
	// This actuator should only be used when MocAIController is present

	// Validate action dimensions
	if (Action.Values.Num() != 7)
	{
		UE_LOG(LogTemp, Error, TEXT("[TacticalParameterActuator] Invalid action dimension: %d (expected 7)"),
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

	// Check if we have a valid MocAgent
	if (!MocAgent)
	{
		// This is expected during training mode - MocTrainer handles actions directly
		if (bDebugLogging)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[TacticalParameterActuator] No MocAgent - likely in training mode (MocTrainer handles actions)"));
		}
		return;
	}

	// Apply weights to AIController (runtime gameplay mode)
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
		// This is expected during training - MocTrainer is the controller, not MocAIController
		if (bDebugLogging)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[TacticalParameterActuator] No MocAIController found - likely in training mode (MocTrainer is controller)"));
		}
	}
}

void UTacticalParameterActuator::InitializeActuator()
{
	// v10.2: This actuator is for RUNTIME GAMEPLAY with MocAIController
	// During training, MocTrainer handles action application directly
	// It's safe for this actuator to fail initialization in training mode

	// Auto-find MocCharacter owner
	if (bAutoFindMoc)
	{
		MocAgent = GetTypedOuter<AMocCharacter>();
		if (MocAgent)
		{
			// Check if we're in training mode (has MocTrainer controller)
			AController* Controller = MocAgent->GetController();
			if (Controller && Controller->GetClass()->GetName().Contains(TEXT("MocTrainer")))
			{
				UE_LOG(LogTemp, Log, TEXT("[TacticalParameterActuator] MocCharacter is in TRAINING mode - this actuator will be inactive (MocTrainer handles actions)"));
				UE_LOG(LogTemp, Log, TEXT("[TacticalParameterActuator] Initialized for %s (Training Mode - Inactive)"), *MocAgent->GetName());
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[TacticalParameterActuator] Initialized for %s (Runtime Mode - Active)"), *MocAgent->GetName());
			}
		}
		else
		{
			// This warning is only relevant for runtime mode
			UE_LOG(LogTemp, Verbose, TEXT("[TacticalParameterActuator] Owner is not AMocCharacter - actuator will be inactive"));
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
