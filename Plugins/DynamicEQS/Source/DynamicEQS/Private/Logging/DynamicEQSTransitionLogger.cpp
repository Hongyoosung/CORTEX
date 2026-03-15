// File: Private/Logging/DynamicEQSTransitionLogger.cpp

#include "Logging/DynamicEQSTransitionLogger.h"
#include "DynamicEQS.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static FString FloatArrayToJSON(const TArray<float>& Arr)
{
    FString Out = TEXT("[");
    for (int32 i = 0; i < Arr.Num(); ++i)
    {
        Out += FString::SanitizeFloat(Arr[i]);
        if (i < Arr.Num() - 1) Out += TEXT(",");
    }
    Out += TEXT("]");
    return Out;
}

// ---------------------------------------------------------------------------
// UDynamicEQSTransitionLogger
// ---------------------------------------------------------------------------

void UDynamicEQSTransitionLogger::Initialize()
{
    const FString Dir = ResolveOutputDirectory();
    IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
    bInitialized = true;
    UE_LOG(LogDynamicEQS, Log, TEXT("[DynamicEQSTransitionLogger] Initialized. Output: %s"), *Dir);
}

void UDynamicEQSTransitionLogger::RecordTransition(
    const TArray<float>& State, const TArray<float>& Action,
    float Reward, const TArray<float>& NextState, bool bTerminal)
{
    if (!bInitialized)
    {
        UE_LOG(LogDynamicEQS, Warning, TEXT("[DynamicEQSTransitionLogger] RecordTransition called before Initialize()."));
        return;
    }

    FDynamicEQSTransitionTuple Tuple;
    Tuple.State     = State;
    Tuple.Action    = Action;
    Tuple.Reward    = Reward;
    Tuple.NextState = NextState;
    Tuple.bTerminal = bTerminal;
    TransitionBuffer.Add(MoveTemp(Tuple));

    if (FlushInterval > 0 && TransitionBuffer.Num() >= FlushInterval)
    {
        FlushToDisk();
    }
}

void UDynamicEQSTransitionLogger::FlushToDisk()
{
    if (TransitionBuffer.Num() == 0) return;

    const FString Dir      = ResolveOutputDirectory();
    const FString TimeStr  = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    const bool    bJSON    = (LogFormat == EDynamicEQSLogFormat::JSON);
    const FString Ext      = bJSON ? TEXT(".json") : TEXT(".bin");
    const FString FilePath = FPaths::Combine(Dir,
        FString::Printf(TEXT("Episode_%04d_%s%s"), EpisodeCount, *TimeStr, *Ext));

    int32 FlushedCount = TransitionBuffer.Num();

    if (bJSON)
    {
        FString Json = SerializeToJSON();
        FFileHelper::SaveStringToFile(Json, *FilePath);
    }
    else
    {
        TArray<uint8> Bytes;
        SerializeToBinary(Bytes);
        FFileHelper::SaveArrayToFile(Bytes, *FilePath);
    }

    UE_LOG(LogDynamicEQS, Log, TEXT("[DynamicEQSTransitionLogger] Flushed %d transitions → %s"),
           FlushedCount, *FilePath);

    TransitionBuffer.Empty();
    ++EpisodeCount;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

FString UDynamicEQSTransitionLogger::ResolveOutputDirectory() const
{
    // Support both absolute and project-relative paths
    if (FPaths::IsRelative(OutputPath))
    {
        return FPaths::Combine(FPaths::ProjectDir(), OutputPath);
    }
    return OutputPath;
}

FString UDynamicEQSTransitionLogger::SerializeToJSON() const
{
    FString Out = TEXT("[\n");
    for (int32 i = 0; i < TransitionBuffer.Num(); ++i)
    {
        const FDynamicEQSTransitionTuple& T = TransitionBuffer[i];
        Out += FString::Printf(
            TEXT("  {\"state\":%s,\"action\":%s,\"reward\":%s,\"next_state\":%s,\"terminal\":%s}"),
            *FloatArrayToJSON(T.State),
            *FloatArrayToJSON(T.Action),
            *FString::SanitizeFloat(T.Reward),
            *FloatArrayToJSON(T.NextState),
            T.bTerminal ? TEXT("true") : TEXT("false"));
        if (i < TransitionBuffer.Num() - 1) Out += TEXT(",\n");
    }
    Out += TEXT("\n]");
    return Out;
}

void UDynamicEQSTransitionLogger::SerializeToBinary(TArray<uint8>& OutBytes) const
{
    FMemoryWriter Writer(OutBytes);

    // Header: number of transitions
    int32 Count = TransitionBuffer.Num();
    Writer << Count;

    for (const FDynamicEQSTransitionTuple& T : TransitionBuffer)
    {
        // Each field: length-prefixed float array or scalar
        TArray<float> StateCopy     = T.State;
        TArray<float> ActionCopy    = T.Action;
        TArray<float> NextStateCopy = T.NextState;
        float Reward = T.Reward;
        bool  bTerm  = T.bTerminal;

        Writer << StateCopy;
        Writer << ActionCopy;
        Writer << Reward;
        Writer << NextStateCopy;
        Writer << bTerm;
    }
}
