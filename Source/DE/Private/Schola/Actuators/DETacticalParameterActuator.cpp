// DETacticalParameterActuator.cpp - Implementation

#include "Schola/Actuators/DETacticalParameterActuator.h"
#include "DynamicEQS/Public/Schola/Base/DynamicEQSTrainerBase.h"
#include "Spaces/BoxSpace.h"
#include "Points/BoxPoint.h"
#include "Characters/DEAgent.h"


UDETacticalParameterActuator::UDETacticalParameterActuator()
{
	// Default constructor
}

FBoxSpace UDETacticalParameterActuator::GetActionSpace() const
{
	// Action Space: Box([-1, 1]^7)
	// 7 continuous values representing EQS weights
	//
	// Dimension mapping:
	// [0]: EnemyObjectiveProximity
	// [1]: AllyObjectiveProximity
	// [2]: CoverDensity
	// [3]: EnemyVisibility
	// [4]: AllyProximity
	// [5]: CombatRange
	// [6]: AssignedBaseProximity

	FBoxSpace Space;
	for (int32 i = 0; i < FDEEQSWeightParameters::NumWeights; ++i)
	{
		Space.Add(-1.0f, 1.0f);
	}

	return Space;
}

void UDETacticalParameterActuator::TakeAction(const FBoxPoint& Action)
{
	// === DIAGNOSTIC: Log every TakeAction call for first 10 actions ===
	if (ActionCount < 10)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalActuator] TakeAction() CALLED (action #%d) — Values.Num()=%d (expected %d)"),
			ActionCount + 1, Action.Values.Num(), FDEEQSWeightParameters::NumWeights);
	}

	if (!ensureMsgf(Action.Values.Num() == FDEEQSWeightParameters::NumWeights,
		TEXT("[TacticalActuator] ACTION REJECTED — expected %d values, got %d. Check action space mismatch."),
		FDEEQSWeightParameters::NumWeights, Action.Values.Num()))
	{
		return;
	}

	FDEEQSWeightParameters Weights = ActionToEQSWeights(Action);

	if (bClampOutputs)
	{
		Weights.Clamp();
	}

	if (!ValidateEQSWeights(Weights))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DETacticalParameterActuator] Weights validation failed, clamping..."));
		Weights.Clamp();
	}

	LastEQSWeights = Weights;
	ActionCount++;

	if (!DEAgent)
	{
		DEAgent = FindDECharacter();
		if (DEAgent)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TacticalActuator] FindDECharacter() found: %s"), *DEAgent->GetName());
		}
	}

	if (DEAgent)
	{
		DEAgent->UpdateTacticalWeights(Weights);  // stores weights + sets bWeightsDirty
		DEAgent->PerformTacticalAction();          // triggers movement (sync EQS in training, BB write in inference)

		// Diagnostic: log first 10 actions at Warning level for visibility
		if (ActionCount <= 10)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[TacticalActuator] %s action #%d DELIVERED: [%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f] | Controller=%s"),
				*DEAgent->GetName(), ActionCount,
				Weights.EnemyObjectiveProximity, Weights.AllyObjectiveProximity,
				Weights.CoverDensity, Weights.EnemyVisibility,
				Weights.AllyProximity, Weights.CombatRange, Weights.AssignedBaseProximity,
				DEAgent->GetController() ? *DEAgent->GetController()->GetName() : TEXT("NULL"));
		}
	}
	else
	{
		// Action from Python was dropped — this directly causes agent freeze
		UObject* MyOuter = GetOuter();
		FString OuterName = MyOuter ? MyOuter->GetName() : TEXT("NULL");
		FString OuterClass = MyOuter ? MyOuter->GetClass()->GetName() : TEXT("NULL");

		// Walk the full outer chain for debugging
		FString OuterChain;
		UObject* Current = GetOuter();
		int32 Depth = 0;
		while (Current && Depth < 10)
		{
			OuterChain += FString::Printf(TEXT("[%d] %s (%s) → "), Depth, *Current->GetName(), *Current->GetClass()->GetName());
			Current = Current->GetOuter();
			Depth++;
		}

		UE_LOG(LogTemp, Error,
			TEXT("[TacticalActuator] ACTION DROPPED (action #%d) — DEAgent not found! bWeightsDirty will NOT be set."),
			ActionCount);
		UE_LOG(LogTemp, Error,
			TEXT("[TacticalActuator] Outer chain: %s"), *OuterChain);
		return;
	}

	if (bDebugLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[DETacticalParameterActuator] Applied weights to %s, Action=%d: %s"),
			*DEAgent->GetName(), ActionCount, *Weights.ToString());
	}
}

