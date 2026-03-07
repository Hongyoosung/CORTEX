// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AI/Models/DETeamWorldModelTypes.h"
#include "DETeamWorldModel.generated.h"


namespace UE::NNE
{
	class IModelInstanceCPU;
}



// FDETeamBatchInput and FDETeamBatchOutput are now in TeamWorldModelTypes.h

/**
 * MOC v10.2: Team World Model
 * Centralized world model for Squad Commander's MCTS planning.
 *
 * Key Differences from v10.1 UDEAgentWorldModel:
 * - Input: FDETeamState (60-dim) instead of individual FDEObservation (52-dim)
 * - Action Space: ETacticalPlay (~10 plays) instead of EDEStrategyType (3 strategies)
 * - Optimization Target: Team win rate instead of individual survival
 * - Usage: Single MCTS by ASquadManager instead of 5 parallel MCTS
 *
 * Performance Target:
 * - Latency: ~1.8ms for Batch=16
 * - Throughput: Used by centralized planner every 0.5s or on critical events
 *
 * Training Notes:
 * - Supervised learning from game replay data (team state transitions)
 * - Labels: Actual next team state + multi-objective outcomes
 * - Augmentation: Different tactical play executions from same initial state
 */
UCLASS(Blueprintable, BlueprintType)
class GAMEAI_PROJECT_API UDETeamWorldModel : public UObject
{
	GENERATED_BODY()

public:
	UDETeamWorldModel();

	/**
	 * Initialize the team world model
	 * @param ModelPath Path to ONNX model file for team state prediction
	 * @return true if initialization successful
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|WorldModel")
	bool InitModel(const FString& ModelPath);

	/**
	 * Predict team-level state transitions for batch of tactical plays
	 * Used by centralized MCTS in ASquadManager
	 *
	 * @param BatchInput Current team states and selected tactical plays
	 * @return Predicted next states, rewards, and confidence scores
	 *
	 * Latency: ~1.8ms (Batch=16) - Blocking call per v10.2 design
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|WorldModel")
	FDETeamBatchOutput PredictBatch(const FDETeamBatchInput& BatchInput);

	/**
	 * Predict single tactical play outcome (convenience wrapper)
	 * @param CurrentState Current team state
	 * @param TacticalPlay Tactical play to execute
	 * @return Predicted next state and reward
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|WorldModel")
	void PredictSingle(
		const FDETeamWorldState& CurrentState,
		ETacticalPlay TacticalPlay,
		FDETeamWorldState& OutNextState,
		FDECompositeReward& OutReward,
		float& OutConfidence
	);

	/**
	 * Check if model is initialized and ready for inference
	 */
	UFUNCTION(BlueprintPure, Category = "AI|WorldModel")
	bool IsModelLoaded() const { return bIsModelLoaded; }

	/**
	 * Get average inference latency (ms) for performance monitoring
	 */
	UFUNCTION(BlueprintPure, Category = "AI|WorldModel")
	float GetAverageLatency() const { return AverageLatencyMs; }

private:
	/**
	 * NNE Model Instance for inference
	 * Uses Unreal Engine's Neural Network Engine with ONNX Runtime backend
	 */
	TSharedPtr<UE::NNE::IModelInstanceCPU> ModelInstance;

	/**
	 * Input tensor binding name (from ONNX model)
	 */
	FString InputTensorName;

	/**
	 * Output tensor binding name (from ONNX model)
	 */
	FString OutputTensorName;

	/**
	 * Model initialization status
	 */
	UPROPERTY()
	bool bIsModelLoaded;

	/**
	 * Performance monitoring
	 */
	UPROPERTY()
	float AverageLatencyMs;

	/**
	 * Internal preprocessing: Convert FDETeamWorldState to tensor format
	 *
	 * @param BatchInput Input team states and tactical plays
	 * @return Flattened tensor ready for neural network
	 */
	TArray<float> PreprocessBatch(const FDETeamBatchInput& BatchInput);

	/**
	 * Internal postprocessing: Convert tensor output to structured results
	 * @param RawOutput Raw neural network output tensor
	 * @return Structured team states, rewards, and confidences
	 */
	FDETeamBatchOutput PostprocessBatch(const TArray<float>& RawOutput, int32 BatchSize);

	/**
	 * Parse 60-dim tensor back to FDETeamState structure
	 * Reverses the transformation done by FDETeamState::ToTensor()
	 * @param Tensor Input tensor array
	 * @param StartIndex Starting index in tensor to read from
	 * @return Reconstructed FDETeamState
	 */
	FDETeamWorldState ParseTensorToTeamState(const TArray<float>& Tensor, int32 StartIndex) const;

	/**
	 * Update performance metrics
	 */
	void UpdateLatencyStats(float LatencyMs);
};
