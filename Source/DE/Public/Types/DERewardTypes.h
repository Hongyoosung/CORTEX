
#pragma once

#include "CoreMinimal.h"
#include "DEClassTypes.h"
#include "DERewardTypes.generated.h"


/**
 * Reward event types for logging and analysis
 */
UENUM(BlueprintType)
enum class EDERewardEventType : uint8
{
    Kill,
    Assist,
    Death,
    DECapturePoint,
    LosePoint,
    Survival,
    DistanceShaping,
    TeamVictory,
    ClassDiversity,
    HealAlly,
    ZoneDurability,
    ZoneGuardKill,
    TeamWipe
};


USTRUCT(BlueprintType)
struct FDERewardEvent
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    EDERewardEventType EventType;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    EDEClassType Class;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    float RewardValue;

    FDERewardEvent() : EventType(EDERewardEventType::Kill), Class(EDEClassType::Strike), RewardValue(0.0f) {}
    FDERewardEvent(EDERewardEventType InEventType, EDEClassType InClass, float InRewardValue)
        : EventType(InEventType), Class(InClass), RewardValue(InRewardValue) {}
};


/**
 * Tracks an agent's reward accumulation state (reset each episode)
 */
USTRUCT(BlueprintType)
struct FDERewardState
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    float CumulativeReward = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TArray<FDERewardEvent> EventLog;

    int32 CachedInjuredAllyIdx = -1;
    int32 InjuredAllyStalenessCounter = 0;

    // Per-capture-point cooldown for loss penalties
    float LastCaptureLossPenaltyTime;

    bool bSparseKillFiredThisStep = false;
    /** Set by Strike range check; consumed by CalculateKillReward to halve close-range kills. */
    bool bWasTooCloseAtKill = false;
    bool bInFriendlyZone = false;
    float LastIndividualStepReward = 0.0f;
    int32 IsolatedConsecutiveSteps = 0;

    /** Per-ally accumulated recent damage tracker for Support targeting (decayed each step). */
    TArray<float> AllyAccumulatedDamage;

    /** True when no non-Support allies are alive; Support falls back to following other Supports. */
    bool bFallbackToSupportTarget = false;

    void Reset()
    {
        CumulativeReward = 0.0f;
        EventLog.Empty();
        CachedInjuredAllyIdx = -1;
        InjuredAllyStalenessCounter = 0;
        LastCaptureLossPenaltyTime = 0;
        bSparseKillFiredThisStep = false;
        bWasTooCloseAtKill = false;
        bInFriendlyZone = false;
        LastIndividualStepReward = 0.0f;
        IsolatedConsecutiveSteps = 0;
        AllyAccumulatedDamage.Init(0.0f, 4);
        bFallbackToSupportTarget = false;
    }
};


/**
 * Agent personality weights for reward scalarization
 */
USTRUCT(BlueprintType)
struct DE_API FDEAgentPersonality
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Personality")
    float WinWeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Personality")
    float SurvivalWeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Personality")
    float ObjectiveWeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Personality")
    float EfficiencyWeight;

    FDEAgentPersonality()
        : WinWeight(1.0f)
        , SurvivalWeight(0.5f)
        , ObjectiveWeight(1.5f)
        , EfficiencyWeight(0.2f)
    {}
};


/**
 * Multi-Objective Reward Structure predicted by the Value Network
 */
USTRUCT(BlueprintType)
struct DE_API FDECompositeReward
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WinProb;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HealthDelta;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ObjectiveScore;

    FDECompositeReward()
        : WinProb(0.0f)
        , HealthDelta(0.0f)
        , ObjectiveScore(0.0f)
    {}

    float Scalarize(const FDEAgentPersonality& Personality) const
    {
        float ClampedHealth = FMath::Clamp(HealthDelta, -1.0f, 1.0f);
        return (Personality.WinWeight * WinProb) +
               (Personality.SurvivalWeight * ClampedHealth) +
               (Personality.ObjectiveWeight * ObjectiveScore);
    }
};


/**
 * Per-step reward breakdown for debug logging.
 */
struct FDERewardBreakdown
{
    float ClassReward       = 0.0f;
    float HealthComponent         = 0.0f;
    float PositionComponent    = 0.0f;
    float ObjectiveComponent   = 0.0f;
    float DeathPenaltyComponent = 0.0f;
    float TimePenaltyComponent = 0.0f;
    float Total                = 0.0f;
};
