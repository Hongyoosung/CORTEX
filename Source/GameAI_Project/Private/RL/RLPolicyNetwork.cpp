// Copyright Epic Games, Inc. All Rights Reserved.

#include "RL/RLPolicyNetwork.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "Misc/Paths.h"
#include "Observation/TeamObservation.h"
#include "Core/ProfilingMacros.h"  // v6.0: Performance profiling

URLPolicyNetwork::URLPolicyNetwork()
	: bUseONNXModel(false)
{
}

// ========================================
// v8.0: Tactical Parameters + Combat Control (Multi-Head)
// ========================================

FMacroAction URLPolicyNetwork::GetMacroAction(const FObservationElement& Observation, EStrategyType AssignedStrategy)
{
	SCOPE_CYCLE_COUNTER(STAT_RLSingleInference);  // v8.0: Profile single inference (target: <2ms)

	FMacroAction Action;

	if (!bUseONNXModel || !ModelInstance.IsValid())
	{
		// v8.0 BOOTSTRAP PHASE: Heuristic policy for initial training
		// Strategy-specific default parameters
		switch (AssignedStrategy)
		{
		case EStrategyType::Assault:
			Action.TacticalParams = FTacticalParameters(0.8f, 0.3f, 0.6f, 0.7f);  // High aggression, low cover
			Action.CombatParams = FCombatParameters(ETargetPriority::Closest);
			break;
		case EStrategyType::Defend:
			Action.TacticalParams = FTacticalParameters(0.2f, 0.9f, 0.4f, 0.3f);  // Low aggression, high cover
			Action.CombatParams = FCombatParameters(ETargetPriority::Closest);
			break;
		case EStrategyType::Support:
			Action.TacticalParams = FTacticalParameters(0.5f, 0.5f, 0.3f, 0.5f);  // Balanced
			Action.CombatParams = FCombatParameters(ETargetPriority::LowestHP);   // Help finish kills
			break;
		case EStrategyType::Retreat:
			Action.TacticalParams = FTacticalParameters(0.1f, 0.8f, 0.9f, 0.9f);  // Very defensive
			Action.CombatParams = FCombatParameters(ETargetPriority::Closest);
			break;
		}
		return Action;
	}

	// Build 68-feature input (64 base + 4 strategy one-hot)
	TArray<float> InputFeatures = BuildNetworkInputV8(Observation, AssignedStrategy);
	check(InputFeatures.Num() == 68);

	// Forward pass through multi-head network
	FNetworkOutputV8 Output = ForwardPassV8(InputFeatures);

	// Select tactical parameters from appropriate strategy head
	Action.TacticalParams = SelectTacticalParameters(Output, AssignedStrategy);

	// Sample combat priority from combat head
	Action.CombatParams.Priority = SampleCombatPriority(Output.CombatLogits);

	/*UE_LOG(LogTemp, Verbose, TEXT("✅ [RL v8.0] Strategy=%s, Aggression=%.2f, CoverPref=%.2f, Combat=%d, Value=%.2f"),
		*UEnum::GetValueAsString(AssignedStrategy),
		Action.TacticalParams.Aggression,
		Action.TacticalParams.CoverPreference,
		static_cast<int32>(Action.CombatParams.Priority),
		Output.Value);*/

	return Action;
}

float URLPolicyNetwork::GetStateValueV8(const FObservationElement& Observation, EStrategyType AssignedStrategy)
{
	SCOPE_CYCLE_COUNTER(STAT_RLGetStateValue);  // v8.0: Profile value estimation

	if (!bUseONNXModel || !ModelInstance.IsValid())
	{
		// Fallback heuristic value
		float value = (Observation.AgentHealth - 0.5f) * 2.0f;
		value -= Observation.VisibleEnemyCount * 0.2f;
		return FMath::Clamp(value, -1.0f, 1.0f);
	}

	// Build 68-feature input
	TArray<float> InputFeatures = BuildNetworkInputV8(Observation, AssignedStrategy);

	// Forward pass
	FNetworkOutputV8 Output = ForwardPassV8(InputFeatures);

	return FMath::Clamp(Output.Value, -1.0f, 1.0f);
}

