
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Types/DEObservationTypes.h"
#include "DEValueNetwork.generated.h"

/**
 * Value Network (The Evaluator)
 * Takes a state as input and instantly estimates the probability of winning or expected value.
 * Replaces Random Rollout (Deep Simulation).
 */
UCLASS(Blueprintable, BlueprintType)
class GAMEAI_PROJECT_API UDEValueNetwork : public UObject
{
    GENERATED_BODY()

public:
    UDEValueNetwork();

    /**
     * Loads and initializes the network model.
     * @param ModelPath: ONNX or NNE model path
     */
    bool InitNetwork(const FString& ModelPath);

    /**
     * "What is the probability of us winning in this state?"
     * * @param State: The current observed game state (Observation)
     * @return The probability of winning or the value score [0.0, 1.0]
     */
    float EvaluateState(const TArray<float>& StateFlatArray);

private:

    float RunInference(const TArray<float>& InputTensor);
};