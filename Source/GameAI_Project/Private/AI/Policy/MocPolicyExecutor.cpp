// File: AI/Policy/MocPolicyExecutor.cpp
// MOC v10.2 Multi-Head Policy Executor Implementation

#include "AI/Policy/MocPolicyExecutor.h"
#include "Types/EQSTypes.h"
#include "Types/MocTypes.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"

DEFINE_LOG_CATEGORY_STATIC(LogMocPolicy, Log, All);

UMocPolicyExecutor::UMocPolicyExecutor()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Default model path
	PolicyModelPath = FPaths::ProjectContentDir() / TEXT("AI/Models/policy_multihead.onnx");
}

void UMocPolicyExecutor::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoLoadModel && !PolicyModelPath.IsEmpty())
	{
		LoadModel(PolicyModelPath);
	}
}

bool UMocPolicyExecutor::LoadModel(const FString& ModelPath)
{
	// Validate file exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.FileExists(*ModelPath))
	{
		UE_LOG(LogMocPolicy, Warning, TEXT("Policy model not found: %s"), *ModelPath);

		if (bUseFallbackDefaults)
		{
			UE_LOG(LogMocPolicy, Log, TEXT("Fallback mode enabled. Will use strategy-specific defaults."));
			return true; // Allow operation with fallbacks
		}
		return false;
	}

	// TODO: Replace with actual ONNX Runtime loading
	// For now, this is a placeholder implementation
	// Real implementation would use:
	// - NNE (Neural Network Engine) in UE5.4+
	// - Third-party ONNX Runtime plugin
	// - Custom ONNX inference wrapper

	UE_LOG(LogMocPolicy, Warning, TEXT("⚠️ ONNX Runtime not yet integrated. Using fallback defaults."));
	UE_LOG(LogMocPolicy, Log, TEXT("Policy model path set: %s"), *ModelPath);

	PolicyModelPath = ModelPath;

	// Placeholder: Mark as not loaded, but allow fallback operation
	bModelLoaded = false;

	// TODO: Initialize ONNX session with multi-head model
	// Expected architecture:
	// - Input: [BatchSize, 52] (local state)
	// - Shared backbone → 256-dim
	// - 3 strategy heads (assault/defend/support)
	// - Output: [BatchSize, 8] (EQS weights)
	/*
	ONNXSession = CreateONNXSession(ModelPath);
	bModelLoaded = (ONNXSession != nullptr);
	*/

	if (!bModelLoaded && !bUseFallbackDefaults)
	{
		UE_LOG(LogMocPolicy, Error, TEXT("Failed to load ONNX policy model and fallback is disabled."));
		return false;
	}

	if (!bModelLoaded)
	{
		UE_LOG(LogMocPolicy, Log, TEXT("Operating in fallback mode with strategy defaults."));
	}
	else
	{
		// Validate model schema
		if (!ValidateModelSchema())
		{
			UE_LOG(LogMocPolicy, Error, TEXT("Model schema validation failed."));
			bModelLoaded = false;
			return false;
		}

		UE_LOG(LogMocPolicy, Log, TEXT("✅ Multi-head policy model loaded successfully: %s"), *ModelPath);
	}

	return true;
}

FEQSWeightParameters UMocPolicyExecutor::InferWeights(
	EStrategyType CommandedStrategy,
	const FObservation& LocalObservation)
{
	if (!bModelLoaded)
	{
		// Use fallback defaults (no local adaptation)
		return GetDefaultWeights(CommandedStrategy);
	}

	// Run ONNX multi-head inference with local state adaptation
	const double StartTime = FPlatformTime::Seconds();
	TArray<float> OutputTensor = RunMultiHeadInference(CommandedStrategy, LocalObservation);
	const double EndTime = FPlatformTime::Seconds();

	LastInferenceTimeMs = static_cast<float>((EndTime - StartTime) * 1000.0);

	if (bEnableProfiling)
	{
		LogInferenceStats(LastInferenceTimeMs);
	}

	// Validate output dimensions
	if (OutputTensor.Num() != 8)
	{
		UE_LOG(LogMocPolicy, Error, TEXT("Invalid ONNX output size: %d (expected 8)"), OutputTensor.Num());
		return GetDefaultWeights(CommandedStrategy);
	}

	// Convert to EQS parameters
	FEQSWeightParameters Weights = FEQSWeightParameters::FromArray(OutputTensor);
	Weights.Clamp(); // Ensure range [-1, 1]

	return Weights;
}