TArray<FMacroAction> URLPolicyNetwork::GetMacroActionsBatched(
	const TArray<FObservationElement>& Observations,
	const TArray<EStrategyType>& AssignedStrategies)
{
	SCOPE_CYCLE_COUNTER(STAT_RLBatchedInference);  // v8.0: Profile batched inference (target: <4ms for 4 agents)

	TArray<FMacroAction> Actions;

	if (Observations.Num() != AssignedStrategies.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("URLPolicyNetwork v8.0: Batch size mismatch"));
		return Actions;
	}

	int32 BatchSize = Observations.Num();
	if (BatchSize == 0) return Actions;

	// Fallback if model not loaded
	if (!bUseONNXModel || !ModelInstance.IsValid())
	{
		for (int32 i = 0; i < BatchSize; ++i)
		{
			Actions.Add(GetMacroAction(Observations[i], AssignedStrategies[i]));
		}
		return Actions;
	}

	// Build batched input tensor [BatchSize, 68]
	TArray<float> BatchedInput;
	BatchedInput.Reserve(BatchSize * 68);

	for (int32 i = 0; i < BatchSize; ++i)
	{
		TArray<float> Features = BuildNetworkInputV8(Observations[i], AssignedStrategies[i]);
		check(Features.Num() == 68);
		BatchedInput.Append(Features);
	}

	// Prepare input tensor
	UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make({
		static_cast<uint32>(BatchSize),
		68u
	});

	// Prepare output buffers (6 outputs from multi-head network)
	TArray<float> AssaultBuffer, DefendBuffer, SupportBuffer, RetreatBuffer, CombatBuffer, ValueBuffer;
	AssaultBuffer.SetNum(BatchSize * 4);   // 4 tactical params per agent
	DefendBuffer.SetNum(BatchSize * 4);
	SupportBuffer.SetNum(BatchSize * 4);
	RetreatBuffer.SetNum(BatchSize * 4);
	CombatBuffer.SetNum(BatchSize * 2);    // 2 combat logits per agent
	ValueBuffer.SetNum(BatchSize);         // 1 value per agent

	// Bind tensors
	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.Add({BatchedInput.GetData(), static_cast<uint64>(BatchedInput.Num() * sizeof(float))});

	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	OutputBindings.Add({AssaultBuffer.GetData(), static_cast<uint64>(AssaultBuffer.Num() * sizeof(float))});
	OutputBindings.Add({DefendBuffer.GetData(), static_cast<uint64>(DefendBuffer.Num() * sizeof(float))});
	OutputBindings.Add({SupportBuffer.GetData(), static_cast<uint64>(SupportBuffer.Num() * sizeof(float))});
	OutputBindings.Add({RetreatBuffer.GetData(), static_cast<uint64>(RetreatBuffer.Num() * sizeof(float))});
	OutputBindings.Add({CombatBuffer.GetData(), static_cast<uint64>(CombatBuffer.Num() * sizeof(float))});
	OutputBindings.Add({ValueBuffer.GetData(), static_cast<uint64>(ValueBuffer.Num() * sizeof(float))});

	// Run batched inference
	TArray<UE::NNE::FTensorShape> InputShapes = {InputShape};
	if (ModelInstance->SetInputTensorShapes(InputShapes) == UE::NNE::EResultStatus::Ok &&
		ModelInstance->RunSync(InputBindings, OutputBindings) == UE::NNE::EResultStatus::Ok)
	{
		// Decode actions from batched output
		for (int32 i = 0; i < BatchSize; ++i)
		{
			FMacroAction Action;

			// Select tactical parameters from appropriate strategy head
			int32 TacticalOffset = i * 4;
			TArray<float> TacticalParams;

			switch (AssignedStrategies[i])
			{
			case EStrategyType::Assault:
				TacticalParams = {AssaultBuffer[TacticalOffset], AssaultBuffer[TacticalOffset + 1],
				                  AssaultBuffer[TacticalOffset + 2], AssaultBuffer[TacticalOffset + 3]};
				break;
			case EStrategyType::Defend:
				TacticalParams = {DefendBuffer[TacticalOffset], DefendBuffer[TacticalOffset + 1],
				                  DefendBuffer[TacticalOffset + 2], DefendBuffer[TacticalOffset + 3]};
				break;
			case EStrategyType::Support:
				TacticalParams = {SupportBuffer[TacticalOffset], SupportBuffer[TacticalOffset + 1],
				                  SupportBuffer[TacticalOffset + 2], SupportBuffer[TacticalOffset + 3]};
				break;
			case EStrategyType::Retreat:
				TacticalParams = {RetreatBuffer[TacticalOffset], RetreatBuffer[TacticalOffset + 1],
				                  RetreatBuffer[TacticalOffset + 2], RetreatBuffer[TacticalOffset + 3]};
				break;
			}

			Action.TacticalParams = FTacticalParameters(
				TacticalParams[0], TacticalParams[1], TacticalParams[2], TacticalParams[3]
			);

			// Sample combat priority from combat head
			int32 CombatOffset = i * 2;
			TArray<float> CombatLogits = {CombatBuffer[CombatOffset], CombatBuffer[CombatOffset + 1]};
			Action.CombatParams.Priority = SampleCombatPriority(CombatLogits);

			Actions.Add(Action);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork v8.0: Batched inference failed"));
		// Fallback to individual inference
		for (int32 i = 0; i < BatchSize; ++i)
		{
			Actions.Add(GetMacroAction(Observations[i], AssignedStrategies[i]));
		}
	}

	return Actions;
}


