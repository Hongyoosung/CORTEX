// TacticalParameterActuator.cpp - Implementation

#include "Schola/Actuators/TacticalParameterActuator.h"
#include "Agent/AgentComponents/ActuatorComponent.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"
#include "Characters/MocCharacter.h"
#include "Training/AbstractTrainer.h"


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
	if (Action.Values.Num() != 7) return;

	FEQSWeightParameters Weights = ActionToEQSWeights(Action);

	if (bClampOutputs)
	{
		Weights.Clamp();
	}

	if (!ValidateEQSWeights(Weights))
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalParameterActuator] Weights validation failed, clamping..."));
		Weights.Clamp();
	}

	LastEQSWeights = Weights;
	ActionCount++;

	if (!MocAgent)
	{
		MocAgent = FindMocCharacter();
	}

	if (MocAgent)
	{
		MocAgent->UpdateTacticalWeights(Weights);

		// Diagnostic: confirm action was delivered to the character
		UE_LOG(LogTemp, Verbose,
			TEXT("[TacticalActuator] %s action #%d delivered: [%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]"),
			*MocAgent->GetName(), ActionCount,
			Weights.EnemyObjectiveProximity, Weights.AllyObjectiveProximity,
			Weights.CoverDensity, Weights.EnemyVisibility,
			Weights.AllyProximity, Weights.CombatRange, Weights.PickupProximity);
	}
	else
	{
		// Action from Python was dropped — this directly causes agent freeze
		UObject* MyOuter = GetOuter();
		FString OuterName = MyOuter ? MyOuter->GetName() : TEXT("NULL");
		FString OuterClass = MyOuter ? MyOuter->GetClass()->GetName() : TEXT("NULL");
		UE_LOG(LogTemp, Error,
			TEXT("[TacticalActuator] ACTION DROPPED (action #%d) — MocCharacter not found! Python sent weights but bWeightsDirty will NOT be set. Outer: %s (%s)"),
			ActionCount, *OuterName, *OuterClass);
		return;
	}

	if (bDebugLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[TacticalParameterActuator] Applied weights to %s, Action=%d: %s"),
			*MocAgent->GetName(), ActionCount, *Weights.ToString());
	}
}

void UTacticalParameterActuator::InitializeActuator()
{
	if (!MocAgent && bAutoFindMoc)
	{
		MocAgent = FindMocCharacter();
	}

	if (MocAgent)
	{
		UE_LOG(LogTemp, Log, TEXT("[TacticalParameterActuator] Initialized for %s"), *MocAgent->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalParameterActuator] No MocCharacter found. Call SetOwnerCharacter() explicitly."));
	}

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

AMocCharacter* UTacticalParameterActuator::FindMocCharacter() const
{
	// Path 1: Direct outer is MocCharacter
	if (AMocCharacter* Direct = GetTypedOuter<AMocCharacter>())
	{
		return Direct;
	}

	// Path 2: Outer is ActuatorComponent on MocCharacter
	if (UActuatorComponent* OwnerComp = Cast<UActuatorComponent>(GetOuter()))
	{
		if (AMocCharacter* FromComp = Cast<AMocCharacter>(OwnerComp->GetOwner()))
		{
			return FromComp;
		}
	}

	// Path 3: Outer is Trainer (standard Schola path) → get possessed pawn
	if (AAbstractTrainer* Trainer = GetTypedOuter<AAbstractTrainer>())
	{
		if (AMocCharacter* FromPawn = Cast<AMocCharacter>(Trainer->GetPawn()))
		{
			return FromPawn;
		}
	}

	return nullptr;
}

void UTacticalParameterActuator::SetOwnerCharacter(AMocCharacter* InCharacter)
{
	MocAgent = InCharacter;
	if (bDebugLogging && MocAgent)
	{
		UE_LOG(LogTemp, Log, TEXT("[TacticalParameterActuator] Owner set to %s"), *MocAgent->GetName());
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
