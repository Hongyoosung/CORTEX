#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticalOption.generated.h"

/**
 * 전술적 전략 유형 정의
 */
UENUM(BlueprintType)
enum class EStrategyType : uint8
{
    None        UMETA(DisplayName = "None"),
    Attack      UMETA(DisplayName = "Attack"),
    Defend      UMETA(DisplayName = "Defend"),
};

/**
 * 종료 조건 (Event-Driven Termination Logic)
 * 옵션 실행 중 언제 이 계획을 중단하고 재계획(Re-plan)할지 결정합니다.
 */
USTRUCT(BlueprintType)
struct FTerminationCondition
{
    GENERATED_BODY()

    /** 최소 지속 시간 (이 시간 전에는 가급적 변경하지 않음) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MinDuration;

    /** 체력이 치명적일 때 즉시 종료 여부 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bTerminateOnHealthCritical;

    /** 목표(Objective) 상태 변경 시 종료 여부 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bTerminateOnObjectiveChange;

    /** 시야 내 적 발견 시 종료 여부 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bTerminateOnEnemySighted;

    FTerminationCondition()
        : MinDuration(5.0f)
        , bTerminateOnHealthCritical(true)
        , bTerminateOnObjectiveChange(true)
        , bTerminateOnEnemySighted(false)
    {}
};

/**
 * CORTEX v10.1: Tactical Option
 * MCTS의 이산적 액션 공간(Discrete Action Space)을 정의합니다.
 * 월드 모델은 이 옵션을 입력으로 받아 미래 상태를 예측합니다.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTacticalOption
{
    GENERATED_BODY()

    // ===== Core Components =====

    /** 수행할 전략 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EStrategyType Strategy;

    /** 전략의 대상 (적, 지점, 오브젝트 등) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<AActor> TargetObjective;

    /** 예상 소요 시간 (초) - 월드 모델 예측의 Time Step 역할 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PlannedDuration;

    // ===== Termination Logic =====
    
    /** 종료 조건 설정 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FTerminationCondition Termination;

    // ===== Constructors =====

    FTacticalOption()
        : Strategy(EStrategyType::None)
        , TargetObjective(nullptr)
        , PlannedDuration(0.0f)
    {}

    FTacticalOption(EStrategyType InStrategy, AActor* InTarget, float InDuration)
        : Strategy(InStrategy)
        , TargetObjective(InTarget)
        , PlannedDuration(InDuration)
    {}

    // ===== Utility Methods =====

    /** 디버깅 및 UI 표시용 설명 반환 */
    FString GetDescription() const
    {
        FString TargetName = TargetObjective ? TargetObjective->GetName() : TEXT("None");
        const UEnum* EnumPtr = StaticEnum<EStrategyType>();
        FString StrategyName = EnumPtr ? EnumPtr->GetNameStringByValue((int64)Strategy) : TEXT("Unknown");

        return FString::Printf(TEXT("[%s] -> %s (%.1fs)"), *StrategyName, *TargetName, PlannedDuration);
    }

    // ===== Operators for TMap/TSet usage =====

    /** 동등성 비교 연산자 */
    bool operator==(const FTacticalOption& Other) const
    {
        return Strategy == Other.Strategy &&
               TargetObjective == Other.TargetObjective &&
               FMath::IsNearlyEqual(PlannedDuration, Other.PlannedDuration);
    }

    bool operator!=(const FTacticalOption& Other) const
    {
        return !(*this == Other);
    }

    /** 해시 함수 (TMap의 Key로 사용하기 위해 필수) */
    friend uint32 GetTypeHash(const FTacticalOption& Option)
    {
        uint32 Hash = GetTypeHash(Option.Strategy);
        Hash = HashCombine(Hash, GetTypeHash(Option.TargetObjective));
        Hash = HashCombine(Hash, GetTypeHash(Option.PlannedDuration));
        return Hash;
    }
};