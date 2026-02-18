// File: AI/Policy/MocPolicyExecutor.cpp
// MOC v10.2 Multi-Head Policy Executor Implementation

#include "AI/Policy/MocPolicyExecutor.h"
#include "Types/EQSTypes.h"
#include "Types/MocTypes.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "NNERuntime.h"

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
	using namespace UE::NNE;

	bModelLoaded = false;
	PolicyModelPath = ModelPath;

	// Validate file exists
	if (!FPaths::FileExists(ModelPath))
	{
		UE_LOG(LogMocPolicy, Warning, TEXT("Policy model not found: %s"), *ModelPath);

		if (bUseFallbackDefaults)
		{
			UE_LOG(LogMocPolicy, Log, TEXT("Fallback mode enabled. Will use strategy-specific defaults."));
			return true;
		}
		return false;
	}

	// Load model file data
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *ModelPath))
	{
		UE_LOG(LogMocPolicy, Error, TEXT("Failed to load model file: %s"), *ModelPath);
		return bUseFallbackDefaults;
	}

	UE_LOG(LogMocPolicy, Log, TEXT("Loaded policy model data (%d bytes) from %s"), FileData.Num(), *ModelPath);

	// Create UNNEModelData
	UNNEModelData* ModelData = NewObject<UNNEModelData>(GetTransientPackage());
	if (!ModelData)
	{
		UE_LOG(LogMocPolicy, Error, TEXT("Failed to create UNNEModelData object"));
		return bUseFallbackDefaults;
	}

	const FString FileExt = FPaths::GetExtension(ModelPath);
	ModelData->Init(
		FileExt,
		TConstArrayView64<uint8>(FileData),
		TMap<FString, TConstArrayView64<uint8>>()
	);

	// Get NNE Runtime (ONNX Runtime CPU backend)
	TWeakInterfacePtr<INNERuntimeCPU> Runtime =
		UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu"));

	if (!Runtime.IsValid())
	{
		UE_LOG(LogMocPolicy, Error, TEXT("Failed to get NNE runtime 'NNERuntimeORTCpu'"));
		if (bUseFallbackDefaults)
		{
			UE_LOG(LogMocPolicy, Log, TEXT("Operating in fallback mode with strategy defaults."));
			return true;
		}
		return false;
	}

	// Create model
	TSharedPtr<IModelCPU> Model = Runtime->CreateModelCPU(ModelData);
	if (!Model.IsValid())
	{
		UE_LOG(LogMocPolicy, Error, TEXT("Failed to create NNE model from data"));
		if (bUseFallbackDefaults)
		{
			UE_LOG(LogMocPolicy, Log, TEXT("Operating in fallback mode with strategy defaults."));
			return true;
		}
		return false;
	}

	// Create model instance
	ModelInstance = Model->CreateModelInstanceCPU();
	if (!ModelInstance.IsValid())
	{
		UE_LOG(LogMocPolicy, Error, TEXT("Failed to create model instance"));
		if (bUseFallbackDefaults)
		{
			UE_LOG(LogMocPolicy, Log, TEXT("Operating in fallback mode with strategy defaults."));
			return true;
		}
		return false;
	}

	// Validate tensor descriptors
	TConstArrayView<FTensorDesc> InputDescs = ModelInstance->GetInputTensorDescs();
	TConstArrayView<FTensorDesc> OutputDescs = ModelInstance->GetOutputTensorDescs();

	UE_LOG(LogMocPolicy, Log, TEXT("Policy model - Input tensors: %d, Output tensors: %d"),
		InputDescs.Num(), OutputDescs.Num());

	for (int32 i = 0; i < InputDescs.Num(); ++i)
	{
		UE_LOG(LogMocPolicy, Log, TEXT("  Input[%d] Name=%s, Rank=%d"),
			i, *InputDescs[i].GetName(), InputDescs[i].GetShape().Rank());
	}
	for (int32 i = 0; i < OutputDescs.Num(); ++i)
	{
		UE_LOG(LogMocPolicy, Log, TEXT("  Output[%d] Name=%s, Rank=%d"),
			i, *OutputDescs[i].GetName(), OutputDescs[i].GetShape().Rank());
	}

	bModelLoaded = true;

	// Validate model schema
	if (!ValidateModelSchema())
	{
		UE_LOG(LogMocPolicy, Error, TEXT("Model schema validation failed."));
		bModelLoaded = false;
		ModelInstance.Reset();
		return bUseFallbackDefaults;
	}

	UE_LOG(LogMocPolicy, Log, TEXT("Multi-head policy model loaded successfully: %s"), *ModelPath);
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

	// Cleanup old model instance
	ModelInstance.Reset();
	ONNXSession = nullptr;
	bModelLoaded = false;

	return LoadModel(NewModelPath);
}

