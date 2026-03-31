// File: Public/Logging/DynamicEQSTransitionLogger.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "DynamicEQSTransitionLogger.generated.h"

/** Log format for transition data serialization. */
UENUM(BlueprintType)
enum class EDynamicEQSLogFormat : uint8
{
    JSON    UMETA(DisplayName = "JSON"),
    Binary  UMETA(DisplayName = "Binary"),
};

/** A single (s, a, r, s', done) transition tuple stored generically as float arrays. */
USTRUCT(BlueprintType)
struct DYNAMICEQS_API FDynamicEQSTransitionTuple
{
    GENERATED_BODY()

    /** Observation vector at time t. */
    UPROPERTY(BlueprintReadWrite)
    TArray<float> State;

    /** Action vector at time t (EQS weights or any float action). */
    UPROPERTY(BlueprintReadWrite)
    TArray<float> Action;

    /** Scalar reward received. */
    UPROPERTY(BlueprintReadWrite)
    float Reward = 0.f;

    /** Observation vector at time t+1. */
    UPROPERTY(BlueprintReadWrite)
    TArray<float> NextState;

    /** Whether this transition ends the episode. */
    UPROPERTY(BlueprintReadWrite)
    bool bTerminal = false;
};

/**
 * UDynamicEQSTransitionLogger
 *
 * Generic (State, Action, Reward, NextState, bTerminal) transition recorder.
 * No project-specific types. Configure output path, flush interval, and
 * log format in the Details panel or via code.
 */
UCLASS(Blueprintable, BlueprintType)
class DYNAMICEQS_API UDynamicEQSTransitionLogger : public UObject
{
    GENERATED_BODY()

public:
    // -----------------------------------------------------------------------
    // Configuration — set before calling Initialize(), or via Details panel
    // -----------------------------------------------------------------------

    /** Root directory where episode files will be written. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Logging")
    FString OutputPath = TEXT("Saved/DynamicEQS/Transitions");

    /**
     * Number of transitions buffered before an automatic flush to disk.
     * Set to 0 to disable automatic flushing (call FlushToDisk() manually).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Logging", meta = (ClampMin = "0"))
    int32 FlushInterval = 1000;

    /** Serialization format used when writing files. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Logging")
    EDynamicEQSLogFormat LogFormat = EDynamicEQSLogFormat::JSON;

    // -----------------------------------------------------------------------
    // API
    // -----------------------------------------------------------------------

    /**
     * Prepares the logger. Must be called before RecordTransition().
     * Creates the output directory if it does not exist.
     */
    UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Logging")
    void Initialize();

    /**
     * Records a single transition.
     * Triggers an automatic flush when the buffer reaches FlushInterval.
     */
    UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Logging")
    void RecordTransition(const TArray<float>& State, const TArray<float>& Action,
                          float Reward, const TArray<float>& NextState, bool bTerminal);

    /**
     * Writes all buffered transitions to disk and clears the buffer.
     * Safe to call at any time (no-op when buffer is empty).
     */
    UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Logging")
    void FlushToDisk();

    /** Returns the number of transitions currently held in the buffer. */
    UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Logging")
    int32 GetBufferedCount() const { return TransitionBuffer.Num(); }

    /** Resets the episode counter without flushing. */
    UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Logging")
    void ResetEpisodeCounter() { EpisodeCount = 0; }

private:
    TArray<FDynamicEQSTransitionTuple> TransitionBuffer;
    int32 EpisodeCount = 0;
    bool bInitialized = false;

    FString ResolveOutputDirectory() const;
    FString SerializeToJSON() const;
    void SerializeToBinary(TArray<uint8>& OutBytes) const;
};
