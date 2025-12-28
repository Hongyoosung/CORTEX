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
	: bEnableExploration(true)
	, bUseONNXModel(false)
	, bIsInitialized(false)
	, CurrentEpisodeReward(0.0f)
	, CurrentEpisodeSteps(0)
{
}

// ========================================
// Initialization
// ========================================

bool URLPolicyNetwork::Initialize(const FRLPolicyConfig& InConfig)
{
	Config = InConfig;
	bIsInitialized = true;

	UE_LOG(LogTemp, Log, TEXT("URLPolicyNetwork: Initialized with %d inputs, %d outputs"),
		Config.InputSize, Config.OutputSize);

	return true;
}

bool URLPolicyNetwork::LoadPolicy(const FString& ModelPath)
{
	Config.ModelPath = ModelPath;

	// Resolve path - support both absolute and relative paths
	FString ResolvedPath = ModelPath;
	if (!FPaths::FileExists(ResolvedPath))
	{
		// Try relative to project content directory
		ResolvedPath = FPaths::ProjectContentDir() / ModelPath;
	}
	if (!FPaths::FileExists(ResolvedPath))
	{
		// Try relative to project saved directory
		ResolvedPath = FPaths::ProjectSavedDir() / ModelPath;
	}

	if (!FPaths::FileExists(ResolvedPath))
	{
		UE_LOG(LogTemp, Error, TEXT("URLPolicyNetwork: Model file not found: %s"), *ModelPath);
		bUseONNXModel = false;
		bIsInitialized = true;
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("URLPolicyNetwork: Loading ONNX model from: %s"), *ResolvedPath);

	// Load model data from file
	TArray<uint8> ModelBytes;
	if (!FFileHelper::LoadFileToArray(ModelBytes, *ResolvedPath))
	{
		UE_LOG(LogTemp, Error, TEXT("URLPolicyNetwork: Failed to read model file"));
		bUseONNXModel = false;
		bIsInitialized = true;
		return false;
	}

	// Get NNE runtime
	TWeakInterfacePtr<INNERuntimeCPU> RuntimeCPU = UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu"));
	if (!RuntimeCPU.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("URLPolicyNetwork: NNERuntimeORTCpu not available. Using rule-based fallback."));
		bUseONNXModel = false;
		bIsInitialized = true;
		return false;
	}

	// Create model from bytes
	TObjectPtr<UNNEModelData> NewModelData = NewObject<UNNEModelData>();
	NewModelData->Init(TEXT("Onnx"), ModelBytes, TMap<FString, TConstArrayView64<uint8>>());
	TSharedPtr<UE::NNE::IModelCPU> Model = RuntimeCPU->CreateModelCPU(NewModelData);
	if (!Model.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("URLPolicyNetwork: Failed to create NNE model from ONNX data"));
		bUseONNXModel = false;
		bIsInitialized = true;
		return false;
	}

	// Create model instance
	ModelInstance = Model->CreateModelInstanceCPU();
	if (!ModelInstance.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("URLPolicyNetwork: Failed to create NNE model instance"));
		bUseONNXModel = false;
		bIsInitialized = true;
		return false;
	}

	// Get input/output tensor info
	TConstArrayView<UE::NNE::FTensorDesc> InputDescs = ModelInstance->GetInputTensorDescs();
	TConstArrayView<UE::NNE::FTensorDesc> OutputDescs = ModelInstance->GetOutputTensorDescs();

	if (InputDescs.Num() < 1 || OutputDescs.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("URLPolicyNetwork: Model must have at least 1 input and 1 output"));
		ModelInstance.Reset();
		bUseONNXModel = false;
		bIsInitialized = true;
		return false;
	}

	// Log tensor info
	UE_LOG(LogTemp, Log, TEXT("URLPolicyNetwork: Model loaded successfully"));
	UE_LOG(LogTemp, Log, TEXT("  Input tensors: %d"), InputDescs.Num());
	UE_LOG(LogTemp, Log, TEXT("  Output tensors: %d"), OutputDescs.Num());

	// Setup input buffer (78 features: 71 obs + 7 objective embedding)
	InputBuffer.SetNum(Config.InputSize);

	// Setup output buffers for dual-head PPO model
	// Output 0: action_logits (8 atomic actions)
	// Output 1: state_value (1 value estimate)
	OutputBuffer.SetNum(Config.OutputSize + 1);  // 8 + 1 = 9 total

	bUseONNXModel = true;
	bIsInitialized = true;

	UE_LOG(LogTemp, Log, TEXT("URLPolicyNetwork: ONNX model ready for inference"));
	return true;
}

