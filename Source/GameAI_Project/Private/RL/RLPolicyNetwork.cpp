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
	// v4.0: Mask parameter ignored for discrete macro actions
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Not initialized, returning default action"));
		FTacticalAction DefaultAction;
		return DefaultAction;
	}

	if (bUseONNXModel && ModelInstance.IsValid())
	{
		// Build enhanced input: 71 observation + 7 objective embedding = 78 features
		TArray<float> InputFeatures = Observation.ToFeatureVector();
		TArray<float> ObjectiveEmbed = GetObjectiveEmbedding(CurrentObjective);
		InputFeatures.Append(ObjectiveEmbed);

		// Forward pass (expects 19-dim output: 6 pos + 6 target + 3 fire + 3 stance + 1 value)
		TArray<float> NetworkOutput = ForwardPass(InputFeatures);

		// Convert to macro action
		FTacticalAction Action = NetworkOutputToAction(NetworkOutput);

		UE_LOG(LogTemp, Display, TEXT("✅ [ONNX MODEL] MacroAction: Pos=%d Target=%d Fire=%d Stance=%d"),
			static_cast<int32>(Action.MacroAction.PositionChoice),
			Action.MacroAction.TargetIndex,
			static_cast<int32>(Action.MacroAction.FireMode),
			static_cast<int32>(Action.MacroAction.Stance));

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
	// v3.0 Sprint 4: Heuristic-based priors to guide MCTS
	// These priors are calculated based on team state to provide intelligent initial guidance
	// Future: Replace with learned priors from dual-head policy network

	TArray<float> Priors;
	Priors.Init(0.1f, 7);  // Start with small baseline probability

	// Objective type indices (matching EObjectiveType enum)
	const int32 ELIMINATE = 0;
	const int32 CAPTURE_OBJ = 1;
	const int32 DEFEND_OBJ = 2;
	const int32 SUPPORT_ALLY = 3;
	const int32 FORMATION_MOVE = 4;
	const int32 RETREAT = 5;
	const int32 RESCUE_ALLY = 6;

	// Context-aware prior assignment
	if (TeamObs.TotalVisibleEnemies > 0)
	{
		// COMBAT SITUATION
		if (TeamObs.bOutnumbered && TeamObs.AverageTeamHealth < 50.0f)
		{
			// Outnumbered and weak → retreat highly preferred
			Priors[RETREAT] = 0.4f;
			Priors[DEFEND_OBJ] = 0.2f;
			Priors[SUPPORT_ALLY] = 0.15f;
			Priors[ELIMINATE] = 0.05f;
		}
		else if (TeamObs.bFlanked)
		{
			// Being flanked → defensive posture + support
			Priors[DEFEND_OBJ] = 0.3f;
			Priors[SUPPORT_ALLY] = 0.25f;
			Priors[FORMATION_MOVE] = 0.2f;  // Regroup
			Priors[ELIMINATE] = 0.1f;
		}
		else if (TeamObs.AverageTeamHealth > 70.0f && !TeamObs.bOutnumbered)
		{
			// Strong position → aggressive
			Priors[ELIMINATE] = 0.35f;
			Priors[CAPTURE_OBJ] = 0.25f;
			Priors[SUPPORT_ALLY] = 0.15f;
			Priors[DEFEND_OBJ] = 0.1f;
		}
		else
		{
			// Balanced combat → mixed tactics
			Priors[ELIMINATE] = 0.25f;
			Priors[DEFEND_OBJ] = 0.2f;
			Priors[SUPPORT_ALLY] = 0.2f;
			Priors[CAPTURE_OBJ] = 0.15f;
		}
	}
	else
	{
		// NO ENEMIES VISIBLE
		if (TeamObs.AverageTeamHealth < 40.0f)
		{
			// Low health, no enemies → recover and defend
			Priors[DEFEND_OBJ] = 0.35f;
			Priors[RESCUE_ALLY] = 0.25f;
			Priors[FORMATION_MOVE] = 0.2f;
		}
		else if (TeamObs.DistanceToObjective > 1000.0f)
		{
			// Far from objective → advance and capture
			Priors[FORMATION_MOVE] = 0.35f;
			Priors[CAPTURE_OBJ] = 0.3f;
			Priors[DEFEND_OBJ] = 0.15f;
		}
		else if (TeamObs.FormationCoherence < 0.5f)
		{
			// Formation broken → regroup
			Priors[FORMATION_MOVE] = 0.4f;
			Priors[DEFEND_OBJ] = 0.2f;
			Priors[CAPTURE_OBJ] = 0.2f;
		}
		else
		{
			// Stable situation → objective-focused
			Priors[CAPTURE_OBJ] = 0.35f;
			Priors[DEFEND_OBJ] = 0.25f;
			Priors[FORMATION_MOVE] = 0.2f;
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

	UE_LOG(LogTemp, Verbose, TEXT("RL Policy Priors: Eliminate=%.2f, Capture=%.2f, Defend=%.2f, Support=%.2f, Move=%.2f, Retreat=%.2f, Rescue=%.2f"),
		Priors[ELIMINATE], Priors[CAPTURE_OBJ], Priors[DEFEND_OBJ], Priors[SUPPORT_ALLY],
		Priors[FORMATION_MOVE], Priors[RETREAT], Priors[RESCUE_ALLY]);

	return Priors;
}

FTacticalAction URLPolicyNetwork::NetworkOutputToAction(const TArray<float>& NetworkOutput)
{
	FTacticalAction Action;

	// v4.0 Network output format: [6 position logits + 6 target logits + 3 fire logits + 3 stance logits + 1 value] = 19
	// Sample discrete actions from each head
	if (NetworkOutput.Num() >= 18)
	{
		// Extract logits for each action dimension
		TArray<float> PositionLogits;
		PositionLogits.Append(&NetworkOutput[0], 6);

		TArray<float> TargetLogits;
		TargetLogits.Append(&NetworkOutput[6], 6);

		TArray<float> FireModeLogits;
		FireModeLogits.Append(&NetworkOutput[12], 3);

		TArray<float> StanceLogits;
		StanceLogits.Append(&NetworkOutput[15], 3);

		// Sample from each discrete distribution
		int32 PositionIdx = SampleFromLogits(PositionLogits);
		int32 TargetIdx = SampleFromLogits(TargetLogits);
		int32 FireModeIdx = SampleFromLogits(FireModeLogits);
		int32 StanceIdx = SampleFromLogits(StanceLogits);

		// Convert indices to enum values
		Action.MacroAction.PositionChoice = static_cast<ETacticalPosition>(FMath::Clamp(PositionIdx, 0, 5));
		Action.MacroAction.TargetIndex = TargetIdx - 1;  // 0 = no target (-1), 1+ = enemy indices (0+)
		Action.MacroAction.FireMode = static_cast<EFireMode>(FMath::Clamp(FireModeIdx, 0, 2));
		Action.MacroAction.Stance = static_cast<EStance>(FMath::Clamp(StanceIdx, 0, 2));

		// Note: Value estimate at NetworkOutput[18] handled separately by GetStateValue()
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("URLPolicyNetwork: Invalid network output size %d, expected 18+"), NetworkOutput.Num());
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
	// 7-element one-hot encoding for objective type
	TArray<float> Embedding;
	Embedding.Init(0.0f, 7);

	if (CurrentObjective)
	{
		// Get objective type and encode as one-hot
		EObjectiveType ObjType = CurrentObjective->Type;

		switch (ObjType)
		{
			case EObjectiveType::Eliminate:
				Embedding[0] = 1.0f;
				break;
			case EObjectiveType::CaptureObjective:
				Embedding[1] = 1.0f;
				break;
			case EObjectiveType::DefendObjective:
				Embedding[2] = 1.0f;
				break;
			case EObjectiveType::SupportAlly:
				Embedding[3] = 1.0f;
				break;
			case EObjectiveType::FormationMove:
				Embedding[4] = 1.0f;
				break;
			case EObjectiveType::Retreat:
				Embedding[5] = 1.0f;
				break;
			case EObjectiveType::RescueAlly:
				Embedding[6] = 1.0f;
				break;
			default:
				// None or unknown - leave as zeros
				break;
		}
	}
	// If null objective, return all zeros (no objective)

	return Embedding;
}
