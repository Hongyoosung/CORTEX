// File: AI/Common/RewardTypes.h
#pragma once

#include "CoreMinimal.h"
#include "StrategyTypes.h"
#include "RewardTypes.generated.h"


/**
 * Reward event types for logging and analysis
 */
UENUM(BlueprintType)
enum class ERewardEventType : uint8
{
    Kill,
    Assist,
    Death,
    CapturePoint,
    LosePoint,
    PickupDeny,
    Survival,
    DistanceShaping,
    TeamVictory,
    StrategyDiversity
};

/**
 * Reward event log entry
 */
USTRUCT(BlueprintType)
struct FRewardEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    ERewardEventType EventType;

    UPROPERTY(BlueprintReadOnly)
    EStrategyType ActiveStrategy;

    UPROPERTY(BlueprintReadOnly)
    float RewardValue = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float Timestamp = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    int32 AgentID = -1;

    FRewardEvent() = default;

    FRewardEvent(ERewardEventType Type, EStrategyType Strategy, float Value, float Time, int32 ID)
        : EventType(Type)
        , ActiveStrategy(Strategy)
        , RewardValue(Value)
        , Timestamp(Time)
        , AgentID(ID)
    {
    }
};


USTRUCT(BlueprintType)
struct FAssaultRewardSettings
{
    GENERATED_BODY()

    //========== Combat Properties =============

    UPROPERTY(EditAnywhere, Category = "Combat")
    float SurvivalRewardScale = 0.8f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float KillRewardScale = 10.0f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float HealthPenalty = 5.0f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float DeathScale = 0.3f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float IdlePenalty = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float TimePenalty = 0.01f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float PickupDenyRewardScale = 1.0f;


    //========== Capture Properties =============

    UPROPERTY(EditAnywhere, Category = "Capture")
    float CaptureRewardScale = 1.5f;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float LossCaptureRewardScale = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float ObjectiveProgressReward = 0.05f;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float ZonePresenceBonus = 3.0f;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float PostCaptureMomentumBonus = 1.5f;

    UPROPERTY(EditAnywhere, Category = "Capture")
    int32 PostCaptureMomentumDuration = 30;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float PostCaptureMomentumMinMove = 100.0f;


    //========== Movement Properties =============

    UPROPERTY(EditAnywhere, Category = "Movement")
    float PenaltyPerMeterScale = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Movement")
    float MovementReward = 0.01f;
};


USTRUCT(BlueprintType)
struct FDefendRewardSettings
{
    GENERATED_BODY()

    //========== Combat Properties =============

    UPROPERTY(EditAnywhere, Category = "Combat")
    float SurvivalRewardScale = 0.8f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float PositionReward = 2.0f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float HealthBonus = 2.0f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float KillRewardScale = 10.0f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float DeathScale = 0.3f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float PickupDenyRewardScale = 1.0f;

    //========== Capture Properties =============

    UPROPERTY(EditAnywhere, Category = "Capture")
    float CaptureRewardScale = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float LossCaptureRewardScale = 1.5f;


    //========== Movement Properties =============

    UPROPERTY(EditAnywhere, Category = "Movement")
    float PenaltyPerMeterScale = 1.0f;
};


USTRUCT(BlueprintType)
struct FSupportRewardSettings
{
    GENERATED_BODY()

    //========== Combat Properties =============

    UPROPERTY(EditAnywhere, Category = "Combat")
    float SurvivalRewardScale = 0.8f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float PositionReward = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float HealthBonus = 1.5f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float KillRewardScale = 10.0f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float DeathScale = 0.3f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float PickupDenyRewardScale = 1.0f;


    //========== Capture Properties =============

    UPROPERTY(EditAnywhere, Category = "Capture")
    float CaptureRewardScale = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float LossCaptureRewardScale = 1.0f;


    //========== Movement Properties =============

    UPROPERTY(EditAnywhere, Category = "Movement")
    float PenaltyPerMeterScale = 1.5f;
};


/**
 * 에이전트의 성향 정의
 * 보상 스칼라화(Scalarization) 과정에서 어떤 목표를 우선할지 결정하는 가중치입니다.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FAgentPersonality
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

    FAgentPersonality()
        : WinWeight(1.0f)
        , SurvivalWeight(0.5f)
        , ObjectiveWeight(1.5f)
        , EfficiencyWeight(0.2f)
    {}
};




/**
 * Multi-Objective Reward Structure
 * Value Network가 예측하는 벡터 형태의 보상입니다.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FCompositeReward
{
    GENERATED_BODY()

    /** 승리 확률 (0.0 ~ 1.0) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WinProb;

    /** 체력 변화량 예측 (정규화된 값, 예: -0.1은 10% 손실) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HealthDelta;

    /** 목표 달성도 점수 (거리, 점령 상태 등) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ObjectiveScore;

    FCompositeReward()
        : WinProb(0.0f)
        , HealthDelta(0.0f)
        , ObjectiveScore(0.0f)
    {}

    /**
     * 벡터 보상을 단일 스칼라 값(Value)으로 변환합니다.
     * 에이전트의 현재 성향(Personality)에 따라 가중치가 달라집니다.
     */
    float Scalarize(const FAgentPersonality& Personality) const
    {
        // 체력 손실은 일반적으로 -1 ~ 1 사이로 클리핑하여 과도한 회피 방지
        float ClampedHealth = FMath::Clamp(HealthDelta, -1.0f, 1.0f);

        return (Personality.WinWeight * WinProb) +
               (Personality.SurvivalWeight * ClampedHealth) +
               (Personality.ObjectiveWeight * ObjectiveScore);
    }
};

/**
 * Per-step reward breakdown for debug logging.
 * Plain C++ struct — not reflected via UE4 macros.
 */
struct FRewardBreakdown
{
    float StrategyReward       = 0.0f;
    float HealthComponent      = 0.0f;
    float PositionComponent    = 0.0f;
    float ObjectiveComponent   = 0.0f;
    float DeathPenaltyComponent = 0.0f;
    float TimePenaltyComponent = 0.0f;
    float Total                = 0.0f;
};
