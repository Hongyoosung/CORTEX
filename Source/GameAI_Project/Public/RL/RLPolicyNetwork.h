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
 * Neural Network-based RL Policy for Tactical Action Selection
 *
 * Architecture:
 *   Input Layer:  71 features (from FObservationElement)
 *   Hidden Layer 1: 128 neurons (ReLU)
 *   Hidden Layer 2: 128 neurons (ReLU)
 *   Hidden Layer 3: 64 neurons (ReLU)
 *   Output Layer: 16 actions (Softmax)
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
	// Atomic Action Inference (v3.0)
	// ========================================

	/**
	 * Get atomic action with objective context (v3.0)
	 * Replaces discrete action selection with continuous action space
	 * @param Observation - Current 71-feature observation
	 * @param CurrentObjective - Current objective from team leader (can be nullptr)
	 * @return 8-dimensional atomic action (move, aim, discrete actions)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v3")
	FTacticalAction GetAction(const FObservationElement& Observation, class UObjective* CurrentObjective);

	/**
	 * Get atomic action with objective context and spatial mask (v3.0)
	 * Applies environmental constraints to prevent invalid actions
	 * @param Observation - Current observation
	 * @param CurrentObjective - Current objective (can be nullptr)
	 * @param Mask - Action space constraints from environment
	 * @return 8-dimensional atomic action (constrained by mask)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v3")
	FTacticalAction GetActionWithMask(const FObservationElement& Observation, class UObjective* CurrentObjective, const FActionSpaceMask& Mask);

	/**
	 * Get state value estimate for MCTS (PPO Critic - Real-Time Training)
	 * Uses the PPO critic network (value function) trained alongside the policy
	 * @param Observation - Current 71-feature observation
	 * @param CurrentObjective - Current objective (for embedding)
	 * @return State value estimate (higher = better expected return)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v3")
	float GetStateValue(const FObservationElement& Observation, class UObjective* CurrentObjective);

	/**
	 * Get action priors for MCTS initialization (v3.0)
	 * Returns prior probabilities for objective types to guide MCTS tree search
	 * @param TeamObs - Team-level observation
	 * @return Array of 7 prior probabilities (one per objective type)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v3")
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
	// Atomic Action Helpers (v3.0)
	// ========================================

	/**
	 * Generate atomic action from network output
	 * @param NetworkOutput - 8-element output from neural network
	 * @return Atomic action struct
	 */
	FTacticalAction NetworkOutputToAction(const TArray<float>& NetworkOutput);

	/**
	 * Apply spatial mask to constrain action
	 * @param Action - Raw action from network
	 * @param Mask - Spatial constraints
	 * @return Constrained action
	 */
	FTacticalAction ApplyMask(const FTacticalAction& Action, const FActionSpaceMask& Mask);

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