FEQSWeightParameters UMocPolicyExecutor::GetStrategyDefaults(EStrategyType Strategy)
{
	return GetDefaultWeights(Strategy);
}

bool UMocPolicyExecutor::ReloadModel(const FString& NewModelPath)
{
	UE_LOG(LogMocPolicy, Log, TEXT("Hot-reloading policy model: %s"), *NewModelPath);

	// TODO: Cleanup old session
	if (ONNXSession != nullptr)
	{
		// CleanupONNXSession(ONNXSession);
		ONNXSession = nullptr;
	}

	bModelLoaded = false;

	return LoadModel(NewModelPath);
}

TArray<float> UMocPolicyExecutor::RunMultiHeadInference(
	EStrategyType Strategy,
	const FObservation& LocalObs)
{
	// TODO: Replace with actual ONNX Runtime call
	// For now, return strategy-specific defaults

	TArray<float> OutputTensor;
	OutputTensor.SetNumZeroed(8);

	// Placeholder implementation (will be replaced with real ONNX call)
	/*
	// Real implementation would look like:

	// 1. Encode local observation to 52-dim tensor
	TArray<float> StateTensor = LocalObs.ToArray(); // 52-dim
	check(StateTensor.Num() == 52);

	// 2. Prepare ONNX input
	Ort::MemoryInfo MemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

	std::vector<float> InputData(StateTensor.GetData(), StateTensor.GetData() + 52);
	std::vector<int64_t> InputShape = {1, 52}; // Batch size = 1

	Ort::Value InputOrtValue = Ort::Value::CreateTensor<float>(
		MemoryInfo,
		InputData.data(),
		InputData.size(),
		InputShape.data(),
		InputShape.size()
	);

	// 3. Select output head based on strategy
	const char* OutputName;
	switch (Strategy)
	{
	case EStrategyType::Assault:
		OutputName = "assault_head_output";
		break;
	case EStrategyType::Defend:
		OutputName = "defend_head_output";
		break;
	case EStrategyType::Support:
		OutputName = "support_head_output";
		break;
	default:
		OutputName = "assault_head_output"; // Default fallback
		break;
	}

	const char* InputNames[] = {"state_input"};
	const char* OutputNames[] = {OutputName};

	// 4. Run inference
	auto OutputTensors = ONNXSession->Run(
		Ort::RunOptions{nullptr},
		InputNames,
		&InputOrtValue,
		1,
		OutputNames,
		1
	);

	// 5. Extract output weights [1, 8]
	float* OutputData = OutputTensors[0].GetTensorMutableData<float>();
	for (int i = 0; i < 8; ++i) {
		OutputTensor[i] = OutputData[i];
	}
	*/

	return OutputTensor;
}

bool UMocPolicyExecutor::ValidateModelSchema()
{
	// TODO: Implement schema validation
	// Expected:
	// - Input: [BatchSize, 52] (local state)
	// - Output: [BatchSize, 8] (EQS weights) from selected head
	// - 3 output nodes: assault_head_output, defend_head_output, support_head_output

	return true; // Placeholder
}

FEQSWeightParameters UMocPolicyExecutor::GetDefaultWeights(EStrategyType Strategy)
{
	FEQSWeightParameters Weights;

	// Strategy-specific default weights (based on v10.0Architecture.md Section 2.5)
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
		UE_LOG(LogMocPolicy, Warning, TEXT("Unknown strategy type: %d. Using neutral weights."),
			static_cast<int32>(Strategy));
		// Neutral weights (all zeros)
		break;
	}

	return Weights;
}

void UMocPolicyExecutor::LogInferenceStats(float InferenceTimeMs)
{
	UE_LOG(LogMocPolicy, Verbose, TEXT("Multi-head policy inference: %.2f ms"), InferenceTimeMs);

	// Warn if exceeding budget (2ms target for real-time operation)
	if (InferenceTimeMs > 2.0f)
	{
		UE_LOG(LogMocPolicy, Warning, TEXT("⚠️ Policy inference exceeds 2ms budget: %.2f ms"), InferenceTimeMs);
	}
}
