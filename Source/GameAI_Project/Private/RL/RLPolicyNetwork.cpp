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
#include "Team/Objective.h"
#include "Observation/TeamObservation.h"

URLPolicyNetwork::URLPolicyNetwork()
	: bUseONNXModel(false)
{
}



// ========================================
// v6.0: Strategy Selection
// ========================================

EStrategyType URLPolicyNetwork::GetStrategy(const FObservationElement& Observation, const FObjectiveContext& ObjectiveContext)
{
	if (!bUseONNXModel || !ModelInstance.IsValid())
	{
		// Fallback heuristic
		if (Observation.AgentHealth < 0.3f)
			return EStrategyType::Retreat;
		if (ObjectiveContext.Type == EObjectiveType::Defend)
			return EStrategyType::Defend;
		return EStrategyType::Assault;
	}

	// Build 68-feature input
	TArray<float> InputFeatures = BuildNetworkInput(Observation, ObjectiveContext);
	check(InputFeatures.Num() == 68);

	// Forward pass
	FNetworkOutput Output = ForwardPassV6(InputFeatures);

	// Sample strategy
	EStrategyType Strategy = SampleStrategy(Output.PolicyLogits);

	UE_LOG(LogTemp, Verbose, TEXT("✅ [RL v6.0] Strategy=%s, Value=%.2f, Objective=%d"),
		*UEnum::GetValueAsString(Strategy),
		Output.Value,
		static_cast<int32>(ObjectiveContext.Type));

	return Strategy;
}

float URLPolicyNetwork::GetStateValue(const FObservationElement& Observation, const FObjectiveContext& ObjectiveContext)
{
	if (!bUseONNXModel || !ModelInstance.IsValid())
	{
		// Fallback heuristic value
		float value = (Observation.AgentHealth - 0.5f) * 2.0f;
		value -= Observation.VisibleEnemyCount * 0.2f;
		return FMath::Clamp(value, -1.0f, 1.0f);
	}

	// Build 68-feature input
	TArray<float> InputFeatures = BuildNetworkInput(Observation, ObjectiveContext);

	// Forward pass
	FNetworkOutput Output = ForwardPassV6(InputFeatures);

	return FMath::Clamp(Output.Value, -1.0f, 1.0f);
}

// ========================================
// v6.0: Batched Inference (Performance Critical)
// ========================================

TArray<EStrategyType> URLPolicyNetwork::GetStrategiesBatched(
	const TArray<FObservationElement>& Observations,
	const TArray<FObjectiveContext>& ObjectiveContexts)
{
	TArray<EStrategyType> Strategies;

	if (Observations.Num() != ObjectiveContexts.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("URLPolicyNetwork: Batch size mismatch"));
		return Strategies;
	}

	int32 BatchSize = Observations.Num();
	if (BatchSize == 0) return Strategies;

	// Fallback if model not loaded
	if (!bUseONNXModel || !ModelInstance.IsValid())
	{
		for (int32 i = 0; i < BatchSize; ++i)
		{
			Strategies.Add(GetStrategy(Observations[i], ObjectiveContexts[i]));
		}
		return Strategies;
	}

	// Build batched input tensor [BatchSize, 68]
	TArray<float> BatchedInput;
	BatchedInput.Reserve(BatchSize * 68);

	for (int32 i = 0; i < BatchSize; ++i)
	{
		TArray<float> Features = BuildNetworkInput(Observations[i], ObjectiveContexts[i]);
		check(Features.Num() == 68);
		BatchedInput.Append(Features);
	}

	// Prepare input tensor
	UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make({
		static_cast<uint32>(BatchSize),
		68u
	});

	// Prepare output buffers
	TArray<float> PolicyBuffer, ValueBuffer;
	PolicyBuffer.SetNum(BatchSize * 4);   // 4 strategy logits per agent
	ValueBuffer.SetNum(BatchSize);        // 1 value per agent

	// Bind tensors
	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.Add({BatchedInput.GetData(), static_cast<uint64>(BatchedInput.Num() * sizeof(float))});

	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	OutputBindings.Add({PolicyBuffer.GetData(), static_cast<uint64>(PolicyBuffer.Num() * sizeof(float))});
	OutputBindings.Add({ValueBuffer.GetData(), static_cast<uint64>(ValueBuffer.Num() * sizeof(float))});

	// Run batched inference
	TArray<UE::NNE::FTensorShape> InputShapes = {InputShape};
	if (ModelInstance->SetInputTensorShapes(InputShapes) == UE::NNE::EResultStatus::Ok &&
		ModelInstance->RunSync(InputBindings, OutputBindings) == UE::NNE::EResultStatus::Ok)
	{
		// Decode strategies from batched output
		for (int32 i = 0; i < BatchSize; ++i)
		{
			int32 Offset = i * 4;
			TArray<float> AgentLogits = {
				PolicyBuffer[Offset],
				PolicyBuffer[Offset + 1],
				PolicyBuffer[Offset + 2],
				PolicyBuffer[Offset + 3]
			};
			Strategies.Add(SampleStrategy(AgentLogits));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Batched inference failed"));
		// Fallback to individual inference
		for (int32 i = 0; i < BatchSize; ++i)
		{
			Strategies.Add(GetStrategy(Observations[i], ObjectiveContexts[i]));
		}
	}

	return Strategies;
}

