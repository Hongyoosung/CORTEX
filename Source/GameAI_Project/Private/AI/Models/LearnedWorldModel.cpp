#include "LearnedWorldModel.h"

ULearnedWorldModel::ULearnedWorldModel()
{
}

bool ULearnedWorldModel::InitModel(const FString& ModelPath)
{
    // TODO: [MOC v10.1] ONNX Runtime C++ API 또는 UE NNE를 사용하여 모델 로드
    // UE_LOG(LogTemp, Log, TEXT("Loading World Model from %s..."), *ModelPath);
    return true;
}

FBatchModelOutput ULearnedWorldModel::PredictBatch(const FBatchModelInput& BatchInput)
{
    FBatchModelOutput Output;
    int32 BatchSize = BatchInput.CurrentStates.Num();

    if (BatchSize == 0)
    {
        return Output;
    }

    // 출력 배열 크기 미리 확보
    Output.PredictedStates.SetNum(BatchSize);
    Output.Rewards.SetNum(BatchSize);
    Output.Confidences.SetNum(BatchSize);

    // ==========================================================
    // [MOC v10.1] Neural Network Inference Logic Placeholder
    // ==========================================================
    // 실제 구현 시:
    // 1. BatchInput을 Flat TArray<float> (Tensor)로 변환
    // 2. ONNX Runtime Run() 호출 (GPU/NPU 가속)
    // 3. 출력 Tensor를 FBatchModelOutput으로 파싱
    // ==========================================================

    // MOCK UP: 테스트를 위한 더미 데이터 생성 (물리 시뮬레이션 없이 즉시 리턴)
    for (int32 i = 0; i < BatchSize; ++i)
    {
        // 1. 상태 예측 (단순 복사 + 약간의 변화 가정)
        Output.PredictedStates[i] = BatchInput.CurrentStates[i]; 
        
        // 2. 보상 예측 (임의 값)
        Output.Rewards[i].WinProb = FMath::RandRange(0.4f, 0.9f);
        Output.Rewards[i].HealthDelta = -0.1f; // 약간의 체력 손실 가정
        Output.Rewards[i].ObjectiveScore = 0.5f;

        // 3. 신뢰도 (Confidence) - 중요!
        // v10.1의 핵심: 모델이 학습되지 않은 상황이면 낮은 값을 반환해야 함
        Output.Confidences[i] = FMath::RandRange(0.8f, 1.0f); // 높은 신뢰도 가정
    }

    return Output;
}

void ULearnedWorldModel::PreprocessInput(const FBatchModelInput& InData)
{
    // 텐서 패킹 로직 구현 위치
}

FBatchModelOutput ULearnedWorldModel::PostprocessOutput()
{
    // 텐서 언패킹 로직 구현 위치
    return FBatchModelOutput();
}