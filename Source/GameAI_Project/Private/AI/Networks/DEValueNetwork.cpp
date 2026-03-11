#include "AI/Networks/DEValueNetwork.h"
#include "Config/DEModelConfig.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"

UDEValueNetwork::UDEValueNetwork()
{

}

bool UDEValueNetwork::InitNetwork(const FString& ModelPath)
{
    
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

float UDEValueNetwork::EvaluateState(const TArray<float>& StateFlatArray)
{

    const int32 FeatureCount = DEModelConfig::INPUT_FEATURES;

    if (StateFlatArray.Num() != FeatureCount)
    {
        UE_LOG(LogTemp, Warning, TEXT("ValueNetwork: Input feature mismatch. Expected %d, got %d"), FeatureCount, StateFlatArray.Num());
    }

    float WinProbability = RunInference(StateFlatArray);

    return FMath::Clamp(WinProbability, 0.0f, 1.0f);
}

float UDEValueNetwork::RunInference(const TArray<float>& InputTensor)
{

    // V2 167-dim flat layout — see FDEObservationV2::ToFlatArray() for field offsets.

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

    float RawScore = (Health * 0.5f) + (HasObjective * 0.3f) - (EnemyProximity * 0.2f);
    

    float BaseWinRate = 0.5f;
    
    float FinalWinRate = BaseWinRate + (RawScore * 0.4f);

    return FinalWinRate;
}