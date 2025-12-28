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
 * Neural Network-based RL Policy for Tactical Action Selection (v4.0 Simplified)
 *
 * Architecture:
 *   Input Layer:  74 features (70 observation + 4 objective embedding)
 *   Shared Trunk: 128 → 128 → 64 (ReLU)
 *   ├─ Position Head: 4 logits (Hold, ForwardCover, Retreat, Advance)
 *   ├─ Target Head: (N+1) logits (None, Enemy_0...Enemy_N) [dynamic, max 6]
 *   ├─ Fire Mode Head: 3 logits (HoldFire, Fire, Suppress)
 *   └─ Critic Head: 1 value (for PPO advantage estimation only)
 *
 * Simplified Changes (v4.0):
 *   - Removed Stance action (no crouch/prone) - agents always standing
 *   - Reduced objective types: 7→4 (Assault/Defend/Support/Retreat)
 *   - Focus on core tactical loop: WHERE to go, WHO to shoot, WHEN to fire
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
	 * Get macro action with objective context and action masking (v4.0)
	 * Masks invalid target indices based on visible enemy count
	 * @param Observation - Current observation (includes VisibleEnemyCount)
	 * @param CurrentObjective - Current objective (can be nullptr)
	 * @return Macro action with masked target selection
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v4")
	FTacticalAction GetActionWithMask(const FObservationElement& Observation, class UObjective* CurrentObjective);

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
	 * @return Array of 4 prior probabilities (Assault, Defend, Support, Retreat)
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
	 * Generate macro action from network output (v4.0)
	 * @param NetworkOutput - Multi-discrete logits: [4 position + 6 target + 3 fire + 1 value] = 14
	 * @param VisibleEnemies - Number of visible enemies for action masking (default 5)
	 * @return Macro action struct
	 */
	FTacticalAction NetworkOutputToAction(const TArray<float>& NetworkOutput, int32 VisibleEnemies = 5);

	/**
	 * Sample discrete action from logits
	 * @param Logits - Unnormalized log probabilities
	 * @return Sampled index
	 */
	int32 SampleFromLogits(const TArray<float>& Logits);

	/**
	 * Build objective embedding for network input (v4.0 simplified)
	 * @param CurrentObjective - Current objective (can be nullptr)
	 * @return 4-element objective embedding (Assault/Defend/Support/Retreat)
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
