// File: Schola/Logging/DEScholaTransitionLogger.h

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Types/DEStrategyTypes.h"
#include "Types/DERewardTypes.h"
#include "DEScholaTransitionLogger.generated.h"

// Data tuples for learning the world model
struct FDETransitionTuple {
    TArray<float> StateBefore;      // s_t
    FDETacticalOption ActionTaken;    // a_t (Option)
    TArray<float> StateAfter;       // s_{t+1}
    FDECompositeReward Reward;        // r_t (Multi-objective)
    float Confidence;               // 모델 예측 신뢰도 (Logging용)
    bool bIsTerminal;

    FDETransitionTuple() {}
};

/**
 * Training data collector
 * Buffer the actions and results selected by MCTS and save them as JSON/Binary.
 */
UCLASS()
class DE_API UDEScholaTransitionLogger : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(FString InLogPath);

    // Transition logging (called at the end of a tick)
    void RecordTransition(const TArray<float>& State, const FDETacticalOption& Option, 
                          const FDECompositeReward& Reward, const TArray<float>& NextState, bool bTerminal);

    // When an episode ends, it is saved to disk.
    void FlushToDisk();


private:
    TArray<FDETransitionTuple> TransitionBuffer;
    FString LogDirectory;
    int32 EpisodeCount = 0;

    // JSON serialization helper
    FString SerializeTuple(const FDETransitionTuple& Tuple);
};