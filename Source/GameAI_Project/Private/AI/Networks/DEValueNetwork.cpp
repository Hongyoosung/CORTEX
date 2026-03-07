#include "AI/Networks/DEValueNetwork.h"
#include "Config/DEModelConfig.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"

UDEValueNetwork::UDEValueNetwork()
{
    // 생성자 로직
}

bool UDEValueNetwork::InitNetwork(const FString& ModelPath)
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

float UDEValueNetwork::EvaluateState(const FDEObservation& State)
{
    // 1. 입력 데이터 전처리 (Flattening)
    // FDEObservation 구조체를 신경망 입력 텐서(TArray<float>)로 변환합니다.
    // 설계 문서에 정의된 입력 피처 수는 54개입니다.
    const int32 FeatureCount = DEModelConfig::INPUT_FEATURES;

    // FDEObservation::ToArray() produces the 49-dim base vector.
    // DETacticalObserver appends a 3-dim strategy one-hot for 52 total dims.
    // Here we use the base 49-dim array for value estimation.
    TArray<float> InputFeatures = State.ToArray();

    if (InputFeatures.Num() != FeatureCount)
    {
        // 피처 수가 맞지 않으면 0으로 패딩하거나 잘라내는 안전장치
        UE_LOG(LogTemp, Warning, TEXT("ValueNetwork: Input feature mismatch. Expected %d, got %d"), FeatureCount, InputFeatures.Num());
    }

    // 2. 추론 실행
    // 실제로는 GPU/NPU로 데이터를 전송하여 연산합니다.
    float WinProbability = RunInference(InputFeatures);

    // 3. 결과값 검증 (0.0 ~ 1.0 범위 보장)
    return FMath::Clamp(WinProbability, 0.0f, 1.0f);
}

float UDEValueNetwork::RunInference(const TArray<float>& InputTensor)
{
    // =========================================================
    // [MOCK INFERENCE LOGIC]
    // 실제 신경망 연동 전, MCTS 알고리즘을 테스트하기 위한 휴리스틱 로직입니다.
    // 학습된 모델이 없으면 이 함수가 '가짜' 승률을 계산해줍니다.
    // =========================================================

    // 49-dim layout (from FDEObservation::ToArray()):
    // [0-2]  = self pos / 7500
    // [3]    = health [0.0-1.0]
    // [4-6]  = velocity / 600
    // [7]    = weapon cooldown
    // [8-23] = 4 allies × [rel_pos/8000 (3), health (1)]
    // [24-43]= 5 enemies × [rel_pos/8000 (3), visible (1)]
    // [44-48]= capture point statuses (+1/0/-1)
    //
    // Feature[3]  = self health
    // Feature[26] = nearest enemy visible flag (enemy slot 0)
    // Feature[44] = first capture point status (proxy for objective control)

    float Health = InputTensor.IsValidIndex(3) ? InputTensor[3] : 0.5f;
    // Enemy proximity: use visible flag of first enemy slot as a binary proxy
    float EnemyProximity = InputTensor.IsValidIndex(27) ? InputTensor[27] : 0.0f;
    // Objective: average capture point status mapped to [0,1]
    float CaptureSum = 0.0f;
    for (int32 i = 44; i <= 48; ++i)
    {
        CaptureSum += InputTensor.IsValidIndex(i) ? InputTensor[i] : 0.0f;
    }
    float HasObjective = FMath::Clamp((CaptureSum / 5.0f + 1.0f) * 0.5f, 0.0f, 1.0f);

    // 간단한 선형 결합으로 승률 계산 (테스트용)
    // 체력이 높고(0.5), 목표를 잡고있으면(0.3) 유리함
    float RawScore = (Health * 0.5f) + (HasObjective * 0.3f) - (EnemyProximity * 0.2f);
    
    // 기본 승률 0.5에서 시작
    float BaseWinRate = 0.5f;
    
    // 시그모이드 함수 흉내 (값을 0~1 사이로 부드럽게 매핑)
    float FinalWinRate = BaseWinRate + (RawScore * 0.4f);

    return FinalWinRate;
}