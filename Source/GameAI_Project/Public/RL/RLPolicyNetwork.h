// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "RL/RLTypes.h"
#include "Observation/ObservationElement.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "RLPolicyNetwork.generated.h"

class UNNEModelData;
class INNERuntime;
class INNERuntimeGPU;

/**
 * Neural Network-based RL Policy for Tactical Action Selection (v4.0 Macro Actions)
 *
 * Architecture:
 *   Input Layer:  78 features (71 observation + 7 objective embedding)
 *   Shared Trunk: 128 → 128 → 64 (ReLU)
 *   ├─ Position Head: 6 logits (Hold, Forward, Retreat, FlankL, FlankR, Advance)
 *   ├─ Target Head: 6 logits (None, Enemy0-4)
 *   ├─ Fire Mode Head: 3 logits (HoldFire, Fire, Suppress)
 *   ├─ Stance Head: 3 logits (Stand, Crouch, Prone)
 *   └─ Critic Head: 1 value (state value estimate for MCTS)
 *
 * Usage:
 *   1. Load trained policy from ONNX: LoadPolicy("path/to/model.onnx")
 *   2. Query for action: GetAction(Observation, Objective)
 *   3. Training handled by real-time RLlib (no C++ experience collection needed)
 */
UCLASS(BlueprintType, Blueprintable)
class GAMEAI_PROJECT_API URLPolicyNetwork : public UObject
{
	GENERATED_BODY()

public:
	URLPolicyNetwork();


	// ========================================
	// Initialization
	// ========================================

	/**
	 * Initialize the policy network
	 * @param InConfig - Policy configuration
	 * @return True if initialization succeeded
	 */
	UFUNCTION(BlueprintCallable, Category = "RL")
	bool Initialize(const FRLPolicyConfig& InConfig);

	/**
	 * Load a trained policy from ONNX file
	 * @param ModelPath - Path to .onnx file
	 * @return True if model loaded successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "RL")
	bool LoadPolicy(const FString& ModelPath);

	/**
	 * Unload the current policy
	 */
	UFUNCTION(BlueprintCallable, Category = "RL")
	void UnloadPolicy();

	// ========================================
	// Macro Action Inference (v4.0)
	// ========================================

	/**
	 * Get macro action with objective context (v4.0)
	 * High-level tactical decisions: WHERE to go, WHO to shoot, HOW to engage
	 * @param Observation - Current 71-feature observation
	 * @param CurrentObjective - Current objective from team leader (can be nullptr)
	 * @return Macro action (position, target, fire mode, stance)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v4")
	FTacticalAction GetAction(const FObservationElement& Observation, class UObjective* CurrentObjective);

	/**
	 * Get macro action with objective context (v4.0)
	 * Note: Action masking not implemented for v4.0 discrete actions
	 * @param Observation - Current observation
	 * @param CurrentObjective - Current objective (can be nullptr)
	 * @param Mask - Action space constraints (ignored in v4.0)
	 * @return Macro action
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v4")
	FTacticalAction GetActionWithMask(const FObservationElement& Observation, class UObjective* CurrentObjective, const FActionSpaceMask& Mask);

	/**
	 * Get state value estimate for MCTS (PPO Critic - v4.0)
	 * Uses the PPO critic network (value function) trained alongside the policy
	 * @param Observation - Current 71-feature observation
	 * @param CurrentObjective - Current objective (for embedding)
	 * @return State value estimate (higher = better expected return)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v4")
	float GetStateValue(const FObservationElement& Observation, class UObjective* CurrentObjective);

	/**
	 * Get action priors for MCTS initialization (v4.0)
	 * Returns prior probabilities for objective types to guide MCTS tree search
	 * @param TeamObs - Team-level observation
	 * @return Array of 7 prior probabilities (one per objective type)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v4")
	TArray<float> GetObjectivePriors(const struct FTeamObservation& TeamObs);


	// ========================================
	// Statistics
	// ========================================

	/**
	 * Get training statistics
	 */
	UFUNCTION(BlueprintCallable, Category = "RL")
	FRLTrainingStats GetTrainingStats() const { return TrainingStats; }

	/**
	 * Reset training statistics
	 */
	UFUNCTION(BlueprintCallable, Category = "RL")
	void ResetStatistics();

	/**
	 * Update epsilon value (for exploration decay)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL")
	void UpdateEpsilon();

	/**
	 * Get current epsilon value
	 */
	UFUNCTION(BlueprintCallable, Category = "RL")
	float GetEpsilon() const { return Config.Epsilon; }

	// ========================================
	// Utility
	// ========================================

	/**
	 * Check if policy is loaded and ready
	 */
	UFUNCTION(BlueprintCallable, Category = "RL")
	bool IsReady() const { return bIsInitialized; }

private:
	// ========================================
	// Neural Network Inference (ONNX)
	// ========================================

	/**
	 * Forward pass through the neural network
	 * @param InputFeatures - 71-element input vector
	 * @return 16-element output vector (action probabilities)
	 */
	TArray<float> ForwardPass(const TArray<float>& InputFeatures);

	/**
	 * Softmax activation function
	 */
	static TArray<float> Softmax(const TArray<float>& Logits);

	// ========================================
	// Macro Action Helpers (v4.0)
	// ========================================

	/**
	 * Generate macro action from network output
	 * @param NetworkOutput - Multi-discrete logits: [6 position + 6 target + 3 fire + 3 stance + 1 value] = 19
	 * @return Macro action struct
	 */
	FTacticalAction NetworkOutputToAction(const TArray<float>& NetworkOutput);

	/**
	 * Sample discrete action from logits
	 * @param Logits - Unnormalized log probabilities
	 * @return Sampled index
	 */
	int32 SampleFromLogits(const TArray<float>& Logits);

	/**
	 * Build objective embedding for network input
	 * @param CurrentObjective - Current objective (can be nullptr)
	 * @return 7-element objective embedding
	 */
	TArray<float> GetObjectiveEmbedding(class UObjective* CurrentObjective);

public:
	// ========================================
	// Configuration
	// ========================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL|Config")
	FRLPolicyConfig Config;

	// Enable epsilon-greedy exploration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL|Config")
	bool bEnableExploration;

	// Use ONNX model for inference (if false, uses rule-based fallback)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL|Config")
	bool bUseONNXModel;


private:
	// ========================================
	// Internal State
	// ========================================

	// Is the policy initialized?
	bool bIsInitialized;

	// Training statistics (for monitoring only)
	FRLTrainingStats TrainingStats;

	// Current episode reward accumulator (for monitoring)
	float CurrentEpisodeReward;

	// Current episode step count (for monitoring)
	int32 CurrentEpisodeSteps;

	// ========================================
	// NNE (Neural Network Engine) State
	// ========================================

	// NNE model data (loaded from .onnx file)
	UPROPERTY()
	TObjectPtr<UNNEModelData> ModelData;

	// NNE model instance for inference
	TSharedPtr<UE::NNE::IModelInstanceCPU> ModelInstance;

	// Input/output tensor bindings
	TArray<float> InputBuffer;
	TArray<float> OutputBuffer;
};
