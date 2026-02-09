// File: AI/Policy/MocPolicyExecutor.h
// MOC v10.2 Policy Executor - Simplified interface for commanded strategies

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/MocTypes.h"
#include "RL/Observation/MocObservation.h"
#include "MocPolicyExecutor.generated.h"

/**
 * MOC v10.2 Policy Executor
 *
 * Simplified policy interface for centralized commander-executor architecture.
 * Generates EQS weights from commanded strategies (Assault/Defend/Support).
 *
 * Architecture:
 * - Input: EStrategyType (commanded by Squad Commander)
 * - Output: 8-dim EQS weight parameters
 * - Supports both ONNX inference and fallback defaults
 *
 * Usage:
 * 1. Load model: LoadModel(ModelPath)
 * 2. Get weights: InferWeights(CommandedStrategy)
 * 3. Apply to EQS query
 *
 * Difference from MultiHeadPolicyExecutor:
 * - Simpler interface (strategy -> weights)
 * - Optimized for v10.2 centralized command flow
 * - No multi-head architecture (single policy head)
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UMocPolicyExecutor : public UActorComponent
{
	GENERATED_BODY()

public:
	UMocPolicyExecutor();

	virtual void BeginPlay() override;

	/**
	 * Load ONNX policy model from file.
	 *
	 * @param ModelPath Absolute or project-relative path to .onnx file
	 * @return True if loaded successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	bool LoadModel(const FString& ModelPath);

	/**
	 * Generate EQS weights from commanded strategy (primary v10.2 interface).
	 *
	 * @param Strategy Commanded strategy from Squad Commander
	 * @return EQS weight parameters (8-dim)
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	FEQSWeightParameters InferWeights(EStrategyType Strategy);

	/**
	 * Generate EQS weights from full observation (alternative interface).
	 * Supports backward compatibility with v10.1-style observation input.
	 *
	 * @param Observation Current state + option + target + duration
	 * @return EQS weight parameters (8-dim)
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	FEQSWeightParameters PredictEQSWeights(const FMocObservation& Observation);

	/**
	 * Check if policy model is loaded and ready.
	 */
	UFUNCTION(BlueprintPure, Category = "MOC|Policy")
	bool IsModelLoaded() const { return bModelLoaded; }

	/**
	 * Get inference latency (milliseconds) from last call.
	 */
	UFUNCTION(BlueprintPure, Category = "MOC|Policy")
	float GetLastInferenceTime() const { return LastInferenceTimeMs; }

	/**
	 * Hot-reload policy model during runtime.
	 *
	 * @param NewModelPath Path to updated .onnx file
	 * @return True if reload successful
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	bool ReloadModel(const FString& NewModelPath);

protected:
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
	/** ONNX Runtime inference session (placeholder for future implementation) */
	void* ONNXSession = nullptr;

	/** Model loaded flag */
	bool bModelLoaded = false;

	/** Last inference time (milliseconds) */
	float LastInferenceTimeMs = 0.0f;

	/**
	 * Internal ONNX inference call.
	 * Takes strategy enum, converts to one-hot, runs inference.
	 *
	 * @param Strategy Strategy type
	 * @return Output weights [8]
	 */
	TArray<float> RunONNXInference(EStrategyType Strategy);

	/**
	 * Validate ONNX model input/output shapes.
	 * Expected: Input [1, 3] (one-hot strategy), Output [1, 8]
	 */
	bool ValidateModelSchema();

	/**
	 * Get default EQS weights for a strategy (fallback when model not loaded).
	 * Based on v10.0Architecture.md Section 2.5 Table.
	 */
	FEQSWeightParameters GetDefaultWeights(EStrategyType Strategy);

	/**
	 * Convert strategy enum to one-hot encoding.
	 * Assault=[1,0,0], Defend=[0,1,0], Support=[0,0,1]
	 */
	TArray<float> StrategyToOneHot(EStrategyType Strategy);

	/** Log inference statistics */
	void LogInferenceStats(float InferenceTimeMs);
};
