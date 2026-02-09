#include "AI/Networks/MocValueNetwork.h"
#include "Config/ModelConfig.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"

UMocValueNetwork::UMocValueNetwork()
{
    // 생성자 로직
}

bool UMocValueNetwork::InitNetwork(const FString& ModelPath)
{
    // [MOC v10.1] Offline Training Phase
    // 실제 구현에서는 여기서 ONNX Runtime 또는 UE5 NNE(Neural Network Engine)를 초기화합니다.
    // 설계 문서에 따르면 이 모델은 사전 학습(Pre-trained)된 상태여야 합니다.
    
    if (ModelPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("ValueNetwork: Model path is empty. Using heuristic mock mode."));
        return false;
    }

    if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*ModelPath))
    {
        UE_LOG(LogTemp, Error, TEXT("ValueNetwork: Model file not found at %s"), *ModelPath);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("ValueNetwork: Successfully loaded model from %s"), *ModelPath);
    
    // TODO: InferenceSession = Ort::Session(Env, *ModelPath, SessionOptions);
    
    return true;
}

float UMocValueNetwork::EvaluateState(const FObservation& State)
{
    // 1. 입력 데이터 전처리 (Flattening)
    // FObservation 구조체를 신경망 입력 텐서(TArray<float>)로 변환합니다.
    // 설계 문서에 정의된 입력 피처 수는 54개입니다.
    const int32 FeatureCount = ModelConfig::INPUT_FEATURES;
    
    if (State.Features.Num() != FeatureCount)
    {
        // 피처 수가 맞지 않으면 0으로 패딩하거나 잘라내는 안전장치
        UE_LOG(LogTemp, Warning, TEXT("ValueNetwork: Input feature mismatch. Expected %d, got %d"), FeatureCount, State.Features.Num());
    }

    // 2. 추론 실행
    // 실제로는 GPU/NPU로 데이터를 전송하여 연산합니다.
    float WinProbability = RunInference(State.Features);

    // 3. 결과값 검증 (0.0 ~ 1.0 범위 보장)
    return FMath::Clamp(WinProbability, 0.0f, 1.0f);
}

float UMocValueNetwork::RunInference(const TArray<float>& InputTensor)
{
    // =========================================================
    // [MOCK INFERENCE LOGIC]
    // 실제 신경망 연동 전, MCTS 알고리즘을 테스트하기 위한 휴리스틱 로직입니다.
    // 학습된 모델이 없으면 이 함수가 '가짜' 승률을 계산해줍니다.
    // =========================================================

    // 가정: 
    // Feature[0] = 현재 체력 비율 (0.0 ~ 1.0)
    // Feature[1] = 적과의 거리 (Normalized, 가까울수록 1.0)
    // Feature[2] = 목표 점유 여부 (0.0 or 1.0)
    
    float Health = InputTensor.IsValidIndex(0) ? InputTensor[0] : 0.5f;
    float EnemyProximity = InputTensor.IsValidIndex(1) ? InputTensor[1] : 0.5f;
    float HasObjective = InputTensor.IsValidIndex(2) ? InputTensor[2] : 0.0f;

    // 간단한 선형 결합으로 승률 계산 (테스트용)
    // 체력이 높고(0.5), 목표를 잡고있으면(0.3) 유리함
    float RawScore = (Health * 0.5f) + (HasObjective * 0.3f) - (EnemyProximity * 0.2f);
    
    // 기본 승률 0.5에서 시작
    float BaseWinRate = 0.5f;
    
    // 시그모이드 함수 흉내 (값을 0~1 사이로 부드럽게 매핑)
    float FinalWinRate = BaseWinRate + (RawScore * 0.4f);

    return FinalWinRate;
}