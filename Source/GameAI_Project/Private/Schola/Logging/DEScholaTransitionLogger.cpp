#include "Schola/Logging/DEScholaTransitionLogger.h"



void UDEScholaTransitionLogger::Initialize(FString InLogPath)
{
}

void UDEScholaTransitionLogger::RecordTransition(const TArray<float>& State, const FDETacticalOption& Option,
                                              const FDECompositeReward& Reward, const TArray<float>& NextState, bool bTerminal)
{
    FDETransitionTuple NewTuple;
    NewTuple.StateBefore = State;
    NewTuple.ActionTaken = Option;
    NewTuple.Reward = Reward;
    NewTuple.StateAfter = NextState;
    NewTuple.bIsTerminal = bTerminal;

    TransitionBuffer.Add(NewTuple);

    // 메모리 관리: 버퍼가 너무 크면 주기적으로 저장
    if (TransitionBuffer.Num() > 1000) {
        FlushToDisk();
    }
}

void UDEScholaTransitionLogger::FlushToDisk()
{
    if (TransitionBuffer.Num() == 0) return;

    FString JsonOutput = "[";
    for (int32 i = 0; i < TransitionBuffer.Num(); ++i)
    {
        JsonOutput += SerializeTuple(TransitionBuffer[i]);
        if (i < TransitionBuffer.Num() - 1) JsonOutput += ",";
    }
    JsonOutput += "]";

    // 파일 저장 로직 (비동기 권장)
    FString FileName = FString::Printf(TEXT("Episode_%d_%s.json"), 
        EpisodeCount++, *FDateTime::Now().ToString());
    FFileHelper::SaveStringToFile(JsonOutput, *(LogDirectory / FileName));

    TransitionBuffer.Empty();
    UE_LOG(LogTemp, Log, TEXT("[Logger] Flushed %d transitions to %s"), TransitionBuffer.Num(), *FileName);
}

FString UDEScholaTransitionLogger::SerializeTuple(const FDETransitionTuple& Tuple)
{
    return FString();
}
