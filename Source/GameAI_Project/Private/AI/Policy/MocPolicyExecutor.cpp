// File: AI/Policy/MocPolicyExecutor.cpp
// MOC v10.2 Policy Executor Implementation

#include "AI/Policy/MocPolicyExecutor.h"
#include "AI/EQS/EQSWeightParameters.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"

DEFINE_LOG_CATEGORY_STATIC(LogMocPolicy, Log, All);

UMocPolicyExecutor::UMocPolicyExecutor()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Default model path
	PolicyModelPath = FPaths::ProjectContentDir() / TEXT("AI/Models/policy_weights.onnx");
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
		UE_LOG(LogMocPolicy, Warning, TEXT("Policy model not found: %s. Using fallback defaults."), *ModelPath);

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
	// - NNE (Neural Network Engine) in UE5.3+
	// - Third-party ONNX Runtime plugin
	// - Custom ONNX inference wrapper

	UE_LOG(LogMocPolicy, Warning, TEXT("⚠️ ONNX Runtime not yet integrated. Using fallback defaults."));
	UE_LOG(LogMocPolicy, Log, TEXT("Policy model path set: %s"), *ModelPath);

	PolicyModelPath = ModelPath;

	// Placeholder: Mark as not loaded, but allow fallback operation
	bModelLoaded = false;

	// TODO: Initialize ONNX session
	// ONNXSession = CreateONNXSession(ModelPath);
	// bModelLoaded = (ONNXSession != nullptr);

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

		UE_LOG(LogMocPolicy, Log, TEXT("✅ Policy model loaded successfully: %s"), *ModelPath);
	}

	return true;
}

FEQSWeightParameters UMocPolicyExecutor::InferWeights(EStrategyType Strategy)
{
	if (!bModelLoaded)
	{
		// Use fallback defaults
		return GetDefaultWeights(Strategy);
	}

	// Run ONNX inference
	const double StartTime = FPlatformTime::Seconds();
	TArray<float> OutputTensor = RunONNXInference(Strategy);
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
		return GetDefaultWeights(Strategy);
	}

	// Convert to EQS parameters
	FEQSWeightParameters Weights = FEQSWeightParameters::FromArray(OutputTensor);
	Weights.Clamp(); // Ensure range [-1, 1]

	return Weights;
}

FEQSWeightParameters UMocPolicyExecutor::PredictEQSWeights(const FMocObservation& Observation)
{
	// Backward compatibility: Extract strategy from observation and use InferWeights
	return InferWeights(Observation.CurrentOption);
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

TArray<float> UMocPolicyExecutor::RunONNXInference(EStrategyType Strategy)
{
	// TODO: Replace with actual ONNX Runtime call
	// For now, return fallback defaults

	TArray<float> OutputTensor;
	OutputTensor.SetNumZeroed(8);

	// Placeholder: Convert strategy to one-hot and run inference
	// TArray<float> StrategyOneHot = StrategyToOneHot(Strategy);

	/*
	// Real implementation would look like:
	Ort::MemoryInfo MemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

	std::vector<float> InputData = { StrategyOneHot[0], StrategyOneHot[1], StrategyOneHot[2] };
	std::vector<int64_t> InputShape = {1, 3};

	Ort::Value InputOrtValue = Ort::Value::CreateTensor<float>(
		MemoryInfo,
		InputData.data(),
		InputData.size(),
		InputShape.data(),
		InputShape.size()
	);

	const char* InputNames[] = {"strategy_input"};
	const char* OutputNames[] = {"eqs_weights"};

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

bool UMocPolicyExecutor::ValidateModelSchema()
{
	// TODO: Implement schema validation
	// Expected input: [1, 3] (one-hot strategy encoding)
	// Expected output: [1, 8] (EQS weights)

	return true; // Placeholder
}

FEQSWeightParameters UMocPolicyExecutor::GetDefaultWeights(EStrategyType Strategy)
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
		UE_LOG(LogMocPolicy, Warning, TEXT("Unknown strategy type: %d. Using neutral weights."),
			static_cast<int32>(Strategy));
		// Neutral weights (all zeros)
		break;
	}

	return Weights;
}

TArray<float> UMocPolicyExecutor::StrategyToOneHot(EStrategyType Strategy)
{
	TArray<float> OneHot = {0.0f, 0.0f, 0.0f};

	switch (Strategy)
	{
	case EStrategyType::Assault:
		OneHot[0] = 1.0f;
		break;
	case EStrategyType::Defend:
		OneHot[1] = 1.0f;
		break;
	case EStrategyType::Support:
		OneHot[2] = 1.0f;
		break;
	default:
		UE_LOG(LogMocPolicy, Warning, TEXT("Invalid strategy type for one-hot encoding: %d"),
			static_cast<int32>(Strategy));
		break;
	}

	return OneHot;
}

void UMocPolicyExecutor::LogInferenceStats(float InferenceTimeMs)
{
	UE_LOG(LogMocPolicy, Verbose, TEXT("Policy inference: %.2f ms"), InferenceTimeMs);

	// Warn if exceeding budget (2ms target for real-time operation)
	if (InferenceTimeMs > 2.0f)
	{
		UE_LOG(LogMocPolicy, Warning, TEXT("⚠️ Policy inference exceeds 2ms budget: %.2f ms"), InferenceTimeMs);
	}
}