void URLPolicyNetwork::UnloadPolicy()
{
	bIsInitialized = false;
	bUseONNXModel = false;
	Config.ModelPath = TEXT("");

	// Reset NNE model instance
	if (ModelInstance.IsValid())
	{
		ModelInstance.Reset();
	}
	ModelData = nullptr;
	InputBuffer.Empty();
	OutputBuffer.Empty();

	UE_LOG(LogTemp, Log, TEXT("URLPolicyNetwork: Policy unloaded"));
}

// ========================================
// Experience Collection - REMOVED
// Real-time PPO training via RLlib handles experience collection automatically
// No need for C++ side JSON export or offline training
// ========================================

// ========================================
// Statistics
// ========================================

void URLPolicyNetwork::ResetStatistics()
{
	TrainingStats = FRLTrainingStats();
	CurrentEpisodeReward = 0.0f;
	CurrentEpisodeSteps = 0;

	UE_LOG(LogTemp, Log, TEXT("URLPolicyNetwork: Reset statistics"));
}

void URLPolicyNetwork::UpdateEpsilon()
{
	Config.Epsilon = FMath::Max(Config.Epsilon * Config.EpsilonDecay, Config.MinEpsilon);
}

// ========================================
// Neural Network Inference
// ========================================

TArray<float> URLPolicyNetwork::ForwardPass(const TArray<float>& InputFeatures)
{
	if (!ModelInstance.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Model instance not valid, returning zero action"));
		TArray<float> ZeroAction;
		ZeroAction.Init(0.0f, Config.OutputSize);
		return ZeroAction;
	}

	// Copy input features to buffer
	if (InputFeatures.Num() != Config.InputSize)
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Input size mismatch (got %d, expected %d)"),
			InputFeatures.Num(), Config.InputSize);
		TArray<float> ZeroAction;
		ZeroAction.Init(0.0f, Config.OutputSize);
		return ZeroAction;
	}

	// Prepare input tensor binding
	InputBuffer = InputFeatures;

	// Create tensor shapes for binding
	UE::NNE::FTensorShape InputTensorShape = UE::NNE::FTensorShape::Make({ 1u, static_cast<uint32>(Config.InputSize) });

	// Create input tensor bindings
	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.Add(UE::NNE::FTensorBindingCPU{
		InputBuffer.GetData(),
		static_cast<uint64>(InputBuffer.Num() * sizeof(float))
	});

	// Create output tensor bindings
	// First output: action_probabilities (16 values)
	// Second output: state_value (1 value)
	TArray<float> ActionProbsBuffer;
	TArray<float> StateValueBuffer;
	ActionProbsBuffer.SetNum(Config.OutputSize);
	StateValueBuffer.SetNum(1);

	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	OutputBindings.Add(UE::NNE::FTensorBindingCPU{
		ActionProbsBuffer.GetData(),
		static_cast<uint64>(ActionProbsBuffer.Num() * sizeof(float))
	});
	OutputBindings.Add(UE::NNE::FTensorBindingCPU{
		StateValueBuffer.GetData(),
		static_cast<uint64>(StateValueBuffer.Num() * sizeof(float))
	});

	// Set input tensor shapes
	TArray<UE::NNE::FTensorShape> InputShapes;
	InputShapes.Add(InputTensorShape);

	UE::NNE::EResultStatus SetInputStatus = ModelInstance->SetInputTensorShapes(InputShapes);
	if (SetInputStatus != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Failed to set input tensor shapes"));
		TArray<float> ZeroAction;
		ZeroAction.Init(0.0f, Config.OutputSize);
		return ZeroAction;
	}

	// Run inference
	UE::NNE::EResultStatus RunStatus = ModelInstance->RunSync(InputBindings, OutputBindings);

	if (RunStatus != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Inference failed, returning zero action"));
		TArray<float> ZeroAction;
		ZeroAction.Init(0.0f, Config.OutputSize);
		return ZeroAction;
	}

	// Model outputs raw logits (8 atomic action dimensions)
	// NetworkOutputToAction() applies appropriate activations per dimension
	return ActionProbsBuffer;
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
// Atomic Action Space (v3.0)
// ========================================