void UDETacticalParameterActuator::InitializeActuator()
{
	UE_LOG(LogTemp, Warning, TEXT("========== [TacticalActuator] InitializeActuator DIAGNOSTIC =========="));

	if (!DEAgent && bAutoFindDE)
	{
		DEAgent = FindDECharacter();
	}

	if (DEAgent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalActuator] ✓ Initialized for: %s"), *DEAgent->GetName());
		UE_LOG(LogTemp, Warning, TEXT("[TacticalActuator]   Controller: %s"),
			DEAgent->GetController() ? *DEAgent->GetController()->GetName() : TEXT("NULL"));
	}
	else
	{
		// Detailed failure diagnosis
		UObject* MyOuter = GetOuter();
		FString OuterChain;
		UObject* Current = MyOuter;
		int32 Depth = 0;
		while (Current && Depth < 10)
		{
			OuterChain += FString::Printf(TEXT("[%d] %s (%s) → "), Depth, *Current->GetName(), *Current->GetClass()->GetName());
			Current = Current->GetOuter();
			Depth++;
		}
		UE_LOG(LogTemp, Error, TEXT("[TacticalActuator] ✗ No DEAgent found! Outer chain: %s"), *OuterChain);
	}

	UE_LOG(LogTemp, Warning, TEXT("======================================================================"));

	CurrentCommandedStrategy = EDEStrategyType::Assault;
	ActionCount = 0;
}

void UDETacticalParameterActuator::ResetActuator()
{
	// Reset to default state
	LastEQSWeights = FDEEQSWeightParameters();
	ActionCount = 0;

	// Keep commanded strategy (set by Squad Commander)
	// Don't reset CurrentCommandedStrategy here

	if (bDebugLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[DETacticalParameterActuator] Reset for %s"),
			DEAgent ? *DEAgent->GetName() : TEXT("NULL"));
	}
}

void UDETacticalParameterActuator::SetCommandedStrategy(EDEStrategyType CommandedStrategy)
{
	if (CurrentCommandedStrategy != CommandedStrategy)
	{
		CurrentCommandedStrategy = CommandedStrategy;

		if (bDebugLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("[DETacticalParameterActuator] %s received strategy command: %d"),
				DEAgent ? *DEAgent->GetName() : TEXT("NULL"),
				static_cast<int32>(CommandedStrategy));
		}
	}
}

ADEAgent* UDETacticalParameterActuator::FindDECharacter() const
{
	// Path 1: Direct outer is DEAgent
	if (ADEAgent* Direct = GetTypedOuter<ADEAgent>())
	{
		return Direct;
	}

	// Path 2: Outer is Trainer (AIController) → get possessed pawn
	if (ADynamicEQSTrainerBase* Trainer = GetTypedOuter<ADynamicEQSTrainerBase>())
	{
		if (ADEAgent* FromPawn = Cast<ADEAgent>(Trainer->GetPawn()))
		{
			return FromPawn;
		}
	}

	return nullptr;
}

void UDETacticalParameterActuator::SetOwnerCharacter(ADEAgent* InCharacter)
{
	DEAgent = InCharacter;
	if (bDebugLogging && DEAgent)
	{
		UE_LOG(LogTemp, Log, TEXT("[DETacticalParameterActuator] Owner set to %s"), *DEAgent->GetName());
	}
}

FDEEQSWeightParameters UDETacticalParameterActuator::ActionToEQSWeights(const FBoxPoint& Action) const
{
	// Direct mapping from 7-dim Box action to FDEEQSWeightParameters
	// Box action space is [-1, 1]^7, matching the weight range

	FDEEQSWeightParameters Weights;
	Weights.EnemyObjectiveProximity = Action.Values[0];
	Weights.AllyObjectiveProximity = Action.Values[1];
	Weights.CoverDensity = Action.Values[2];
	Weights.EnemyVisibility = Action.Values[3];
	Weights.AllyProximity = Action.Values[4];
	Weights.CombatRange = Action.Values[5];
	Weights.AssignedBaseProximity = Action.Values[6];

	return Weights;
}

bool UDETacticalParameterActuator::ValidateEQSWeights(const FDEEQSWeightParameters& Weights) const
{
	// Check if any weight is outside [-1, 1] range
	TArray<float> WeightArray = Weights.ToArray();

	for (int32 i = 0; i < WeightArray.Num(); ++i)
	{
		if (WeightArray[i] < -1.0f || WeightArray[i] > 1.0f)
		{
			if (bDebugLogging)
			{
				UE_LOG(LogTemp, Warning, TEXT("[DETacticalParameterActuator] Weight[%d] out of range: %.3f"),
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
			UE_LOG(LogTemp, Error, TEXT("[DETacticalParameterActuator] Weight[%d] is NaN or Inf: %.3f"),
				i, WeightArray[i]);
			return false;
		}
	}

	return true;
}
