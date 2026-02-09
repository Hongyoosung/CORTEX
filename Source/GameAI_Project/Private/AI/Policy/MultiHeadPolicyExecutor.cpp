// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/Policy/MultiHeadRLPolicy.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Core/ProfilingMacros.h"

UMultiHeadPolicyExecutor::UMultiHeadPolicyExecutor()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Default model path (can be overridden in Blueprint)
	PolicyModelPath = FPaths::ProjectContentDir() / TEXT("AI/Models/policy_weights.onnx");
}

void UMultiHeadPolicyExecutor::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoLoadModel && !PolicyModelPath.IsEmpty())
	{
		LoadPolicyModel(PolicyModelPath);
	}
}

bool UMultiHeadPolicyExecutor::LoadPolicyModel(const FString& ModelPath)
{
	PROFILE_SCOPE_LOG("LoadPolicyModel");

	// Validate file exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.FileExists(*ModelPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Policy model not found: %s"), *ModelPath);
		return false;
	}

	// TODO: Replace with actual ONNX Runtime loading
	// For now, this is a placeholder implementation
	// Real implementation would use:
	// - NNE (Neural Network Engine) in UE5.3+
	// - Third-party ONNX Runtime plugin
	// - Custom ONNX inference wrapper

	UE_LOG(LogTemp, Warning, TEXT("⚠️ ONNX Runtime not yet integrated. Using placeholder."));
	UE_LOG(LogTemp, Log, TEXT("Policy model path set: %s"), *ModelPath);

	PolicyModelPath = ModelPath;

	// Placeholder: Mark as loaded (will be replaced with actual loading)
	bModelLoaded = false; // Set to true when real implementation is added

	// TODO: Initialize ONNX session
	// ONNXSession = CreateONNXSession(ModelPath);
	// bModelLoaded = (ONNXSession != nullptr);

	if (!bModelLoaded)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load ONNX policy model. Inference will return default weights."));
		return false;
	}

	// Validate model schema
	if (!ValidateModelSchema())
	{
		UE_LOG(LogTemp, Error, TEXT("Model schema validation failed. Expected input: [1,61], output: [1,8]"));
		bModelLoaded = false;
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("✅ Policy model loaded successfully: %s"), *ModelPath);
	return true;
}

FEQSWeightParameters UMultiHeadPolicyExecutor::GetEQSWeights(const FMocObservation& Observation)
{
	PROFILE_SCOPE_LOG("PolicyInference");

	if (!bModelLoaded)
	{
		UE_LOG(LogTemp, Warning, TEXT("Policy model not loaded. Returning default weights for strategy: %d"),
			static_cast<int32>(Observation.CurrentOption));

		// Return strategy-specific default weights (fallback behavior)
		return GetDefaultWeightsForStrategy(Observation.CurrentOption);
	}

	// Convert observation to input tensor
	TArray<float> InputTensor = Observation.ToTensor();
	check(InputTensor.Num() == 61);

	// Run ONNX inference
	const double StartTime = FPlatformTime::Seconds();
	TArray<float> OutputTensor = RunONNXInference(InputTensor);
	const double EndTime = FPlatformTime::Seconds();

	LastInferenceTimeMs = static_cast<float>((EndTime - StartTime) * 1000.0);

	if (bEnableProfiling)
	{
		LogInferenceStats(1, LastInferenceTimeMs);
	}

	// Validate output dimensions
	if (OutputTensor.Num() != 8)
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid ONNX output size: %d (expected 8)"), OutputTensor.Num());
		return GetDefaultWeightsForStrategy(Observation.CurrentOption);
	}

	// Convert to EQS parameters
	FEQSWeightParameters Weights = FEQSWeightParameters::FromArray(OutputTensor);
	Weights.Clamp(); // Ensure range [-1, 1]

	return Weights;
}