FTacticalAction URLPolicyNetwork::GetAction(const FObservationElement& Observation, UObjective* CurrentObjective)
{
	return GetActionWithMask(Observation, CurrentObjective);
}

FTacticalAction URLPolicyNetwork::GetActionWithMask(const FObservationElement& Observation, UObjective* CurrentObjective)
{
	// v4.0: Implements action masking for dynamic target count
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Not initialized, returning default action"));
		FTacticalAction DefaultAction;
		return DefaultAction;
	}

	if (bUseONNXModel && ModelInstance.IsValid())
	{
		// Build enhanced input: 70 observation + 4 objective embedding = 74 features
		TArray<float> InputFeatures = Observation.ToFeatureVector();
		TArray<float> ObjectiveEmbed = GetObjectiveEmbedding(CurrentObjective);
		InputFeatures.Append(ObjectiveEmbed);

		// Forward pass (expects 14-dim output: 4 pos + 6 target + 3 fire + 1 value)
		TArray<float> NetworkOutput = ForwardPass(InputFeatures);

		// Convert to macro action with action masking based on visible enemies
		int32 VisibleEnemies = FMath::Min(Observation.VisibleEnemyCount, 5);  // Max 5 enemies tracked
		FTacticalAction Action = NetworkOutputToAction(NetworkOutput, VisibleEnemies);

		UE_LOG(LogTemp, Display, TEXT("✅ [ONNX MODEL] MacroAction: Pos=%d Target=%d Fire=%d (VisibleEnemies=%d)"),
			static_cast<int32>(Action.MacroAction.PositionChoice),
			Action.MacroAction.TargetIndex,
			static_cast<int32>(Action.MacroAction.FireMode),
			VisibleEnemies);

		return Action;
	}
	else
	{
		return FTacticalAction();  // Default action
	}
}

float URLPolicyNetwork::GetStateValue(const FObservationElement& Observation, UObjective* CurrentObjective)
{
	if (!bIsInitialized)
	{
		return 0.0f;
	}

	// Build input: 71 observation + 7 objective embedding = 78 features
	TArray<float> InputFeatures = Observation.ToFeatureVector();
	TArray<float> ObjectiveEmbed = GetObjectiveEmbedding(CurrentObjective);
	InputFeatures.Append(ObjectiveEmbed);

	// Use PPO critic network if loaded
	if (bUseONNXModel && ModelInstance.IsValid())
	{
		// Prepare input tensor
		InputBuffer = InputFeatures;
		UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make({ 1u, static_cast<uint32>(Config.InputSize) });

		// Create buffers
		TArray<float> ActionBuffer, ValueBuffer;
		ActionBuffer.SetNum(Config.OutputSize);
		ValueBuffer.SetNum(1);

		// Bind tensors
		TArray<UE::NNE::FTensorBindingCPU> InputBindings;
		InputBindings.Add({ InputBuffer.GetData(), static_cast<uint64>(InputBuffer.Num() * sizeof(float)) });

		TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
		OutputBindings.Add({ ActionBuffer.GetData(), static_cast<uint64>(ActionBuffer.Num() * sizeof(float)) });
		OutputBindings.Add({ ValueBuffer.GetData(), static_cast<uint64>(ValueBuffer.Num() * sizeof(float)) });

		// Run inference
		TArray<UE::NNE::FTensorShape> InputShapes = { InputShape };
		if (ModelInstance->SetInputTensorShapes(InputShapes) == UE::NNE::EResultStatus::Ok &&
			ModelInstance->RunSync(InputBindings, OutputBindings) == UE::NNE::EResultStatus::Ok)
		{
			return FMath::Clamp(ValueBuffer[0], -1.0f, 1.0f);
		}
	}

	// Fallback heuristic (if model not loaded)
	float Value = (Observation.AgentHealth - 50.0f) / 50.0f;
	Value -= Observation.VisibleEnemyCount * 0.2f;
	if (Observation.bHasCover) Value += 0.3f;

	return FMath::Clamp(Value, -1.0f, 1.0f);
}

