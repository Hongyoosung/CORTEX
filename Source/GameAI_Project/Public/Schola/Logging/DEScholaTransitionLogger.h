// File: Schola/Logging/DEScholaTransitionLogger.h

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Types/DEStrategyTypes.h"
#include "Types/DERewardTypes.h"
#include "DEScholaTransitionLogger.generated.h"

// World Model 학습을 위한 데이터 튜플
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
 * 학습 데이터 수집기
 * MCTS가 선택한 행동과 그 결과를 버퍼링하고 JSON/Binary로 저장합니다.
 */
UCLASS()
class GAMEAI_PROJECT_API UDEScholaTransitionLogger : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(FString InLogPath);

    // 트랜지션 기록 (한 틱이 끝날 때 호출)
    void RecordTransition(const TArray<float>& State, const FDETacticalOption& Option, 
                          const FDECompositeReward& Reward, const TArray<float>& NextState, bool bTerminal);

    // 에피소드가 끝나면 디스크에 저장
    void FlushToDisk();


private:
    TArray<FDETransitionTuple> TransitionBuffer;
    FString LogDirectory;
    int32 EpisodeCount = 0;

    // JSON 직렬화 헬퍼
    FString SerializeTuple(const FDETransitionTuple& Tuple);
};