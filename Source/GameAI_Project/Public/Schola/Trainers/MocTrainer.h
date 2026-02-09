// File: Schola/Trainers/MocTrainer.h

#pragma once

#include "CoreMinimal.h"
#include "Training/AbstractTrainer.h"
#include "Perception/AIPerceptionComponent.h"
#include "Types/MocTypes.h"
#include "AI/EQS/EQSWeightParameters.h"
#include "MocTrainer.generated.h"


class UScholaMocAgent;
class UScholaTransitionLogger;
class AMocCharacter;
class UEnvQuery;


/**
 * MOC v10.2 전용 Schola Trainer
 *
 * 역할:
 * - Executor Agent RL Policy 학습 (Commander의 전략 지시를 EQS 가중치로 변환)
 * - Python (RLlib/Gym)과 UE5 사이의 브릿지
 * - Commanded Strategy를 조건으로 한 Policy 학습
 * - EQS 가중치 형태의 Action 수집 및 적용
 * - Team-level Reward 계산 (Squad Commander에서 할당받은 전략 기준)
 *
 * Action Space: Box(8) - Continuous EQS weights [-1, 1]
 * Observation Space: Box(52) - Agent State only (Strategy는 외부에서 commanded)
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API AMocTrainer : public AAbstractTrainer
{
    GENERATED_BODY()

public:
    AMocTrainer();

    // ==================== Lifecycle Overrides ====================

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    /** Initialize the trainer after spawning */
    void InitializeMocTrainer(UScholaMocAgent* InAgent);

    // ==================== AAbstractTrainer Overrides ====================

    virtual float ComputeReward() override;
    virtual EAgentTrainingStatus ComputeStatus() override;
    virtual void GetInfo(TMap<FString, FString>& Info) override;
    virtual void ResetTrainer() override;
    virtual void OnCompletion() override;

    // ==================== Schola Interface ====================

    /**
     * Python에서 받은 Action 적용
     * @param ActionValues - [8-dim] EQS Weights:
     *   [EnemyObjProx, AllyObjProx, Cover, Visibility, AllyProx, Range, Pickup, Height]
     */
    virtual void ApplyAction(const TArray<float>& ActionValues);
    
    /**
     * 현재 Observation 반환 (Python으로 전송)
     * @return 52-dim vector: Agent state (commanded strategy는 Character에서 이미 설정됨)
     */
    TArray<float> GetObservation();

    /**
     * Episode 종료 체크
     * @return true if episode should terminate
     */
    bool IsEpisodeDone();

    /**
     * Episode 리셋 (legacy Schola interface)
     */
    void ResetEpisode();

    

protected:
// ==================== Helper Functions ====================

    /** 52-dim State 수집 */
    FObservation GatherStateObservation();

    /** Team-aligned Reward 계산 (commanded strategy 수행 품질) */
    float ComputeCommandedStrategyReward(EStrategyType CommandedStrategy,
                                         const FObservation& Prev,
                                         const FObservation& Current,
                                         const FEQSWeightParameters& Action);

    /** Transition 로깅 (World Model 학습용) */
    void LogTransition(const FObservation& State,
                      EStrategyType CommandedStrategy,
                      const FEQSWeightParameters& Action,
                      float Reward,
                      const FObservation& NextState,
                      bool bDone);


    /** EQS 가중치를 Character의 이동에 실제 적용 */
    UFUNCTION(BlueprintNativeEvent, Category = "AI")
    void ApplyEQSWeightsToCharacter_Implementation(const FEQSWeightParameters& Weights);