TArray<FEQSWeightParameters> UMultiHeadPolicyExecutor::GetEQSWeightsBatch(const TArray<FMocObservation>& Observations)
{
	PROFILE_SCOPE_LOG("PolicyInferenceBatch");

	TArray<FEQSWeightParameters> Results;
	const int32 BatchSize = Observations.Num();

	if (BatchSize == 0)
	{
		return Results;
	}

	if (!bModelLoaded)
	{
		UE_LOG(LogTemp, Warning, TEXT("Policy model not loaded. Returning default weights for %d agents"), BatchSize);

		// Return default weights for each observation
		Results.Reserve(BatchSize);
		for (const FMocObservation& Obs : Observations)
		{
			Results.Add(GetDefaultWeightsForStrategy(Obs.CurrentOption));
		}
		return Results;
	}

	// Flatten batch input [BatchSize, 61]
	TArray<float> BatchInputTensor;
	BatchInputTensor.Reserve(BatchSize * 61);

	for (const FMocObservation& Obs : Observations)
	{
		BatchInputTensor.Append(Obs.ToTensor());
	}

	// Run batch inference
	const double StartTime = FPlatformTime::Seconds();
	TArray<float> BatchOutputTensor = RunONNXInferenceBatch(BatchInputTensor, BatchSize);
	const double EndTime = FPlatformTime::Seconds();

	LastInferenceTimeMs = static_cast<float>((EndTime - StartTime) * 1000.0);

	if (bEnableProfiling)
	{
		LogInferenceStats(BatchSize, LastInferenceTimeMs);
	}

	// Parse output [BatchSize, 8]
	if (BatchOutputTensor.Num() != BatchSize * 8)
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid batch ONNX output size: %d (expected %d)"),
			BatchOutputTensor.Num(), BatchSize * 8);

		// Return defaults
		Results.Reserve(BatchSize);
		for (const FMocObservation& Obs : Observations)
		{
			Results.Add(GetDefaultWeightsForStrategy(Obs.CurrentOption));
		}
		return Results;
	}

	// Convert to EQS parameters
	Results.Reserve(BatchSize);
	for (int32 i = 0; i < BatchSize; ++i)
	{
		const int32 Offset = i * 8;
		TArray<float> WeightArray;
		WeightArray.Reserve(8);

		for (int32 j = 0; j < 8; ++j)
		{
			WeightArray.Add(BatchOutputTensor[Offset + j]);
		}

		FEQSWeightParameters Weights = FEQSWeightParameters::FromArray(WeightArray);
		Weights.Clamp();
		Results.Add(Weights);
	}

	return Results;
}

bool UMultiHeadPolicyExecutor::ReloadPolicyModel(const FString& NewModelPath)
{
	UE_LOG(LogTemp, Log, TEXT("Hot-reloading policy model: %s"), *NewModelPath);

	// TODO: Cleanup old session
	if (ONNXSession != nullptr)
	{
		// CleanupONNXSession(ONNXSession);
		ONNXSession = nullptr;
	}

	bModelLoaded = false;

	return LoadPolicyModel(NewModelPath);
}

TArray<float> UMultiHeadPolicyExecutor::RunONNXInference(const TArray<float>& InputTensor)
{
	check(InputTensor.Num() == 61);

	// TODO: Replace with actual ONNX Runtime call
	// Placeholder implementation returns strategy-dependent defaults

	TArray<float> OutputTensor;
	OutputTensor.SetNumZeroed(8);

	// Example placeholder (will be replaced with real ONNX call)
	/*
	// Real implementation would look like:
	Ort::MemoryInfo MemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

	std::vector<float> InputData(InputTensor.GetData(), InputTensor.GetData() + 61);
	std::vector<int64_t> InputShape = {1, 61};

	Ort::Value InputOrtValue = Ort::Value::CreateTensor<float>(
		MemoryInfo,
		InputData.data(),
		InputData.size(),
		InputShape.data(),
		InputShape.size()
	);

	const char* InputNames[] = {"input"};
	const char* OutputNames[] = {"output"};

	auto OutputTensors = ONNXSession->Run(
		Ort::RunOptions{nullptr},
		InputNames,
		&InputOrtValue,
		1,
		OutputNames,
		1
	);

	float* OutputData = OutputTensors[0].GetTensorMutableData<float>();
	for (int i = 0; i < 8; ++i) {
		OutputTensor[i] = OutputData[i];
	}
	*/

	return OutputTensor;
}