TArray<float> URLPolicyNetwork::GetObjectivePriors(const FTeamObservation& TeamObs)
{
	// v4.0: Heuristic-based priors for 4 simplified objective types
	// Provides intelligent initial guidance for MCTS action selection

	TArray<float> Priors;
	Priors.Init(0.1f, 4);  // v4.0: Only 4 objective types

	// Objective type indices (matching v4.0 EObjectiveType enum)
	const int32 ASSAULT = 0;   // Attack enemies/objectives
	const int32 DEFEND = 1;    // Hold position/defend area
	const int32 SUPPORT = 2;   // Support allies
	const int32 RETREAT = 3;   // Fall back to safety

	// Context-aware prior assignment
	if (TeamObs.TotalVisibleEnemies > 0)
	{
		// COMBAT SITUATION
		if (TeamObs.bOutnumbered && TeamObs.AverageTeamHealth < 50.0f)
		{
			// Outnumbered and weak → retreat highly preferred
			Priors[RETREAT] = 0.5f;
			Priors[DEFEND] = 0.25f;
			Priors[SUPPORT] = 0.15f;
			Priors[ASSAULT] = 0.1f;
		}
		else if (TeamObs.bFlanked)
		{
			// Being flanked → defensive posture + support
			Priors[DEFEND] = 0.4f;
			Priors[SUPPORT] = 0.3f;
			Priors[RETREAT] = 0.2f;
			Priors[ASSAULT] = 0.1f;
		}
		else if (TeamObs.AverageTeamHealth > 70.0f && !TeamObs.bOutnumbered)
		{
			// Strong position → aggressive
			Priors[ASSAULT] = 0.5f;
			Priors[SUPPORT] = 0.25f;
			Priors[DEFEND] = 0.15f;
			Priors[RETREAT] = 0.1f;
		}
		else
		{
			// Balanced combat → mixed tactics
			Priors[ASSAULT] = 0.35f;
			Priors[DEFEND] = 0.25f;
			Priors[SUPPORT] = 0.25f;
			Priors[RETREAT] = 0.15f;
		}
	}
	else
	{
		// NO ENEMIES VISIBLE
		if (TeamObs.AverageTeamHealth < 40.0f)
		{
			// Low health, no enemies → recover and defend
			Priors[DEFEND] = 0.5f;
			Priors[SUPPORT] = 0.3f;
			Priors[RETREAT] = 0.15f;
			Priors[ASSAULT] = 0.05f;
		}
		else if (TeamObs.DistanceToObjective > 1000.0f)
		{
			// Far from objective → advance
			Priors[ASSAULT] = 0.5f;
			Priors[DEFEND] = 0.25f;
			Priors[SUPPORT] = 0.15f;
			Priors[RETREAT] = 0.1f;
		}
		else
		{
			// Stable situation → objective-focused
			Priors[ASSAULT] = 0.4f;
			Priors[DEFEND] = 0.35f;
			Priors[SUPPORT] = 0.15f;
			Priors[RETREAT] = 0.1f;
		}
	}

	// Normalize to sum to 1.0
	float Sum = 0.0f;
	for (float Prior : Priors)
	{
		Sum += Prior;
	}
	if (Sum > 0.0f)
	{
		for (int32 i = 0; i < Priors.Num(); ++i)
		{
			Priors[i] /= Sum;
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("RL Policy Priors (v4.0): Assault=%.2f, Defend=%.2f, Support=%.2f, Retreat=%.2f"),
		Priors[ASSAULT], Priors[DEFEND], Priors[SUPPORT], Priors[RETREAT]);

	return Priors;
}

FTacticalAction URLPolicyNetwork::NetworkOutputToAction(const TArray<float>& NetworkOutput, int32 VisibleEnemies)
{
	FTacticalAction Action;

	// v4.0 Simplified: [4 position + 6 target + 3 fire + 1 value] = 14 outputs
	// Position: Hold, ForwardCover, Retreat, Advance (4)
	// Target: None + Enemy_0...Enemy_N (dynamic, max 6 for now)
	// Fire: HoldFire, Fire, Suppress (3)
	if (NetworkOutput.Num() >= 13)
	{
		// Extract logits for each action dimension
		TArray<float> PositionLogits;
		PositionLogits.Append(&NetworkOutput[0], 4);

		TArray<float> TargetLogits;
		TargetLogits.Append(&NetworkOutput[4], 6);

		// ACTION MASKING: Mask invalid target indices based on visible enemy count
		// Target indices: [0=None, 1=Enemy_0, 2=Enemy_1, ..., 5=Enemy_4]
		// If only 2 enemies visible, mask indices 3+ (Enemy_2, Enemy_3, Enemy_4)
		for (int32 i = VisibleEnemies + 1; i < TargetLogits.Num(); ++i)
		{
			TargetLogits[i] = -1e9f;  // Effectively zero probability after softmax
		}

		TArray<float> FireModeLogits;
		FireModeLogits.Append(&NetworkOutput[10], 3);

		// Sample from each discrete distribution
		int32 PositionIdx = SampleFromLogits(PositionLogits);
		int32 TargetIdx = SampleFromLogits(TargetLogits);
		int32 FireModeIdx = SampleFromLogits(FireModeLogits);

		// Convert indices to enum values
		Action.MacroAction.PositionChoice = static_cast<ETacticalPosition>(FMath::Clamp(PositionIdx, 0, 3));
		Action.MacroAction.TargetIndex = TargetIdx - 1;  // 0 = no target (-1), 1+ = enemy indices (0+)
		Action.MacroAction.FireMode = static_cast<EFireMode>(FMath::Clamp(FireModeIdx, 0, 2));

		// Note: Value estimate at NetworkOutput[13] handled separately by GetStateValue()
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Invalid network output size %d, expected 13+"), NetworkOutput.Num());
	}

	return Action;
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

TArray<float> URLPolicyNetwork::GetObjectiveEmbedding(UObjective* CurrentObjective)
{
	// 4-element one-hot encoding for objective type (v4.0 simplified)
	TArray<float> Embedding;
	Embedding.Init(0.0f, 4);

	if (CurrentObjective)
	{
		// Get objective type and encode as one-hot
		EObjectiveType ObjType = CurrentObjective->Type;

		switch (ObjType)
		{
			case EObjectiveType::Assault:
				Embedding[0] = 1.0f;
				break;
			case EObjectiveType::Defend:
				Embedding[1] = 1.0f;
				break;
			case EObjectiveType::Support:
				Embedding[2] = 1.0f;
				break;
			case EObjectiveType::Retreat:
				Embedding[3] = 1.0f;
				break;
			default:
				// None or unknown - leave as zeros
				break;
		}
	}
	// If null objective, return all zeros (no objective)

	return Embedding;
}
