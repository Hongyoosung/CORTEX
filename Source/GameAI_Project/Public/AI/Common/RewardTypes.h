// File: AI/Common/RewardTypes.h
#pragma once

#include "CoreMinimal.h"
#include "RewardTypes.generated.h"

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