TArray<float> UMultiHeadPolicyExecutor::RunONNXInferenceBatch(const TArray<float>& InputTensors, int32 BatchSize)
{
	check(InputTensors.Num() == BatchSize * 61);

	TArray<float> OutputTensors;
	OutputTensors.SetNumZeroed(BatchSize * 8);

	// TODO: Replace with actual batch ONNX Runtime call
	// For now, fall back to sequential inference (inefficient)
	for (int32 i = 0; i < BatchSize; ++i)
	{
		TArray<float> SingleInput;
		SingleInput.Reserve(61);

		const int32 InputOffset = i * 61;
		for (int32 j = 0; j < 61; ++j)
		{
			SingleInput.Add(InputTensors[InputOffset + j]);
		}

		TArray<float> SingleOutput = RunONNXInference(SingleInput);

		const int32 OutputOffset = i * 8;
		for (int32 j = 0; j < 8; ++j)
		{
			OutputTensors[OutputOffset + j] = SingleOutput[j];
		}
	}

	return OutputTensors;
}

bool UMultiHeadPolicyExecutor::ValidateModelSchema()
{
	// TODO: Implement schema validation
	// Check input shape: [1, 61] or [batch, 61]
	// Check output shape: [1, 8] or [batch, 8]

	return true; // Placeholder
}

void UMultiHeadPolicyExecutor::LogInferenceStats(int32 BatchSize, float InferenceTimeMs)
{
	if (BatchSize == 1)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Policy inference: %.2f ms"), InferenceTimeMs);
	}
	else
	{
		const float TimePerSample = InferenceTimeMs / BatchSize;
		UE_LOG(LogTemp, Verbose, TEXT("Policy batch inference: %.2f ms (%.2f ms/sample, batch=%d)"),
			InferenceTimeMs, TimePerSample, BatchSize);
	}

	// Warn if exceeding budget
	if (InferenceTimeMs > 2.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ Policy inference exceeds 2ms budget: %.2f ms"), InferenceTimeMs);
	}
}

FEQSWeightParameters UMultiHeadPolicyExecutor::GetDefaultWeightsForStrategy(EStrategyType Strategy)
{
	FEQSWeightParameters Weights;

	// Strategy-specific default weights (based on v10.0Architecture.md Table 2.5)
	switch (Strategy)
	{
	case EStrategyType::Assault:
		Weights.EnemyObjectiveProximity = 0.9f;
		Weights.AllyObjectiveProximity = -0.3f;
		Weights.CoverDensity = 0.2f;
		Weights.EnemyVisibility = 0.8f;
		Weights.AllyProximity = 0.4f;
		Weights.CombatRange = 0.6f;
		Weights.PickupProximity = -0.5f;
		Weights.HeightAdvantage = 0.5f;
		break;

	case EStrategyType::Defend:
		Weights.EnemyObjectiveProximity = -0.7f;
		Weights.AllyObjectiveProximity = 0.9f;
		Weights.CoverDensity = 0.8f;
		Weights.EnemyVisibility = 0.3f;
		Weights.AllyProximity = 0.7f;
		Weights.CombatRange = -0.4f;
		Weights.PickupProximity = 0.2f;
		Weights.HeightAdvantage = 0.7f;
		break;

	case EStrategyType::Support:
		Weights.EnemyObjectiveProximity = 0.0f;
		Weights.AllyObjectiveProximity = 0.5f;
		Weights.CoverDensity = 0.5f;
		Weights.EnemyVisibility = 0.2f;
		Weights.AllyProximity = 0.9f; // Stay close to allies
		Weights.CombatRange = 0.0f;
		Weights.PickupProximity = 0.6f; // Collect resources for team
		Weights.HeightAdvantage = 0.3f;
		break;

	default:
		UE_LOG(LogTemp, Warning, TEXT("Unknown strategy type: %d. Using neutral weights."),
			static_cast<int32>(Strategy));
		// Neutral weights (all zeros)
		break;
	}

	return Weights;
}
