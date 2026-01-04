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
 * Neural Network-based RL Policy for Tactical Action Selection (v5.0 Multi-Head)
 *
 * Architecture:
 *   Input Layer:  64 features (streamlined observation)
 *   Shared Trunk: 128 → 128 → 64 (ReLU) - learns common features
 *   Strategy-Gated Multi-Head Network:
 *   ├─ Assault Head:  Position(4) + Target(6) + Fire(3) = 13 logits
 *   ├─ Defend Head:   Position(4) + Target(6) + Fire(3) = 13 logits
 *   ├─ Support Head:  Position(4) + Target(6) + Fire(3) = 13 logits
 *   ├─ Retreat Head:  Position(4) + Target(6) + Fire(3) = 13 logits
 *   └─ Critic Head:   1 value (for MCTS value estimation)
 *
 * v5.0 Changes:
 *   - Multi-head architecture: 4 strategy-specific heads (Assault/Defend/Support/Retreat)
 *   - Streamlined observations: 64 features (down from 74)
 *   - Individual strategy assignment: MCTS assigns different strategies per agent
 *   - Strategy-specific rewards: Each head trained with specialized reward functions
 *
 * Usage:
 *   1. Load trained policy from ONNX: LoadPolicy("path/to/assault_policy.onnx")
 *   2. Query for action: GetAction(Observation, Strategy)
 *   3. Strategy selection handled by MCTS (TeamLeaderComponent)
 *   4. Training handled by real-time RLlib with multi-head PPO
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
	bool Initialize(const FMultiHeadPolicyConfig& InConfig);

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
	// Macro Action Inference (v5.0 Multi-Head)
	// ========================================

	/**
	 * Get macro action with strategy context (v5.0 Multi-Head)
	 * High-level tactical decisions: WHERE to go, WHO to shoot, HOW to engage
	 * Strategy-gated head selection for specialized behavior
	 * @param Observation - Current 64-feature observation
	 * @param CurrentStrategy - Current strategy assigned by MCTS (Assault/Defend/Support/Retreat)
	 * @return Macro action (position, target, fire mode) - strategy-specific
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v5")
	FTacticalAction GetAction(const FObservationElement& Observation, EStrategyType CurrentStrategy);

	/**
	 * Get macro action with strategy context and action masking (v5.0 Multi-Head)
	 * Masks invalid target indices based on visible enemy count
	 * Strategy-gated head selection for specialized behavior
	 * @param Observation - Current 64-feature observation (includes VisibleEnemyCount)
	 * @param CurrentStrategy - Current strategy assigned by MCTS
	 * @return Macro action with masked target selection (strategy-specific)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v5")
	FTacticalAction GetActionWithMask(const FObservationElement& Observation, EStrategyType CurrentStrategy);

	/**
	 * Get state value estimate for MCTS (Shared Critic - v5.0)
	 * Uses the shared PPO critic network (value function) trained alongside all strategy heads
	 * @param Observation - Current 64-feature observation
	 * @return State value estimate (higher = better expected return)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v5")
	float GetStateValue(const FObservationElement& Observation);

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
	 * Forward pass through the neural network (v5.0)
	 * @param InputFeatures - 64-element input vector (streamlined observation)
	 * @return Multi-head output: [4 heads × 13 logits each] or selected head output
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
	 * Generate macro action from network output (v5.0)
	 * @param NetworkOutput - Multi-discrete logits: [4 position + 6 target + 3 fire] = 13 per head
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
	 * Select strategy-specific head output from multi-head network (v5.0)
	 * @param AllHeadsOutput - Output from all 4 heads (52 logits total: 4×13)
	 * @param Strategy - Current strategy type
	 * @return Selected head output (13 logits: 4 pos + 6 target + 3 fire)
	 */
	TArray<float> SelectStrategyHead(const TArray<float>& AllHeadsOutput, EStrategyType Strategy);

public:
	// ========================================
	// Configuration
	// ========================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL|Config")
	FMultiHeadPolicyConfig Config;

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
