// File: AI/Policy/MocPolicyExecutor.h
// MOC v10.2 Multi-Head Policy Executor - Command-driven spatial reasoning

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/StrategyTypes.h"
#include "Types/ObservationTypes.h"
#include "MocPolicyExecutor.generated.h"

struct FEQSWeightParameters;
struct FObservation;

/**
 * MOC v10.2 Multi-Head Policy Executor
 *
 * Architecture: Command-Driven Multi-Head Policy with Local State Adaptation
 *
 * Key Innovation: Combines centralized command (from Squad Commander) with
 * local state awareness for context-adaptive spatial reasoning.
 *
 * Design:
 * - Multi-head architecture: 3 strategy-specialized heads (Assault/Defend/Support)
 * - Commanded strategy selects which head to execute
 * - Local observation provides context for weight adaptation
 *
 * Difference from v10.1 MultiHeadRLPolicy:
 * - v10.1: Agent chooses strategy via MCTS → runs policy with full 61-dim obs
 * - v10.2: Commander assigns strategy → agent executes with local 52-dim state
 *
 * Usage (v10.2):
 * 1. Squad Commander issues commanded strategy (Assault/Defend/Support)
 * 2. Executor calls: InferWeights(CommandedStrategy, LocalObservation)
 * 3. Policy selects appropriate head based on command
 * 4. Generates 8-dim EQS weights adapted to local state
 * 5. EQS uses weights for spatial reasoning
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UMocPolicyExecutor : public UActorComponent
{
	GENERATED_BODY()

public:
	UMocPolicyExecutor();

	virtual void BeginPlay() override;

	//========================================
	// Primary v10.2 Interface
	//========================================

	/**
	 * PRIMARY INTERFACE: Generate EQS weights from commanded strategy + local state
	 *
	 * This is the main interface for v10.2 executor agents. Combines:
	 * - High-level command (from Squad Commander)
	 * - Local state awareness (health, position, nearby enemies, etc.)
	 *
	 * @param CommandedStrategy Strategy assigned by Squad Commander (Assault/Defend/Support)
	 * @param LocalObservation Agent's current local state (52-dim: health, position, allies, enemies, etc.)
	 * @return EQS weight parameters (8-dim) adapted to local conditions
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	FEQSWeightParameters InferWeights(
		EStrategyType CommandedStrategy,
		const FObservation& LocalObservation
	);

	/**
	 * SIMPLIFIED INTERFACE: Get strategy default weights (no local adaptation)
	 *
	 * Used when:
	 * - Model not loaded (fallback behavior)
	 * - Testing/debugging with pure strategy defaults
	 * - Baseline comparison
	 *
	 * @param Strategy Strategy type
	 * @return Default EQS weights for strategy (based on v10.0Architecture.md Table 2.5)
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	FEQSWeightParameters GetStrategyDefaults(EStrategyType Strategy);

	//========================================
	// Model Management
	//========================================

	/**
	 * Load multi-head ONNX policy model
	 *
	 * Expected model architecture:
	 * - Input: [BatchSize, 52] (local state only, no option/target/duration)
	 * - Architecture: Shared backbone → 3 strategy heads
	 * - Output: [BatchSize, 8] (EQS weights)
	 *
	 * @param ModelPath Absolute or project-relative path to .onnx file
	 * @return True if loaded successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	bool LoadModel(const FString& ModelPath);

	/**
	 * Hot-reload policy model during runtime
	 *
	 * @param NewModelPath Path to updated .onnx file
	 * @return True if reload successful
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	bool ReloadModel(const FString& NewModelPath);

	/**
	 * Check if policy model is loaded and ready
	 */
	UFUNCTION(BlueprintPure, Category = "MOC|Policy")
	bool IsModelLoaded() const { return bModelLoaded; }

	/**
	 * Get inference latency (milliseconds) from last call
	 */
	UFUNCTION(BlueprintPure, Category = "MOC|Policy")
	float GetLastInferenceTime() const { return LastInferenceTimeMs; }

protected:
	//========================================
	// Configuration
	//========================================

	/** Path to ONNX policy model */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
	FString PolicyModelPath;

	/** Auto-load model on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
	bool bAutoLoadModel = true;

	/** Enable performance profiling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
	bool bEnableProfiling = false;

	/** Use fallback defaults when model not loaded */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
	bool bUseFallbackDefaults = true;

private:
	//========================================
	// Internal Implementation
	//========================================

	/**
	 * Internal multi-head ONNX inference
	 *
	 * Process:
	 * 1. Encode local observation → 52-dim tensor
	 * 2. Select policy head based on commanded strategy
	 * 3. Run head-specific inference
	 * 4. Return 8-dim EQS weights
	 *
	 * @param Strategy Commanded strategy (selects head)
	 * @param LocalObs Local state observation
	 * @return Output weights [8]
	 */
	TArray<float> RunMultiHeadInference(
		EStrategyType Strategy,
		const FObservation& LocalObs
	);

	/**
	 * Validate ONNX model input/output shapes
	 * Expected: Input [BatchSize, 52], Output [BatchSize, 8]
	 */
	bool ValidateModelSchema();

	/**
	 * Get default EQS weights for a strategy (fallback when model not loaded)
	 * Based on v10.0Architecture.md Section 2.5 Table
	 */
	FEQSWeightParameters GetDefaultWeights(EStrategyType Strategy);

	/** Log inference statistics */
	void LogInferenceStats(float InferenceTimeMs);

	//========================================
	// Runtime State
	//========================================

	/** ONNX Runtime inference session (multi-head model) */
	void* ONNXSession = nullptr;

	/** Model loaded flag */
	bool bModelLoaded = false;

	/** Last inference time (milliseconds) */
	float LastInferenceTimeMs = 0.0f;
};