TArray<float> URLPolicyNetwork::GetStateValuesBatched(
	const TArray<FObservationElement>& Observations,
	const TArray<FObjectiveContext>& ObjectiveContexts)
{
	TArray<float> Values;

	if (Observations.Num() != ObjectiveContexts.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("URLPolicyNetwork: Batch size mismatch"));
		return Values;
	}

	int32 BatchSize = Observations.Num();
	if (BatchSize == 0) return Values;

	// Fallback if model not loaded
	if (!bUseONNXModel || !ModelInstance.IsValid())
	{
		for (int32 i = 0; i < BatchSize; ++i)
		{
			Values.Add(GetStateValue(Observations[i], ObjectiveContexts[i]));
		}
		return Values;
	}

	// Build batched input tensor (same as GetStrategiesBatched)
	TArray<float> BatchedInput;
	BatchedInput.Reserve(BatchSize * 68);

	for (int32 i = 0; i < BatchSize; ++i)
	{
		TArray<float> Features = BuildNetworkInput(Observations[i], ObjectiveContexts[i]);
		BatchedInput.Append(Features);
	}

	// Run batched inference (same tensor binding as above)
	UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make({
		static_cast<uint32>(BatchSize),
		68u
	});

	TArray<float> PolicyBuffer, ValueBuffer;
	PolicyBuffer.SetNum(BatchSize * 4);
	ValueBuffer.SetNum(BatchSize);

	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.Add({BatchedInput.GetData(), static_cast<uint64>(BatchedInput.Num() * sizeof(float))});

	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	OutputBindings.Add({PolicyBuffer.GetData(), static_cast<uint64>(PolicyBuffer.Num() * sizeof(float))});
	OutputBindings.Add({ValueBuffer.GetData(), static_cast<uint64>(ValueBuffer.Num() * sizeof(float))});

	TArray<UE::NNE::FTensorShape> InputShapes = {InputShape};
	if (ModelInstance->SetInputTensorShapes(InputShapes) == UE::NNE::EResultStatus::Ok &&
		ModelInstance->RunSync(InputBindings, OutputBindings) == UE::NNE::EResultStatus::Ok)
	{
		// Extract values from batched output
		Values = ValueBuffer;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Batched value inference failed"));
		// Fallback
		for (int32 i = 0; i < BatchSize; ++i)
		{
			Values.Add(GetStateValue(Observations[i], ObjectiveContexts[i]));
		}
	}

	return Values;
}

// ========================================
// v6.0: Helper Methods
// ========================================

TArray<float> URLPolicyNetwork::BuildNetworkInput(const FObservationElement& Observation, const FObjectiveContext& ObjectiveContext) const
{
	TArray<float> Input;
	Input.Reserve(68);

	// Base observation (64 features)
	TArray<float> BaseFeatures = Observation.ToFeatureVector();
	check(BaseFeatures.Num() == 64);
	Input.Append(BaseFeatures);

	// Objective context (4 features)
	TArray<float> ObjectiveFeatures = ObjectiveContext.ToFeatureVector();
	check(ObjectiveFeatures.Num() == 4);
	Input.Append(ObjectiveFeatures);

	return Input;
}

