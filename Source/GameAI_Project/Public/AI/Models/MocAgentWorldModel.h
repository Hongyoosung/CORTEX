#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Types/MocTypes.h"
#include "MocAgentWorldModel.generated.h"


// 보상 구조체 (Multi-Objective)
struct FCompositeReward {
    float WinProb;
    float HealthDelta;
    float ObjectiveScore;
};

// 입력 배치 구조체
struct FBatchModelInput {
    TArray<FObservation> CurrentStates;      // Shape: [BatchSize, 56]
    TArray<FTacticalOption> SelectedOptions; // Shape: [BatchSize, OptionFeats]
};

// 출력 배치 구조체
struct FBatchModelOutput {
    TArray<FObservation> PredictedStates;    // Shape: [BatchSize, 56] (Next State)
    TArray<FCompositeReward> Rewards;        // Shape: [BatchSize]
    TArray<float> Confidences;               // Shape: [BatchSize]
};

/**
 * MOC v10.1: Agent World Model
 * 물리 엔진 대신 신경망을 사용하여 상태 전이를 예측합니다.
 * 배치 처리를 통해 GPU 처리량을 극대화합니다.
 */
UCLASS(Blueprintable, BlueprintType)
class GAMEAI_PROJECT_API UMocAgentWorldModel : public UObject
{
    GENERATED_BODY()

public:
    UMocAgentWorldModel();

    /**
     * 초기화: ONNX 모델 로드 등
     */
    bool InitModel(const FString& ModelPath);

    /**
     * 배치 추론 실행 (Blocking Call - v10.1 Design)
     * Latency 목표: ~1.8ms (Batch=16 기준)
     */
    FBatchModelOutput PredictBatch(const FBatchModelInput& BatchInput);

private:
    // 실제 ONNX 런타임 세션이나 NNE 인터페이스가 이곳에 포함됩니다.
    // void* InferenceSession; 
    
    // 내부 헬퍼: 텐서 변환 등
    void PreprocessInput(const FBatchModelInput& InData);
    FBatchModelOutput PostprocessOutput();
};