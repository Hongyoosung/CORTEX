// File: AI/Networks/ValueNetwork.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AI/Models/MocLearnedWorldModel.h" // FObservation 정의 포함
#include "MocValueNetwork.generated.h"

/**
 * MOC v10.1: Value Network (The Evaluator)
 * 상태(State)를 입력받아 승리 확률 또는 기대 가치를 즉시 추정합니다.
 * Random Rollout(심층 시뮬레이션)을 대체합니다.
 */
UCLASS(Blueprintable, BlueprintType)
class GAMEAI_PROJECT_API UMocValueNetwork : public UObject
{
    GENERATED_BODY()

public:
    UValueNetwork();

    /**
     * 네트워크 모델을 로드하고 초기화합니다.
     * @param ModelPath ONNX 또는 NNE 모델 경로
     */
    bool InitNetwork(const FString& ModelPath);

    /**
     * "이 상태에서 우리가 이길 확률은 얼마인가?"
     * * @param State 현재 관측된 게임 상태 (Observation)
     * @return 승리 확률 또는 가치 점수 [0.0, 1.0]
     */
    float EvaluateState(const FObservation& State);

private:
    // 실제 추론 엔진(ONNX Runtime 등)과의 연결을 위한 핸들
    // void* InferenceSession;

    /** 내부 추론 로직 (배치 처리 가능성 열어둠) */
    float RunInference(const TArray<float>& InputTensor);
};