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

/** //============================================================
 * Neural Network-based RL Policy for Tactical Parameter Control (v8.0 Multi-Head)
 *
 * v8.0 Architecture (Multi-Head):
 *   Input Layer:  56 features (52 base + 4 strategy one-hot from MCTS)
 *   Shared Trunk: 128 → 128 → 64 (ReLU)
 *   ├─ Assault Head:  [4] tactical parameters (Aggression, Cover, Spread, Risk)
 *   ├─ Defend Head:   [4] tactical parameters
 *   ├─ Support Head:  [4] tactical parameters
 *   ├─ Retreat Head:  [4] tactical parameters
 *   ├─ Combat Head:   [2] target priority logits (Closest, LowestHP)
 *   └─ Critic Head:   [1] state value estimate
 *
 * Observation Space (52 base features - v9.0):
 *   - Agent State (4): pos(3), health(1)
 *   - Combat (1): enemy_dist(1)
 *   - Perception (16): raycasts(16)
 *   - Support Context (5): ally_needs(1), ally_health(1), ally_dist(1), ally_dir(2)
 *   - Enemy Info (16): count(1), nearby(15)
 *   - Tactical (4): has_cover(1), cover_dist(1), cover_dir(2)
 *   - Objective Context (6): friendly_obj_dist(1), friendly_obj_dir(2), hostile_obj_dist(1), hostile_obj_dir(2)
 *
 * Usage:
 *   1. Get macro action: GetMacroAction(Observation, AssignedStrategy)
 *   2. Batched inference: GetMacroActionsBatched(Observations, Strategies)
 *   3. MCTS value query: GetStateValueV8(Observation, Strategy)
 */ //============================================================
UCLASS(BlueprintType, Blueprintable)
class GAMEAI_PROJECT_API URLPolicyNetwork : public UObject
{
	GENERATED_BODY()

public:
	URLPolicyNetwork();


	// ========================================
	// Tactical Parameters + Combat Control (Multi-Head)
	// ========================================

	/**
	 * Get tactical parameters and combat parameters for current observation + assigned strategy (v8.0)
	 * @param Observation - Agent's 52-feature observation (v9.0)
	 * @param AssignedStrategy - Strategy assigned by MCTS (determines which head to use)
	 * @return Macro action with tactical and combat parameters
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v8")
	FMacroAction GetMacroAction(const FObservationElement& Observation, EStrategyType AssignedStrategy);

	/**
	 * Get state value estimate (used by PPO training) (v8.0)
	 * @param Observation - Agent's 52-feature observation (v9.0)
	 * @param AssignedStrategy - Strategy assigned by MCTS
	 * @return Value estimate [-1, 1]
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v8")
	float GetStateValueV8(const FObservationElement& Observation, EStrategyType AssignedStrategy);

	// ========================================
	// API: Batched Inference (Performance Critical)
	// ========================================

	/**
	 * Get macro actions for multiple agents in single network call (v8.0)
	 * PERFORMANCE: Batched inference is 2-3× faster than sequential calls
	 * @param Observations - Array of agent observations
	 * @param AssignedStrategies - Array of strategies assigned by MCTS (same size as Observations)
	 * @return Array of macro actions (same size as input)
	 */
	UFUNCTION(BlueprintCallable, Category = "RL|v8")
	TArray<FMacroAction> GetMacroActionsBatched(
		const TArray<FObservationElement>& Observations,
		const TArray<EStrategyType>& AssignedStrategies
	);


private:
	// ========================================
	// Network Input/Output Helpers
	// ========================================

	/**
	 * Build 56-feature input from observation + assigned strategy (v9.0)
	 * @param Observation - Agent's 52-feature observation
	 * @param AssignedStrategy - Strategy assigned by MCTS (one-hot encoded)
	 * @return 56-element vector (52 base + 4 strategy one-hot)
	 */
	TArray<float> BuildNetworkInputV8(const FObservationElement& Observation, EStrategyType AssignedStrategy) const;

	/**
	 * Network output structure (v8.0 Multi-Head)
	 */
	struct FNetworkOutputV8 {
		TArray<float> AssaultTactical;   // [4] - Tactical params for Assault
		TArray<float> DefendTactical;    // [4] - Tactical params for Defend
		TArray<float> SupportTactical;   // [4] - Tactical params for Support
		TArray<float> RetreatTactical;   // [4] - Tactical params for Retreat
		TArray<float> CombatLogits;      // [2] - Combat priority logits [Closest, LowestHP]
		float Value;                     // State value estimate
	};

	/**
	 * Forward pass through multi-head network (v8.0)
	 * @param InputFeatures - 68-element input vector
	 * @return All strategy heads, combat head, and value head outputs
	 */
	FNetworkOutputV8 ForwardPassV8(const TArray<float>& InputFeatures);

	/**
	 * Select appropriate tactical parameters based on assigned strategy (v8.0)
	 * @param NetworkOutput - Output from multi-head network
	 * @param AssignedStrategy - Strategy assigned by MCTS
	 * @return Tactical parameters from the appropriate strategy head
	 */
	FTacticalParameters SelectTacticalParameters(const FNetworkOutputV8& NetworkOutput, EStrategyType AssignedStrategy) const;

	/**
	 * Sample combat priority from logits (v8.0)
	 * @param CombatLogits - 2-element logit vector [Closest, LowestHP]
	 * @return Sampled combat priority
	 */
	ETargetPriority SampleCombatPriority(const TArray<float>& CombatLogits) const;

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