URLPolicyNetwork::FNetworkOutput URLPolicyNetwork::ForwardPassV6(const TArray<float>& InputFeatures)
{
	FNetworkOutput Output;

	// Prepare input tensor
	InputBuffer = InputFeatures;
	UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make({1u, 68u});

	// Prepare output tensors
	TArray<float> PolicyBuffer, ValueBuffer;
	PolicyBuffer.SetNum(4);   // 4 strategy logits
	ValueBuffer.SetNum(1);    // 1 value estimate

	// Bind tensors
	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.Add({InputBuffer.GetData(), static_cast<uint64>(InputBuffer.Num() * sizeof(float))});

	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	OutputBindings.Add({PolicyBuffer.GetData(), static_cast<uint64>(PolicyBuffer.Num() * sizeof(float))});
	OutputBindings.Add({ValueBuffer.GetData(), static_cast<uint64>(ValueBuffer.Num() * sizeof(float))});

	// Run inference
	TArray<UE::NNE::FTensorShape> InputShapes = {InputShape};
	if (ModelInstance->SetInputTensorShapes(InputShapes) == UE::NNE::EResultStatus::Ok &&
		ModelInstance->RunSync(InputBindings, OutputBindings) == UE::NNE::EResultStatus::Ok)
	{
		Output.PolicyLogits = PolicyBuffer;
		Output.Value = ValueBuffer[0];
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Inference failed"));
		Output.PolicyLogits = {0.0f, 0.0f, 0.0f, 0.0f};
		Output.Value = 0.0f;
	}

	return Output;
}

EStrategyType URLPolicyNetwork::SampleStrategy(const TArray<float>& Logits) const
{
	if (Logits.Num() != 4)
	{
		return EStrategyType::Assault;
	}

	// Softmax
	TArray<float> Probs = Softmax(Logits);

	// Sample
	float Rand = FMath::FRand();
	float CumulativeProb = 0.0f;

	for (int32 i = 0; i < 4; ++i)
	{
		CumulativeProb += Probs[i];
		if (Rand <= CumulativeProb)
		{
			return static_cast<EStrategyType>(i);
		}
	}

	return EStrategyType::Assault;
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


int32 URLPolicyNetwork::SampleFromLogits(const TArray<float>& Logits)
{
	if (Logits.Num() == 0)
	{
		return 0;
	}

	// Convert logits to probabilities via softmax
	TArray<float> Probs = Softmax(Logits);

	// Sample from categorical distribution
	float Rand = FMath::FRand();
	float CumulativeProb = 0.0f;

	for (int32 i = 0; i < Probs.Num(); ++i)
	{
		CumulativeProb += Probs[i];
		if (Rand <= CumulativeProb)
		{
			return i;
		}
	}

	// Fallback to last index (should rarely happen due to floating point errors)
	return Probs.Num() - 1;
}

TArray<float> URLPolicyNetwork::SelectStrategyHead(const TArray<float>& AllHeadsOutput, EStrategyType Strategy)
{
	// v5.0: Select appropriate head output based on strategy
	// AllHeadsOutput format: [Assault(13) | Defend(13) | Support(13) | Retreat(13)] = 52 logits
	// Each head outputs: [Position(4) | Target(6) | Fire(3)] = 13 logits

	if (AllHeadsOutput.Num() < 52)
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork::SelectStrategyHead: Expected 52 logits, got %d"),
			AllHeadsOutput.Num());
		return AllHeadsOutput;  // Return as-is if size mismatch
	}

	const int32 HeadSize = 13;  // 4 pos + 6 target + 3 fire
	int32 HeadIndex = 0;

	// Map strategy to head index
	switch (Strategy)
	{
		case EStrategyType::Assault:
			HeadIndex = 0;
			break;
		case EStrategyType::Defend:
			HeadIndex = 1;
			break;
		case EStrategyType::Support:
			HeadIndex = 2;
			break;
		case EStrategyType::Retreat:
			HeadIndex = 3;
			break;
		default:
			UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork::SelectStrategyHead: Unknown strategy, defaulting to Assault"));
			HeadIndex = 0;
			break;
	}

	// Extract head-specific logits
	TArray<float> SelectedHead;
	SelectedHead.Reserve(HeadSize);
	int32 StartIdx = HeadIndex * HeadSize;

	for (int32 i = 0; i < HeadSize; ++i)
	{
		SelectedHead.Add(AllHeadsOutput[StartIdx + i]);
	}

	return SelectedHead;
}
