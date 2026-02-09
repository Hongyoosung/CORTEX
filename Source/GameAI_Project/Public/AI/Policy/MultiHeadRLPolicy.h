// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/MocTypes.h"
#include "RL/Observation/MocObservation.h"
#include "MultiHeadRLPolicy.generated.h"




/**
 * Multi-Head RL Policy Executor
 *
 * Executes ONNX-based multi-head policy network to generate EQS weight parameters.
 *
 * Architecture:
 * - Input: 61-dim (State=52, OptionOneHot=5, Target=3, Duration=1)
 * - Output: 8-dim (EQS weight parameters)
 * - Five strategy-specialized heads: Assault, Defend, Support
 *
 * Usage:
 * 1. Load ONNX model: LoadPolicyModel(ModelPath)
 * 2. Prepare observation: FMocObservation Obs = {...}
 * 3. Get weights: FEQSWeightParameters Weights = GetEQSWeights(Obs)
 * 4. Apply to EQS: ApplyWeightsToQuery(EQSQuery, Weights)
 *
 * See v10.0Architecture.md Section 2 for full specification.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UMultiHeadPolicyExecutor : public UActorComponent
{
	GENERATED_BODY()

public:
	UMultiHeadPolicyExecutor();

	virtual void BeginPlay() override;

	/**
	 * Load ONNX policy model from file.
	 *
	 * @param ModelPath Absolute or project-relative path to .onnx file
	 * @return True if loaded successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	bool LoadPolicyModel(const FString& ModelPath);

	/**
	 * Generate EQS weight parameters from observation.
	 *
	 * @param Observation Current state + option + target + duration (61-dim)
	 * @return EQS weight parameters (8-dim)
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	FEQSWeightParameters GetEQSWeights(const FMocObservation& Observation);

	/**
	 * Batch inference for multiple agents (performance optimization).
	 *
	 * @param Observations Array of observations
	 * @return Array of EQS weight parameters
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	TArray<FEQSWeightParameters> GetEQSWeightsBatch(const TArray<FMocObservation>& Observations);

	/**
	 * Check if policy model is loaded and ready.
	 */
	UFUNCTION(BlueprintPure, Category = "MOC|Policy")
	bool IsPolicyLoaded() const { return bModelLoaded; }

	/**
	 * Get inference latency (milliseconds) from last call.
	 */
	UFUNCTION(BlueprintPure, Category = "MOC|Policy")
	float GetLastInferenceTime() const { return LastInferenceTimeMs; }

	/**
	 * Hot-reload policy model during runtime (for training iterations).
	 *
	 * @param NewModelPath Path to updated .onnx file
	 * @return True if reload successful
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	bool ReloadPolicyModel(const FString& NewModelPath);

protected:
	/** Path to ONNX policy model */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
	FString PolicyModelPath;

	/** Auto-load model on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
	bool bAutoLoadModel = true;

	/** Enable performance profiling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
	bool bEnableProfiling = true;

private:
	/** ONNX Runtime inference session */
	// TODO: Replace with actual ONNX runtime interface when available
	// For now, using placeholder for compilation
	void* ONNXSession = nullptr;

	/** Model loaded flag */
	bool bModelLoaded = false;

	/** Last inference time (milliseconds) */
	float LastInferenceTimeMs = 0.0f;

	/**
	 * Internal ONNX inference call.
	 *
	 * @param InputTensor Flattened input array [1, 61]
	 * @return Output weights [1, 8]
	 */
	TArray<float> RunONNXInference(const TArray<float>& InputTensor);

	/**
	 * Internal batch ONNX inference.
	 *
	 * @param InputTensors Flattened batch input [BatchSize, 61]
	 * @param BatchSize Number of samples
	 * @return Batch output weights [BatchSize, 8]
	 */
	TArray<float> RunONNXInferenceBatch(const TArray<float>& InputTensors, int32 BatchSize);

	/**
	 * Validate ONNX model input/output shapes.
	 */
	bool ValidateModelSchema();

	/** Log inference statistics */
	void LogInferenceStats(int32 BatchSize, float InferenceTimeMs);

	/**
	 * Get default EQS weights for a strategy (fallback when model not loaded).
	 * Based on v10.0Architecture.md Section 2.5 Table.
	 */
	FEQSWeightParameters GetDefaultWeightsForStrategy(EStrategyType Strategy);
};