public:
    // ==================== Training Configuration ====================

    /** EQS Query Template for tactical positioning */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|EQS")
    UEnvQuery* EQSQueryTemplate;

    /** EQS 실행 반경 */
    UPROPERTY(EditAnywhere, Category = "AI|EQS")
    float EQSSearchRadius = 2000.0f;

    /** EQS 결과 수용 거리 */
    UPROPERTY(EditAnywhere, Category = "AI|EQS")
    float EQSAcceptanceRadius = 50.0f;

    /** Episode 최대 길이 */
    UPROPERTY(EditAnywhere, Category="Training")
    int32 MaxEpisodeSteps = 3000;

    /** Transition 로깅 활성화 (World Model 학습용) */
    UPROPERTY(EditAnywhere, Category="Training|Logging")
    bool bLogTransitions = true;

    /** 로그 파일 경로 */
    UPROPERTY(EditAnywhere, Category="Training|Logging")
    FString TransitionLogPath = TEXT("Saved/Transitions/cortex_v10.2_executor.json");

    /** 비주얼 디버깅 활성화 */
    UPROPERTY(EditAnywhere, Category="Debug")
    bool bEnableDebugVisualization = false;

    // ==================== Reward Shaping Parameters ====================

    /** Assault 전략 보상 가중치 */
    UPROPERTY(EditAnywhere, Category="Training|Rewards")
    float AssaultMovementReward = 0.01f;

    UPROPERTY(EditAnywhere, Category="Training|Rewards")
    float AssaultHealthPenalty = 5.0f;

    /** Defend 전략 보상 가중치 */
    UPROPERTY(EditAnywhere, Category="Training|Rewards")
    float DefendPositionReward = 2.0f;

    UPROPERTY(EditAnywhere, Category="Training|Rewards")
    float DefendHealthBonus = 2.0f;

    /** Support 전략 보상 가중치 */
    UPROPERTY(EditAnywhere, Category="Training|Rewards")
    float SupportPositionReward = 1.0f;

    UPROPERTY(EditAnywhere, Category="Training|Rewards")
    float SupportHealthBonus = 1.5f;

    /** 공통 페널티 */
    UPROPERTY(EditAnywhere, Category="Training|Rewards")
    float DeathPenalty = 100.0f;

    UPROPERTY(EditAnywhere, Category="Training|Rewards")
    float TimePenalty = 0.001f;

protected:
    // ==================== Internal State ====================

    /** 캐스팅된 Agent 참조 */
    UPROPERTY()
    UScholaMocAgent* MocAgent;

    /** 제어 중인 Character */
    UPROPERTY()
    AMocCharacter* ControlledCharacter;

    /** Episode 통계 */
    int32 CurrentEpisodeSteps;
    float EpisodeReward;

    /** 이전 상태 (Reward 계산용) */
    FObservation PreviousObservation;
    FObservation CurrentObservation;

    /** 마지막 Action */
    FEQSWeightParameters LastAction;

    /** Transition 로거 */
    UPROPERTY()
    UScholaTransitionLogger* TransitionLogger;

    // ==================== Training Statistics ====================

    /** 총 에피소드 수 */
    int32 TotalEpisodes;

    /** 누적 보상 (전체 에피소드) */
    float CumulativeReward;

    /** 평균 에피소드 길이 */
    float AverageEpisodeLength;

    /** 마지막 EQS 쿼리 결과 위치 (디버깅용) */
    FVector LastEQSTargetLocation;

    /** 현재 commanded strategy 캐시 */
    EStrategyType CachedCommandedStrategy;

    

public:
    // ==================== Debug & Utilities ====================

    /** 현재 훈련 통계 반환 */
    UFUNCTION(BlueprintCallable, Category="Training|Stats")
    void GetTrainingStats(int32& OutEpisodes, float& OutAvgReward, float& OutAvgLength) const;

    /** 현재 보상 breakdown 로깅 */
    UFUNCTION(BlueprintCallable, Category="Debug")
    void LogRewardBreakdown() const;

    /** 비주얼 디버깅 그리기 */
    UFUNCTION(BlueprintCallable, Category="Debug")
    void DrawTrainingDebug();

    /** EQS 가중치 유효성 검사 */
    bool ValidateEQSWeights(const FEQSWeightParameters& Weights) const;

    /** Observation 유효성 검사 */
    bool ValidateObservation(const FObservation& Obs) const;

private:
    /** 훈련 통계 업데이트 */
    void UpdateTrainingStatistics();

    /** 보상 breakdown 계산 (디버깅용) */
    struct FRewardBreakdown
    {
        float StrategyReward;
        float HealthComponent;
        float PositionComponent;
        float DeathPenaltyComponent;
        float TimePenaltyComponent;
        float Total;
    };

    FRewardBreakdown CalculateRewardBreakdown(
        EStrategyType Strategy,
        const FObservation& Prev,
        const FObservation& Current
    ) const;
};