TArray<float> URLPolicyNetwork::Softmax(const TArray<float>& Logits)
{
	TArray<float> Probabilities;
	Probabilities.SetNum(Logits.Num());

	// Find max logit for numerical stability
	float MaxLogit = -MAX_FLT;
	for (float Logit : Logits)
	{
		if (Logit > MaxLogit)
		{
			MaxLogit = Logit;
		}
	}

	// Compute exp(logit - max) and sum
	float SumExp = 0.0f;
	for (int32 i = 0; i < Logits.Num(); i++)
	{
		Probabilities[i] = FMath::Exp(Logits[i] - MaxLogit);
		SumExp += Probabilities[i];
	}

	// Normalize
	for (int32 i = 0; i < Probabilities.Num(); i++)
	{
		Probabilities[i] /= SumExp;
	}

	return Probabilities;
}

// ========================================
// v8.0: Helper Methods
// ========================================

TArray<float> URLPolicyNetwork::BuildNetworkInputV8(const FObservationElement& Observation, EStrategyType AssignedStrategy) const
{
	SCOPE_CYCLE_COUNTER(STAT_RLBuildInput);  // v8.0: Profile input preparation

	TArray<float> Input;
	Input.Reserve(68);

	// Base observation (64 features)
	TArray<float> BaseFeatures = Observation.ToFeatureVector();
	check(BaseFeatures.Num() == 64);
	Input.Append(BaseFeatures);

	// Strategy one-hot encoding (4 features)
	// [Assault, Defend, Support, Retreat]
	TArray<float> StrategyOneHot = {0.0f, 0.0f, 0.0f, 0.0f};
	int32 StrategyIndex = static_cast<int32>(AssignedStrategy);
	if (StrategyIndex >= 0 && StrategyIndex < 4)
	{
		StrategyOneHot[StrategyIndex] = 1.0f;
	}
	Input.Append(StrategyOneHot);

	check(Input.Num() == 68);
	return Input;
}

