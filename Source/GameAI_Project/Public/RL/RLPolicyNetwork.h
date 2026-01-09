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
 * Neural Network-based RL Policy for Strategy Selection (v6.0 Single-Head)
 *
 * v7.0 Architecture (Single-Head):
 *   Input Layer:  68 features (64 base + 4 mission context)
 *   Shared Trunk: 128 → 128 → 64 (ReLU)
 *   ├─ Policy Head:  4 strategy logits (Assault, Defend, Support, Retreat)
 *   └─ Critic Head:  1 value estimate (for MCTS leaf evaluation)
 *
 * v7.0 Changes:
 *   - Single-head architecture: Simplified action space (4 strategies only)
 *   - Mission-aware: Observation includes mission context (4 features)
 *   - Batched inference: Process multiple agents in single forward pass
 *   - MCTS integration: Assigns missions, RL selects strategies
 *   - Rules-based execution: Strategy → Position/Target mapping is deterministic
 *
 * v5.0 → v7.0 Migration:
 *   - MCTS: Strategy assignment → Mission assignment
 *   - RL: Multi-head (44 actions) → Single-head (4 strategies)
 *   - Execution: RL micro-actions → Rules-based execution
 *
 * Usage:
 *   1. Load trained policy: LoadPolicy("Models/cortex_policy_v6.onnx")
 *   2. Get strategy: GetStrategy(Observation, MissionContext)
 *   3. Batched inference: GetStrategiesBatched(Observations, MissionContexts)
 *   4. MCTS value query: GetStateValue(Observation, MissionContext)
 */
UCLASS(BlueprintType, Blueprintable)
class GAMEAI_PROJECT_API URLPolicyNetwork : public UObject
{
	GENERATED_BODY()

public:
	URLPolicyNetwork();


	// ========================================
	// v6.0 API: Strategy Selection (Single Agent)
	// ========================================

	/**
	 * Get strategy for current observation + Mission (v6.0)
	 * @param Observation - Agent's 64-feature observation
	 * @param MissionContext - Assigned Mission context (4 features)
	 * @return Selected strategy (Assault/Defend/Support/Retreat)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v6")
	EStrategyType GetStrategy(const FObservationElement& Observation, const FMissionContext& MissionContext);

	/**
	 * Get state value estimate (used by MCTS for leaf evaluation) (v6.0)
	 * @param Observation - Agent's 64-feature observation
	 * @param MissionContext - Assigned Mission context (4 features)
	 * @return Value estimate [-1, 1]
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v6")
	float GetStateValue(const FObservationElement& Observation, const FMissionContext& MissionContext);

	// ========================================
	// v6.0 API: Batched Inference (Performance Critical)
	// ========================================

	/**
	 * Get strategies for multiple agents in single network call (v6.0)
	 * PERFORMANCE: 2.6× faster than sequential calls (8ms → 3ms for 4 agents)
	 * @param Observations - Array of agent observations
	 * @param MissionContexts - Array of Mission contexts (same size as Observations)
	 * @return Array of strategies (same size as input)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v6")
	TArray<EStrategyType> GetStrategiesBatched(
		const TArray<FObservationElement>& Observations,
		const TArray<FMissionContext>& MissionContexts
	);

	/**
	 * Get state values for multiple agents in single network call (v6.0)
	 * Used by MCTS for evaluating multiple agent-Mission assignments
	 * @param Observations - Array of agent observations
	 * @param MissionContexts - Array of Mission contexts
	 * @return Array of value estimates [-1, 1]
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v6")
	TArray<float> GetStateValuesBatched(
		const TArray<FObservationElement>& Observations,
		const TArray<FMissionContext>& MissionContexts
	);


private:
	// ========================================
	// v6.0: Network Input/Output Helpers
	// ========================================

	/**
	 * Build 68-feature input from observation + Mission context (v6.0)
	 */
	TArray<float> BuildNetworkInput(const FObservationElement& Observation, const FMissionContext& MissionContext) const;

	/**
	 * Network output structure (v6.0)
	 */
	struct FNetworkOutput {
		TArray<float> PolicyLogits;  // [4] - Strategy logits
		float Value;                  // State value estimate
	};

	/**
	 * Forward pass through single-head network (v6.0)
	 * @param InputFeatures - 68-element input vector
	 * @return Policy logits (4) and value (1)
	 */
	FNetworkOutput ForwardPassV6(const TArray<float>& InputFeatures);

	/**
	 * Sample strategy from logits (v6.0)
	 */
	EStrategyType SampleStrategy(const TArray<float>& Logits) const;


	/**
	 * Softmax activation function
	 */
	static TArray<float> Softmax(const TArray<float>& Logits);

public:
	// ========================================
	// Configuration
	// ========================================

	// Use ONNX model for inference (if false, uses rule-based fallback)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL|Config")
	bool bUseONNXModel;


private:
	// ========================================
	// NNE (Neural Network Engine) State
	// ========================================

	// NNE model instance for inference
	TSharedPtr<UE::NNE::IModelInstanceCPU> ModelInstance;

	// Input/output tensor bindings
	TArray<float> InputBuffer;
	TArray<float> OutputBuffer;
};
