#pragma once

#include "CoreMinimal.h"
#include "MocObservation.generated.h"

/**
 * RL Policy Observation (60-dim input to multi-head policy)
 * Combines base state, option, target, and duration.
 * See v10.0Architecture.md Section 2.1 for specification.
 */
USTRUCT(BlueprintType)
struct FMocObservation
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

	FMocObservation() = default;

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