URLPolicyNetwork::FNetworkOutputV8 URLPolicyNetwork::ForwardPassV8(const TArray<float>& InputFeatures)
{
	SCOPE_CYCLE_COUNTER(STAT_RLForwardPass);  // v8.0: Profile ONNX forward pass

	FNetworkOutputV8 Output;

	// Prepare input tensor
	InputBuffer = InputFeatures;
	UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make({1u, 68u});

	// Prepare output tensors (6 outputs from multi-head network)
	TArray<float> AssaultBuffer, DefendBuffer, SupportBuffer, RetreatBuffer, CombatBuffer, ValueBuffer;
	AssaultBuffer.SetNum(4);   // 4 tactical params
	DefendBuffer.SetNum(4);
	SupportBuffer.SetNum(4);
	RetreatBuffer.SetNum(4);
	CombatBuffer.SetNum(2);    // 2 combat logits [Closest, LowestHP]
	ValueBuffer.SetNum(1);     // 1 value estimate

	// Bind tensors
	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.Add({InputBuffer.GetData(), static_cast<uint64>(InputBuffer.Num() * sizeof(float))});

	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	OutputBindings.Add({AssaultBuffer.GetData(), static_cast<uint64>(AssaultBuffer.Num() * sizeof(float))});
	OutputBindings.Add({DefendBuffer.GetData(), static_cast<uint64>(DefendBuffer.Num() * sizeof(float))});
	OutputBindings.Add({SupportBuffer.GetData(), static_cast<uint64>(SupportBuffer.Num() * sizeof(float))});
	OutputBindings.Add({RetreatBuffer.GetData(), static_cast<uint64>(RetreatBuffer.Num() * sizeof(float))});
	OutputBindings.Add({CombatBuffer.GetData(), static_cast<uint64>(CombatBuffer.Num() * sizeof(float))});
	OutputBindings.Add({ValueBuffer.GetData(), static_cast<uint64>(ValueBuffer.Num() * sizeof(float))});

	// Run inference
	TArray<UE::NNE::FTensorShape> InputShapes = {InputShape};
	if (ModelInstance->SetInputTensorShapes(InputShapes) == UE::NNE::EResultStatus::Ok &&
		ModelInstance->RunSync(InputBindings, OutputBindings) == UE::NNE::EResultStatus::Ok)
	{
		Output.AssaultTactical = AssaultBuffer;
		Output.DefendTactical = DefendBuffer;
		Output.SupportTactical = SupportBuffer;
		Output.RetreatTactical = RetreatBuffer;
		Output.CombatLogits = CombatBuffer;
		Output.Value = ValueBuffer[0];
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork v8.0: Inference failed"));
		// Return default values
		Output.AssaultTactical = {0.5f, 0.5f, 0.5f, 0.5f};
		Output.DefendTactical = {0.5f, 0.5f, 0.5f, 0.5f};
		Output.SupportTactical = {0.5f, 0.5f, 0.5f, 0.5f};
		Output.RetreatTactical = {0.5f, 0.5f, 0.5f, 0.5f};
		Output.CombatLogits = {0.0f, 0.0f};
		Output.Value = 0.0f;
	}

	return Output;
}

FTacticalParameters URLPolicyNetwork::SelectTacticalParameters(const FNetworkOutputV8& NetworkOutput, EStrategyType AssignedStrategy) const
{
	TArray<float> Params;

	// Route to appropriate strategy head
	switch (AssignedStrategy)
	{
	case EStrategyType::Assault:
		Params = NetworkOutput.AssaultTactical;
		break;
	case EStrategyType::Defend:
		Params = NetworkOutput.DefendTactical;
		break;
	case EStrategyType::Support:
		Params = NetworkOutput.SupportTactical;
		break;
	case EStrategyType::Retreat:
		Params = NetworkOutput.RetreatTactical;
		break;
	default:
		// Fallback to Assault
		Params = NetworkOutput.AssaultTactical;
		break;
	}

	// Ensure we have 4 parameters
	if (Params.Num() != 4)
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork v8.0: Invalid tactical params size"));
		return FTacticalParameters(0.5f, 0.5f, 0.5f, 0.5f);
	}

	// Construct tactical parameters (already in [0,1] range from Sigmoid)
	FTacticalParameters TacticalParams(Params[0], Params[1], Params[2], Params[3]);
	TacticalParams.Clamp();  // Ensure all values are in [0,1]

	return TacticalParams;
}

ETargetPriority URLPolicyNetwork::SampleCombatPriority(const TArray<float>& CombatLogits) const
{
	SCOPE_CYCLE_COUNTER(STAT_RLSampleStrategy);  // Reuse existing profiling counter

	if (CombatLogits.Num() != 2)
	{
		return ETargetPriority::Closest;
	}

	// Softmax
	TArray<float> Probs = Softmax(CombatLogits);

	// Sample from 2-way distribution
	float Rand = FMath::FRand();

	if (Rand <= Probs[0])
	{
		return ETargetPriority::Closest;
	}
	else
	{
		return ETargetPriority::LowestHP;
	}
}
