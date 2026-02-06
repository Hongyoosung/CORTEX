// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/MocTypes.h"
#include "Observation/ObservationTypes.h"
#include "MultiHeadRLPolicy.generated.h"

/**
 * EQS Weight Parameters (8-dim output from RL policy)
 * These weights configure Environment Query System tests for spatial reasoning.
 * See v10.0Architecture.md Section 2.5 for detailed parameter descriptions.
 */
USTRUCT(BlueprintType)
struct FEQSWeightParameters
{
	GENERATED_BODY()

	/** Distance to enemy objective (approach enemy base) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float EnemyObjectiveProximity = 0.0f;

	/** Distance to ally objective (defend friendly base) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float AllyObjectiveProximity = 0.0f;

	/** Cover density (prioritize cover points) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float CoverDensity = 0.0f;

	/** Enemy visibility (maintain line-of-sight) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float EnemyVisibility = 0.0f;

	/** Ally proximity (stay near teammates) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float AllyProximity = 0.0f;

	/** Combat range (preferred engagement distance) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float CombatRange = 0.0f;

	/** Pickup proximity (collect health/ammo) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float PickupProximity = 0.0f;

	/** Height advantage (seek elevated positions) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float HeightAdvantage = 0.0f;

	FEQSWeightParameters() = default;

	/** Convert to flat array for logging/debugging */
	TArray<float> ToArray() const
	{
		return {
			EnemyObjectiveProximity,
			AllyObjectiveProximity,
			CoverDensity,
			EnemyVisibility,
			AllyProximity,
			CombatRange,
			PickupProximity,
			HeightAdvantage
		};
	}

	/** Create from flat array (from ONNX output) */
	static FEQSWeightParameters FromArray(const TArray<float>& Weights)
	{
		check(Weights.Num() == 8);

		FEQSWeightParameters Params;
		Params.EnemyObjectiveProximity = Weights[0];
		Params.AllyObjectiveProximity = Weights[1];
		Params.CoverDensity = Weights[2];
		Params.EnemyVisibility = Weights[3];
		Params.AllyProximity = Weights[4];
		Params.CombatRange = Weights[5];
		Params.PickupProximity = Weights[6];
		Params.HeightAdvantage = Weights[7];

		return Params;
	}

	/** Clamp all weights to valid range [-1, 1] */
	void Clamp()
	{
		EnemyObjectiveProximity = FMath::Clamp(EnemyObjectiveProximity, -1.0f, 1.0f);
		AllyObjectiveProximity = FMath::Clamp(AllyObjectiveProximity, -1.0f, 1.0f);
		CoverDensity = FMath::Clamp(CoverDensity, -1.0f, 1.0f);
		EnemyVisibility = FMath::Clamp(EnemyVisibility, -1.0f, 1.0f);
		AllyProximity = FMath::Clamp(AllyProximity, -1.0f, 1.0f);
		CombatRange = FMath::Clamp(CombatRange, -1.0f, 1.0f);
		PickupProximity = FMath::Clamp(PickupProximity, -1.0f, 1.0f);
		HeightAdvantage = FMath::Clamp(HeightAdvantage, -1.0f, 1.0f);
	}
};

/**
 * RL Policy Observation (60-dim input to multi-head policy)
 * Combines base state, option, target, and duration.
 * See v10.0Architecture.md Section 2.1 for specification.
 */
USTRUCT(BlueprintType)
struct FRLObservation
{
	GENERATED_BODY()

	/** Base game state (52-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation")
	FObservation BaseState;

	/** Current tactical strategy */
	UPROPERTY(BlueprintReadWrite, Category = "Observation")
	EStrategyType CurrentOption = EStrategyType::Assault;

	/** MCTS-assigned target position */
	UPROPERTY(BlueprintReadWrite, Category = "Observation")
	FVector TargetPosition = FVector::ZeroVector;

	/** Expected strategy duration (seconds) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation")
	float OptionDuration = 10.0f;

	FRLObservation() = default;

	/**
	 * Convert to flat tensor for ONNX inference.
	 * Layout: [State(52), OptionOneHot(5), Target(3), Duration(1)] = 61-dim
	 */
	TArray<float> ToTensor() const
	{
		TArray<float> Tensor;
		Tensor.Reserve(61);

		// 1. Base state (52-dim)
		Tensor.Append(BaseState.ToArray());

		// 2. Option as one-hot encoding (5-dim)
		TArray<float> OptionOneHot = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
		const int32 OptionIndex = static_cast<int32>(CurrentOption);
		if (OptionIndex >= 0 && OptionIndex < 5)
		{
			OptionOneHot[OptionIndex] = 1.0f;
		}
		Tensor.Append(OptionOneHot);

		// 3. Target position (3-dim)
		Tensor.Add(TargetPosition.X);
		Tensor.Add(TargetPosition.Y);
		Tensor.Add(TargetPosition.Z);

		// 4. Duration (1-dim)
		Tensor.Add(OptionDuration);

		ensure(Tensor.Num() == 61);
		return Tensor;
	}
};

/**
 * Multi-Head RL Policy Executor
 *
 * Executes ONNX-based multi-head policy network to generate EQS weight parameters.
 *
 * Architecture:
 * - Input: 61-dim (State=52, OptionOneHot=5, Target=3, Duration=1)
 * - Output: 8-dim (EQS weight parameters)
 * - Five strategy-specialized heads: Assault, Defend, Support, Scout, Retreat
 *
 * Usage:
 * 1. Load ONNX model: LoadPolicyModel(ModelPath)
 * 2. Prepare observation: FRLObservation Obs = {...}
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
	FEQSWeightParameters GetEQSWeights(const FRLObservation& Observation);

	/**
	 * Batch inference for multiple agents (performance optimization).
	 *
	 * @param Observations Array of observations
	 * @return Array of EQS weight parameters
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Policy")
	TArray<FEQSWeightParameters> GetEQSWeightsBatch(const TArray<FRLObservation>& Observations);

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