TArray<float> UMocPolicyExecutor::RunMultiHeadInference(
	EStrategyType Strategy,
	const FObservation& LocalObs)
{
	using namespace UE::NNE;

	// Build input tensor: 57-dim observation + 1-dim strategy index = 58-dim
	TArray<float> ObsTensor = LocalObs.ToArray(); // 57-dim
	const int32 StrategyIndex = static_cast<int32>(Strategy);

	// Construct full input: obs (57) + strategy_index (1) = 58
	constexpr int32 InputDim = 58;
	TArray<float> InputData;
	InputData.Reserve(InputDim);
	InputData.Append(ObsTensor);

	// Pad or truncate obs to 57 dims
	while (InputData.Num() < 57)
	{
		InputData.Add(0.0f);
	}
	if (InputData.Num() > 57)
	{
		InputData.SetNum(57);
	}

	// Append strategy index (normalized to [0,1] range: 0=Assault, 1=Defend, 2=Support → 0.0, 0.5, 1.0)
	InputData.Add(static_cast<float>(StrategyIndex) / 2.0f);

	// Output tensor: 8-dim (7 EQS weights + 1 confidence)
	constexpr int32 OutputDim = 8;
	TArray<float> OutputData;
	OutputData.SetNumUninitialized(OutputDim);

	if (!ModelInstance.IsValid())
	{
		UE_LOG(LogMocPolicy, Error, TEXT("Model instance invalid during inference"));
		OutputData.Init(0.0f, OutputDim);
		return OutputData;
	}

	// Set input tensor shape
	FTensorShape InputShape = FTensorShape::Make({1, InputDim});
	const auto SetShapeStatus = ModelInstance->SetInputTensorShapes({InputShape});
	if (SetShapeStatus != IModelInstanceRunSync::ESetInputTensorShapesStatus::Ok)
	{
		UE_LOG(LogMocPolicy, Error, TEXT("Failed to set input tensor shape"));
		OutputData.Init(0.0f, OutputDim);
		return OutputData;
	}

	// Create tensor bindings
	TArray<FTensorBindingCPU> InputBindings;
	TArray<FTensorBindingCPU> OutputBindings;

	InputBindings.Add(FTensorBindingCPU{InputData.GetData(), InputData.Num() * sizeof(float)});
	OutputBindings.Add(FTensorBindingCPU{OutputData.GetData(), OutputData.Num() * sizeof(float)});

	// Run inference
	const auto RunResult = ModelInstance->RunSync(InputBindings, OutputBindings);
	if (RunResult != IModelInstanceRunSync::ERunSyncStatus::Ok)
	{
		UE_LOG(LogMocPolicy, Error, TEXT("Policy inference failed with code %d"), static_cast<int32>(RunResult));
		// Fallback to defaults
		FEQSWeightParameters Defaults = GetDefaultWeights(Strategy);
		OutputData[0] = Defaults.EnemyObjectiveProximity;
		OutputData[1] = Defaults.AllyObjectiveProximity;
		OutputData[2] = Defaults.CoverDensity;
		OutputData[3] = Defaults.EnemyVisibility;
		OutputData[4] = Defaults.AllyProximity;
		OutputData[5] = Defaults.CombatRange;
		OutputData[6] = Defaults.PickupProximity;
		OutputData[7] = 0.0f; // Low confidence
	}

	return OutputData;
}

bool UMocPolicyExecutor::ValidateModelSchema()
{
	if (!ModelInstance.IsValid())
	{
		return false;
	}

	TConstArrayView<UE::NNE::FTensorDesc> InputDescs = ModelInstance->GetInputTensorDescs();
	TConstArrayView<UE::NNE::FTensorDesc> OutputDescs = ModelInstance->GetOutputTensorDescs();

	if (InputDescs.Num() == 0 || OutputDescs.Num() == 0)
	{
		UE_LOG(LogMocPolicy, Error, TEXT("Model has no input or output tensors"));
		return false;
	}

	// Verify input rank is at least 2 (batch, features)
	if (InputDescs[0].GetShape().Rank() < 2)
	{
		UE_LOG(LogMocPolicy, Warning, TEXT("Input tensor rank %d < 2, may be incompatible"),
			InputDescs[0].GetShape().Rank());
	}

	return true;
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
		break;

	case EStrategyType::Defend:
		Weights.EnemyObjectiveProximity = -0.7f;
		Weights.AllyObjectiveProximity = 0.9f;
		Weights.CoverDensity = 0.8f;
		Weights.EnemyVisibility = 0.3f;
		Weights.AllyProximity = 0.7f;
		Weights.CombatRange = -0.4f;
		Weights.PickupProximity = 0.2f;
		break;

	case EStrategyType::Support:
		Weights.EnemyObjectiveProximity = 0.0f;
		Weights.AllyObjectiveProximity = 0.5f;
		Weights.CoverDensity = 0.5f;
		Weights.EnemyVisibility = 0.2f;
		Weights.AllyProximity = 0.9f; // Stay close to allies
		Weights.CombatRange = 0.0f;
		Weights.PickupProximity = 0.6f; // Collect resources for team
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
		UE_LOG(LogMocPolicy, Warning, TEXT("Policy inference exceeds 2ms budget: %.2f ms"), InferenceTimeMs);
	}
}
